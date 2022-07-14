#include "logging.h"

#include <Windows.h>

extern "C" IMAGE_DOS_HEADER __ImageBase;

namespace lune {
namespace details {
namespace {
bool GetSection(void **va, size_t *size)
{
	auto nthdr = (IMAGE_NT_HEADERS *)((BYTE *)&__ImageBase + __ImageBase.e_lfanew);
	auto sections = (IMAGE_SECTION_HEADER *)((BYTE *)&nthdr->OptionalHeader + nthdr->FileHeader.SizeOfOptionalHeader);
	for(WORD i = 0; i < nthdr->FileHeader.NumberOfSections; i++) {
		auto &s = sections[i];
		if(!memcmp(s.Name, ".traceev", 8)) {
			*va = (BYTE *)&__ImageBase + s.VirtualAddress;
			*size = s.SizeOfRawData;
			return true;
		}
	}
	return false;
}

void PopulateTraceEvents(std::vector<LuneDurationEventInfo *> &list)
{
	void *va;
	size_t size;
	if(!GetSection(&va, &size))
		return;
	LuneDurationEventInfo *ev = (LuneDurationEventInfo *)va;
	LuneDurationEventInfo *end = ev + size / sizeof(LuneDurationEventInfo);
	while(ev < end) {
		if(ev->enabled == 0xFEEFF00F && ev->category && ev->name) {
			ev->enabled = 0;
			list.push_back(ev);
		} else if(ev->category || ev->name || ev->enabled) {
			// This is likely indicative of a bad packing, advance one pounter, try again
			ev = (LuneDurationEventInfo*)((void**)ev + 1);
			continue;
		}
		ev++;
	}
}

} // namespace

std::vector<LuneDurationEventInfo *> OsPopulateDurationEvents()
{
	std::vector<LuneDurationEventInfo *> list;
	PopulateTraceEvents(list);
	return list;
}

} // namespace details
} // namespace lune
