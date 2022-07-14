#include "clock.h"

#include <assert.h>
#include <Windows.h>

namespace lune {

thread_local uint64_t ClkNow;

namespace {

uint64_t mult;
uint64_t qpf_value;

uint64_t realtime_to_time_adj = 0;

bool TryReduce(uint32_t n)
{
	if(!(mult % n) && !(qpf_value % n)) {
		mult /= n;
		qpf_value /= n;
		return true;
	}
	return false;
}

uint64_t ClkInit()
{
	// We time in microseconds
	mult = 1000000;

	LARGE_INTEGER qpf;
	if(!QueryPerformanceFrequency(&qpf))
		abort();
	qpf_value = qpf.QuadPart;
	for(;;) {
		// Try reduction by the simple prime factors
		while(TryReduce(2) || TryReduce(3) || TryReduce(5) || TryReduce(7)) {}
		// Range problems with 64-bit integer storage
		// To perform the calculation entirely within the integer domain, we calculate
		// us = sample * mult / qpf to give the current timestamp in whole microseconds.
		// We have 64 bits of data, from which we strip log2(mult) bits from the top. The timestamp
		// value is in seconds * qpf so our effective range in seconds in bits is
		// (64 - log2(mult) - log2(qpf)). We would want at least 25 bits of seconds, to allow for
		// one year runtime. It follows then that mult * qpf should be < 39 bits
		// max() is used in case of very large qpf values, which can overflow (defined unsigned overflow)
		if(max(qpf_value, mult * qpf_value) >= 1ULL << 38) {
			// Drop the lowest bit. One or both of these are probably odd and this may lead to further reductions
			mult >>= 1;
			qpf_value >>= 1;
		} else {
			break;
		}
	}
	QueryPerformanceCounter(&qpf);
	// While this doesn't measure any specific timebase, setting a zero point does prevent the above potential
	// range problem from affecting systems with extremely high uptime or high counter frequency resulting
	// in large initial values
	return qpf.QuadPart;
}

uint64_t zero = ClkInit();

} // namespace

uint64_t ClkUpdateTime()
{
	return ClkUpdateRealtime() + realtime_to_time_adj;
}

uint64_t ClkUpdateRealtime()
{
	LARGE_INTEGER now;
	QueryPerformanceCounter(&now);
	uint64_t delta = now.QuadPart - zero;
	uint64_t us = (delta * mult) / qpf_value;
	ClkNow = us;
	return us;
}

uint64_t ClkGetTime()
{
	return ClkNow + realtime_to_time_adj;
}
uint64_t ClkGetRealtime()
{
	return ClkNow;
}

void ClkAddOffset(uint64_t n)
{
	realtime_to_time_adj += n;
}

double ClkTimeSeconds()
{
	return static_cast<double>(ClkUpdateTime()) / 1000000.0;
}

} // namespace lune
