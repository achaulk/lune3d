#include "memory.h"
#include "device.h"

namespace lune {
namespace gfx {

MemoryArea::~MemoryArea()
{
	if(!mem_)
		return;
	dev_->delete_queue.Enqueue(
	    used_,
	    std::make_unique<std::function<void()>>(std::bind(&vmaFreeMemory, dev_->alloc, mem_.release())));
}

bool MemoryArea::Use()
{
	used_ = dev_->sequence;
	if(!vmaTouchAllocation(dev_->alloc, mem_.get())) {
		lost_ = true;
		OnLost();
		dev_->Free(mem_.release());
		return false;
	}
	return true;
}

DeletionList::DeletionList(Device *dev) : dev(dev)
{
	collect = 0;
	frames.emplace_back();
}

DeletionList::~DeletionList() = default;

void DeletionList::Enqueue(uint64_t used, GenericDeleteFn fn, void *p)
{
	if(used && used <= collect.load(std::memory_order_acquire)) {
		fn(dev->device, p, dev->allocator);
		return;
	}
	cs.lock();
	frames.back().list.emplace_back(fn, p);
	cs.unlock();
}

void DeletionList::Enqueue(uint64_t used, std::unique_ptr<std::function<void()>> fn)
{
	if(used && used <= collect.load(std::memory_order_acquire)) {
		fn->operator()();
		return;
	}
	cs.lock();
	frames.back().list.emplace_back(nullptr, fn.release());
	cs.unlock();
}

void DeletionList::DoneFrame(VkDevice dev, const VkAllocationCallbacks *cb, uint64_t id)
{
	collect.store(id, std::memory_order_release);
	while(!frames.empty() && frames.front().id <= id) {
		for(auto &e : frames.front().list) {
			if(e.fn) {
				e.fn(dev, e.p, cb);
			} else {
				(*e.genfn)();
				delete e.genfn;
			}
		}
		frames.pop_front();
	}
}

void DeletionList::NewFrame(uint64_t id)
{
	cs.lock();
	prev_ = id;
	if(frames.back().list.empty())
		frames.back().id = id;
	else
		frames.emplace_back(id);
	cs.unlock();
}

} // namespace gfx
} // namespace lune
