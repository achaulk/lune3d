#pragma once

#include "config.h"

#include <functional>

namespace lune {
namespace sys {

void TryCatch(const std::function<void()> &fn);

}
}
