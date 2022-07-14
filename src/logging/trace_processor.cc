#include "trace_processor.h"
#include "trace_chromium_json.h"

namespace lune {

TraceProcessor::TraceProcessor(TraceAggregator *chunk_return)
    : chunk_return_(chunk_return), serialize_thread_("TraceSerializer")
{
	serialize_runner_ = &serialize_thread_;
	thread_pool_runner_ = &serialize_thread_;
	converter_ = &trace::BinToChromiumJson;
}

TraceProcessor::~TraceProcessor()
{
	serialize_thread_.PostTask(std::bind(&TraceProcessor::QuitWhenFlushed, this));
	serialize_thread_.Join();
}

void TraceProcessor::SinkChunk(EventsChunk *chunk)
{
	serialize_runner_->PostTask(std::bind(&TraceProcessor::Sequence, this, chunk));
}

void TraceProcessor::Sequence(EventsChunk *chunk)
{
	if(sink_) {
		Chunk *c = new Chunk();
		c->incoming = chunk;
		c->sequence_number = sequence_++;
		thread_pool_runner_->PostTask(std::bind(&TraceProcessor::Convert, this, c));
	} else {
		pending_chunks_.push_back(chunk);
		if(pending_chunks_.size() > max_stored_chunks_)
			pending_chunks_.pop_front();
	}
}

void TraceProcessor::Convert(Chunk *c)
{
	c->data = converter_(c->incoming);
	serialize_runner_->PostTask(std::bind(&TraceProcessor::OnConverted, this, c));
}

void TraceProcessor::QuitOnFlushed()
{
	serialize_thread_.Quit();
}

void TraceProcessor::OnConverted(Chunk *c)
{
	if(c->sequence_number != next_flush_id_) {
		flush_pending_list_.push_back(c);
		return;
	}
	do {
		next_flush_id_++;
		if(sink_)
			sink_->SinkData(c->data);
		else
			delete c->data;
		if(chunk_return_)
			chunk_return_->ReturnChunk(c->incoming);
		else
			delete c->incoming;
		delete c;

		c = nullptr;
		for(auto it = flush_pending_list_.begin(); it != flush_pending_list_.end(); ++it) {
			if((*it)->sequence_number == next_flush_id_) {
				c = *it;
				*it = flush_pending_list_.back();
				flush_pending_list_.pop_back();
				continue;
			}
		}
	} while(c);

	if(quit_when_flushed_ && sequence_ == next_flush_id_)
		QuitOnFlushed();
}

void TraceProcessor::SetConverter(std::function<std::string *(EventsChunk *)> converter)
{
	std::function<std::string *(EventsChunk *)> *fn =
	    new std::function<std::string *(EventsChunk *)>(std::move(converter));
	serialize_runner_->PostTask(std::bind(&TraceProcessor::SetConverterInternal, this, fn));
}

void TraceProcessor::SetConverterInternal(std::function<std::string *(EventsChunk *)> *converter)
{
	converter_ = std::move(*converter);
	delete converter;
}

void TraceProcessor::SetSink(TraceProcessorSink *sink)
{
	serialize_runner_->PostTask(std::bind(&TraceProcessor::SetSinkInternal, this, sink));
}

void TraceProcessor::SetSinkInternal(TraceProcessorSink *sink)
{
	bool start = sink && !sink_;
	sink_ = sink;

	if(start) {
		sequence_ = 0;
		for(auto e : pending_chunks_) {
			Chunk *c = new Chunk();
			c->incoming = e;
			c->sequence_number = sequence_++;
			thread_pool_runner_->PostTask(std::bind(&TraceProcessor::Convert, this, c));
		}
	}
}

void TraceProcessor::BeginSinking() {}

void TraceProcessor::QuitWhenFlushed()
{
	quit_when_flushed_ = true;
	thread_pool_runner_ = serialize_runner_;
	if(sequence_ == next_flush_id_)
		QuitOnFlushed();
}

} // namespace lune
