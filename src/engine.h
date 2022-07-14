#pragma once

#include <memory>
#include <vector>

#include "worker.h"

namespace lune {

class World;
namespace gfx {
struct Device;
class Screen;
}

class Engine
{
public:
	Engine();
	~Engine();

	void AddWorld(std::unique_ptr<World> w);
	void RemoveWorld(World *w);

	void AddScreen(std::unique_ptr<gfx::Screen> s);
	void RemoveScreen(gfx::Screen *s);

	void FirstFrame(double t0);
	void SysUpdate(double dt);
	void Swap();

	void InitWorkers(PoolThreadCommon *pool);

	void SetDevice(gfx::Device *dev)
	{
		dev_ = dev;
	}

private:
	void RebuildWorkers();

	void OnWorkDone(uint32_t id);

	void ScreenLost(gfx::Screen *s);

	struct WorldInfo
	{
		std::unique_ptr<World> w;
		double tNow = 0.0;
		double physics_step = 16666.0;
		double world_speed = 1.0;
		double physics_accum = 0.0;
		bool update_enabled = true;
	};
	std::vector<WorldInfo> worlds_;

	struct ScreenInfo
	{
		std::unique_ptr<gfx::Screen> s;
		bool always_active = false;
		bool active_this_frame = false;
	};
	std::vector<ScreenInfo> screens_;

	std::vector<PoolWorkGroup *> work_group_list_;

	PoolThreadCommon *pool_ = nullptr;
	bool need_work_rebuild_ = true;
	uint64_t frame_ = 0;

	gfx::Device *dev_ = nullptr;
};

extern Engine *gEngine;

}
