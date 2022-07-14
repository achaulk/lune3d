#pragma once

#include "logging.h"
#include "sys/sync.h"
#include "sys/thread.h"

#include <vector>

namespace lune {

enum
{
	CHUNK_SKIPPED = 0,
	CHUNK_BEGIN = 1,
	CHUNK_END = 2,
	CHUNK_COMPLETE = 3,
	CHUNK_COUNTER = 4,
	CHUNK_META = 5,
	CHUNK_ASTART = 6,
	CHUNK_AEND = 7,
	CHUNK_AINSTANT = 8,
	CHUNK_INSTANT = 9,
	CHUNK_OBJ_CREATE = 10,
	CHUNK_OBJ_DESTROY = 11,
	CHUNK_OBJ_SNAP = 12,

	CHUNK_HAS_DATA = 0x80,
};

struct EventsChunk
{
	uint32_t valid_entries;
	uint32_t allocated_entries;
	uint32_t tid;
	uint32_t pid;
	// 24 bytes per entry. Extended data steals extra entries
	struct Entry
	{
		uint64_t ts;
		// Lower 5 bits provide the type
		// 2 bits undefined
		// If bit 7 is set, second 8 bits provide a count of subsequent entries in use for data
		// Upper 48 bits are used for type-specific data, eg durations
		uint64_t flags;
		details::LuneDurationEventInfo *info;
	};
	Entry entries[1];
};

class TraceSink
{
public:
	virtual ~TraceSink() = default;
	virtual void SinkChunk(EventsChunk *chunk) = 0;
};

class TraceAggregator
{
public:
	TraceAggregator(TraceSink *sink = nullptr);

	EventsChunk *AllocateChunk();
	void CompleteChunk(EventsChunk *chunk);
	void ReturnChunk(EventsChunk *chunk);

	void SetTraceSink(TraceSink *sink);

private:
	TraceSink *sink_ = nullptr;

	CriticalSection lock_;
	std::vector<EventsChunk *> unused_chunks_;
};

class TraceCollector
{
public:
	TraceCollector(TraceAggregator *aggregator);
	~TraceCollector();

	void Flush();

	void Begin(details::LuneDurationEventInfo *info, uint64_t start);
	void End(details::LuneDurationEventInfo *info, uint64_t end);
	void Complete(details::LuneDurationEventInfo *info, uint64_t start, uint64_t end);

	void AsyncBegin(details::LuneDurationEventInfo *info, uint64_t id, uint64_t start);
	void AsyncNow(details::LuneDurationEventInfo *info, uint64_t id, uint64_t start);
	void AsyncEnd(details::LuneDurationEventInfo *info, uint64_t id, uint64_t start);

	void ObjNew(details::LuneDurationEventInfo *info, uint64_t id, uint64_t start);
	void ObjDel(details::LuneDurationEventInfo *info, uint64_t id, uint64_t start);

private:
	void WriteThreadMeta();

	EventsChunk::Entry &EnsureChunk();

	EventsChunk *current_chunk_ = nullptr;
	TraceAggregator *aggregator_;
	uint32_t tid_;
	uint32_t pid_;
	bool first_chunk_ = true;
};

} // namespace lune
