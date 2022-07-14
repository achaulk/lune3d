#include "luabuiltin.h"

#include <string>
#include <vector>
#include <map>

namespace lune {
namespace internal {
namespace {
std::map<std::string, void *> *fn_lookup;
std::string *aggegate_init_code;
std::vector<std::pair<const char *, int (*)(lua_State *)>> *global_fns;
} // namespace

bool RegisterSetup(const char *text)
{
	if(!aggegate_init_code) {
		aggegate_init_code = new std::string(R"(
local weakk = {__mode="k"}
local internal = {blobmap=setmetatable({}, weakk)}
)");
		aggegate_init_code->reserve(16384);
	}
	aggegate_init_code->append("do \n");
	aggegate_init_code->append(text);
	aggegate_init_code->append("\nend\n");
	return true;
}

bool RegisterGlobal(const char *path, int (*fn)(lua_State *))
{
	if(!global_fns)
		global_fns = new std::vector<std::pair<const char *, int (*)(lua_State *)>>();
	global_fns->emplace_back(path, fn);
	return true;
}

void RegisterGeneric(const char *name, const char *fnname, void *fn)
{
	if(!fn_lookup) {
		fn_lookup = new std::map<std::string, void *>();
	}
	std::string n = std::string(name) + std::string(fnname);
	n = name;
	n.push_back('_');
	n.append(fnname);
	fn_lookup->emplace(std::move(n), fn);
}

void *LuaGetProcAddress(const char *name)
{
	auto it = fn_lookup->find(name);
	if(it == fn_lookup->end())
		return nullptr;
	return it->second;
}

bool PrepareState(lua_State *L)
{
	lua_getglobal(L, "lune");
	if(global_fns) {
		int top = lua_gettop(L);
		for(auto &e : *global_fns) {
			const char *name = e.first;
			const char *sub = strchr(name, '.');
			if(sub) {
				lua_pushlstring(L, name, sub - name);
				lua_pushvalue(L, -1);
				lua_rawget(L, -3);
				if(lua_isnil(L, -1)) {
					lua_newtable(L);
					lua_pushvalue(L, -3);
					lua_pushvalue(L, -2);
					lua_rawset(L, top);
				}
				name = sub + 1;
			}
			lua_pushcfunction(L, e.second);
			lua_setfield(L, -2, name);

			if(sub)
				lua_settop(L, top);
		}
	}
#ifdef _DEBUG
	lua_pushinteger(L, 1);
	lua_setfield(L, -2, "debugBuild");
#endif
	lua_setglobal(L, "lune");
	if(!aggegate_init_code)
		return true;
	return !luaL_loadbuffer(L, aggegate_init_code->data(), aggegate_init_code->size(), "[lune INIT]") &&
	       !lua_pcall(L, 0, 0, 0);
}

}
}
