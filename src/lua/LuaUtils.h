#ifndef __LUAUTILS_H__
#define __LUAUTILS_H__

#include "lua.h"

#include "refptr.h"

#include <type_traits>
#include <string>
#include <vector>

class LuaVariant;
class LuaFunction;
class LuaTable;
namespace lune{
class LuaObject;
}

namespace lua
{

inline void Push(lua_State *L, bool v)
{
	lua_pushboolean(L, v);
}

template<typename T, typename = std::enable_if<std::is_integral_v<T>>::type>
inline void Push(lua_State *L, T v)
{
	lua_pushinteger(L, static_cast<lua_Integer>(v));
}

inline void Push(lua_State *L, lua_Number v)
{
	lua_pushnumber(L, v);
}

inline void Push(lua_State *L, const char *s, size_t n)
{
	lua_pushlstring(L, s, n);
}

inline void Push(lua_State *L, const char *s)
{
	lua_pushstring(L, s);
}

template<size_t N>
inline void Push(lua_State *L, const char (&s)[N])
{
	lua_pushlstring(L, s, N);
}

inline void Push(lua_State *L, const std::string &s)
{
	lua_pushlstring(L, s.data(), s.size());
}

inline void Push(lua_State *L, const std::string_view &s)
{
	lua_pushlstring(L, s.data(), s.size());
}

void Push(lua_State *L, lune::LuaObject *v);

template<typename T, typename = std::enable_if_t<!std::is_base_of_v<lune::LuaObject, T>>>
inline void Push(lua_State *L, T *p)
{
	lua_pushlightuserdata(L, p);
}

template<typename T>
inline void Push(lua_State *L, const std::unique_ptr<T> &p)
{
	Push(L, p.get());
}

template<typename T>
inline void Push(lua_State *L, const std::shared_ptr<T> &p)
{
	Push(L, p.get());
}

} // namespace lua

class LuaVariant
{
    friend LuaTable;
	friend LuaFunction;
public:
	LuaVariant() { L = nullptr; At = 0; }

	LuaVariant(lua_State *L)
	{
		this->L = L;
		At = lua_gettop(L);
	}
	LuaVariant(lua_State *L, long at)
	{
		this->L = L;
		At = at < 0 ? lua_gettop(L)+at+1 : at;
	}
	LuaVariant(const LuaVariant& r) : L(r.L), At(r.At) {}
	LuaVariant(LuaVariant&& r) : L(r.L), At(r.At) {}

	lua_State *state() const { return L; }
	long at() const { return At; }

	bool Exists() const { return L && At <= lua_gettop(L) && !lua_isnil(L, At); }

    int Index() const { return At; }
	int Type() const { return lua_type(L, At); }
	bool Bool() const { return lua_toboolean(L, At) != 0; }
	lua_Integer Integer() const { return lua_tointeger(L, At); }
	float Float() const { return (float)lua_tonumber(L, At); }
	double Double() const { return lua_tonumber(L, At); }
	const char *String() const { return lua_tostring(L, At); }
	const char *SafeString() const
	{
		const char *s = lua_tostring(L, At);
		return s ? s : "";
	}
	std::string_view StringView() const
	{
		size_t n;
		auto p = lua_tolstring(L, At, &n);
		return std::string_view(p, n);
	}
	LuaTable Table() const;
	LuaFunction Function() const;
	void *Userdata() const { return lua_touserdata(L, At); }

	int Reference() { lua_pushvalue(L, At); return luaL_ref(L, LUA_REGISTRYINDEX); }
	void Push() { lua_pushvalue(L, At); }

	bool IsString() const { return Type() == LUA_TSTRING; }
	bool IsTable() const { return Type() == LUA_TTABLE; }
	bool IsFunction() const { return Type() == LUA_TFUNCTION; }

	bool operator == (LuaVariant& r) const
	{
		if(L == r.L)
			return lua_equal(L, At, r.At) != 0;
		return false;
	}

	bool operator==(const char *str) const
	{
		size_t len;
		auto p = lua_tolstring(L, At, &len);
		if(!str || !p)
			return false;
		return !strcmp(str, p);
	}

	LuaVariant& operator + (LuaVariant& r)
	{
		if(IsString() && r.IsString()) {
			lua_pushvalue(L, At);
			if(L == r.L) {
				lua_pushvalue(L, r.At);
			} else {
				size_t n;
				const char *str = lua_tolstring(r.L, r.At, &n);
				lua_pushlstring(L, str, n);
			}
			lua_concat(L, 2);
		} else
			return *this;

		lua_replace(L, At);
		return *this;
	}

	operator LuaTable();

