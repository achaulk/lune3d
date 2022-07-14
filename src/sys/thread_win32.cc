#include "thread.h"
#include "io/file.h"

#include <Windows.h>

namespace lune {
HANDLE g_ioIOCP = CreateIoCompletionPort(INVALID_HANDLE_VALUE, NULL, 0, 0);

namespace {

struct TaskEntry
{
	SLIST_ENTRY entry;
	std::function<void()> std_fn;
	void (*fn)(void *);
	void *context;
};

typedef HRESULT(WINAPI *SetThreadDescription)(HANDLE hThread, PCWSTR lpThreadDescription);

void SetThreadNameLegacy(HANDLE h, const std::string &name)
{
	typedef struct tagTHREADNAME_INFO
	{
		DWORD dwType;     // Must be 0x1000.
		LPCSTR szName;    // Pointer to name (in user addr space).
		DWORD dwThreadID; // Thread ID (-1=caller thread).
		DWORD dwFlags;    // Reserved for future use, must be zero.
	} THREADNAME_INFO;
	THREADNAME_INFO info;
	info.dwType = 0x1000;
	info.szName = name.c_str();
	info.dwThreadID = GetThreadId(h);
	info.dwFlags = 0;

	__try {
		RaiseException(0x406D1388, 0, sizeof(info) / sizeof(DWORD), reinterpret_cast<DWORD_PTR *>(&info));
	} __except(EXCEPTION_EXECUTE_HANDLER) {
	}
}

void SetThreadName(HANDLE h, const std::string &name)
{
	static auto set_thread_description_func = reinterpret_cast<SetThreadDescription>(
	    ::GetProcAddress(::GetModuleHandle(L"Kernel32.dll"), "SetThreadDescription"));
	if(set_thread_description_func) {
		std::wstring str;
		str.reserve(name.size());
		for(auto c : name) str.push_back(c);
		set_thread_description_func(h, str.c_str());
	}

	if(!IsDebuggerPresent())
		return;
	SetThreadNameLegacy(h, name);
}

}

ThreadId OsThread::CurrentTid()
{
	return GetCurrentThreadId();
}

void OsThread::DoOsInit()
{
	tid_ = GetThreadId(thread_.native_handle());
	SetThreadName(thread_.native_handle(), name_);
}


TaskThread::TaskThread(const std::string &name) : exit_(false), event_(CreateEvent(NULL, FALSE, FALSE, NULL))
{
	InitializeSListHead((PSLIST_HEADER)queue_);
	InitializeSListHead((PSLIST_HEADER)free_);
	handle_ = OsThread::CreateRawThread(std::bind(&TaskThread::ThreadMain, this), name, ThreadType::TASK);
	handle_->SetTaskRunner(this);
}

TaskThread::~TaskThread()
{
	Quit();
	Join();
	CloseHandle(event_);

	LUNE_ASSERT_MSG(!InterlockedFlushSList((PSLIST_HEADER)queue_), "thread queue not drained");
	auto e = InterlockedFlushSList((PSLIST_HEADER)free_);
	while(e) {
		TaskEntry *t = (TaskEntry *)e;
		e = e->Next;
		delete t;
	}
}

void TaskThread::Quit()
{
	exit_ = true;
	SetEvent(event_);
}

void TaskThread::PostTask(std::function<void()> fn)
{
	auto e = CONTAINING_RECORD(InterlockedPopEntrySList((PSLIST_HEADER)free_), TaskEntry, entry);
	if(!e) {
		e = new TaskEntry();
	}
	e->std_fn = std::move(fn);
	InterlockedPushEntrySList((PSLIST_HEADER)queue_, &e->entry);
	SetEvent(event_);
}

void TaskThread::PostTask(void (*fn)(void *), void *context)
{
	auto e = CONTAINING_RECORD(InterlockedPopEntrySList((PSLIST_HEADER)free_), TaskEntry, entry);
	if(!e) {
		e = new TaskEntry();
	}
	e->fn = fn;
	e->context = context;
	InterlockedPushEntrySList((PSLIST_HEADER)queue_, &e->entry);
	SetEvent(event_);
}

void TaskThread::ThreadMain()
{
	TaskEntry *entries[4096];
	do {
		WaitForSingleObject(event_, INFINITE);
		auto list = InterlockedFlushSList((PSLIST_HEADER)queue_);
		if(!list)
			continue;
		auto e = list;
		PSLIST_ENTRY end = e;

		uint32_t n = 0;
		for(auto e = list; e; e = e->Next) {
			end = e;
			entries[n++] = CONTAINING_RECORD(e, TaskEntry, entry);
		}

		// Call in reverse order
		uint32_t i = n;
		while(i-- > 0) {
			if(entries[i]->fn) {
				entries[i]->fn(entries[i]->context);
				entries[i]->fn = nullptr;
			} else {
				entries[i]->std_fn();
				entries[i]->std_fn = std::function<void()>();
			}
		}
		InterlockedPushListSList((PSLIST_HEADER)free_, list, end, n);
	} while(!exit_.load(std::memory_order_acquire));
}

WindowMessageLoop::WindowMessageLoop() : tid_(GetCurrentThreadId())
{
	MSG m;
	// Ensure the queue is created early
	PeekMessage(&m, 0, 0, 0, PM_NOREMOVE);
}

WindowMessageLoop::~WindowMessageLoop() = default;

void WindowMessageLoop::PostTask(std::function<void()> fn)
{
	auto e = new std::function<void()>(std::move(fn));
	PostThreadMessage(tid_, WM_USER + 2, (WPARAM)e, 0);
}

void WindowMessageLoop::PostTask(void (*fn)(void *), void *context)
{
	PostThreadMessage(tid_, WM_USER + 1, (WPARAM)fn, (LPARAM)context);
}

void WindowMessageLoop::PostHalt()
{
	PostThreadMessage(tid_, WM_USER + 3, 0, 0);
}

void WindowMessageLoop::RunUntilHalt()
{
	MSG m;
	while(GetMessage(&m, NULL, 0, 0)) {
		if(m.message == WM_QUIT)
			quit_ = true;
		if(m.message == WM_USER + 3)
			break;
		if(m.message == WM_USER + 2) {
			auto f = (std::function<void()> *)m.wParam;
			(*f)();
			delete f;
			continue;
		}
		if(m.message == WM_USER + 1) {
			((void (*)(void *))m.wParam)((void *)m.lParam);
			continue;
		}
		if(m.message < WM_USER) {
			TranslateMessage(&m);
			DispatchMessage(&m);
		}
	}
}

void WindowMessageLoop::RunUntilIdle()
{
	MSG m;
	while(PeekMessage(&m, NULL, 0, 0, PM_REMOVE)) {
		if(m.message == WM_QUIT)
			quit_ = true;
		if(m.message == WM_USER + 2) {
			auto f = (std::function<void()> *)m.wParam;
			(*f)();
			delete f;
			continue;
		}
		if(m.message == WM_USER + 1) {
			((void (*)(void *))m.wParam)((void *)m.lParam);
			continue;
		}
		if(m.message < WM_USER) {
			TranslateMessage(&m);
			DispatchMessage(&m);
		}
	}
}

TaskRunner *GetPoolIo();
TaskRunner *GetPoolUser();
TaskRunner *GetPoolUserLongRunning();




namespace {
int32_t Win32ErrorIoErr(DWORD err)
{
	switch(err) {
	case ERROR_HANDLE_EOF:
		return io_err::kEOF;
	}
	LUNE_BP();
	return 0;
}
} // namespace

class IocpPool : public TaskRunner
{
	static constexpr uint32_t kMagicTask0 = 0xFFFFFFF0;
	static constexpr uint32_t kMagicTask1 = 0xFFFFFFF1;
	static constexpr uint32_t kMagicTask2 = 0xFFFFFFF2;
	static constexpr uint32_t kMagicForceOp = 0xFFFFFFFF;

public:
	IocpPool()
	{
		std::string iocp_thread_name = "IocpPool";
		uint32_t n = 4;
		for(uint32_t i = 0; i < n; i++)
			threads_.emplace_back(OsThread::CreateRawThread(std::bind(&IocpPool::ThreadProc, this), iocp_thread_name, ThreadType::IO));
	}

private:
	void PostTask(std::function<void()> fn) override
	{
		auto p = new std::function<void()>(std::move(fn));
		PostQueuedCompletionStatus(g_ioIOCP, kMagicTask1, 0, (OVERLAPPED *)p);
	}
	void PostTask(void (*fn)(void *), void *context) override
	{
		PostQueuedCompletionStatus(g_ioIOCP, kMagicTask0, (ULONG_PTR)context, (OVERLAPPED *)fn);
	}

