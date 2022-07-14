#pragma once

#include <memory>
#include <string>

#include "third_party/glm/glm/integer.hpp"

namespace lune {
namespace gfx {

struct WindowOptions
{
	std::string title;
	int x = -1;
	int y = -1;
	int w = -1;
	int h = -1;
};

struct Size
{
	uint32_t width, height;
};

class Window
{
public:
	virtual ~Window() = default;
	virtual void *GetHandle() = 0;

	virtual Size GetSize() = 0;
};

std::unique_ptr<Window> CreateWindow(const WindowOptions &opts);

}
}
