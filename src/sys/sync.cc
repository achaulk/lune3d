#include "sync.h"

#if _WIN32
#include <Windows.h>
#endif

#if IS_LINUX
#include <linux/futex.h>
#include <stdint.h>
#include <sys/time.h>
#include <limits.h>
#include <sys/syscall.h>
#include <unistd.h>

static int futex(void *uaddr, int futex_op, uint32_t val, const struct timespec *timeout, uint32_t *uaddr2, uint32_t val3)
{
   return syscall(SYS_futex, uaddr, futex_op, val, timeout, uaddr2, val3);
}

#endif

namespace lune {

#if _WIN32
CriticalSectionImpl::CriticalSectionImpl()
{
	static_assert(sizeof(CriticalSectionImpl) == sizeof(CRITICAL_SECTION), "data_ must be CRITICAL_SECTION sized");
	InitializeCriticalSection((LPCRITICAL_SECTION)data_);
}

CriticalSectionImpl::~CriticalSectionImpl()
{
	DeleteCriticalSection((LPCRITICAL_SECTION)data_);
}

void CriticalSectionImpl::lock()
{
	// Note that critical sections are recursive, whereas std::mutex is not
	EnterCriticalSection((LPCRITICAL_SECTION)data_);
}
void CriticalSectionImpl::unlock()
{
	LeaveCriticalSection((LPCRITICAL_SECTION)data_);
}
bool CriticalSectionImpl::try_lock()
{
	return TryEnterCriticalSection((LPCRITICAL_SECTION)data_);
}

void CondVarImpl::notify_one()
{
	WakeConditionVariable((PCONDITION_VARIABLE)&data_);
}
void CondVarImpl::notify_all()
{
	WakeAllConditionVariable((PCONDITION_VARIABLE)&data_);
}

void CondVarImpl::wait(std::unique_lock<CriticalSectionImpl> &lock)
{
	SleepConditionVariableCS((PCONDITION_VARIABLE)&data_, (PCRITICAL_SECTION)lock.mutex(), INFINITE);
}

bool CondVarImpl::wait_direct(CriticalSectionImpl &lock, uint32_t milliseconds)
{
	return SleepConditionVariableCS((PCONDITION_VARIABLE)&data_, (PCRITICAL_SECTION)&lock, milliseconds) != ERROR_TIMEOUT;
}


#endif

#if _WIN32
namespace {
BOOL(__stdcall *WaitOnAddressFn)(void *, void *, SIZE_T, DWORD);
void(__stdcall *WakeByAddressAllFn)(void *);

bool LoadWaitForAddress()
{
	auto dll = LoadLibrary(L"API-MS-Win-Core-Synch-l1-2-0.dll");
	if(!dll)
		return false;
	WaitOnAddressFn = (decltype(WaitOnAddressFn))GetProcAddress(dll, "WaitOnAddress");
	WakeByAddressAllFn = (decltype(WakeByAddressAllFn))GetProcAddress(dll, "WakeByAddressAll");
	return true;
}
bool has_wait_on_address = LoadWaitForAddress();
} // namespace

OneShotEvent::OneShotEvent()
{
	data_.store(nullptr, std::memory_order_relaxed);
}

OneShotEvent::~OneShotEvent()
{
	if(!has_wait_on_address)
		CloseHandle(data_);
}

void OneShotEvent::wait()
{
	void *p;
	if(!(p = data_.load(std::memory_order_acquire))) {
		if(has_wait_on_address) {
			void *old = p;
			do {
				WaitOnAddressFn(&data_, &old, sizeof(data_), INFINITE);
			} while(!data_.load(std::memory_order_acquire));
		} else {
			HANDLE h = CreateEvent(NULL, TRUE, FALSE, NULL);
			if(!data_.compare_exchange_strong(p, h)) {
				CloseHandle(h);
				return;
			}
			WaitForSingleObject(h, INFINITE);
		}
	}
}

void OneShotEvent::signal()
{
	if(has_wait_on_address) {
		data_.store((void *)1, std::memory_order_release);
		WakeByAddressAllFn(&data_);
	} else {
		void *p = data_.load(std::memory_order_acquire);
		if(!p && data_.compare_exchange_strong(p, (void *)1))
			return;
		SetEvent(data_.load(std::memory_order_acquire));
	}
}

SeqEvent::SeqEvent() : data_(0), extra_(nullptr) {}
SeqEvent::~SeqEvent()
{
	if(extra_)
		CloseHandle(extra_);
}

void SeqEvent::wait_for(uint64_t seq)
{
	uint64_t p;
	if(has_wait_on_address) {
		if((p = data_.load(std::memory_order_acquire)) < seq) {
			uint64_t old = p;
			do {
				WaitOnAddressFn(&data_, &old, sizeof(data_), INFINITE);
			} while(data_.load(std::memory_order_acquire) < seq);
		}
	}
}
void SeqEvent::signal_at(uint64_t seq)
{
	data_.store(seq, std::memory_order_release);
	WakeByAddressAllFn(&data_);
}

void SeqEvent::signal_inc(uint64_t v)
{
	data_.fetch_add(v, std::memory_order_acq_rel);
	WakeByAddressAllFn(&data_);
}


SyncEvent::SyncEvent()
{
	data_.store(CreateEvent(NULL, TRUE, FALSE, NULL), std::memory_order_relaxed);
}
SyncEvent::~SyncEvent()
{
	CloseHandle(data_.load(std::memory_order_relaxed));
}

void SyncEvent::wait()
{
	WaitForSingleObject(data_.load(std::memory_order_relaxed), INFINITE);
}
void SyncEvent::signal()
{
	SetEvent(data_.load(std::memory_order_relaxed));
}

void SyncEvent::reset()
{
	ResetEvent(data_.load(std::memory_order_relaxed));
}

#endif

#if IS_LINUX
OneShotEvent::OneShotEvent() : data_(0) {}

OneShotEvent::~OneShotEvent() {}

void OneShotEvent::wait()
{
	while(!data_)
		futex(&data_, FUTEX_PRIVATE_FLAG | FUTEX_WAIT, 0, 0, 0, 0);
}

void OneShotEvent::signal()
{
	data_ = (void*)1;
	futex(&data_, FUTEX_PRIVATE_FLAG | FUTEX_WAKE, INT_MAX, 0, 0, 0);
}
#endif

} // namespace lune