	bool operator()() const { return Exists(); }
	operator bool() const { return Bool(); }
	operator int() const { return (int)Integer(); }
	operator unsigned int() const { return (unsigned int)Integer(); }
	operator long() const { return (long)Integer(); }
	operator unsigned long() const { return (unsigned long)Integer(); }
	operator float() const { return (float)Float(); }
	operator double() const { return Float(); }
	operator const char *() const { return String(); }

private:
	long At;
	lua_State *L;
};

class LuaFunction
{
public:
	LuaFunction() { L = 0; At = 0; }
	LuaFunction(lua_State *L)
	{
		this->L = L;
		At = lua_gettop(L);
	}
	LuaFunction(lua_State *L, long at)
	{
		this->L = L;
		At = at < 0 ? lua_gettop(L)+at+1 : at;
	}
    LuaFunction(LuaVariant& v)
    {
        this->L = v.L;
        At = v.At;
    }

	bool Exists() { return L && At <= lua_gettop(L) && lua_isfunction(L, At); }

	bool operator()(int nr) {
		if(!Exists()) {
			lua_pushstring(L, "trying to call something that isn't a function!");
			return false;
		}

		int top = lua_gettop(L);
		return lua_pcall(L, top - At, nr, 0) == 0;
	}

	std::string error() {
		std::string s;
		const char *str;
		size_t n;
		str = lua_tolstring(L, At, &n);
		s.assign(str, n);
		if(lua_gettop(L) == At)
			lua_pop(L, 1);
		return std::move(s);
	}

private:
	long At;
	lua_State *L;
};


class LuaTable
{
public:
	LuaTable() { L = 0; At = 0; }
	LuaTable(lua_State *L)
	{
		this->L = L;
		At = lua_gettop(L);
	}
	LuaTable(lua_State *L, long at)
	{
		this->L = L;
		At = at < 0 ? lua_gettop(L)+at+1 : at;
	}
    LuaTable(LuaVariant& v)
    {
        this->L = v.L;
        At = v.At;
    }
	LuaTable(const LuaTable& r) : L(r.L), At(r.At) {}
	LuaTable(LuaTable&& r) : L(r.L), At(r.At) {}

	static LuaTable Make(lua_State *L)
	{
		lua_newtable(L);
		return LuaTable(L);
	}

	template<typename K>
	LuaTable MakeSubTable(K&& k)
	{
		lua_newtable(L);
		lua::Push(L, k);
		lua_pushvalue(L, -2);
		lua_rawset(L, At);
		return LuaTable(L);
	}

	template<typename V>
	void SetArray(const std::vector<V> &v)
	{
		for(size_t i = 0; i < v.size(); i++) {
			lua::Push(L, v[i]);
			lua_rawseti(L, At, (int)(i + 1));
		}
	}

	template<typename K, typename V>
	void Set(K&& k, V&& v)
	{
		lua::Push(L, k);
		lua::Push(L, v);
		lua_rawset(L, At);
	}

	LuaTable& operator = (const LuaTable& r) { L=r.L; At=r.At; return *this; }
	LuaTable& operator = (const LuaVariant& r) { L=r.L; At=r.At; return *this; }

//	operator lua_State* () { return L; }

	lua_State *state() const { return L; }
	int at() const
	{
		return At;
	}

	bool Exists() const { return L && lua_istable(L, At); }
	operator bool() const { return Exists(); }

	LuaVariant Get(const char *s)
	{
		lua_checkstack(L, 8);
		lua_getfield(L, At, s);
		return LuaVariant(L);
	}
	LuaVariant RawGet(const char *s)
	{
		lua_checkstack(L, 8);
		lua_pushstring(L, s);
		lua_rawget(L, At);
		return LuaVariant(L);
	}
	LuaVariant Get(long k)
	{
		lua_checkstack(L, 8);
		lua_pushinteger(L, k);
		lua_gettable(L, At);
		return LuaVariant(L);
	}
	LuaVariant RawGet(long k)
	{
		lua_checkstack(L, 8);
		lua_rawgeti(L, At, k);
		return LuaVariant(L);
	}

	LuaVariant operator[] (const char *s) { return Get(s); }
	LuaVariant operator[] (int s) { return Get(s); }
	LuaVariant operator() (const char *s) { return Get(s); }
	LuaVariant operator() (int s) { return Get(s); }
	bool operator() () { return Exists(); }

	template<class T>
	T GetDefault (const char *s, T defaultValue) {
		if(!Exists())
			return defaultValue;
		LuaVariant v(Get(s));
		return v.Exists() ? v : defaultValue;
	}

	template<class T>
	T GetDefault (int s, T defaultValue) {
		if(!Exists())
			return defaultValue;
		LuaVariant v(Get(s));
		return v.Exists() ? v : defaultValue;
	}

