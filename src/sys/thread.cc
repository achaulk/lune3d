#include "thread.h"
#include "except.h"

namespace lune {

TLS_DECL(OsThread *) tls_CurrentThread;
TLS_DECL(uint32_t) tls_IoOk = 0;

OsThread g_MainThread;

OsThread::OsThread() : name_("Main Thread")
{
	DoOsInit();
}

OsThread::OsThread(std::function<void()> fn, std::string name, ThreadType type)
    : name_(std::move(name)), type_(type), thread_entry_(std::move(fn))
{
}

OsThread::~OsThread()
{
	Join();
}

void OsThread::Start()
{
	thread_ = std::thread([t = shared_from_this()]() {
		tls_CurrentThread = t.get();
		OPTICK_THREAD(t->name_.c_str());
		sys::TryCatch(t->thread_entry_);
		t->exited_ = true;
	});
	DoOsInit();
}


OsThread *OsThread::Current()
{
	return tls_CurrentThread;
}

void OsThread::Join()
{
	if(thread_.joinable())
		thread_.join();
}

void OsThread::Sleep(uint64_t microseconds)
{
	std::this_thread::sleep_for(std::chrono::microseconds(microseconds));
}

void OsThread::OnIo()
{
#if LUNE_DEBUG
	auto self = tls_CurrentThread;
	auto type = self->type_;
	switch(type) {
	case ThreadType::USER:
	case ThreadType::TASK:
	case ThreadType::FRAME:
		LUNE_ASSERT(tls_IoOk > 0);
		break;
	}
#endif
}

ScopedIoOk::ScopedIoOk()
{
	tls_IoOk++;
}

ScopedIoOk::~ScopedIoOk()
{
	tls_IoOk--;
}


std::shared_ptr<OsThread> OsThread::CreateRawThread(std::function<void()> fn, std::string name, ThreadType type)
{
	auto t = std::make_shared<OsThread>(std::move(fn), std::move(name), type);
	t->Start();
	return t;
}

UserThread::UserThread(std::function<void()> fn, const std::string &name)
    : handle_(OsThread::CreateRawThread(std::move(fn), name, ThreadType::USER))
{
}

UserThread::~UserThread() = default;

namespace details {

void InitMainThread()
{
	tls_CurrentThread = &g_MainThread;
}

} // namespace details

}
