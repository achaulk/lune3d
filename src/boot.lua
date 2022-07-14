R"___"--(

local ffi = require('ffi')
local C = ffi.C

ffi.cdef[[
struct LuneEvent
{
	uint32_t type;
	uint32_t flags;
	double a0;
	double a1;
	double a2;
	double a3;
	double a4;
};
struct LuneEventList
{
	const struct LuneEvent *ev;
	uint32_t n;
};
void lune_firstFrame();
void lune_newFrame();
void lune_endFrame();
const struct LuneEventList* lune_popEvents();
void lune_pushEvent(uint32_t id, double a0, double a1, double a2, double a3, double a4, double a5);

void lune_sysUpdate(double dt);
]]

io = nil

lune = {
	options = {
		window = {
			title = "Untitled",
			width = 800,
			height = 600,
		},
		main_file = "main.lua",
		update_file = nil,
	},
	createJail = createJail,
}

function parseOpts(args)
end

function getConfig(fn)
	local t = lune.options
	local e = {cfg = t, args=args, love = {}, pairs=pairs, ipairs=ipairs, next=next}
	-- We need to run the config file but also we can't trust user code at this point.
	-- So create a dummy jail for it
	setfenv(fn, e)
	local ret = fn(t)
	if e.love and type(e.love.conf) == 'function' then
		e.love.conf(t)
		e.love.conf = nil
		t.love = true
		return t
	end
	return ret or t
end

local config_file = '/data/conf.lua'

local confFn = globalGetConfig or globalLuneLoadFile(config_file)


parseOpts(args)
lune.options = getConfig(confFn, lune.options)

-- This preps everything (or throws an error)
globalLuneInit(lune.options)
globalLuneInit = nil


local use_jail = true
if lune.options.requirefullaccess and not globalForceJail then
	use_jail = false
end
if use_jail then
	root_jail = lune.createJail()
else
	root_jail = _G
end
lune.options.jailed = use_jail

local main_file = '/data/' .. (lune.options.main_file or 'main.lua')


-- If we have created a jail then lune.fs.load is now jailed
local main = lune.fs.load(main_file)

local function lune_exec(callbacks)
	C.lune_firstFrame()
	while true do
		local ev = C.lune_popEvents()
		if ev.n == 0 then break end
		local p = ev.ev
		for i=0,ev.n - 1 do
			callbacks[p[i].type](p[i].a0, p[i].a1, p[i].a2, p[i].a3, p[i].a4)
		end
	end
end

local function lune_main()
	local fns = main()
	if not fns then error('main() is expected to return a table of callbacks!') end
	local cb_lookup = {}
	local nullfn = function() end

	-- If something needs to access or modify the Lua context, normally that is impossible, so
	-- queueing callback 0 lets C do whatever it wants
	cb_lookup[0] = globalLuaToCEv
	cb_lookup[1] = function(dt) C.lune_sysUpdate(dt) end
	cb_lookup[2] = function() C.lune_endFrame() end
	cb_lookup[3] = function() C.lune_newFrame() end
	for k,v in pairs(globalLuneEventMap) do
		cb_lookup[v] = fns[k] or nullfn
	end
	lune_exec(cb_lookup)
end

local function love_main()
	main()
	
	local fn = love.run()
	while true do
		fn()
	end
end

if lune.options.love or lune.options.love_compatible then
	local l = {
		audio = {},
		data = {},
		event = {},
		filesystem = {},
		font = {},
		graphics = {},
		image = {},
		joystick = {},
		keyboard = {},
		math = {},
		mouse = {},
		physics = {},
		sound = {},
		system = {},
		thread = {},
		timer = {},
		touch = {},
		video = {},
		window = {},
		_version_major = 11,
		_version_minor = 0,
		_version_revision = 0,
		getVersion = function() return l._version_major, l._version_minor, l._version_revision, "Mysterious Mysteries - Lune3D" end,
		hasDeprecationOutput = function() return false end,
		setDeprecationOutput = function() end,
		isVersionCompatible = function(a, b, c)
			if type(a) == 'string' then
				local v = string.match(a, "(%d+).%d+")
				return v <= 11
			else
				return a <= 11
			end
		end,
	}
	love = require('love2d_compat')
end

if lune.options.update_file then
end


if lune.options.love then
	return love_main
end

function bootMain(main_wrapper)
	main_wrapper()
end

return lune_main
--)___"--"
