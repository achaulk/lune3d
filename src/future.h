#pragma once

#include <atomic>
#include <functional>
#include <type_traits>

#include "logging.h"
#include "sys/sync.h"
#include "sys/thread.h"
#include "refptr.h"

namespace lune {

// A promise is a way to transfer a generic computed result between threads
// This type of promise differs in a few ways. It has exactly 1 future, and supports
// JS-like .Then() chaining. Promises automatically delete themselves when they
// are resolved and their future is destroyed
// Promises are expected to resolve one way or another, in finite time. ResolveNull
// may be used to resolve "no value" - which is typically considered an error
// Lifecycle
// Created by provider, promise pushed to async op, future returned
// If Resolve called first
// - stash value in promise, set resolved_
// - when Then called
// -- if no task runner call callback immediately, delete object
// -- if thread specified, post task. When called call callback and delete object
// - when Take called
// -- provide value and delete object
// If Then called first
// - stash callback in promise
// - when Resolved called
// -- if no task runner call callback immediately, delete object
// -- if thread specified, post task. When called call callback and delete object
// If Take called first
// - begin waiting
// - when Resolved called, provide object, signal wait
// - when wait finishes, take value and delete object
template<typename T>
class Promise
{
	using Ty = T;
	using Fn = std::function<void(Ty &, bool)>;
	static_assert(std::is_fundamental<T>::value || std::is_move_assignable<T>::value,
	    "Promise type must be fundamental or move-assignable");

public:
	void ResolveNull()
	{
		lock_.lock();
		null_ = true;
		PostResolveLocked();
	}
	void Resolve(T obj)
	{
		lock_.lock();
		value_ = std::move(obj);
		PostResolveLocked();
	}

	static Promise *Make() { return new Promise(); }
	static Promise *MakeResolved(T val)
	{
		Promise *p = new Promise();
		p->value_ = std::move(val);
		p->resolved_ = true;
		return p;
	}

	class Ref
	{
	public:
		Ref() : p(nullptr) {}
		explicit Ref(Promise *p) : p(p) {}
		~Ref() { LUNE_ASSERT_MSG(!p, "Future destroyed without being used! Call ThenNothing()"); }

		Ref(Ref &&o) : p(o.p) { o.p = nullptr; }
		void operator=(Ref &&o)
		{
			p = o.p;
			o.p = nullptr;
		}

		// Discard this future. Intended only for active development
		void ThenNothing()
		{
#if LUNE_FUTURES_MUST_BE_USED
			LUNE_ASSERT(false, "LUNE_FUTURES_MUST_BE_USED - ThenNothing is illegal");
#endif
			Then([](T &, bool) {});
		}

		// Call the provided function-like parameter on whatever thread the completion occurs on.
		// Callable must be implicitle convertible to std::function<void(T&, bool)>
		template<typename Callable>
		void Then(Callable &&fn)
		{
			p->lock_.lock();
			if(p->resolved_) {
				fn(p->value_, !p->null_);
				p->lock_.unlock();
				delete p;
			} else {
				p->then_ = std::move(fn);
				p->lock_.unlock();
			}
			p = nullptr;
		}

		// Call the provided function-like parameter on a specific task runner
		// Callable must be implicitle convertible to std::function<void(T&, bool)>
		template<typename Callable>
		void Then(TaskRunner *runner, Callable &&fn)
		{
			p->lock_.lock();
			p->then_ = std::move(fn);
			p->runner_ = runner;
			if(p->resolved_) {
				runner_->PostTask(&Run, this);
			}
			p->lock_.unlock();
			p = nullptr;
		}

		// Synchronously acquire the value, as with std::future.
		// Be careful, as this will always deadlock if this thread is expected to *produce*
		// the eventual value. It is always better to Then() to a task runner than to
		// Take() on that same thread - however sometimes operations must be waited for
		// immediately
		// Returns false if a null value was provided, otherwise moves the value into *out.
		bool Take(T *out)
		{
			// This is a check on the legality of blocking, so it doesn't matter whether or not
			// it is resolved immediately.
			OsThread::OnBlocking();
			{
				std::unique_lock<CriticalSection> l(p->lock_);
				while(!p->resolved_) p->cv_.wait(l);
			}
			if(p->null_) {
				assert(false);
				delete p;
				p = nullptr;
				return false;
			}
			*out = std::move(p->value_);
			assert(*out);
			delete p;
			p = nullptr;
			return true;
		}

		bool IsResolved() const
		{
			p->lock_.lock();
			bool ret = p->resolved_;
			p->lock_.unlock();
			return ret;
		}

	private:
		Promise *p;
	};

	// Is it undefined behaviour to make multiple futures - though this is not strictly enforced
	Ref MakeFuture() { return Ref(this); }

private:
	void PostResolveLocked()
	{
		if(then_) {
			lock_.unlock();
			if(runner_) {
				runner_->PostTask(&Run, this);
			} else {
				Run(this);
			}
		} else {
			// Either a thread is waiting, or no future op has happened yet
			resolved_ = true;
			lock_.unlock();
			cv_.notify_all();
		}
	}

	static void Run(void *arg)
	{
		Promise *self = (Promise *)arg;
		self->then_(self->value_, !self->null_);
		delete self;
	}

	Promise() = default;
	~Promise() = default;

