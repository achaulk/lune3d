#pragma once

#include "config.h"
#include "blob.h"
#include "sys/thread.h"
#include "sys/sync.h"
#include "refptr.h"

#include <type_traits>

namespace lune {

typedef unsigned long BufLen;

class IoBuffer : public Refcounted
{
public:
	virtual ~IoBuffer() = default;

	bool AllocRead(void **data, uint32_t req, BufLen *bytes)
	{
		if(rd == wr)
			return false;
		*data = ptr + rd;
		uint32_t n = wr - rd;
		if(n > req)
			n = req;
		*bytes = n;
		return true;
	}
	bool AllocWrite(void **data, uint32_t req, BufLen *bytes)
	{
		if(end == wr)
			return false;
		*data = ptr + wr;
		uint32_t n = end - wr;
		if(n > req)
			n = req;
		*bytes = n;
		return true;
	}

	void Read(uint32_t n) { rd += n; }
	void Write(uint32_t n) { wr += n; }

	void *FillReadArea(BufLen *n)
	{
		*n = rd;
		return ptr;
	}
	void *GetValidArea(BufLen *n)
	{
		*n = wr - rd;
		return ptr + rd;
	}

	void Reset()
	{
		rd = wr = 0;
	}

	static IoBuffer *AllocEmptyForFill(size_t max_size);

	static IoBuffer *WrapOwnedStringForEmpty(std::string *s);
	static IoBuffer *WrapOwnedMallocForEmpty(void *buf, uint32_t offset, uint32_t size);
	static IoBuffer *WrapUnownedMemory(void *buf, uint32_t rd, uint32_t wr, uint32_t size);
	static IoBuffer *WrapEmptyBlob(Blob *b);

protected:
	IoBuffer(void *ptr, uint32_t rd, uint32_t wr, uint32_t end) : ptr(static_cast<uint8_t*>(ptr)), rd(rd), wr(wr), end(end) {}

	uint8_t *ptr;
	uint32_t rd, wr, end;
};

static constexpr uint64_t kAppendOffset = UINT64_MAX;

namespace io_err {
static constexpr int32_t kEOF = 1;
}

struct SGBuf
{
#if IS_WIN
	// Must match the definition of WSABUF
	BufLen len;
	void *buf;
#endif
};

struct AsyncOp
{
	// OS-specific data
#if IS_WIN
	void *overlapped[sizeof(void*) == 4 ? 5 : 4];
#endif

	// A logical buffer. Used to hold a reference to a buffer somwhere, but not required
	RefPtr<IoBuffer> buffer;

	// Used to hold a ref to any other needed resources.
	Refcounted *unref;

	// The actual buffer pointer, logical I/O offset (eg file start byte) & byte count
	SGBuf *sg;
	int nsg;
	uint64_t offset;

	SGBuf default_sg[2];

	// Filled in by the I/O layer as the ultimate result of the operation
	int32_t err;
	uint32_t transferred;

	// This is provided for internal I/O layer implementation use
	void (*op)(AsyncOp *op);
	void *op_context;

	// This specifies the client's completion function, if any
	TaskRunner *runner;
	void (*completion)(void *ctx, AsyncOp *op);
	void *completion_context;
	void *completion_context2;

	uint8_t static_buffer[32];

	void SetCompletion(void (*fn)(void *ctx, AsyncOp *op), void *ctx = nullptr, TaskRunner *r = nullptr)
	{
		completion = fn;
		completion_context = ctx;
		runner = r;
	}

	void CompleteErr(int32_t err)
	{
		transferred = 0;
		this->err = err;
		Complete();
	}
	void Complete(uint32_t n)
	{
		err = 0;
		transferred = n;
		Complete();
	}
	void Complete()
	{
		if(!completion)
			return Release();
		if(runner)
			runner->PostTask(std::bind(completion, completion_context, this));
		else
			completion(completion_context, this);
	}
	static void CompleteOp(AsyncOp *op) {
		auto unref = op->unref;
		op->Complete();
		if(unref)
			unref->Release();
	}

	static AsyncOp *Alloc();
	void Release();

	static AsyncOp *AllocForMaxRead(IoBuffer *buffer);
	static AsyncOp *AllocForMaxWrite(IoBuffer *buffer);

	static AsyncOp *OpInto(const void *buf, size_t len);

	static AsyncOp *AllocForSyncIo(OneShotEvent *event);

	void SetCompleteOneshot(OneShotEvent *event);

	AsyncOp(const AsyncOp &) = delete;
	void operator=(const AsyncOp &) = delete;

private:
	AsyncOp() = default;
	~AsyncOp() = default;
};
static_assert(std::is_standard_layout<AsyncOp>::value, "AsyncOp must be standard-layout");

} // namespace lune
