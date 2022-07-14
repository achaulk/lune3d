#pragma once

#include "config.h"

#include <string>
#include <string_view>
#include <vector>

#include "lua/lua.h"

namespace lune {

struct LuneConfig
{
	struct Window
	{
		bool create = true;
		int x = -1, y = -1, w = -1, h = -1;
		enum class Mode
		{
			WINDOWED,
			BORDERLESS_FULLSCREEN,
			EXCLUSIVE_FULLSCREEN
		} mode = Mode::WINDOWED;
	} window;
	std::string app_name;
};
extern LuneConfig g_Config;

void AddCommandline(const std::vector<std::string_view>& args);
int LuneMain();

void EarlyFatalError(const char *err);

void CustomLuaSetup(lua_State *L);

}