	T value_;
	Fn then_;
	TaskRunner *runner_ = nullptr;
	CriticalSection lock_;
	CondVar cv_;
	bool resolved_ = false;
	bool null_ = false;
};

// This is intended to be used like class T : public Promisable<T>
// Multiple things can queue events for this object
template<typename T>
class Promisable
{
public:
	using Fn = std::function<void(T *, bool)>;

	Promisable(bool resolved = false) : resolved_(resolved) {}

	// Callable must be implicitly convertible to std::function<void(T*, bool)>
	template<typename Callable>
	void Then(Callable &&fn)
	{
		lock_.lock();
		if(resolved_) {
			fn(static_cast<T *>(this), !errored_);
		} else {
			then_.emplace_back(std::pair<TaskRunner *, Fn>(nullptr, std::move(fn)));
		}
		lock_.unlock();
	}

	// Call the provided function-like parameter on a specific task runner
	// Callable must be implicitly convertible to std::function<void(T*, bool)>
	template<typename Callable>
	void Then(TaskRunner *runner, Callable &&fn)
	{
		lock_.lock();
		if(resolved_) {
			runner->PostTask(std::bind(fn, static_cast<T *>(this), !errored_));
		} else {
			then_.emplace_back(std::pair<TaskRunner *, Fn>(runner, std::move(fn)));
		}
		lock_.unlock();
	}

	bool errored() const { return errored_; }

	void AssertResolved() { assert(resolved_); }

protected:
	void Resolved(bool error)
	{
		std::vector<std::pair<TaskRunner *, Fn>> l;
		lock_.lock();
		errored_ = error;
		resolved_ = true;
		l.swap(then_);
		lock_.unlock();

		for(auto &e : l) {
			if(e.first) {
				e.first->PostTask(std::bind(e.second, static_cast<T *>(this), !errored_));
			} else {
				e.second(static_cast<T *>(this), !error);
			}
		}
	}

	CriticalSection lock_;
	bool resolved_ = false;
	bool errored_ = false;
	std::vector<std::pair<TaskRunner *, Fn>> then_;
};

template<typename T>
class Promisable<RefPtr<T>>
{
public:
	using Fn = std::function<void(RefPtr<T>, bool)>;

	Promisable(bool resolved = false) : resolved_(resolved) {}

	// Callable must be implicitly convertible to std::function<void(T*, bool)>
	template<typename Callable>
	void Then(Callable &&fn)
	{
		lock_.lock();
		if(resolved_) {
			fn(static_cast<T *>(this), !errored_);
			lock_.unlock();
		} else {
			then_.emplace_back(std::pair<TaskRunner *, Fn>(nullptr, std::move(fn)));
			lock_.unlock();
		}
	}

	// Call the provided function-like parameter on a specific task runner
	// Callable must be implicitly convertible to std::function<void(T*, bool)>
	template<typename Callable>
	void Then(TaskRunner *runner, Callable &&fn)
	{
		lock_.lock();
		if(resolved_) {
			lock_.unlock();
			runner->PostTask(std::bind(fn, RefPtr<T>(static_cast<T *>(this)), !errored_));
		} else {
			then_.emplace_back(std::pair<TaskRunner *, Fn>(runner, std::move(fn)));
			lock_.unlock();
		}
	}

	template<typename U>
	void Then(void (U::*fn)(T *, bool), U *obj)
	{
		lock_.lock();
		if(resolved_) {
			lock_.unlock();
			(obj->*fn)(static_cast<T *>(this), !errored_);
		} else {
			then_.emplace_back(
			    std::pair<TaskRunner *, Fn>(nullptr, std::bind(fn, obj, std::placeholders::_1, std::placeholders::_2)));
			lock_.unlock();
		}
	}

	template<typename U>
	void Then(TaskRunner *runner, void (U::*fn)(T *, bool), U *obj)
	{
		lock_.lock();
		if(resolved_) {
			lock_.unlock();
			runner->PostTask(std::bind(fn, obj, RefPtr<T>(static_cast<T *>(this)), !errored_));
		} else {
			then_.emplace_back(
			    std::pair<TaskRunner *, Fn>(runner, std::bind(fn, obj, std::placeholders::_1, std::placeholders::_2)));
			lock_.unlock();
		}
	}

	void wait()
	{
		lock_.lock();
		if(!resolved_) {
			OneShotEvent ev;
			then_.emplace_back(std::pair<TaskRunner *, Fn>(nullptr, std::bind(&OneShotEvent::signal, &ev)));
			lock_.unlock();
			ev.wait();
			return;
		}
		lock_.unlock();
	}

	bool errored() const { return errored_; }

	bool resolved() const { return resolved_; }

	void AssertResolved() { assert(resolved_); }

protected:
	void Resolved(bool error)
	{
		std::vector<std::pair<TaskRunner *, Fn>> l;
		lock_.lock();
		errored_ = error;
		resolved_ = true;
		l.swap(then_);
		lock_.unlock();

		for(auto &e : l) {
			if(e.first) {
				e.first->PostTask(std::bind(e.second, RefPtr<T>(static_cast<T *>(this)), !errored_));
			} else {
				e.second(static_cast<T *>(this), !error);
			}
		}
	}

	CriticalSection lock_;
	bool resolved_ = false;
	bool errored_ = false;
	std::vector<std::pair<TaskRunner *, Fn>> then_;
};

template<typename T>
inline Promise<T> *MakePromise()
{
	return Promise<T>::Make();
}

template<typename T>
using Future = typename Promise<T>::Ref;

} // namespace lune
