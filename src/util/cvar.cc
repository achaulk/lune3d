#include "cvar.h"

#include <algorithm>

#include "lua/luabuiltin.h"

namespace lune {
namespace {
details::CVAR *gHeadCVAR = nullptr;

details::CVAR *Find(std::string_view name)
{
	for(auto p = gHeadCVAR; p; p = p->next) {
		if(name == p->name)
			return p;
	}
	return nullptr;
}

int lua_ReadCVAR(lua_State *L)
{
	lua_settop(L, 2);
	size_t n;
	const char *s = luaL_checklstring(L, 1, &n);
	auto cvar = Find(std::string_view(s, n));
	if(!cvar)
		return luaL_error(L, "no CVAR %s", s);
	if(!cvar->info.readable)
		return luaL_error(L, "CVAR %s not readable", s);
	lua_replace(L, -2);
	return cvar->LuaRead(L);
}
int lua_WriteCVAR(lua_State *L)
{
	lua_settop(L, 2);
	size_t n;
	const char *s = luaL_checklstring(L, 1, &n);
	auto cvar = Find(std::string_view(s, n));
	if(!cvar)
		return luaL_error(L, "no CVAR %s", s);
	if(!cvar->info.writable)
		return luaL_error(L, "CVAR %s not writable", s);
	lua_replace(L, -2);
	return cvar->LuaWrite(L);
}
int lua_GetCVARs(lua_State *L)
{
	lua_newtable(L);
	for(auto p = gHeadCVAR; p; p = p->next) {
		lua_newtable(L);
		lua_pushboolean(L, p->info.readable);
		lua_setfield(L, -2, "r");
		lua_pushboolean(L, p->info.writable);
		lua_setfield(L, -2, "w");
		if(p->info.desc) {
			lua_pushstring(L, p->info.desc);
			lua_setfield(L, -2, "description");
		}
		if(p->info.readable) {
			p->LuaRead(L);
			lua_setfield(L, -2, "value");
		}
		lua_setfield(L, -2, p->name);
	}
	return 1;
}

LUA_REGISTER_GLOBAL("readCVAR", lua_ReadCVAR);
LUA_REGISTER_GLOBAL("writeCVAR", lua_WriteCVAR);
LUA_REGISTER_GLOBAL("getCVARs", lua_GetCVARs);

LUA_REGISTER_SETUP(R"(
lune.cvars = setmetatable({}, {__index=lune.readCVAR, __newindex=lune.writeCVAR})
)");

}

namespace details {
CVAR::CVAR(const char *name, const CVARInfo &info) : name(name), info(info), next(gHeadCVAR)
{
	gHeadCVAR = this;
}


CVARInfo CVARInfoF::common() const
{
	return CVARInfo{readable, writable, desc};
}
CVARInfo CVARInfoI::common() const
{
	return CVARInfo{readable, writable, desc};
}


CVARStr::CVARStr(
    const char *name, const char *default_value, const CVARInfo &info, std::function<void(const std::string &)> fn)
    : CVAR(name, info), val(default_value), changed(std::move(fn))
{
}

int CVARStr::LuaRead(lua_State *L)
{
	lua_pushlstring(L, val.data(), val.size());
	return 1;
}

int CVARStr::LuaWrite(lua_State *L)
{
	size_t n;
	const char *s = luaL_checklstring(L, 1, &n);
	*this = std::string_view(s, n);
	return 0;
}

void CVARStr::operator=(std::string_view v)
{
	val = v;
	if(changed)
		changed(val);
}


CVARFloat::CVARFloat(const char *name, double default_value, const CVARInfoF &info, std::function<void(double)> fn)
    : CVAR(name, info.common()), val(std::clamp(default_value, info.min, info.max)), changed(std::move(fn))
{
	min = info.min;
	max = info.max;
}

int CVARFloat::LuaRead(lua_State *L)
{
	lua_pushnumber(L, val);
	return 1;
}
int CVARFloat::LuaWrite(lua_State *L)
{
	double v = luaL_checknumber(L, 1);
	*this = v;
	return 0;
}

void CVARFloat::operator=(double v)
{
	v = std::clamp(v, min, max);
	if(val == v)
		return;
	val = v;
	if(changed)
		changed(v);
}


CVARInt::CVARInt(const char *name, int64_t default_value, const CVARInfoI &info, std::function<void(int64_t)> fn)
    : CVAR(name, info.common()), val(std::clamp(default_value, info.min, info.max)), changed(std::move(fn))
{
	min = info.min;
	max = info.max;
}

int CVARInt::LuaRead(lua_State *L)
{
	lua_pushnumber(L, static_cast<double>(val));
	return 1;
}
int CVARInt::LuaWrite(lua_State *L)
{
	double v = luaL_checknumber(L, 1);
	*this = static_cast<int64_t>(v);
	return 0;
}

void CVARInt::operator=(int64_t v)
{
	v = std::clamp(v, min, max);
	if(val == v)
		return;
	val = v;
	if(changed)
		changed(v);
}

} // namespace details
} // namespace lune
