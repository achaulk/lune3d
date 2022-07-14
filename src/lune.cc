#include "lune.h"

#include "lua/lua.h"
#include "lua/luabuiltin.h"

#include "sys/thread.h"

#include "io/file.h"

#include "clock.h"
#include "event.h"

#include "gfx/gfx.h"
#include "gfx/viewport.h"
#include "gfx/window.h"
#include "gfx/framegraph.h"

#include "engine.h"
#include "worker.h"

#include <deque>

LUNE_MODULE()

namespace lune {

LuneConfig g_Config;

namespace {
std::vector<std::string_view> g_args;

struct Option
{
	Option(const char *name, const char *default_value = nullptr) : arg(name)
	{
		if(default_value)
			value = default_value;
	}
	const char *arg;
	std::string value;

	operator const std::string &() const
	{
		return value;
	}
	operator std::string_view() const
	{
		return value;
	}
};

Option g_game_path("game");
Option g_log_file("log", "lune.log");
Option g_trace_file("trace");

std::vector<Option *> g_options = {&g_game_path, &g_log_file, &g_trace_file};

WindowMessageLoop g_messageloop;

const char kBootSrc[] =
#include "boot.lua"
    ;

const char kJailSrc[] =
#include "jail.lua"
    ;

const char kEngineThreadSrc[] = R"(
local ffi = require('ffi')
local C = ffi.C

ffi.cdef[[
struct LuneEngineEventRef
{
	double type;
	double id;
};

const struct LuneEngineEventRef* lune_popEngineEvent();
]]

local fns = {}

local jail = (nojail and _G) or createJail()
jail.lune.objectUpdateFuncs = fns

if g_UpdateSource then
	local fn, err = jail.loadstring(ffi.string(g_UpdateSource))
	g_UpdateSource = nil
	if not fn then error(err) end
	if fn then
		local ok, err2 = xpcall(fn, debug.traceback)
		if not ok then error(err2) end
	end
end

local function pump()
	while true do
		local ev = C.lune_popEngineEvent()
		if ev == nil then return end
		local fn = fns[ev.type]
		if fn then
			fn(ev.id, ev.type)
		end
	end
end

while true do
	local ok, err = pcall(pump)
	if not ok then
		g_engineThreadError(err)
	end
	if ok then break end
end
)";

void ProcessArgs()
{
	for(size_t i = 0; i < g_args.size(); i++) {
		auto s = g_args[i];
		if(s.substr(0, 2) == "--" && i + 1 < g_args.size()) {
			for(auto &e : g_options) {
				if(s.substr(2) == e->arg) {
					e->value = g_args[i + 1];
					g_args.erase(g_args.begin() + i, g_args.begin() + i + 2);
					i--;
				}
			}
		}
	}
}
} // namespace

std::vector<LuaEvent> g_pendingEvents;
std::vector<LuaEvent> g_currentEvents;
CriticalSection g_pendingEventsLock;
LuaEventList g_eventList;
uint64_t g_PrevFrameTimestamp;
uint64_t g_CurrentFrameTimestamp;
double g_TargetFrameTime = 1.0 / 60.0;

RefPtr<Blob> g_updateSource;

PoolThreadCommon g_PoolCommon;
TLS_DECL(PoolThreadInfo *) currentThreadInfo;

void LuneEndFrame()
{
	g_PoolCommon.swap_wait.signal_inc();

	OPTICK_EVENT();
}

void LuneNewFrame()
{
    // First clean up anything remaining from the previous frame

	uint64_t now = ClkUpdateTime();
	double raw_dt = (double)now;

#if LUNE_OPTICK
	::Optick::EndFrame();
	::Optick::Update();
	auto frame_number = ::Optick::BeginFrame();
	::Optick::Event ev(*::Optick::GetFrameDescription());
	OPTICK_TAG("Frame", frame_number);
#endif

	g_PrevFrameTimestamp = g_CurrentFrameTimestamp;
	g_CurrentFrameTimestamp = now;

	double dt = (double)(g_CurrentFrameTimestamp - g_PrevFrameTimestamp) / 1000000.0;
	if(dt < g_TargetFrameTime * 0.75) {
		uint64_t micros = (uint64_t)((g_TargetFrameTime * 0.75 - dt) * 1000000.0);
		std::this_thread::sleep_for(std::chrono::microseconds(micros));
	}

	// Pump OS messages
	g_pendingEventsLock.lock();
	g_messageloop.RunUntilIdle();

	g_pendingEvents.emplace_back(LuneToLuaEv::UserUpdate, dt);
	g_pendingEvents.emplace_back(LuneToLuaEv::SysUpdate, raw_dt);
	g_pendingEvents.emplace_back(LuneToLuaEv::UserDraw, dt);
	g_pendingEvents.emplace_back(LuneToLuaEv::Swap, raw_dt);
	g_pendingEvents.emplace_back(LuneToLuaEv::LateUserUpdate, dt);
	g_pendingEventsLock.unlock();
}

