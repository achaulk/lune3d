#include <stdint.h>
#include <stddef.h>
#include <optional>

#include "lua.h"
#include "LuaUtils.h"

// Function adaptation
// LuaPushFunctor(L, some_func) will push a C closure that adapts to some_func
// some_func must be in one of the following formats
// void f(T, T,...)
// int f(lua_State*, T, T,...)
// void C::f(T, T,...)
// int C::f(lua_State*, T, T,...)
//
// Functions that are members of C:: will implicitly assume that arg 1 is an object
// Functions that take lua_State* pointers as their first arg can return multiple values
// T can be any integral or floating value, or const char* or std::string, or a user specialization
namespace lune {
class LuaObject;

namespace detail {

template<typename T>
struct LuaFetchValueIntegral
{
	static T Get(lua_State *L, int N)
	{
		return static_cast<T>(luaL_checkinteger(L, N));
	}
	static std::optional<T> MaybeGet(lua_State *L, int N)
	{
		if(lua_type(L, N) != LUA_TNUMBER)
			return std::optional<T>();
		return static_cast<T>(lua_tointeger(L, N));
	}
};

template<typename T>
struct LuaFetchValueFloating
{
	static T Get(lua_State *L, int N)
	{
		return static_cast<T>(luaL_checknumber(L, N));
	}
	static std::optional<T> MaybeGet(lua_State *L, int N)
	{
		if(lua_type(L, N) != LUA_TNUMBER)
			return std::optional<T>();
		return static_cast<T>(lua_tonumber(L, N));
	}
};

template<typename T>
struct LuaFetchValue;

// clang-format off
template<> struct LuaFetchValue<int16_t> : public LuaFetchValueIntegral<int16_t> {};
template<> struct LuaFetchValue<int32_t> : public LuaFetchValueIntegral<int32_t> {};
template<> struct LuaFetchValue<int64_t> : public LuaFetchValueIntegral<int64_t> {};
template<> struct LuaFetchValue<uint16_t> : public LuaFetchValueIntegral<uint16_t> {};
template<> struct LuaFetchValue<uint32_t> : public LuaFetchValueIntegral<uint32_t> {};
template<> struct LuaFetchValue<uint64_t> : public LuaFetchValueIntegral<uint64_t> {};

template<> struct LuaFetchValue<float> : public LuaFetchValueFloating<float> {};
template<> struct LuaFetchValue<double> : public LuaFetchValueFloating<double> {};

// clang-format on

template<>
struct LuaFetchValue<LuaTable>
{
	static LuaTable Get(lua_State *L, int N)
	{
		if(!lua_istable(L, N))
			luaL_argerror(L, N, "expected table");
		return LuaTable(L, N);
	}
	static std::optional<LuaTable> MaybeGet(lua_State *L, int N)
	{
		if(!lua_istable(L, N))
			return std::optional<LuaTable>();
		return std::optional<LuaTable>(LuaTable(L, N));
	}
};

template<>
struct LuaFetchValue<const char *>
{
	static const char *Get(lua_State *L, int N)
	{
		return luaL_checkstring(L, N);
	}
	static std::optional<const char *> MaybeGet(lua_State *L, int N)
	{
		if(lua_type(L, N) != LUA_TSTRING)
			return std::optional<const char *>();
		return std::optional<const char *>(lua_tostring(L, N));
	}
};

template<>
struct LuaFetchValue<std::string_view>
{
	static std::string_view Get(lua_State *L, int N)
	{
		size_t n;
		auto s = lua_tolstring(L, N, &n);
		return std::string_view(s, n);
	}
	static std::optional<std::string_view> MaybeGet(lua_State *L, int N)
	{
		if(lua_type(L, N) != LUA_TSTRING)
			return std::optional<std::string_view>();
		size_t n;
		auto s = lua_tolstring(L, N, &n);
		return std::optional<std::string_view>(std::string_view(s, n));
	}
};

template<>
struct LuaFetchValue<bool>
{
	static bool Get(lua_State *L, int N)
	{
		return static_cast<bool>(lua_toboolean(L, N));
	}
	static std::optional<bool> MaybeGet(lua_State *L, int N)
	{
		if(lua_type(L, N) != LUA_TBOOLEAN)
			return std::optional<bool>();
		return static_cast<bool>(lua_toboolean(L, N));
	}
};

template<typename T>
struct LuaFetchValue<T *>
{
	template<typename = std::enable_if_t<std::is_base_of_v<LuaObject, T>>>
	static T *Get(lua_State *L, int N)
	{
		auto p = T::TryCast(L, N);
		if(!p)
			luaL_error(L, "Expected object in arg #%d", N);
		return p;
	}
	template<typename = std::enable_if_t<std::is_base_of_v<LuaObject, T>>>
	static std::optional<T *> MaybeGet(lua_State *L, int N)
	{
		auto p = T::TryCast(L, N);
		if(!p)
			return std::optional<T>();
		return p;
	}
};

template<typename U>
struct LuaFetchValue<std::optional<U>>
{
	static std::optional<U> Get(lua_State *L, int N)
	{
		return LuaFetchValue<U>::MaybeGet(L, N);
	}
};

template<typename Tuple, class F, std::size_t... I>
constexpr decltype(auto) LuaCallFn(lua_State *L, F &&f, std::index_sequence<I...>)
{
	return std::invoke(std::forward<F>(f), LuaFetchValue<std::tuple_element<I, Tuple>::type>::Get(L, I + 1)...);
}

template<typename Tuple, class F, std::size_t... I>
constexpr decltype(auto) LuaCallFnL(lua_State *L, F &&f, std::index_sequence<I...>)
{
	return std::invoke(std::forward<F>(f), L, LuaFetchValue<std::tuple_element<I, Tuple>::type>::Get(L, I + 1)...);
}

template<unsigned incr, typename Tuple, typename C, class F, std::size_t... I>
constexpr decltype(auto) LuaCallFn(C *c, lua_State *L, F &&f, std::index_sequence<I...>)
{
	return std::invoke(std::forward<F>(f), c, LuaFetchValue<std::tuple_element<I, Tuple>::type>::Get(L, I + incr)...);
}

template<unsigned incr, typename Tuple, typename C, class F, std::size_t... I>
constexpr decltype(auto) LuaCallFnL(C *c, lua_State *L, F &&f, std::index_sequence<I...>)
{
	return std::invoke(
	    std::forward<F>(f), c, L, LuaFetchValue<std::tuple_element<I, Tuple>::type>::Get(L, I + incr)...);
}

} // namespace detail

template<typename R, typename... Args>
inline void LuaPushFunctor(lua_State *L, R (*fn)(Args...))
{
	lua_pushlightuserdata(L, fn);
	lua_pushcclosure(
	    L,
	    [](lua_State *L) -> int {
		    R (*fn)(Args...) = (R(*)(Args...))lua_touserdata(L, lua_upvalueindex(1));
		    detail::LuaCallFn<std::tuple<Args...>>(L, fn, std::make_index_sequence<sizeof...(Args)>{});
		    return 0;
	    },
	    1);
}

template<typename R, typename... Args>
inline void LuaPushFunctor(lua_State *L, R (*fn)(lua_State *, Args...))
{
	lua_pushlightuserdata(L, fn);
	lua_pushcclosure(
	    L,
	    [](lua_State *L) -> int {
		    R (*fn)(lua_State *, Args...) = (R(*)(lua_State *, Args...))lua_touserdata(L, lua_upvalueindex(1));
		    return detail::LuaCallFnL<std::tuple<Args...>>(L, fn, std::make_index_sequence<sizeof...(Args)>{});
	    },
	    1);
}

template<typename R, typename C, typename... Args>
inline void LuaPushFunctor(lua_State *L, R (C::*fn)(Args...))
{
	static_assert(sizeof(fn) <= 2 * sizeof(void *));
	void *fnraw[2] = {0, 0};
	memcpy(fnraw, &fn, sizeof(fn));

	lua_pushlightuserdata(L, fnraw[0]);
	lua_pushlightuserdata(L, fnraw[1]);
	lua_pushcclosure(
	    L,
	    [](lua_State *L) -> int {
		    C *c = C::TryCast(L, 1);
		    if(!c)
			    return luaL_error(L, "Expected object");

		    void *raw[2] = {lua_touserdata(L, lua_upvalueindex(1)), lua_touserdata(L, lua_upvalueindex(2))};
		    R (C::*fn)(Args...);
		    memcpy(&fn, raw, sizeof(fn));
		    detail::LuaCallFn<2, std::tuple<Args...>>(c, L, fn, std::make_index_sequence<sizeof...(Args)>{});
		    return 0;
	    },
	    2);
}

template<typename R, typename C, typename... Args>
inline void LuaPushFunctor(lua_State *L, R (C::*fn)(lua_State *, Args...))
{
	static_assert(sizeof(fn) <= 2 * sizeof(void *));
	void *fnraw[2];
	memcpy(fnraw, &fn, sizeof(fn));

	lua_pushlightuserdata(L, fnraw[0]);
	lua_pushlightuserdata(L, fnraw[1]);
	lua_pushcclosure(
	    L,
	    [](lua_State *L) -> int {
		    C *c = C::TryCast(L, 1);
		    if(!c)
			    return luaL_error(L, "Expected object");

		    void *raw[2] = {lua_touserdata(L, lua_upvalueindex(1)), lua_touserdata(L, lua_upvalueindex(2))};
		    R (C::*fn)(Args...);
		    memcpy(&fn, raw, sizeof(fn));
		    return detail::LuaCallFn<2, std::tuple<Args...>>(c, L, fn, std::make_index_sequence<sizeof...(Args)>{});
	    },
	    2);
}

inline void LuaPushFunctor(lua_State *L, lua_CFunction fn)
{
	lua_pushcfunction(L, fn);
}

template<typename C>
inline void LuaPushFunctor(lua_State *L, int (C::*fn)(lua_State *))
{
	static_assert(sizeof(fn) <= 2 * sizeof(void *));
	void *fnraw[2] = {0, 0};
	memcpy(fnraw, &fn, sizeof(fn));

	lua_pushlightuserdata(L, fnraw[0]);
	lua_pushlightuserdata(L, fnraw[1]);
	lua_pushcclosure(
	    L,
	    [](lua_State *L) -> int {
		    C *c = C::TryCast(L, 1);
		    if(!c)
			    return luaL_error(L, "Expected object");
		    void *raw[2] = {lua_touserdata(L, lua_upvalueindex(1)), lua_touserdata(L, lua_upvalueindex(2))};
		    int (C::*fn)(lua_State *);
		    memcpy(&fn, raw, sizeof(fn));
		    return (c->*fn)(L);
	    },
	    2);
}

} // namespace lune