	void ThreadProc()
	{
		OsThread::Current()->SetTaskRunner(this);
		GQCSLoop();
	}

	void GQCSLoop()
	{
		DWORD n;
		ULONG_PTR k;
		OVERLAPPED *ov;
		while(true) {
			BOOL ret = GetQueuedCompletionStatus(g_ioIOCP, &n, &k, &ov, INFINITE);
			if(!ret && !ov)
				break;
			if(!ov) {
				// Nothing to do?
				if(ret) // Exit signal
					break;
			} else if(n < 0xFFFFFFF0) {
				AsyncOp *op = CONTAINING_RECORD(ov, AsyncOp, overlapped);
				op->err = ret ? 0 : Win32ErrorIoErr(GetLastError());
				op->transferred = n;
				op->op(op);
			} else {
				switch(n) {
				case kMagicTask0:
					((void (*)(void *))ov)((void *)k);
					break;
				case kMagicTask1: {
					std::function<void()> *fn = (std::function<void()> *)ov;
					fn->operator()();
					delete fn;
				} break;
				case kMagicTask2: {
					std::function<void()> *fn = (std::function<void()> *)ov;
					fn->operator()();
				} break;
				case kMagicForceOp: {
					AsyncOp *op = CONTAINING_RECORD(ov, AsyncOp, overlapped);
					op->transferred = n;
					op->op(op);
				} break;
				}
			}
		}
	}

	std::vector<std::shared_ptr<OsThread>> threads_;
};
IocpPool iocp_pool;

}
