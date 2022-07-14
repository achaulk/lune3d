#include "trace_chromium_json.h"

#include <stdarg.h>

namespace lune {
namespace {

struct StringWriter
{
	StringWriter(std::string *s) : s(*s) {}

	static void do_printf(std::string &s, const char *fmt, va_list varg)
	{
		char tmp[1024];
		int n = vsnprintf(tmp, sizeof(tmp), fmt, varg);
		s.append(tmp, n);
	}

	void printf(const char *fmt, ...)
	{
		va_list varg;
		va_start(varg, fmt);
		do_printf(s, fmt, varg);
		va_end(varg);
	}

	std::string &s;
};

} // namespace

namespace trace {

std::string *BinToChromiumJson(EventsChunk *chunk)
{
	char tmp[64];
	snprintf(tmp, sizeof(tmp), "{\"pid\":%u,\"tid\":%u,\"ph\":\"", chunk->pid, chunk->tid);
	auto s = new std::string();
	s->reserve(100 * chunk->valid_entries);
	StringWriter wr(s);

	// Combine begin/end to complete to reduce output size
	std::vector<uint32_t> open;
	for(uint32_t i = 0; i < chunk->valid_entries; i++) {
		auto &e = chunk->entries[i];
		if((e.flags & 0x1F) == CHUNK_BEGIN) {
			open.push_back(i);
		} else if((e.flags & 0x1F) == CHUNK_END && !open.empty()) {
			// Close the previous event
			auto& b = chunk->entries[open.back()];
			assert(e.info == b.info);
			assert(e.ts >= b.ts);
			open.pop_back();
			b.flags = CHUNK_COMPLETE | ((e.ts - b.ts) << 16);
			e.flags = CHUNK_SKIPPED;
		}
	}

	for(uint32_t i = 0; i < chunk->valid_entries; i++) {
		auto &e = chunk->entries[i];
		switch(e.flags & 0x1F) {
		case CHUNK_SKIPPED:
			break;
		case CHUNK_BEGIN:
			wr.printf(
			    "%sB\",\"cat\":\"%s\",\"name\":\"%s\",\"ts\":%llu},\n", tmp, e.info->category, e.info->name, e.ts);
			break;
		case CHUNK_END:
			wr.printf("%sE\",\"ts\":%llu},\n", tmp, e.ts);
			break;
		case CHUNK_COMPLETE:
			wr.printf("%sX\",\"cat\":\"%s\",\"name\":\"%s\",\"ts\":%llu,\"dur\":%llu},\n", tmp, e.info->category,
			    e.info->name, e.ts, e.flags >> 16);
			break;
		case CHUNK_META:
			wr.printf("%sM\",\"name\":\"%s\",\"args\":{\"name\":\"%s\"}},\n", tmp, e.info->name, (char *)(&e + 1));
			break;
		case CHUNK_ASTART:
			wr.printf("%sb\",\"cat\":\"%s\",\"name\":\"%s\",\"ts\":%llu,\"id\":\"%p\"},\n", tmp, e.info->category,
			    e.info->name, e.ts, e.flags >> 16);
			break;
		case CHUNK_AEND:
			wr.printf("%se\",\"cat\":\"%s\",\"name\":\"%s\",\"ts\":%llu,\"id\":\"%p\"},\n", tmp, e.info->category,
			    e.info->name, e.ts, e.flags >> 16);
			break;
		case CHUNK_AINSTANT:
			wr.printf("%sn\",\"cat\":\"%s\",\"name\":\"%s\",\"ts\":%llu,\"id\":\"%p\"},\n", tmp, e.info->category,
			    e.info->name, e.ts, e.flags >> 16);
			break;
		case CHUNK_OBJ_CREATE:
			wr.printf("%sN\",\"name\":\"%s\",\"ts\":%llu,\"id\":\"%p\"},\n", tmp, e.info->name, e.ts, e.flags >> 16);
			break;
		case CHUNK_OBJ_DESTROY:
			wr.printf("%sD\",\"name\":\"%s\",\"ts\":%llu,\"id\":\"%p\"},\n", tmp, e.info->name, e.ts, e.flags >> 16);
			break;
		case CHUNK_OBJ_SNAP:
			wr.printf(
			    "%sO\",\"name\":\"%s\",\"ts\":%llu,\"id\":\"%p\",\"args\":{\"snapshot\":{", tmp, e.info->name, e.ts, e.flags >> 16);
			{
				for(uint32_t j = 0; j < ((e.flags >> 8) & 0xFF); j++) {
					auto &n = chunk->entries[i + j + 1];
					wr.printf("\"%s\":%lld,", (const char *)n.info, (int64_t)n.ts);
				}
			}
			wr.printf("}}},\n");
			break;
		case CHUNK_COUNTER:
			wr.printf("%sC\",\"name\":\"%s\",\"ts\":%llu,\"id\":%llu,\"args\":{", tmp, e.info->name, e.ts, e.flags >> 16);
			{
				for(uint32_t j = 0; j < ((e.flags >> 8) & 0xFF); j++) {
					auto& n = chunk->entries[i + j + 1];
					wr.printf("\"%s\":%lld,", (const char*)n.info, (int64_t)n.ts);
				}
			}
			wr.printf("}},\n");
		default:
			LUNE_BP();
		}

		if(e.flags & 0x80)
			i += (e.flags >> 8) & 0xFF;
	}
	return s;
}

} // namespace trace
} // namespace lune
