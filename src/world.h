#pragma once

namespace lune {

// A World is a unique space in which objects can exist
// Multiple worlds can simultaneously exist but an object in one world cannot
// interact with an object in a different world. Objects are bound to a specific world
// and worlds can update simultaneously and independently
class World
{
public:
	void Step(double step_size, int num_steps);
	void SetPhysicsOffset(double offset);
};

} // namespace lune
