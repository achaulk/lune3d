#pragma once

#include <stdint.h>

#include <atomic>
#include <deque>

#include "sys/sync.h"

namespace lune {

struct LuaChannelMessage
{
	void *mem;
	size_t sz;
};

struct LuaChannel
{
	LuaChannel() : refs(0) {}
	CriticalSection l;
	CondVar rv, wv;

	uint32_t rd = 0, wr = 0;
	std::deque<LuaChannelMessage> messages;

	std::string name;
	bool push_event = false;
	std::atomic<uint32_t> refs;
};

}