	template<class T>
	T operator() (const char *s, T v) { return GetDefault(s, v); }

	void Push() const { lua_pushvalue(L, At); }

	class iterator
	{
	public:
		iterator() { Done = false; Self = nullptr; }
		iterator(bool done) { Done = done; Self = nullptr; }
		iterator(iterator&& r) : Done(r.Done), Self(r.Self), Depth(r.Depth) { r.Self = nullptr; }
		iterator(const LuaTable& t) { Done = false; Self = &t; lua_pushnil(t.L); Depth = lua_gettop(t.L); Next(); }
		~iterator() { if(Self)lua_settop(Self->L, Depth-1); }

		std::pair<LuaVariant, LuaVariant> operator * () { return std::pair<LuaVariant, LuaVariant>(Key(), Value()); }

		LuaVariant Key() { return LuaVariant(Self->L, Depth+2); }
		LuaVariant Value() { return LuaVariant(Self->L, Depth+1); }

		bool operator == (const iterator& r) const { return (Done == r.Done); }
		bool operator != (const iterator& r) const { return (Done != r.Done); }

		void operator ++ () { Next(); }
		void operator ++ (int) { Next(); }

		bool valid() { return !Done; }
		bool good() { return valid(); }
		bool next() { return Next(); }

	private:
		bool Next()
		{
			lua_settop(Self->L, Depth);
			if(!lua_next(Self->L, Self->At)) {
				Done = true;
				return false;
			}

			lua_pushvalue(Self->L, -2);

			return true;
		}

		const LuaTable *Self;
		int Depth;
		bool Done;
	};

	iterator begin() const { return iterator(*this); }
	iterator end() const { return iterator(true); }

private:
	long At;
	lua_State *L;
};

inline LuaTable::iterator begin(const LuaTable& t)
{
	return t.begin();
}
inline LuaTable::iterator end(const LuaTable& t)
{
	return t.end();
}

inline LuaVariant::operator LuaTable() { return LuaTable(L, At); }
inline LuaTable LuaVariant::Table() const { return LuaTable(L, At); }
inline LuaFunction LuaVariant::Function() const { return LuaFunction(L, At); }

class LuaAutoStack
{
public:
	LuaAutoStack(lua_State *L) : L(L)
	{
		index = lua_gettop(L);
	}
	~LuaAutoStack()
	{
		lua_settop(L, index);
	}

private:
	lua_State *L;
	int index;
};

class LuaReference
{
public:
	LuaReference(lua_State *L, int index) : L(L), ref(luaL_ref(L, LUA_REGISTRYINDEX)) {}
	LuaReference(const LuaTable& t)
	{
		L = t.state();
		t.Push();
		ref = luaL_ref(L, LUA_REGISTRYINDEX);
	}
	~LuaReference()
	{
		if(ref != LUA_NOREF)
			luaL_unref(L, LUA_REGISTRYINDEX, ref);
	}

	LuaReference(const LuaReference &) = delete;
	void operator=(const LuaReference &) = delete;

	operator bool() const { return ref != LUA_NOREF; }
	void push() const
	{
		lua_rawgeti(L, LUA_REGISTRYINDEX, ref);
	}

	lua_State *state() const { return L; }

private:
	lua_State *L;
	int ref = LUA_NOREF;
};

class LuaCache
{
public:
	LuaCache(const LuaReference *table, const char *name) : table(table), name(name), L(table->state()) {}
	~LuaCache()
	{
		if(ref != LUA_NOREF && ref != LUA_REFNIL)
			luaL_unref(L, LUA_REGISTRYINDEX, ref);
	}

	LuaCache(const LuaCache &) = delete;
	void operator=(const LuaCache &) = delete;

	bool push()
	{
		if(ref == LUA_NOREF) {
			table->push();
			if(lua_type(L, -1) == LUA_TTABLE) {
				// Okay the table exists
				lua_pushstring(L, name);
				lua_gettable(L, -2);
				lua_replace(L, -2);
			}

			if(lua_isnil(L, -1)) {
				lua_pop(L, 1);
				ref = LUA_REFNIL;
			} else {
				ref = luaL_ref(L, LUA_REGISTRYINDEX);
			}
		}
		if(ref == LUA_REFNIL)
			return false;
		lua_rawgeti(L, LUA_REGISTRYINDEX, ref);
		return true;
	}

private:
	const LuaReference *table;
	const char *name;
	lua_State *L;
	int ref = LUA_NOREF;
};

namespace lua {
typedef LuaTable Table;
typedef LuaFunction Function;
typedef LuaVariant Variant;
typedef LuaAutoStack AutoStack;
//typedef LuaUserdata Userdata;
}

#endif
