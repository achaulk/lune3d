#pragma once

// There are two types of Lua Objects
// One is implemented in CPP and called by Lua.
// One is implemented in Lua and called by CPP.
// Additionally, Lua can call either through the normal path, or the FFI.
// Finally, lifetimes can be managed by Lua, CPP, or both.

#include "lua.h"

#include "LuaUtils.h"
#include "luafunctor.h"

#include "refptr.h"

#include <map>
#include <new>
#include <optional>
#include <string>
#include <type_traits>
#include <vector>

constexpr uint64_t operator"" _id(const char *s)
{
	return 0;
}

namespace lune {

constexpr uint64_t MakeU64ID(char i0, char i1, char i2, char i3, char i4, char i5, char i6, char i7)
{
	return static_cast<uint64_t>(i0) | (static_cast<uint64_t>(i1) << 8) | (static_cast<uint64_t>(i2) << 16) |
	       (static_cast<uint64_t>(i3) << 24) | (static_cast<uint64_t>(i4) << 32) | (static_cast<uint64_t>(i5) << 40) |
	       (static_cast<uint64_t>(i6) << 48) | (static_cast<uint64_t>(i7) << 56);
}

//#define MAKE_U64_ID(i) ::lune::MakeU64ID(#i[0], #i[1], #i[2], #i[3], #i[4], #i[5], #i[6], #i[7])

// Base class for all user-accessible userdata
struct LuaUserdata
{
	template<class T>
	static T *Create(lua_State *L)
	{
		void *buf = lua_newuserdata(L, sizeof(T));
		if(!buf)
			return nullptr;
		T *p = new(buf) T();
		return p;
	}

	static int GC(lua_State *L)
	{
		LuaUserdata *ref = (LuaUserdata *)lua_touserdata(L, 1);
		if(ref && ref->gc)
			ref->gc(L, ref);
		return 0;
	}

	uint64_t type_id;
	void (*gc)(lua_State *L, LuaUserdata *) = nullptr;
	void *obj;
};

struct LuaObjRef : public LuaUserdata
{
	LuaObjRef()
	{
		gc = &OnObjGC;
	}
	static void OnObjGC(lua_State *L, LuaUserdata *ud);

	LuaObjRef *prev, *next;
};

// Include this in any class that wants to be exposed to Lua
// name should be the type of the implementing object
// parent should be the type of the parent class
// unique_id should be some unique 8 character stringizable ID
#define LUA_OBJECT_IMPL(name, parent, unique_id)                                                                  \
	using Parent = parent;                                                                                        \
	static uint64_t StaticGetTypeId()                                                                             \
	{                                                                                                             \
		return MAKE_U64_ID(unique_id);                                                                            \
	}                                                                                                             \
	uint64_t GetTypeId() override                                                                                 \
	{                                                                                                             \
		return StaticGetTypeId();                                                                                 \
	}                                                                                                             \
	void *TryCastInternal(uint64_t type_id) override                                                              \
	{                                                                                                             \
		return TryCastImpl(this, type_id);                                                                        \
	}                                                                                                             \
	static void *TryCastImpl(::lune::LuaObject *p, uint64_t type_id)                                              \
	{                                                                                                             \
		return (type_id == StaticGetTypeId()) ? (void *)static_cast<name *>(p) : Parent::TryCastImpl(p, type_id); \
	}                                                                                                             \
	static const char *StaticGetClassName()                                                                       \
	{                                                                                                             \
		return #name;                                                                                             \
	}                                                                                                             \
	const char *GetClassName()                                                                                    \
	{                                                                                                             \
		return #name;                                                                                             \
	}                                                                                                             \
	static name *TryCast(lua_State *L, int n)                                                                     \
	{                                                                                                             \
		::lune::LuaObjRef *p = (::lune::LuaObjRef *)lua_touserdata(L, n);                                         \
		if(!p || !p->obj)                                                                                         \
			return nullptr;                                                                                       \
		return reinterpret_cast<name *>(                                                                          \
		    reinterpret_cast<LuaObject *>(p->obj)->TryCastInternal(name::StaticGetTypeId()));                     \
	}                                                                                                             \
	static name *TryCast(LuaVariant v)                                                                            \
	{                                                                                                             \
		::lune::LuaObjRef *p = (::lune::LuaObjRef *)v.Userdata();                                                 \
		if(!p || !p->obj)                                                                                         \
			return nullptr;                                                                                       \
		return reinterpret_cast<name *>(                                                                          \
		    reinterpret_cast<LuaObject *>(p->obj)->TryCastInternal(name::StaticGetTypeId()));                     \
	}

// Either use this, or the DECL/IMPL pair to declare the function bindings
// Each function should be specified, first as a string that will have that name bound
// and then as the function pointer it should be bound to
// The following formats are allowed
// void f(T, T,...)
// int f(lua_State*, T, T,...)
// void C::f(T, T,...)
// int C::f(lua_State*, T, T,...)
// Functions taking a state and returning int can return values, functions returning void cannot.
// Non-static functions will automatically convert (and check) the pointer in arg 1
// Any T can be std::optional<U> to not fail if that arg is not there or does not convert
// T can be a raw pointer that derives from LuaObject, the pointer will be checked and converted
// T can be any integral or floating point
// T can be bool
// T can be LuaTable to receive a table ref
// T can be const char* or std::string_view to receive a string
#define LUA_REGISTER_FNS(...)                           \
	void RegisterNormalFunctions(lua_State *L) override \
	{                                                   \
		DoRegisterFuncs(L, __VA_ARGS__);                \
		Parent::RegisterNormalFunctions(L);             \
	}
#define LUA_REGISTER_FNS_DECL() void RegisterNormalFunctions(lua_State *L) override
#define LUA_REGISTER_FNS_IMPL(t, ...)                      \
	void t::RegisterNormalFunctions(lua_State *L) override \
	{                                                      \
		DoRegisterFuncs(L, __VA_ARGS__);                   \
		Parent::RegisterNormalFunctions(L);                \
	}

// If Lua wholly owns this object and C++ has no refs to is, this can be specified
// to provide the required definitions
#define LUA_WHOLLY_OWNED()                  \
	bool LuaOwnsReferences() const override \
	{                                       \
		return true;                        \
	}                                       \
	void LuaCallAddReference() override {}  \
	void LuaCallRemoveReference() override  \
	{                                       \
		delete this;                        \
	}

class LuaObject
{
public:
	LuaObject();
	virtual ~LuaObject();

