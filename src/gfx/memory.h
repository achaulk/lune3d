#pragma once

#include "refptr.h"
#include "types.h"

#include "vk_mem_alloc.h"

#include "sys/sync.h"

#include <functional>

#include <vulkan/vulkan_core.h>

#include <atomic>
#include <list>

typedef VmaAllocation_T *VmaAllocation;

namespace lune {
namespace gfx {

struct Device;

enum class MemoryPriority
{
	Critical, // Cannot be invalidated, can exceed budget
	Important, // Can exceed budget
	Normal,
	Low, // Cannot invalidate other allocations
};

typedef void (*GenericDeleteFn)(VkDevice dev, void *p, const VkAllocationCallbacks *callbacks);
struct DeletionList
{
	struct Pending
	{
		GenericDeleteFn fn;
		union
		{
			void *p;
			std::function<void()> *genfn;
		};
	};

	struct Frame
	{
		Frame(uint64_t id = 0) : id(id) {}
		uint64_t id;
		std::vector<Pending> list;
	};

	DeletionList(Device *dev);
	~DeletionList();

	template<typename T>
	void Enqueue(uint64_t used, void (*fn)(VkDevice dev, T p, const VkAllocationCallbacks *callbacks), T p)
	{
		if(p)
			Enqueue(used, (GenericDeleteFn)fn, p);
	}
	void Enqueue(uint64_t used, GenericDeleteFn fn, void *p);
	void Enqueue(uint64_t used, std::unique_ptr<std::function<void()>> fn);

	void Enqueue(VkFramebuffer x)
	{
		Enqueue(prev_ , &vkDestroyFramebuffer, x);
	}

	void DoneFrame(VkDevice dev, const VkAllocationCallbacks *cb, uint64_t id);
	void NewFrame(uint64_t id);

private:
	uint64_t prev_ = 0;
	Device *dev;
	CriticalSection cs;
	std::atomic<uint64_t> collect;
	std::list<Frame> frames;
};

// The root type for images and buffers
class MemoryArea : public Refcounted
{
public:
	~MemoryArea() override;

	bool Use();

protected:
	MemoryArea(Device *dev, VmaAllocation mem) : dev_(dev), mem_(mem) {}

	virtual void OnLost() = 0;

	bool lost_ = false;
	uint64_t used_ = 0;
	Device *dev_;
	std::unique_ptr<VmaAllocation_T, VulkanEnsureDeleted> mem_;
};

}
} // namespace lune
