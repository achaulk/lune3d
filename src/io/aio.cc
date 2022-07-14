#include "aio.h"

#include <memory>

namespace lune {
namespace {
class OwnedStringIoBuffer : public IoBuffer
{
public:
	OwnedStringIoBuffer(std::string *s) : IoBuffer(s->data(), 0, (uint32_t)s->size(), (uint32_t)s->size()), str_(s) {}

private:
	std::unique_ptr<std::string> str_;
};

class OwnedMallocBuffer : public IoBuffer
{
public:
	OwnedMallocBuffer(void *buf, uint32_t rd, uint32_t wr, uint32_t size) : IoBuffer(buf, rd, wr, size) {}
	~OwnedMallocBuffer() { free(ptr); }

private:
	char *buf_;
	uint32_t offset_;
	uint32_t size_;
};

class BlobBuffer : public IoBuffer
{
public:
	BlobBuffer(Blob *b, uint32_t rd, uint32_t wr) : IoBuffer(b->GetData(), rd, wr, (uint32_t)b->GetSize()), blob(b) {}

private:
	BlobPtr blob;
};
} // namespace

IoBuffer *IoBuffer::AllocEmptyForFill(size_t max_size)
{
	return new OwnedMallocBuffer(malloc(max_size), 0, 0, (uint32_t)max_size);
}

IoBuffer *IoBuffer::WrapOwnedStringForEmpty(std::string *s)
{
	return new OwnedStringIoBuffer(s);
}

IoBuffer *IoBuffer::WrapOwnedMallocForEmpty(void *buf, uint32_t offset, uint32_t size)
{
	return new OwnedMallocBuffer(buf, offset, size, size);
}

IoBuffer *IoBuffer::WrapUnownedMemory(void *buf, uint32_t rd, uint32_t wr, uint32_t size)
{
	return new IoBuffer(buf, rd, wr, size);
}
IoBuffer *IoBuffer::WrapEmptyBlob(Blob *b)
{
	return new BlobBuffer(b, 0, 0);
}

AsyncOp *AsyncOp::Alloc()
{
	auto op = new AsyncOp();
#if IS_WIN
	memset(op->overlapped, 0, sizeof(op->overlapped));
#endif
	op->runner = nullptr;
	op->completion = nullptr;
	op->unref = nullptr;
	TRACE_ASYNC_START("io.verbose", "AsyncOp", op);
	return op;
}
void AsyncOp::Release()
{
	TRACE_ASYNC_END("io.verbose", "AsyncOp", this);
	delete this;
}

AsyncOp *AsyncOp::AllocForMaxRead(IoBuffer *buffer)
{
	auto op = Alloc();
	op->sg = op->default_sg;
	op->nsg = 1;
	op->buffer = buffer;
	if(!buffer->AllocRead(&op->default_sg[0].buf, 0xFFFFFFFF, &op->default_sg[0].len)) {
		op->Release();
		return nullptr;
	}
	return op;
}

AsyncOp *AsyncOp::OpInto(const void *buf, size_t len)
{
	auto op = Alloc();
	op->sg = op->default_sg;
	op->nsg = 1;
	op->default_sg[0].buf = const_cast<void*>(buf);
	op->default_sg[0].len = (lune::BufLen)len;
	return op;
}

AsyncOp *AsyncOp::AllocForMaxWrite(IoBuffer *buffer)
{
	auto op = Alloc();
	op->sg = op->default_sg;
	op->nsg = 1;
	op->buffer = buffer;
	if(!buffer->AllocWrite(&op->default_sg[0].buf, 0xFFFFFFFF, &op->default_sg[0].len)) {
		op->Release();
		return nullptr;
	}
	return op;
}

AsyncOp *AsyncOp::AllocForSyncIo(OneShotEvent *event)
{
	auto op = Alloc();
	op->SetCompleteOneshot(event);
	return op;
}

void AsyncOp::SetCompleteOneshot(OneShotEvent *event)
{
	SetCompletion([](void *ctx, AsyncOp *op) { ((OneShotEvent *)ctx)->signal(); }, event);
}

} // namespace lune
