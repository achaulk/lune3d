#pragma once

#include "types.h"

#include <vector>

namespace lune {
namespace gfx {

struct CommandPoolSet
{
	struct PerThread
	{
		vulkan_ptr<VkCommandPool> pools[3];
	};
	std::vector<std::unique_ptr<PerThread>> per_thread;
};

} // namespace gfx
} // namespace lune
