#include "luaobject.h"

#include <assert.h>

namespace lua {
void Push(lua_State *L, lune::LuaObject *v)
{
	v->LuaPushReference(L);
}
}

namespace lune {

void LuaObjRef::OnObjGC(lua_State *L, LuaUserdata *ud)
{
	static_cast<LuaObject*>(static_cast<LuaObjRef *>(ud)->obj)->OnGC(L, static_cast<LuaObjRef *>(ud));
}

LuaObject::LuaObject() {}
LuaObject::~LuaObject()
{
	assert(!head_ || !lua_can_own_refs_);
	// In case refs still exist, remove them
	CleanupReferences();
}

void LuaObject::LuaError(lua_State *L, const char *err, ...)
{
	assert(false);
	lua_error(L);
}

void LuaObject::CleanupReferences()
{
	LuaObjRef *ref = head_;
	while(ref) {
		ref->obj = nullptr;
		ref->type_id = 0;
		ref = ref->next;
	}
	head_ = nullptr;
}

void LuaObject::LuaPushReference(lua_State *L)
{
	lua_getfield(L, LUA_REGISTRYINDEX, "__bindings");
	if(lua_isnil(L, -1)) {
		lua_pop(L, 1);
		lua_newtable(L);
		lua_pushvalue(L, -1);
		lua_setfield(L, LUA_REGISTRYINDEX, "__bindings");
		lua_newtable(L);
		lua_pushstring(L, "v");
		lua_setfield(L, -2, "__mode");
		lua_setmetatable(L, -2);
	}
	lua_pushlightuserdata(L, this);
	lua_rawget(L, -2);
	// If ref is null, then we do not exist in this state at all
	// If ref->obj is null then we did exist, but for a previous object at this address.
	// Lua is persisting a dead reference, so we replace it.
	// If ref and ref->obj are non-null then this is a good reference we can use as-is
	LuaObjRef *ref = (LuaObjRef *)lua_touserdata(L, -1);
	if(!ref || !ref->obj) {
		lua_can_own_refs_ = LuaOwnsReferences();

		lua_pop(L, 1);
		LuaObjRef *p = InternalMakeReference(L);
		if(!p) {
			lua_pop(L, 1);
			lua_pushnil(L);
			return;
		}
		lua_pushlightuserdata(L, this);
		lua_pushvalue(L, -2);
		lua_rawset(L, -4);

		p->prev = NULL;
		p->next = head_;
		if(head_) {
			head_->prev = p;
		} else if(lua_can_own_refs_) {
			LuaCallAddReference();
		}
		head_ = p;
	}
	lua_replace(L, -2);
}

void LuaObject::LuaPushMetatable(lua_State *L)
{
	void *id = (void*)GetTypeId();
	lua_pushlightuserdata(L, id);
	lua_rawget(L, LUA_REGISTRYINDEX);
	if(lua_isnil(L, -1)) {
		lua_pop(L, 1);
		lua_newtable(L);
		lua_pushlightuserdata(L, id);
		lua_pushvalue(L, -2);
		lua_rawset(L, LUA_REGISTRYINDEX);
		LuaRegister(L);
	}
}

void LuaObject::LuaRegister(lua_State *L)
{
	lua_pushstring(L, "Protected metatable");
	lua_setfield(L, -2, "__metatable");
	lua_pushcfunction(L, &LuaUserdata::GC);
	lua_setfield(L, -2, "__gc");
	lua_newtable(L);
	RegisterNormalFunctions(L);
	lua_setfield(L, -2, "__index");
	RegisterSpecialFunctions(L);
}

LuaObjRef *LuaObject::InternalMakeReference(lua_State *L)
{
	LuaObjRef *p = LuaUserdata::Create<LuaObjRef>(L);
	p->obj = this;
	p->type_id = GetTypeId();

	LuaPushMetatable(L);
	if(lua_isnil(L, -1)) {
		lua_pop(L, 2); // also pop off the userdata
		return nullptr;
	}

	lua_setmetatable(L, -2);

	return p;
}

void LuaObject::OnGC(lua_State *L, LuaObjRef *ref)
{
	ref->obj = nullptr;
	ref->type_id = 0;
	if(ref->prev) {
		ref->prev->next = ref->next;
	}
	if(ref->next) {
		ref->next->prev = ref->prev;
	}
	if(ref == head_) {
		head_ = ref->next;
	}
	if(!head_ && lua_can_own_refs_)
		LuaCallRemoveReference();
}

}
