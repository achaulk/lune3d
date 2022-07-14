#pragma once

#include "io/file.h"
#include "trace_processor.h"

namespace lune {

class TraceFileSink : public TraceProcessorSink
{
public:
	TraceFileSink(RefPtr<IoFile> file);
	~TraceFileSink() override;

	void SinkData(std::string *data) override;

private:
	RefPtr<IoFile> file_;
};

} // namespace lune
