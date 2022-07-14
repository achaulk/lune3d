#pragma once

#include "config.h"

#include <condition_variable>
#include <mutex>
#include <stdint.h>

// This file provides the ability to select which sync primitives are used

namespace lune {

class alignas(8) CriticalSectionImpl
{
public:
	CriticalSectionImpl();
	~CriticalSectionImpl();

	void lock();
	void unlock();
	bool try_lock();

	std::unique_lock<CriticalSectionImpl> autolock()
	{
		return std::unique_lock<CriticalSectionImpl>(*this);
	}

private:
#if IS_WIN
#if _M_AMD64
	static constexpr uint32_t kDataSize = 40;
#elif _M_IX86
	static constexpr uint32_t kDataSize = 24;
#endif
#elif IS_LINUX
	static constexpr uint32_t kDataSize = 4;
#endif
	uint8_t data_[kDataSize];
};

#if CRITICAL_SECTION_IS_STDMUTEX
typedef std::mutex CriticalSection;
#else
typedef CriticalSectionImpl CriticalSection;
#endif

class CondVarImpl
{
public:
	CondVarImpl() = default;
	~CondVarImpl() = default;

	void notify_one();
	void notify_all();

	void wait(std::unique_lock<CriticalSectionImpl> &lock);
	bool wait_direct(CriticalSectionImpl &lock, uint32_t milliseconds);

private:
#if IS_WIN
	void *data_ = 0;
#endif
};

#if CONDVAR_IS_STDCONDVAR
typedef std::condition_variable CondVar;
#else
typedef CondVarImpl CondVar;
#endif

class OneShotEvent
{
public:
	OneShotEvent();
	~OneShotEvent();

	void wait();
	void signal();

private:
	std::atomic<void*> data_;
};

class SyncEvent
{
public:
	SyncEvent();
	~SyncEvent();

	void wait();
	void signal();
	void reset();

private:
	std::atomic<void *> data_;
};

class SeqEvent
{
public:
	SeqEvent();
	~SeqEvent();

	void wait_for(uint64_t seq);
	void signal_at(uint64_t seq);
	void signal_inc(uint64_t v = 1);

private:
	std::atomic<uint64_t> data_;
	void *extra_;
};

} // namespace lune
