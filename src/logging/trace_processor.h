#pragma once

#include "sys/sync.h"
#include "trace_collector.h"

#include <atomic>
#include <list>
#include <string>

namespace lune {

class TraceProcessorSink
{
public:
	virtual ~TraceProcessorSink() = default;
	virtual void SinkData(std::string *data) = 0;
};

class TraceProcessor : public TraceSink
{
public:
	TraceProcessor(TraceAggregator *chunk_return = nullptr);
	virtual ~TraceProcessor();

	void SinkChunk(EventsChunk *chunk) override;

	void SetConverter(std::function<std::string *(EventsChunk *)> converter);
	void SetSink(TraceProcessorSink *sink);

	TaskRunner *serialize_runner() const { return serialize_runner_; }

private:
	struct Chunk
	{
		uint64_t sequence_number;
		EventsChunk *incoming;
		std::string *data = nullptr;
	};

	void Sequence(EventsChunk *chunk);
	void Convert(Chunk *c);
	void OnConverted(Chunk *c);

	void SetConverterInternal(std::function<std::string *(EventsChunk *)> *converter);
	void SetSinkInternal(TraceProcessorSink *sink);

	void BeginSinking();
	void QuitWhenFlushed();

	void QuitOnFlushed();

	uint64_t sequence_ = 0;
	TraceAggregator *chunk_return_;

	TaskThread serialize_thread_;
	TaskRunner *serialize_runner_;
	TaskRunner *thread_pool_runner_;

	std::list<EventsChunk *> pending_chunks_;
	uint32_t max_stored_chunks_ = 200;
	TraceProcessorSink *sink_ = nullptr;
	std::function<std::string*(EventsChunk*)> converter_;

	std::vector<Chunk*> flush_pending_list_;
	uint64_t next_flush_id_ = 0;
	bool quit_when_flushed_ = false;
};

} // namespace lune
