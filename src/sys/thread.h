#pragma once

#include "config.h"
#include "logging.h"

#include <atomic>
#include <functional>
#include <list>
#include <string>
#include <mutex>
#include <thread>

namespace lune {

#if IS_WIN
typedef unsigned long ThreadId;
#elif IS_LINUX
typedef int ThreadId;
#endif

enum class ThreadPriority
{
	kNormalPriority,
	kRealtimePriority,
};

enum class ThreadType
{
	MAIN,
	IO,
	FRAME,
	TASK,
	USER,
	POOL,
};

class ScopedIoOk
{
public:
	ScopedIoOk();
	~ScopedIoOk();

	ScopedIoOk(const ScopedIoOk &) = delete;
	void operator=(const ScopedIoOk &) = delete;
};

class TaskRunner
{
public:
	virtual ~TaskRunner() = default;

	virtual void PostTask(std::function<void()> fn) = 0;
	virtual void PostTask(const std::function<void()> *fn)
	{
		PostTask(*fn);
	}
	virtual void PostTask(void (*fn)(void *), void *context) = 0;
	void PostTask(void (*fn)())
	{
		PostTask((void (*)(void *))fn, nullptr);
	}
	std::function<void()> ForwardTo(std::function<void()> fn)
	{
		return [this, fn = std::move(fn)]() { PostTask(std::move(fn)); };
	}
	template<typename... Args>
	std::function<void(Args &&...)> ForwardTo(std::function<void(Args &&...)> fn)
	{
		return [this, fn = std::move(fn)](Args &&args...) { PostTask(std::bind(fn), args...); };
	}

	static TaskRunner *Current();
};

// A Sequence is an abstraction of the notion of a thread. It provides temporal ordering.
// A thread could have multiple sequences, and a single sequence could run on multiple
// distinct threads so long as it only ever runs on one at a time.
class Sequence
{
public:
	Sequence() = default;
	TaskRunner *GetTaskRunner() const
	{
		return runner_;
	}

	static Sequence *Current();

	void SetTaskRunner(TaskRunner *runner)
	{
		runner_ = runner;
	}

protected:
	virtual ~Sequence() = default;

	TaskRunner *runner_ = nullptr;
};

class SequenceChecker
{
public:
	SequenceChecker() : sequence_(nullptr) {}
	~SequenceChecker() = default;

	void BindToCurrent()
	{
		sequence_ = Sequence::Current();
	}
	void AssertCurrent()
	{
		LUNE_ASSERT_MSG(sequence_ == Sequence::Current(), "Invalid sequence");
	}

private:
	Sequence *sequence_;
};

class OsThread : public Sequence, public std::enable_shared_from_this<OsThread>
{
public:
	OsThread();
	OsThread(std::function<void()> fn, std::string name, ThreadType type);
	~OsThread() override;

	void Start();

	static OsThread *Current();
	static ThreadId CurrentTid();

	ThreadId tid() const
	{
		return tid_;
	}

	const std::string &name() const
	{
		return name_;
	}

	static ThreadType CurrentType()
	{
		return Current()->type_;
	}

	void Join();

	// Some thread types may be disallowed from initiating I/O, in particular any thread that can exit
	// before the I/O has completed. On Windows I/O initiated from a stopped thread is canceled
	static void OnIo();

	static void Sleep(uint64_t microseconds);

	static std::shared_ptr<OsThread> CreateRawThread(std::function<void()> fn, std::string name, ThreadType type);

	static void SetPriority(ThreadPriority priority);

protected:
	void DoOsInit();

	ThreadId tid_ = 0;
	std::string name_;
	ThreadType type_;

	std::function<void()> thread_entry_;

	bool exited_ = false;
	std::mutex lock_;

	std::thread thread_;
};

inline TaskRunner *TaskRunner::Current()
{
	return Sequence::Current()->GetTaskRunner();
}

class WindowMessageLoop : public TaskRunner
{
public:
	WindowMessageLoop();
	~WindowMessageLoop() override;

	WindowMessageLoop(const WindowMessageLoop &) = delete;
	void operator=(const WindowMessageLoop &) = delete;

	void PostTask(std::function<void()> fn) override;
	void PostTask(void (*fn)(void *), void *context) override;

	void RunUntilIdle();
	void RunUntilHalt();
	void PostHalt();

	bool quit() const
	{
		return quit_;
	}

private:
	bool quit_ = false;
	uint32_t tid_;
};

// TaskThread exists only to post tasks to
class TaskThread : public TaskRunner
{
public:
	explicit TaskThread(const std::string &name);
	~TaskThread();

	void PostQuit()
	{
		PostTask(std::bind(&TaskThread::Quit, this));
	}
	void Quit();
	void Join()
	{
		handle_->Join();
	}

	void PostTask(std::function<void()> fn) override;
	void PostTask(void (*fn)(void *), void *context) override;

private:
	void ThreadMain();

	std::shared_ptr<OsThread> handle_;
	std::atomic<bool> exit_ = false;
#if IS_WIN
	void *event_;
	alignas(2 * sizeof(void *)) void *queue_[2];
	alignas(2 * sizeof(void *)) void *free_[2];
#else
	// Generic OS-independent implementation
	OneShotEvent ev_;
	struct Entry
	{
		std::function<void()> fn;
		void (*pfn)(void *);
		void *context;
	};
	CriticalSection queue_lock_;
	CondVar cvar_;
	std::list<Entry *> queue_;
#endif
};

// A user thread executes some function until it returns. It is *not* allowed to initiate I/O.
// Alternatively a long-running user task runner (GetPoolUserLongRunning) may work better. That is allowed
// to initiate I/O
class UserThread
{
public:
	UserThread() = default;
	UserThread(std::function<void()> fn, const std::string &name);
	~UserThread();

	UserThread(const UserThread &) = default;

	OsThread *thread() const
	{
		return handle_.get();
	}

private:
	std::shared_ptr<OsThread> handle_;
};

TaskRunner *GetPoolIo();
TaskRunner *GetPoolUser();
TaskRunner *GetPoolUserLongRunning();

namespace details {
void InitMainThread();
}

} // namespace lune
