#pragma once

#include "lua.h"
#include "luafunctor.h"

namespace lune {
namespace internal {
void *LuaGetProcAddress(const char *name);

void RegisterGeneric(const char *name, const char *fnname, void *fn);

template<typename R, typename... Args>
inline void DoRegisterFFIImpl(const char *name, const char *fnname, R (*fn)(Args...))
{
	RegisterGeneric(name, fnname, reinterpret_cast<void *>(fn));
}

inline bool DoRegisterFFIFns(const char *name)
{
	return true;
}

template<typename T, typename... Args>
inline bool DoRegisterFFIFns(const char *name, const char *fnname, T fn, Args... args)
{
	DoRegisterFFIImpl(name, fnname, fn);
	return DoRegisterFFIFns(name, args...);
}

bool RegisterSetup(const char *text);

bool RegisterGlobal(const char *path, int (*fn)(lua_State *));

bool PrepareState(lua_State *L);

} // namespace internal

#pragma section(".force", read, write)

#ifndef CONCAT
#define CONCAT2(x, y) x##y
#define CONCAT(x, y) CONCAT2(x, y)
#endif

// Register some Lua code to be executed
#define LUA_REGISTER_SETUP(lua_text) \
	static bool CONCAT(setup_registered_L, __LINE__) = ::lune::internal::RegisterSetup(lua_text)

// Register a global to be loaded under lune.name
#define LUA_REGISTER_GLOBAL(name_path, fn) \
	static bool CONCAT(global_registered_L, __LINE__) = ::lune::internal::RegisterGlobal(name_path, fn)

// Register many FFI functions, in name/pointer pairs
#define LUA_REGISTER_FFI_FNS(name, ...) \
	static bool CONCAT(ffi_fns_registered_L, __LINE__) = ::lune::internal::DoRegisterFFIFns(name, __VA_ARGS__)
} // namespace lune
