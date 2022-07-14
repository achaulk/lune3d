#include "trace_collector.h"
#include "sys/thread.h"

#include <stdio.h>
#include <string.h>

namespace lune {
namespace {
details::LuneDurationEventInfo thread_name_meta_info = {0, "thread_name"};
}

TraceCollector::TraceCollector(TraceAggregator *aggregator)
    : aggregator_(aggregator), tid_(OsThread::CurrentTid()), pid_(1)
{
	;
}

TraceCollector::~TraceCollector()
{
	Flush();
}

EventsChunk::Entry &TraceCollector::EnsureChunk()
{
	if(!current_chunk_) {
		current_chunk_ = aggregator_->AllocateChunk();
		if(first_chunk_) {
			first_chunk_ = false;
			WriteThreadMeta();
		}
	} else if(current_chunk_->allocated_entries == current_chunk_->valid_entries) {
		current_chunk_->tid = tid_;
		current_chunk_->pid = pid_;
		aggregator_->CompleteChunk(current_chunk_);
		current_chunk_ = aggregator_->AllocateChunk();
		current_chunk_->valid_entries = 0;
	}
	return current_chunk_->entries[current_chunk_->valid_entries++];
}

void TraceCollector::WriteThreadMeta()
{
	auto &name = OsThread::Current()->name();
	uint32_t n = (uint32_t)name.size() / sizeof(EventsChunk::Entry) + 1;

	auto *entries = &current_chunk_->entries[0];
	entries[0].flags = CHUNK_META | CHUNK_HAS_DATA | (n << 8);
	entries[0].ts = ((uint64_t)pid_ << 32) | tid_;
	entries[0].info = &thread_name_meta_info;
	memcpy(entries + 1, name.data(), name.size() + 1);

	current_chunk_->valid_entries = n + 1;
}

void TraceCollector::Flush()
{
	if(current_chunk_) {
		aggregator_->CompleteChunk(current_chunk_);
		current_chunk_ = nullptr;
	}
}

void TraceCollector::Begin(details::LuneDurationEventInfo *info, uint64_t start)
{
	auto &e = EnsureChunk();
	e.ts = start;
	e.info = info;
	e.flags = CHUNK_BEGIN;
}

void TraceCollector::End(details::LuneDurationEventInfo *info, uint64_t end)
{
	auto &e = EnsureChunk();
	e.ts = end;
	e.info = info;
	e.flags = CHUNK_END;
}

void TraceCollector::Complete(details::LuneDurationEventInfo *info, uint64_t start, uint64_t end)
{
	auto &e = EnsureChunk();
	e.ts = start;
	e.info = info;
	e.flags = CHUNK_COMPLETE | ((end - start) << 16);
}

void TraceCollector::AsyncBegin(details::LuneDurationEventInfo *info, uint64_t id, uint64_t start)
{
	auto &e = EnsureChunk();
	e.ts = start;
	e.info = info;
	e.flags = CHUNK_ASTART | (id << 16);
}

void TraceCollector::AsyncNow(details::LuneDurationEventInfo *info, uint64_t id, uint64_t start)
{
	auto &e = EnsureChunk();
	e.ts = start;
	e.info = info;
	e.flags = CHUNK_AINSTANT | (id << 16);
}

void TraceCollector::AsyncEnd(details::LuneDurationEventInfo *info, uint64_t id, uint64_t start)
{
	auto &e = EnsureChunk();
	e.ts = start;
	e.info = info;
	e.flags = CHUNK_AEND | (id << 16);
}

void TraceCollector::ObjNew(details::LuneDurationEventInfo *info, uint64_t id, uint64_t start)
{
	auto &e = EnsureChunk();
	e.ts = start;
	e.info = info;
	e.flags = CHUNK_OBJ_CREATE | (id << 16);
}

void TraceCollector::ObjDel(details::LuneDurationEventInfo *info, uint64_t id, uint64_t start)
{
	auto &e = EnsureChunk();
	e.ts = start;
	e.info = info;
	e.flags = CHUNK_OBJ_DESTROY | (id << 16);
}

TraceAggregator::TraceAggregator(TraceSink *sink) : sink_(sink) {}

EventsChunk *TraceAggregator::AllocateChunk()
{
	lock_.lock();
	if(unused_chunks_.empty()) {
		lock_.unlock();
		// 8 kB chunks
		EventsChunk *ret = (EventsChunk *)malloc(sizeof(EventsChunk) + sizeof(EventsChunk::Entry) * 333);
		ret->allocated_entries = 334;
		return ret;
	}
	EventsChunk *ret = unused_chunks_.back();
	unused_chunks_.pop_back();
	lock_.unlock();
	return ret;
}

void TraceAggregator::CompleteChunk(EventsChunk *chunk)
{
	if(sink_)
		sink_->SinkChunk(chunk);
	else
		ReturnChunk(chunk);
}

void TraceAggregator::ReturnChunk(EventsChunk *chunk)
{
	std::unique_lock<CriticalSection> l(lock_);
	if(unused_chunks_.size() > 8)
		free(chunk);
	else
		unused_chunks_.push_back(chunk);
}

void TraceAggregator::SetTraceSink(TraceSink *sink)
{
	sink_ = sink;
}

} // namespace lune
