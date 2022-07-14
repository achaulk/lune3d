#pragma once

#include <vector>

#include "sys/sync.h"

namespace lune {

enum class LuneToLuaEv : uint32_t
{
	Callback,
	SysUpdate,
	Swap,
	NewFrame,
	UpdateDone,
	PendingChannelMessages,
	KeyPressed,
	KeyReleased,
	TextInput,
	MouseMoved,
	MousePressed,
	MouseReleased,
	WheelMoved,
	Focus,
	MouseFocus,
	Visible,
	Resized,
	UserDraw,
	UserUpdate,
	LateUserUpdate,
	EndFrame,
};

struct LuaEvent
{
	LuaEvent() = default;
	LuaEvent(LuneToLuaEv ty, double a0 = 0, double a1 = 0, double a2 = 0, double a3 = 0, double a4 = 0)
	    : type(ty), flags(0), arg{a0, a1, a2, a3, a4}
	{
	}
	LuneToLuaEv type;
	uint32_t flags;
	double arg[5];
};
struct LuaEventList
{
	const LuaEvent *ev;
	uint32_t valid;
};

void PostEvent(LuneToLuaEv ev, double a0 = 0, double a1 = 0, double a2 = 0, double a3 = 0, double a4 = 0);
void PostPendingMessage();

} // namespace lune
