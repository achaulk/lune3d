#pragma once

#include "refptr.h"

namespace lune {
class World;

namespace gfx {

// Cameras provide the link between viewports and worlds.
// A viewport draws a view of a world; the contents of which may be specified by a camera
class Camera : public Refcounted
{
public:
	World *GetWorld()
	{
		return world_;
	}

private:
	World *world_ = nullptr;
};

} // namespace gfx
} // namespace lune
