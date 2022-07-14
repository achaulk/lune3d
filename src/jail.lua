R"___"--(
local bit = require("bit") or {}
local coroutine = coroutine
local math = math
local table = table
local string = string
local type = type
local error = error
local gmt = getmetatable
local smt = setmetatable
local real_loadstring = loadstring
local real_setfenv = setfenv
local real_getfenv = getfenv

-- Prevent silly things like changing the metatable of strings
local function safe_gmt(o)
	return (type(o) == 'table') and gmt(o)
end

local function safe_smt(o, mt)
	return (type(o) == 'table') and smt(o, mt)
end

local blacklist = {ffi=true, debug=true, jit=true}
local function safe_require(m, t)
	if blacklist[m] then
		error("can't load " .. m)
	end
	local v = package.loaded[m]
	if v then
		return v
	end
	local fn, err = lune.fs.load(m .. '.lua')
	if err then error(err) end
	setfenv(fn, t)
	local ret = fn(m)
	package.loaded[m] = ret or true
	return ret
end

local function traceback(t)
	return debug.traceback((type(t) == 'thread') and t)
end

local function deep_copy(t)
	local ret = {}
	for k,v in pairs(t) do
		if type(v) == 'table' then v = deep_copy(v) end
		ret[k] = v
	end
	return ret
end

function createJail(mini)
	local t = {
		assert = assert, error = error, ipairs = ipairs, next = next, pairs = pairs,
		pcall = pcall, tonumber = tonumber, tostring = tostring, type = type,
		unpack = unpack, pcall = pcall, select = select, xpcall = xpcall,
		print = print,
		getmetatable = safe_gmt,
		setmetatable = safe_smt,
		debug = { traceback = traceback },
		coroutine = { create = coroutine.create, resume = coroutine.resume, running = coroutine.running,
			status = coroutine.status, wrap = coroutine.wrap, yield = coroutine.yield},
		string = { byte = string.byte, char = string.char, find = string.find, 
			format = string.format, gmatch = string.gmatch, gsub = string.gsub, 
			len = string.len, lower = string.lower, match = string.match, 
			rep = string.rep, reverse = string.reverse, sub = string.sub, 
			upper = string.upper },
		table = { insert = table.insert, maxn = table.maxn, remove = table.remove, 
			sort = table.sort, concat = table.concat },
		math = { abs = math.abs, acos = math.acos, asin = math.asin, 
			atan = math.atan, atan2 = math.atan2, ceil = math.ceil, cos = math.cos, 
			cosh = math.cosh, deg = math.deg, exp = math.exp, floor = math.floor, 
			fmod = math.fmod, frexp = math.frexp, huge = math.huge, 
			ldexp = math.ldexp, log = math.log, log10 = math.log10, max = math.max, 
			min = math.min, modf = math.modf, pi = math.pi, pow = math.pow, 
			rad = math.rad, random = math.random, sin = math.sin, sinh = math.sinh, 
			sqrt = math.sqrt, tan = math.tan, tanh = math.tanh, round = math.round },
		bit = { tobit = bit.tobit, tohex = bit.tohex, bnot = bit.bnot, band = bit.band,
			bor = bit.bor, bxor = bit.bxor, lshift = bit.lshift, rshift = bit.rshift,
			arshift = bit.arshift, rol = bit.rol, ror = bit.ror, bswap = bit.bswap },
	}
	t._G = t

	-- minijails can't access any eval-like or env functionality, or engine functionality
	if not mini then
		local envs = setmetatable({t=true}, {__mode="k", __metatable=false})
		t._ENVS = envs

		function t.loadstring(s)
			if type(s) ~= "string" then return error("loadstring called with non-string " .. type(s)) end
			if s:sub(1, 4) == "\27Lua" then return error("Loading binary chunks is not allowed") end
			local f, err = real_loadstring(s)
			if not f then return nil, err end
			real_setfenv(f, t)
			return f
		end

		function t.setfenv(f, nt)
			if type(f) ~= "function" then return end
			local rt = real_getfenv(f)
			-- Permit changing the environment of functions owned by this jail
			-- There should be no way of getting a ref to the main environment, so this
			-- should be fine and allows subjails to exist
			if rt ~= t then return end
			real_setfenv(f, nt)
			return f
		end

		function t.getfenv(f)
			if type(f) ~= "function" then return end
			local env = real_getfenv(f)
			-- Only allow getting a ref to environments the jail knows about
			if t._ENVS[env] then return env end
		end

		function t.require(m)
			return safe_require(m, t)
		end

		local l = deep_copy(lune)

		l.fs.load = function(n)
			local chunk, err = lune.fs.load(n)
			if type(chunk) == 'function' then real_setfenv(chunk, t) end
			return chunk, err
		end

		t.lune = l
	end

	local up = real_getfenv(2)
	if up._ENVS and type(up._ENVS) == 'table' then up._ENVS[t] = true end

	return t
end
--)___"--"
