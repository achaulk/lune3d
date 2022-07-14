#include "lune.h"

void lune::CustomLuaSetup(lua_State *L) {}

int main(int argc, char **argv)
{
	std::vector<std::string_view> args(argv + 1, argv + argc);
	lune::AddCommandline(args);
	return lune::LuneMain();
}

#ifdef _WIN32
#include <Windows.h>

int APIENTRY WinMain(HINSTANCE, HINSTANCE, LPSTR, int)
{
	std::vector<std::string_view> args(__argv + 1, __argv + __argc);
	lune::AddCommandline(args);
	return lune::LuneMain();
}

void lune::EarlyFatalError(const char *err)
{
	MessageBoxA(NULL, err, "Lune Init Error", 0);
	exit(1);
}
#endif
