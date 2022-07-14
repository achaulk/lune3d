#include "worker.h"
#include "logging.h"

namespace lune {
std::vector<bool (*)(PoolThreadInfo *self, PoolThreadCommon *common)> g_ThreadSequence;

PoolThreadInfo::PoolThreadInfo(PoolThreadCommon *common) : common(common), fn(&WorkFrameStart) {}

bool WorkSyncThreads(PoolThreadInfo *self, PoolThreadCommon *common)
{
	OPTICK_EVENT();
	if(common->seq.fetch_add(1, std::memory_order_acq_rel) == self->expected_seq) {
		common->update_fn(self->subseq);
		common->seq_wait.signal_inc();
	} else {
		common->seq_wait.wait_for(self->subseq);
	}
	self->expected_seq += common->num_threads;
	self->fn = g_ThreadSequence[++self->subseq];
	return false;
}

bool WorkFrameStart(PoolThreadInfo *self, PoolThreadCommon *common)
{
	OPTICK_EVENT();
	common->frame_wait.wait_for(self->next_frame);
	self->subseq = 0;
	self->expected_seq = common->num_threads - 1;
	self->fn = g_ThreadSequence[0];
	return false;
}

bool WorkFrameEnd(PoolThreadInfo *self, PoolThreadCommon *common)
{
	OPTICK_EVENT();
	if(common->seq.fetch_add(1, std::memory_order_acq_rel) == self->expected_seq) {
		common->swap_wait.wait_for(self->next_frame);
		common->seq.store(0, std::memory_order_release);
		common->on_frame_done();
	}
	self->next_frame++;
	self->fn = &WorkFrameStart;
	return false;
}

bool WorkDoWork(PoolThreadInfo *self, PoolThreadCommon *common)
{
	auto g = common->current_work_group.load(std::memory_order_acquire);
	while(true) {
		auto i = g->current_frame_index.fetch_add(1, std::memory_order_relaxed);
		if(i >= g->num_valid) {
			break;
		}
		auto wu = g->work_units[i];
		uint64_t id = wu->exec(wu);
		if(id) {
			self->fn = &WorkContinueWork;
			self->event.id = id;
			self->event.type = g->guid;
			self->wu = wu;
			return true;
		}
	}
	return WorkSyncThreads(self, common);
}

bool WorkContinueWork(PoolThreadInfo *self, PoolThreadCommon *common)
{
	uint64_t id = self->wu->exec(self->wu);
	if(id) {
		self->event.id = id;
		return true;
	}
	self->fn = &WorkDoWork;
	return false;
}


}
