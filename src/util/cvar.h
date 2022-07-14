#pragma once

#include <functional>
#include <limits>
#include <string>
#include <string_view>
#include <vector>

#include "config.h"
#include "lua/lua.h"

// Define CVARs
// eg CVAR_STR(SomeVar, "default", {.writable=false})
// creates CVAR_SomeVar which operates similar to std::string_view
// It is also exposed to lua as lune.cvars.SomeVar as a read-only value
// Optionally a callback can be specified which will get called any time the value
// is changed from anywhere

#define CVAR_STR(name, default_value, ...) \
	static ::lune::details::CVARStr LUNE_CONCAT(CVAR_, name)(#name, default_value, ::CVARInfo{__VA_ARGS__})
#define CVAR_STR_CB(name, default_value, cb, ...) \
	static ::lune::details::CVARStr LUNE_CONCAT(CVAR_, name)( \
	    #name, default_value, ::lune::details::CVARInfo{__VA_ARGS__}, cb)

#define CVAR_FLOAT(name, default_value, ...) \
	static ::lune::details::CVARFloat LUNE_CONCAT(CVAR_, name)( \
	    #name, default_value, ::lune::details::CVARInfoF{__VA_ARGS__})
#define CVAR_FLOAT_CB(name, default_value, cb, ...)      \
	static ::lune::details::CVARFloat LUNE_CONCAT(CVAR_, name)( \
	    #name, default_value, ::lune::details::CVARInfoF{__VA_ARGS__}, cb)

#define CVAR_INT(name, default_value, ...) \
	static ::lune::details::CVARInt LUNE_CONCAT(CVAR_, name)( \
	    #name, default_value, ::lune::details::CVARInfoI{__VA_ARGS__})
#define CVAR_INT_CB(name, default_value, cb, ...) \
	static ::lune::details::CVARInt LUNE_CONCAT(CVAR_, name)( \
	    #name, default_value, ::lune::details::CVARInfoI{__VA_ARGS__}, cb)

namespace lune {
namespace details {

struct CVARInfo
{
	bool readable = true;
	bool writable = true;
	const char *desc = nullptr;
};

struct CVARInfoF
{
	CVARInfo common() const;
	bool readable = true;
	bool writable = true;
	const char *desc = nullptr;

	double min = std::numeric_limits<double>::lowest(), max = std::numeric_limits<double>::max();
};

struct CVARInfoI
{
	CVARInfo common() const;
	bool readable = true;
	bool writable = true;
	const char *desc = nullptr;

	int64_t min = std::numeric_limits<int64_t>::lowest(), max = std::numeric_limits<int64_t>::max();
};

struct CVAR
{
	CVAR(const char *name, const CVARInfo &info);
	virtual ~CVAR() = default;

	virtual int LuaRead(lua_State *L) = 0;
	virtual int LuaWrite(lua_State *L) = 0;

	const char *name;
	CVARInfo info;
	CVAR *next;
};

struct CVARStr : public CVAR
{
	CVARStr(const char *name, const char *default_value, const CVARInfo &info,
	    std::function<void(const std::string &)> fn = std::function<void(const std::string &)>());

	int LuaRead(lua_State *L) override;
	int LuaWrite(lua_State *L) override;

	operator const std::string &() const
	{
		return val;
	}
	void operator=(std::string_view v);

private:
	std::string val;
	std::function<void(const std::string &)> changed;
};

struct CVARFloat : public CVAR
{
	CVARFloat(const char *name, double default_value, const CVARInfoF &info,
	    std::function<void(double)> fn = std::function<void(double)>());

	int LuaRead(lua_State *L) override;
	int LuaWrite(lua_State *L) override;

	operator const double() const
	{
		return val;
	}
	void operator=(double v);

private:
	double val;
	std::function<void(double)> changed;
	double min, max;
};

struct CVARInt : public CVAR
{
	CVARInt(const char *name, int64_t default_value, const CVARInfoI &info,
	    std::function<void(int64_t)> fn = std::function<void(int64_t)>());

	int LuaRead(lua_State *L) override;
	int LuaWrite(lua_State *L) override;

	operator const int64_t() const
	{
		return val;
	}
	void operator=(int64_t v);

private:
	int64_t val;
	std::function<void(int64_t)> changed;
	int64_t min, max;
};

} // namespace details
} // namespace lune
