#include "trace_file_sink.h"

namespace lune {

TraceFileSink::TraceFileSink(RefPtr<IoFile> file) : file_(std::move(file)) {}
TraceFileSink::~TraceFileSink() {}

void TraceFileSink::SinkData(std::string *data)
{
	auto buf = IoBuffer::WrapOwnedStringForEmpty(data);
	auto op = AsyncOp::AllocForMaxRead(buf);
	op->offset = kAppendOffset;
	file_->BeginWrite(op);
}

} // namespace lune
