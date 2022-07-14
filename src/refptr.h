#pragma once

#include <assert.h>
#include <atomic>
#include <memory>
#include <stdint.h>

namespace lune {

class Refcounted
{
public:
	Refcounted() : refs_(0) {}
	virtual ~Refcounted() = default;

	void AddRef() const
	{
		refs_.fetch_add(1, std::memory_order_relaxed);
	}
	void Release() const
	{
		if(refs_.fetch_sub(1, std::memory_order_acq_rel) == 1)
			const_cast<Refcounted*>(this)->RefcountedDestroy();
	}

protected:
	virtual void RefcountedDestroy() { delete this; }

	mutable std::atomic_uint32_t refs_;
};

class RefcountedThreadUnsafe
{
public:
	RefcountedThreadUnsafe() = default;
	virtual ~RefcountedThreadUnsafe() = default;

	void AddRef() { refs_++; }
	void Release()
	{
		if(!--refs_)
			RefcountedDestroy();
	}

protected:
	virtual void RefcountedDestroy() { delete this; }

	uint32_t refs_ = 0;
};

template<typename T>
class RefPtr
{
public:
	typedef T *pointer;

	RefPtr() noexcept = default;
	RefPtr(std::nullptr_t) noexcept {}
	RefPtr(T *p) noexcept : ptr_(p)
	{
		if(p)
			p->AddRef();
	}
	RefPtr(const RefPtr &o) noexcept : ptr_(o.ptr_)
	{
		if(ptr_)
			ptr_->AddRef();
	}
	RefPtr(RefPtr &&o) noexcept : ptr_(o.ptr_) { o.ptr_ = nullptr; }

	template<typename U, typename = typename std::enable_if<std::is_convertible<typename U::pointer, T *>::value>::type>
	RefPtr(U &&o) noexcept : ptr_(o.ptr_)
	{
		o.ptr_ = nullptr;
	}
	template<typename U, typename = typename std::enable_if<std::is_convertible<typename U::pointer, T *>::value>::type>
	RefPtr(const U &o) noexcept : ptr_(o.ptr_)
	{
		if(ptr_)
			ptr_->AddRef();
	}

	~RefPtr()
	{
		if(ptr_)
			ptr_->Release();
	}

	void operator=(std::nullptr_t) noexcept
	{
		if(ptr_)
			ptr_->Release();
		ptr_ = nullptr;
	}
	void operator=(const RefPtr &o) noexcept { reset(o.ptr_); }
	void operator=(RefPtr &&o) noexcept
	{
		if(ptr_)
			ptr_->Release();
		ptr_ = o.ptr_;
		o.ptr_ = nullptr;
	}

	static RefPtr Adopt(T *p) noexcept
	{
		RefPtr r;
		r.ptr_ = p;
		return r;
	}

	void reset(T *p) noexcept
	{
		if(p)
			p->AddRef();
		if(ptr_)
			ptr_->Release();
		ptr_ = p;
	}

	void swap(RefPtr &o) noexcept
	{
		auto t = ptr_;
		ptr_ = o.ptr_;
		o.ptr_ = t;
	}

	T *get() const noexcept { return ptr_; }

	operator T *() const { return ptr_; }

	explicit operator bool() const noexcept { return !!ptr_; }
	T &operator*() const noexcept { return *ptr_; }
	T *operator->() const noexcept { return ptr_; }

	bool operator==(const RefPtr &o) noexcept { return ptr_ == o.ptr_; }
	bool operator!=(const RefPtr &o) noexcept { return ptr_ != o.ptr_; }
	bool operator<=(const RefPtr &o) noexcept { return ptr_ <= o.ptr_; }
	bool operator<(const RefPtr &o) noexcept { return ptr_ < o.ptr_; }
	bool operator>=(const RefPtr &o) noexcept { return ptr_ >= o.ptr_; }
	bool operator>(const RefPtr &o) noexcept { return ptr_ > o.ptr_; }

private:
	template<typename U>
	friend class RefPtr;

	T *ptr_ = nullptr;
};

template<typename T>
class WeakPtrFactory
{
	struct ControlBlock
	{
		explicit ControlBlock(T *ptr) : ptr(ptr) { weak_count.store(0, std::memory_order_relaxed); }
		std::atomic<uint32_t> weak_count;
		T *ptr;
	};

	ControlBlock *cb;

public:
	explicit WeakPtrFactory(T *ptr);
	~WeakPtrFactory();

	class weakptr
	{
	public:
		~weakptr()
		{
			if(cb) {

			}
		}

		weakptr() : cb(nullptr) {}
		weakptr(ControlBlock *cb) : cb(cb)
		{
			cb->weak_count.fetch_add(1, std::memory_order_relaxed);
		}

		weakptr(const weakptr &o);
		void operator=(const weakptr& o);

		weakptr(weakptr &&o);
		void operator=(weakptr &&o);

		RefPtr<T> lock();

	private:
		ControlBlock *cb;
	};

	weakptr get_weak();
};

template<typename T>
using WeakPtr = typename WeakPtrFactory<T>::weakptr;

} // namespace lune

template<typename T>
using RefPtr = lune::RefPtr<T>;