const LuaEventList *LunePopEvents()
{
	g_pendingEventsLock.lock();
	while(g_pendingEvents.empty()) {
		g_pendingEventsLock.unlock();
		g_messageloop.RunUntilHalt();
		g_pendingEventsLock.lock();
	}
	g_currentEvents.resize(0);
	g_pendingEvents.swap(g_currentEvents);
	g_pendingEventsLock.unlock();

	g_eventList.ev = g_currentEvents.data();
	g_eventList.valid = (uint32_t)g_currentEvents.size();
	return &g_eventList;
}

void PostEvent(LuneToLuaEv ev, double a0, double a1, double a2, double a3, double a4)
{
	g_pendingEventsLock.lock();
	if(g_pendingEvents.empty())
		g_messageloop.PostHalt();
	g_pendingEvents.emplace_back(ev, a0, a1, a2, a3, a4);
	g_pendingEventsLock.unlock();
}

void PostPendingMessage()
{
	g_pendingEventsLock.lock();
	if(g_pendingEvents.empty())
		g_messageloop.PostHalt();
	if(g_pendingEvents.empty() || g_pendingEvents.back().type != LuneToLuaEv::PendingChannelMessages)
		g_pendingEvents.emplace_back(LuneToLuaEv::PendingChannelMessages, 0, 0, 0, 0, 0);
	g_pendingEventsLock.unlock();
}


void OnFrameWorkDone()
{
	OPTICK_EVENT();
	gEngine->Swap();
	PostEvent(LuneToLuaEv::NewFrame);
}

void LuneFirstFrame()
{
	gEngine->InitWorkers(&g_PoolCommon);

	g_CurrentFrameTimestamp = ClkUpdateTime();
	g_pendingEvents.reserve(1000);
	g_currentEvents.reserve(1000);

	g_pendingEvents.emplace_back(LuneToLuaEv::NewFrame);

	gEngine->FirstFrame((double)g_CurrentFrameTimestamp);

	g_PoolCommon.on_frame_done = &OnFrameWorkDone;
}

void LuneSysUpdate(double dt)
{
	OPTICK_EVENT();
	g_PoolCommon.dt = dt;
	gEngine->SysUpdate(dt);
	g_PoolCommon.frame_wait.signal_inc();
}

int LuneGlobalCEv(lua_State *L)
{
	return 0;
}

const void *LunePopEngineEvent()
{
	// Process all frame phases as needed
	auto i = currentThreadInfo;
	while(!i->fn(i, i->common))
		if(i->exit)
			return nullptr;
	return &i->event;
}

LUA_REGISTER_FFI_FNS("lune", "newFrame", &LuneNewFrame, "popEvents", &LunePopEvents, "firstFrame", &LuneFirstFrame,
    "sysUpdate", &LuneSysUpdate, "pushEvent", &PostEvent, "endFrame", &LuneEndFrame, "popEngineEvent",
    &LunePopEngineEvent);

void AddCommandline(const std::vector<std::string_view> &args)
{
	g_args.insert(g_args.end(), args.begin(), args.end());
}

enum class Action
{
	Quit,
	Restart,
};

int LuneGlobalLoadFile(lua_State *L)
{
	const char *filename = luaL_checkstring(L, 1);

	auto file = safe_vfs.OpenFile(filename, file_flags::kReadOnly);
	if(!file)
		return luaL_error(L, "Can't open config file %s!", filename);

	auto blob = file.MapToBlob();
	if(!blob)
		return luaL_error(L, "Can't map config file %s!", filename);

	if(luaL_loadbufferx(L, (const char *)blob->GetData(), blob->GetSize(), filename, "t"))
		return lua_error(L);
	return 1;
}

int LuneErrorHandler(lua_State *L)
{
	lua_getglobal(L, "debug");
	if(lua_istable(L, -1)) {
		lua_getfield(L, -1, "traceback");
		lua_replace(L, -2);
		if(lua_isfunction(L, -1)) {
			if(!lua_pcall(L, 0, 1, 0))
				lua_concat(L, 2);
		} else {
			lua_pop(L, 1);
		}
	} else {
		lua_pop(L, 1);
	}
	const char *err = lua_tostring(L, -1);
	LOG("LUA Exec Error: %s", err);
	LUNE_BP();
	return 1;
}

int LuneGlobalInit(lua_State *L)
{
	if(!internal::PrepareState(L))
		return lua_error(L);
	return 0;
}

