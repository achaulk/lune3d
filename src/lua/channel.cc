#include "channel.h"

#include "luabuiltin.h"

#include "clock.h"
#include "logging.h"
#include "event.h"

#include <map>

namespace lune {

LUA_REGISTER_SETUP(R"(
local ffi = require "ffi"
local C = ffi.C

ffi.cdef[[
void chan_close(void*);
void* chan_ref(const char*, size_t);
void chan_lock(void*);
void chan_unlock(void*);
void chan_clear(void*);
bool chan_demand(void*, double);
uint32_t chan_push(void*, const char*, size_t, double);
uint32_t chan_get_count(void*);
bool chan_has_read(void*, uint32_t id);
const char* chan_peek_str(void*);
size_t chan_peek_sz(void*);
void chan_read(void*);
]]

local chans = setmetatable({}, {__mode="k"})

local pcall = pcall
local unpack = unpack

local channel = {}
local channel_mt = {__index=channel, __metatable="Channel", __gc = function(t) C.chan_close(chans[t]) end}

local type=type
local function ser(v)
	local t = type(v)
	if t == 'boolean' then
		return (v and 'b1') or 'b0'
	elseif t == 'string' then
		return 's' .. v
	elseif t == 'number' then
		return 'n' .. tostring(v)
	elseif t == 'table' then
		local r = '{'
		return r .. '}'
	else
		error("unsupported type for channels")
	end
end

local function deser(v)
end

local function peek_message(o)
	local s = ffi.string(C.chan_peek_str(o), C.chan_peek_size(o))
	return deser(s)
end

local function read_message(o)
	local v = peek_message(o)
	C.chan_read(o)
	return v
end


function channel:clear()
	local o = chans[self]
	C.chan_clear(o);
end

function channel:demand(timeout)
	local o = chans[self]
	C.chan_lock(o);
	local r
	if C.chan_demand(o, to or 999999999) then
		r = read_message(o)
	end
	C.chan_unlock(o);
	return r
end

function channel:peek()
	local o = chans[self]
	C.chan_lock(o);
	local r
	if C.chan_demand(o, 0) then
		r = peek_message(o)
	end
	C.chan_unlock(o);
	return r
end

function channel:performAtomic(fn, ...)
	local o = chans[self]
	C.chan_lock(o);
	local r = {pcall(fn, self, ...)}
	C.chan_unlock(o);
	if not r[1] then error(r[2]) end
	return unpack(r, 2)
end

function channel:pop()
	local o = chans[self]
	C.chan_lock(o);
	local r
	if C.chan_demand(o, 0) then
		r = read_message(o)
	end
	C.chan_unlock(o);
	return r
end

function channel:push(v)
	local o = chans[self]
	local val = ser(v)
	C.chan_lock(o);
	local id = C.chan_push(o, val, #val, 0)
	C.chan_unlock(o);
	return id
end

function channel:supply(v, to)
	local o = chans[self]
	local val = ser(v)
	C.chan_lock(o);
	local id = C.chan_push(o, val, #val, to or 999999999)
	local read = C.chan_has_read(id)
	C.chan_unlock(o);
	return read, id
end

function channel:getCount()
	local o = chans[self]
	return C.chan_get_count(o)
end

function channel:hasRead(n)
	local o = chans[self]
	C.chan_lock(o);
	local r = C.chan_has_read(o)
	C.chan_unlock(o);
	return r
end


lune.thread = lune.thread or {}

function lune.thread.getChannel(n)
	local o = C.chan_ref(n, #n)
	local t = setmetatable({}, channel_mt)
	chans[t] = o
	return t
end

function lune.thread.getSelfChannel()
	return lune.thread.getChannel("##SELF")
end

)");

namespace lune {
std::map<std::string, LuaChannel> channels;

void chan_close(LuaChannel *c)
{
	if(c->refs.fetch_sub(1, std::memory_order_acq_rel) == 1) {
		channels.erase(c->name);
	}
}
LuaChannel *chan_ref(const char *n, size_t s)
{
	auto &e = channels[std::string(n, s)];
	if(e.name.empty()) {
		e.name = std::string(n, s);
		if(e.name == "main") {
			e.push_event = true;
		}
	}
	e.refs.fetch_add(1, std::memory_order_relaxed);
	return &e;
}
void chan_lock(LuaChannel *c)
{
	c->l.lock();
}
void chan_unlock(LuaChannel *c)
{
	c->l.unlock();
}
void chan_clear(LuaChannel *c)
{
	c->l.lock();
	c->messages.clear();
	c->l.unlock();
}

bool chan_demand(LuaChannel *c, double to)
{
	if(!to)
		return !c->messages.empty();
	uint64_t target = ClkGetRealtime() + (uint64_t)(to * 1000.0);
	do {
		if(!c->messages.empty()) {
			return true;
		}
	} while(c->wv.wait_direct(c->l, (uint32_t)((target - ClkUpdateRealtime()) / 1000)));
	return false;
}

uint32_t chan_push(LuaChannel *c, const char *v, size_t l, double to)
{
	void *p = malloc(l);
	assert(p);
	memcpy(p, v, l);
	c->messages.emplace_back(LuaChannelMessage{p, l});
	uint32_t r = c->wr++;
	c->wv.notify_all();

	if(to != 0) {
		uint64_t target = ClkGetRealtime() + (uint64_t)(to * 1000.0);
		while(c->rd < r && c->wv.wait_direct(c->l, (uint32_t)((target - ClkUpdateRealtime()) / 1000)));
	}
	if(c->push_event)
		PostPendingMessage();

	return r;
}

uint32_t chan_get_count(LuaChannel *c)
{
	c->l.lock();
	uint32_t r = (uint32_t)c->messages.size();
	c->l.unlock();
	return r;
}
bool chan_has_read(LuaChannel *c, uint32_t id)
{
	return c->rd > id;
}

const void *chan_peek_str(LuaChannel *c)
{
	return c->messages.front().mem;
}
size_t chan_peek_sz(LuaChannel *c)
{
	return c->messages.front().sz;
}
void chan_read(LuaChannel *c)
{
	c->rd++;
	c->rv.notify_all();
}


LUA_REGISTER_FFI_FNS("chan", "close", &chan_close, "ref", &chan_ref, "lock", &chan_lock, "unlock", &chan_unlock,
    "clear", &chan_clear, "demand", &chan_demand, "push", &chan_push, "get_count", &chan_get_count, "has_read",
    &chan_has_read, "peek_str", &chan_peek_str, "peek_sz", &chan_peek_sz, "read", &chan_read);
} // namespace lune

}
