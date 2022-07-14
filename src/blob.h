#pragma once

#include "future.h"

#include <stdlib.h>
#include <string>

namespace lune {

// A blob is a generic data storage object, that can be waited on
class Blob : public Refcounted, public Promisable<RefPtr<Blob>>
{
public:
	typedef int ty;
	typedef RefPtr<Blob> pointer;

	size_t GetSize()
	{
		return GetContents().second;
	}
	void *GetData()
	{
		return GetContents().first;
	}
	virtual std::pair<void *, size_t> GetContents() = 0;

	using Promisable<RefPtr<Blob>>::Resolved;

	std::string AsString()
	{
		std::string s;
		size_t sz = GetSize();
		if(sz > 0) {
			const void *p = GetData();
			s.assign(static_cast<const char*>(p), sz);
		}
		return s;
	}

protected:
	virtual ~Blob() = default;
};
typedef RefPtr<Blob> BlobPtr;

class OwnedMemoryBlob : public Blob
{
public:
	OwnedMemoryBlob(size_t bytes) : mem_(malloc(bytes)), size_(bytes) {}
	OwnedMemoryBlob(void *mem, size_t bytes) : mem_(mem), size_(bytes) {}

	std::pair<void *, size_t> GetContents() override
	{
		return std::make_pair(mem_, size_);
	}

protected:
	~OwnedMemoryBlob() { free(mem_); }

	void *mem_;
	size_t size_;
};

class DynamicBlob : public OwnedMemoryBlob
{
public:
	DynamicBlob() : OwnedMemoryBlob(nullptr, 0) {}

	void Set(void *p, size_t sz, bool err = false)
	{
		mem_ = p;
		size_ = sz;
		Resolved(err);
	}

	void Copy(const void *p, size_t sz, bool err = false)
	{
		mem_ = malloc(sz);
		memcpy(mem_, p, sz);
		size_ = sz;
		Resolved(err);
	}

	void Set(const std::string &s, bool err = false)
	{
		mem_ = malloc(s.size() + 1);
		memcpy(mem_, s.c_str(), s.size() + 1);
		size_ = s.size();
		Resolved(err);
	}
	void Set(const char *s, bool err = false)
	{
		size_ = strlen(s);
		mem_ = malloc(size_ + 1);
		memcpy(mem_, s, size_ + 1);
		Resolved(err);
	}
};

}
