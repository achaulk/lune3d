#pragma once

#include <string>

#include "trace_collector.h"

namespace lune {
namespace trace {

std::string* BinToChromiumJson(EventsChunk *chunk);

}
}