void SetGlobals(lua_State *L)
{
	lua_newtable(L);
	for(size_t i = 0; i < g_args.size(); i++) {
		lua_pushlstring(L, g_args[i].data(), g_args[i].size());
		lua_rawseti(L, -2, (int)i + 1);
	}
	lua_setglobal(L, "args");

	lua_pushcfunction(L, [](lua_State *L) -> int { return 0; });
	lua_setglobal(L, "R");

	lua_pushcfunction(L, LuneGlobalInit);
	lua_setglobal(L, "globalLuneInit");

	lua_pushcfunction(L, LuneGlobalLoadFile);
	lua_setglobal(L, "globalLuneLoadFile");

	lua_pushcfunction(L, LuneGlobalCEv);
	lua_setglobal(L, "globalLuaToCEv");

	lua_newtable(L);
#define EV(x) lua_pushinteger(L, (lua_Integer)LuneToLuaEv::x), lua_setfield(L, -2, #x)
	EV(KeyPressed);
	EV(KeyReleased);
	EV(TextInput);
	EV(MouseMoved);
	EV(MousePressed);
	EV(MouseReleased);
	EV(WheelMoved);
	EV(Focus);
	EV(MouseFocus);
	EV(Visible);
	EV(Resized);
	EV(EndFrame);
#undef EV
	lua_pushinteger(L, (lua_Integer)LuneToLuaEv::UserUpdate), lua_setfield(L, -2, "Update");
	lua_pushinteger(L, (lua_Integer)LuneToLuaEv::UserDraw), lua_setfield(L, -2, "Draw");
	lua_pushinteger(L, (lua_Integer)LuneToLuaEv::LateUserUpdate), lua_setfield(L, -2, "LateUpdate");
	lua_setglobal(L, "globalLuneEventMap");
}

bool GetField(lua_State *L, const char *field, std::string &out)
{
	bool ret = false;
	lua_getfield(L, -1, field);
	if(!lua_isnil(L, -1)) {
		out = lua_tostring(L, -1);
		ret = true;
	}
	lua_pop(L, 1);
	return ret;
}

bool GetField(lua_State *L, const char *field, int &out)
{
	bool ret = false;
	lua_getfield(L, -1, field);
	if(!lua_isnil(L, -1)) {
		out = (int)lua_tointeger(L, -1);
		ret = true;
	}
	lua_pop(L, 1);
	return ret;
}

void GetWindowOptions(lua_State *L, gfx::WindowOptions &opts)
{
	GetField(L, "title", opts.title);
	GetField(L, "x", opts.x);
	GetField(L, "y", opts.y);
	GetField(L, "width", opts.w);
	GetField(L, "height", opts.h);
}

void PoolThreadMain(std::string *err, PoolThreadInfo *info)
{
	currentThreadInfo = info;

	lua_State *L = luaL_newstate();
	lua_newtable(L);
	lua_setglobal(L, "lune");

	lua_pushcfunction(L, [](lua_State *L) -> int { return 0; });
	lua_setglobal(L, "g_engineThreadError");

	if(g_updateSource) {
		g_updateSource->wait();
		lua_pushlstring(L, (const char *)g_updateSource->GetData(), g_updateSource->GetSize());
		lua_setglobal(L, "g_UpdateSource");
	}

	lua_pushboolean(L, 1);
	lua_setglobal(L, "g_isEngineThread");

	luaL_openlibs(L);

	if(!internal::PrepareState(L)) {
		const char *err = lua_tostring(L, -1);
		abort();
	}
	if(luaL_loadbuffer(L, kJailSrc, sizeof(kJailSrc) - 1, "[lune jail.lua]") || lua_pcall(L, 0, 0, 0)) {
		const char *err = lua_tostring(L, -1);
		LOGF("Lua fail %s\n", err);
		lua_close(L);
		abort();
	}

	if(luaL_loadbuffer(L, kEngineThreadSrc, sizeof(kEngineThreadSrc) - 1, "thread.lua")) {
		const char *err = lua_tostring(L, -1);
		LOGF("Lua fail %s\n", err);
		lua_close(L);
		abort();
	}
	if(lua_pcall(L, 0, 0, 0)) {
		*err = lua_tostring(L, -1);
	}
	lua_close(L);
}

