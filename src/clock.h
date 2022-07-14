#pragma once

#include <stdint.h>

namespace lune {

// Update ClkNow and return the new value
uint64_t ClkUpdateTime();
uint64_t ClkUpdateRealtime();

uint64_t ClkGetTime();
uint64_t ClkGetRealtime();

void ClkAddOffset(uint64_t n);

double ClkTimeSeconds();

}
