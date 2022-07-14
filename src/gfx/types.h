#pragma once

#define GLM_FORCE_INTRINSICS
#define GLM_FORCE_CXX17
#define GLM_FORCE_AVX2
#define GLM_FORCE_DEPTH_ZERO_TO_ONE
#define GLM_FORCE_SIZE_T_LENGTH
#include "third_party/glm/glm/glm.hpp"
#include "third_party/glm/glm/gtc/quaternion.hpp"
#include "third_party/glm/glm/gtc/matrix_transform.hpp"

#include <memory>

#include <vulkan/vulkan_core.h>

namespace lune {
namespace gfx {

typedef glm::vec2 Vec2;
typedef glm::vec3 Vec3;

typedef glm::ivec2 IVec2;

typedef glm::quat Quat;

typedef glm::mat4 Mat4;

template<typename Vec>
struct GenRect
{
	GenRect MakePoints(Vec a, Vec b)
	{
		return IRect(a, glm::max(b - a, Vec()));
	}
	GenRect MakeSized(Vec o, Vec sz)
	{
		return IRect(o, sz);
	}

	bool contains(Vec pt) const
	{
		return pt.x >= origin.x && pt.y >= origin.y && pt.x < (origin.x + size.x) && pt.y < (origin.y + size.y);
	}

	Vec origin;
	Vec size;

private:
	GenRect(Vec origin, Vec size) : origin(origin), size(size) {}
};

typedef GenRect<IVec2> IRect;
typedef GenRect<Vec2> Rect;


struct VulkanEnsureDeleted
{
	template<typename T>
	void operator()(T v)
	{
		assert(v == 0);
	}
};

template<typename T>
using vulkan_ptr = std::unique_ptr<std::remove_pointer_t<T>, VulkanEnsureDeleted>;


}
}
