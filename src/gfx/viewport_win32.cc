#include "viewport.h"

#include "device.h"

#define VK_USE_PLATFORM_WIN32_KHR
#include <Windows.h>
#include <vulkan/vulkan_win32.h>

namespace lune {
namespace gfx {

std::unique_ptr<Screen> Screen::Create(std::unique_ptr<Window> w)
{
	auto dev = Device::Get();
	auto fn = (PFN_vkCreateWin32SurfaceKHR)vkGetInstanceProcAddr(dev->instance, "vkCreateWin32SurfaceKHR");
	if(!fn)
		return nullptr;
	VkSurfaceKHR surface;
	VkWin32SurfaceCreateInfoKHR ci = {
	    VK_STRUCTURE_TYPE_WIN32_SURFACE_CREATE_INFO_KHR, nullptr, 0, GetModuleHandle(NULL), (HWND)w->GetHandle()};
	if(fn(dev->instance, &ci, dev->allocator, &surface) == VK_SUCCESS) {
		return SwapchainScreen::Create(dev, surface, std::move(w));
	}
	return nullptr;
}

}
} // namespace lune
