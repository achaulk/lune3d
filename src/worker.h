#pragma once

#include <stdint.h>
#include <functional>

#include "sys/sync.h"

namespace lune {

struct PoolWorkUnit
{
	uint64_t (*exec)(PoolWorkUnit *u);
	uint32_t count;
	uint32_t index;
};

struct PoolWorkGroup
{
	std::atomic<uint32_t> current_frame_index;
	uint32_t num_valid;
	uint32_t guid;
	std::vector<PoolWorkUnit *> work_units;
};

struct PoolThreadCommon
{
	SeqEvent frame_wait;
	SeqEvent swap_wait;
	std::atomic<uint32_t> seq;
	SeqEvent seq_wait;
	uint32_t num_threads;
	double dt;

	std::function<void(uint32_t)> update_fn;
	std::function<void()> on_frame_done;

	std::atomic<PoolWorkGroup*> current_work_group;
};

struct PoolThreadInfo
{
	PoolThreadInfo(PoolThreadCommon *common);

	PoolThreadCommon *common;
	struct LuneEngineEventRef
	{
		double type = -1.0;
		double id = -1.0;
	} event;
	bool (*fn)(PoolThreadInfo *self, PoolThreadCommon *common);
	uint64_t next_frame = 1;
	uint32_t subseq = 0;
	uint32_t expected_seq = 0;
	PoolWorkUnit *wu = nullptr;
	bool exit = false;
};

// Not a list entry. Waits for the frame to increment and then starts running the list
bool WorkFrameStart(PoolThreadInfo *self, PoolThreadCommon *common);
// Final entry. Calls on_frame_done when all threads reach here and starts back at WorkFrameState
bool WorkFrameEnd(PoolThreadInfo *self, PoolThreadCommon *common);
// Wait for all threads to reach this sync point and calls update_fn with the index
bool WorkSyncThreads(PoolThreadInfo *self, PoolThreadCommon *common);

// Dequeue and execute work units. Implies WorkSyncThreads as current_work_group needs
// updating
bool WorkDoWork(PoolThreadInfo *self, PoolThreadCommon *common);
bool WorkContinueWork(PoolThreadInfo *self, PoolThreadCommon *common);

// This is the sequence of functions each thread executes. It can be changed in between frames
extern std::vector<bool (*)(PoolThreadInfo *self, PoolThreadCommon *common)> g_ThreadSequence;

}