	void LuaPushReference(lua_State *L);
	void LuaPushMetatable(lua_State *L);

	virtual void *TryCastInternal(uint64_t type_id) = 0;

protected:
	void LuaError(lua_State *L, const char *err, ...);

	void LuaRegister(lua_State *L);

	virtual uint64_t GetTypeId() = 0;
	virtual const char *GetClassName() = 0;

	static void *TryCastImpl(LuaObject *p, uint64_t type_id)
	{
		return nullptr;
	}

	/*
	Return true iff you want Lua to be able to own references to this object.

	False indicates that C controls the lifetime; you shouldn't override the reference
	counting either, otherwise the lua references may never be released and the object could leak

	You should override the ref/deref calls so lua can control the lifetime
	*/
	virtual bool LuaOwnsReferences() const
	{
		return false;
	}
	virtual void LuaCallAddReference() {}
	virtual void LuaCallRemoveReference() {}

	// For cases where lua ownership is more abstract or conditional, this can avoid the
	// dtor assert, manually cleaning up references.
	void CleanupReferences();

	template<typename... Args>
	void DoRegisterFuncs(lua_State *L, Args &&...args)
	{
		DoRegisterFuncsChunk(L, args...);
	}

	static int LuaGC(lua_State *L);
	void OnGC(lua_State *L, LuaObjRef *ref);

	virtual void RegisterNormalFunctions(lua_State *L) {}
	virtual void RegisterSpecialFunctions(lua_State *L) {}

private:
	friend struct LuaObjRef;

	struct FuncEntry
	{
		const char *name;
		int (*fn)(lua_State *);
		void *proxy[2];
	};

	LuaObjRef *InternalMakeReference(lua_State *L);

	template<typename T, typename... Args>
	void DoRegisterFuncsChunk(lua_State *L, const char *name, T fn, Args &&...args)
	{
		DoRegisterFuncsImpl(L, name, fn);
		DoRegisterFuncsChunk(L, args...);
	}
	template<typename T>
	void DoRegisterFuncsChunk(lua_State *L, const char *name, T fn)
	{
		DoRegisterFuncsImpl(L, name, fn);
	}

	template<typename T>
	void DoRegisterFuncsImpl(lua_State *L, const char *name, T fn)
	{
		LuaPushFunctor(L, fn);
		lua_setfield(L, -2, name);
	}

	void DoRegisterFuncsChunk(lua_State *L) {}

	bool lua_can_own_refs_ = false;

	LuaObjRef *head_ = nullptr;
};

template<typename T>
T *TryCast(lua_State *L, int n)
{
	LuaObjRef *p = (LuaObjRef *)lua_touserdata(L, n);
	if(!p)
		return nullptr;
	return reinterpret_cast<T *>(reinterpret_cast<LuaObject *>(p->obj)->TryCastInternal(T::StaticGetTypeId()));
}

template<typename T>
T *TryCast(LuaVariant v)
{
	LuaObjRef *p = (LuaObjRef *)v.Userdata();
	if(!p)
		return nullptr;
	return reinterpret_cast<T *>(reinterpret_cast<LuaObject *>(p->obj)->TryCastInternal(T::StaticGetTypeId()));
}

template<typename T>
class LuaImplemented : public T, public RefcountedThreadUnsafe
{
public:
	using T::T;

	void Bind(lua_State *L)
	{
		this->L = L;
		this->LuaPushReference(L);
	}

	bool LuaOwnsReferences() const override
	{
		return true;
	}
	void LuaCallAddReference() override
	{
		AddRef();
	}
	void LuaCallRemoveReference() override
	{
		Release();
	}

private:
	lua_State *L = nullptr;
};

} // namespace lune
