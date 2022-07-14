#include "engine.h"

#include "world.h"
#include "gfx/viewport.h"
#include "worker.h"
#include "logging.h"

namespace lune {

Engine *gEngine = nullptr;

Engine::Engine() = default;
Engine::~Engine() = default;

void Engine::AddWorld(std::unique_ptr<World> w)
{
	worlds_.emplace_back(WorldInfo{std::move(w)});
}

void Engine::RemoveWorld(World *w)
{
	for(auto it = worlds_.begin(); it != worlds_.end(); ++it) {
		if(it->w.get() == w) {
			worlds_.erase(it);
			return;
		}
	}
}

void Engine::AddScreen(std::unique_ptr<gfx::Screen> s)
{
	bool update = s->ShouldAlwaysUpdate();
	screens_.emplace_back(ScreenInfo{std::move(s), update});
}

void Engine::RemoveScreen(gfx::Screen *s)
{
	for(auto it = screens_.begin(); it != screens_.end(); ++it) {
		if(it->s.get() == s) {
			screens_.erase(it);
			return;
		}
	}
}

void Engine::FirstFrame(double t0)
{

}

void Engine::ScreenLost(gfx::Screen *s)
{
	LUNE_BP();
}

void Engine::SysUpdate(double dt)
{
	frame_++;

	if(need_work_rebuild_) {
		need_work_rebuild_ = false;
		RebuildWorkers();
	}

	for(size_t i = 0; i < screens_.size(); i++) {
		if(screens_[i].active_this_frame || screens_[i].always_active) {
			screens_[i].active_this_frame = true;
			if(!screens_[i].s->BeginFrame()) {
				ScreenLost(screens_[i].s.get());
				if(!screens_[i].s->BeginFrame())
					abort();
			}
		}
	}

	for(auto &e : worlds_) {
		if(!e.update_enabled)
			continue;
		double wt = dt * e.world_speed;
		e.tNow += wt;
		e.physics_accum += wt;
		int steps = (int)floor(e.physics_accum / e.physics_step);
		e.physics_accum -= steps * e.physics_step;

		e.w->Step(e.physics_step, steps);
		e.w->SetPhysicsOffset(e.physics_accum);
	}

	if(need_work_rebuild_) {

	}

	if(dev_->viewport_graph->dirty) {
		dev_->viewport_graph->Clear();
		for(size_t i = 0; i < screens_.size(); i++) dev_->viewport_graph->AddRoot(screens_[i].s->viewport());
	}

	PoolWorkGroup *wg = work_group_list_[0];
	if(wg) {
		wg->current_frame_index.store(0, std::memory_order_release);
	}
	pool_->current_work_group.store(wg, std::memory_order_release);
}

void Engine::InitWorkers(PoolThreadCommon *pool)
{
	pool_ = pool;
	pool_->update_fn = std::bind_front(&Engine::OnWorkDone, this);
}

void Engine::RebuildWorkers()
{
	work_group_list_.resize(0);
	g_ThreadSequence.resize(0);

	work_group_list_.push_back(nullptr);
	g_ThreadSequence.push_back(&WorkFrameEnd);
}

void Engine::OnWorkDone(uint32_t id)
{
	PoolWorkGroup *wg = work_group_list_[id + 1];
	if(wg) {
		wg->current_frame_index.store(0, std::memory_order_release);
	}
	pool_->current_work_group.store(wg, std::memory_order_release);
}

void Engine::Swap()
{
	for(size_t i = 0; i < screens_.size(); i++) {
		if(screens_[i].active_this_frame) {
			screens_[i].active_this_frame = false;
			screens_[i].s->EndFrame();
		}
	}
	gfx::WindowSwapManager::Get()->Present(dev_->present->queues[0]);
}


} // namespace lune
