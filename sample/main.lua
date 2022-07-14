--[[
A lune application's main loop goes through several phases:
Every frame starts the same
Events -> Update -> SysUpdate -> Draw -> LateUpdate

Update occurs immediately after input processing.
The primary responsibilities here are to process input and trigger anything that must be shown
this current frame. It must also notify any offscreen framebuffers that they need to update.
This should do the minimum amount of work necessary. Complex updates should be deferred to LateUpdate
as this occurs synchronously before any drawing is issued

SysUpdate is an internal phase. It ticks physics, objects and kicks off the draw of all active screens

Draw is responsible for drawing any custom elements for any viewport that has enabled user draw.
Any actions in draw logically take place after any other drawing has occurred

LateUpdate is designed to be run after any asynchronous processing is started. If complex updates are needed
this is the place to do them




APIs

lune.gfx

lune.gfx.Device
Contains information about the established Vulkan device and physical device

lune.gfx.TextureFormats
Contains every valid texture format family that the engine and the device support
Both in array and key=true form, eg {"RGBA8", RGBA8 = true}.
Attempting to use a format not enabled in here is an error.

lune.gfx.createScreen(opts)
- opts has the same form as config.window
- If opts.offscreen is true then an offscreen render target will be created. in this case
opts.format and opts.element have the same meanings as in createViewport. Otherwise a
system window will be created with a valid format. If present opts.format and opts.element
act as suggestions, defaulting to RGBA8, SRGB
- It will create a new screen and return a Screen object

lune.gfx.createWorld()
- Creates a World

lune.gfx.createFrameGraph()
- Creates a FrameGraph object

lune.gfx.setFrameGraph(g)
- Sets a FrameGraph as the current graph. The current framegraph will be traversed in SysUpdate


FrameGraph
A Frame Graph consists of some number of passes
Each pass produces some number of outputs from some number of inputs
:bindOutput(index, screen)
- binds the specified Screen to the specified zero-based index
:addPass(name, [type])
- adds and returns a new FrameGraphPass with the given name and type (compute or graphics, defaults to graphics)
:setBackbuffer(index, name)
- sets the backbuffer for output at index to the named buffer
:setPersist(name)
- some dependencies are cyclical, this specifies that frame N's named buffer comes from frame N-1
:build()
- prior to use the FrameGraph must be built, the same after any modifications
- returns nil on success, or an error string
:setConditionalDraw(name, enable)
- sets if the conditional pass should draw this frame

FrameGraphPass
:addColorOutput(name, info, source)
:addBufferOutput(name, info, source)
:setDepthStencilOutput(name, info, source)
- create an output buffer with the specified name
- if info is specified then this buffer must be of the provided type
- if source is specified then an input is automatically added with that name
:addColorInput(name, info)
:addBufferInput(name, info)
:setDepthStencilInput(name, info)
- create an input buffer with the specified name
- if info is specified then this buffer must be of the provided type
:setConditional()
- set that this pass should be conditionally enabled
:getTaskGraph()
- returns the TaskGraph for this pass

TaskGraph
A Task Graph handles dispatching draw commands for a pass
The TaskGraph processes several phases in-order
EarlyDraw, EarlyOpaque*, Opaque*, LateDraw*, LateDrawOrdered, Translucent, TranslucentUnordered*, PostDraw*, UI
Phases marked with * can issue out-of-order
Unlike FrameGraph a TaskGraph can be modified frame-to-frame without rebuilding, but it
must be modified by the end of Update in order to take effect
:addCamera(camera)
- adds a camera to draw a view of a World
:addTask(phase, task)
- add a given task to the named phase

Usage example, a deferred rendering + HDR pass

fg = lune.gfx.createFrameGraph()
local deferred = fg:addPass('deferred')
deferred:addColorOutput('emissive')
deferred:addColorOutput('albedo')
deferred:addColorOutput('normal')
deferred:addColorOutput('diffuse')
deferred:setDepthStencilOutput('depth')

local lighting = fg:addPass('lighting')
lighting:addColorInput('emissive')
lighting:addColorInput('albedo')
lighting:addColorInput('normal')
lighting:addColorInput('diffuse')
lighting:setDepthStencilInput('depth')
lighting:addColorOutput('HDR')

local color = fg:addPass('color')
color:addColorInput('HDR')
color:addColorOutput('color')

fg:setBackbuffer(0, "color")


lune.sys

lune.sys.registerObjectUpdateFile(filename)
lune.sys.registerObjectUpdateSource(src)

]]

local app = {}

function app.Draw()
	print("frame")
end

return app