Action RunLune()
{
	lua_State *L = luaL_newstate();

	OPTICK_THREAD("Main")

	gEngine = new Engine();

	luaL_openlibs(L);
	lua_getglobal(L, "debug");
	lua_getfield(L, -1, "traceback");
	lua_replace(L, -2);
	auto errfunc = lua_gettop(L);

	// Let the app do whatever it wants
	CustomLuaSetup(L);

	SafeVFSSplit::Options opts;
	opts.data_dir = g_game_path;
	if(!SafeVFSSplit::PreInitialize(opts)) {
		EarlyFatalError("Cannot early initialize filesystem");
		lua_close(L);
		return Action::Quit;
	}

	SetGlobals(L);

	if(luaL_loadbuffer(L, kJailSrc, sizeof(kJailSrc) - 1, "[lune jail.lua]") || lua_pcall(L, 0, 0, errfunc)) {
		EarlyFatalError(lua_tostring(L, -1));
		lua_close(L);
		return Action::Quit;
	}

	if(luaL_loadbuffer(L, kBootSrc, sizeof(kBootSrc) - 1, "[lune boot.lua]") || lua_pcall(L, 0, 1, errfunc)) {
		EarlyFatalError(lua_tostring(L, -1));
		lua_close(L);
		return Action::Quit;
	}

	if(!lua_isfunction(L, -1)) {
		EarlyFatalError("Expected boot.lua to return a function!");
		lua_close(L);
		return Action::Quit;
	}

	// Do post load initialization
	lua_getglobal(L, "lune");
	lua_getfield(L, -1, "options");
	GetField(L, "identity", opts.app_name);
	GetField(L, "local_save_dir", opts.use_writable_app_dir_if_possible);

	int n_threads = -1;
	GetField(L, "worker_threads", n_threads);
	if(n_threads < 1) {
		n_threads = 8;
	}

	g_updateSource = nullptr;
	std::string update_file;
	GetField(L, "update_file", update_file);
	if(!update_file.empty()) {
		std::string s = "/data/";
		s.append(update_file);
		auto f = safe_vfs.OpenFile(s, file_flags::kAppendOnly);
		if(!f) {
			EarlyFatalError("Cannot open update file");
			lua_close(L);
			return Action::Quit;
		}
		g_updateSource = f.ReadToFutureBlob();
	}

	std::string gfxerr;
	if(!gfx::InitializeGraphicsContext(opts.app_name.c_str(), gfxerr)) {
		EarlyFatalError(gfxerr.c_str());
		lua_close(L);
		return Action::Quit;
	}

	lua_getfield(L, -1, "window");
	if(!lua_isnil(L, -1)) {
		gfx::WindowOptions windowOpts;
		GetWindowOptions(L, windowOpts);

		auto w = gfx::CreateWindow(windowOpts);
		auto s = gfx::Screen::Create(std::move(w));
		gEngine->AddScreen(std::move(s));
	}
	lua_pop(L, 1);

	lua_pop(L, 2);

	if(!SafeVFSSplit::Initialize(opts)) {
		EarlyFatalError("Cannot late initialize filesystem");
		lua_close(L);
		return Action::Quit;
	}

	struct WorkThread
	{
		std::unique_ptr<UserThread> t;
		std::string err;
		PoolThreadInfo *info;
		std::unique_ptr<uint8_t[]> mem;
	};
	std::vector<WorkThread> work_threads(n_threads);

	g_PoolCommon.num_threads = n_threads;

	uint32_t cl_size = 64;
	for(int i = 0; i < n_threads; i++) {
		work_threads[i].mem.reset(new uint8_t[cl_size * ((sizeof(PoolThreadInfo) + cl_size - 1) / cl_size)]);
		work_threads[i].info = new(work_threads[i].mem.get()) PoolThreadInfo(&g_PoolCommon);
		work_threads[i].t.reset(
		    new UserThread(std::bind(&PoolThreadMain, &work_threads[i].err, work_threads[i].info), "EngineWorkThread"));
	}

	lua_getglobal(L, "bootMain");
	lua_pushvalue(L, 2);
	lua_remove(L, 2);
	if(lua_pcall(L, 1, 0, errfunc)) {
		EarlyFatalError(lua_tostring(L, -1));
		lua_close(L);
		return Action::Quit;
	}
	for(auto &t : work_threads) { t.info->exit = true; }
	g_PoolCommon.frame_wait.signal_inc();

	for(auto &t : work_threads) { t.t->thread()->Join(); }

	Action ret = Action::Quit;
	if(lua_type(L, -1) == LUA_TSTRING && strcmp(lua_tostring(L, -1), "restart") == 0)
		ret = Action::Restart;

	gfx::DestroyGraphicsContext();

	delete gEngine;
	gEngine = nullptr;

	lua_close(L);
	return ret;
}

int LuneMain()
{
	details::InitMainThread();

	luaJIT_setgetprocaddr(&internal::LuaGetProcAddress);
	ProcessArgs();

	details::EarlyLogSetup(g_log_file, g_trace_file, true);

	while(RunLune() == Action::Restart)
		;
	OPTICK_SHUTDOWN();

	return 0;
}

} // namespace lune
