#pragma once

#include "refptr.h"
#include "types.h"

#include "memory.h"
#include "sys/sync.h"

#include <vector>

namespace lune {
namespace gfx {

class SamplerCache;

struct QueueFamily
{
	bool graphics;
	bool compute;
	bool transfer;

	uint32_t index;
	uint32_t count;

	std::vector<VkQueue> queues;
};

struct DeviceStorage;
struct ViewportGraph;

struct Device
{
	Device();
	~Device();

	static Device *Get();

	void Free(VmaAllocation mem);
	void OnFrame(uint64_t frame);

	uint64_t sequence = 0;

	VkInstance instance;
	VkDevice device;
	VkPhysicalDevice phys_dev;
	VkAllocationCallbacks *allocator = nullptr;

	CriticalSection alloc_cs;
	VmaAllocator alloc;

	VkPhysicalDeviceProperties device_properties;
	VkPhysicalDeviceFeatures device_features;
	VkPhysicalDeviceMemoryProperties memory_properties;

	std::vector<QueueFamily> queue_families;
	QueueFamily *graphics;
	QueueFamily *compute;
	QueueFamily *transfer;
	QueueFamily *present;

	std::unique_ptr<SamplerCache> samplercache;
	DeletionList delete_queue;
	std::unique_ptr<DeviceStorage> storage;

	std::unique_ptr<ViewportGraph> viewport_graph;
};

void VulkanError(VkResult);
#define VK_CHECK(x)                     \
	if(VkResult err = x) \
		VulkanError(err);

class BinarySemaphore
{
public:
	explicit BinarySemaphore(Device *dev) noexcept : dev_(dev)
	{
		VkSemaphoreCreateInfo ci = {VK_STRUCTURE_TYPE_SEMAPHORE_CREATE_INFO, nullptr, 0};
		VK_CHECK(vkCreateSemaphore(dev->device, &ci, dev->allocator, &sem_));
	}
	~BinarySemaphore()
	{
		if(sem_)
			vkDestroySemaphore(dev_->device, sem_, dev_->allocator);
	}

	BinarySemaphore(const BinarySemaphore &) = delete;
	void operator=(const BinarySemaphore &) = delete;

	operator VkSemaphore() const noexcept
	{
		return sem_;
	}

private:
	Device *dev_;
	VkSemaphore sem_;
};

class TimelineSemaphore
{
public:
	explicit TimelineSemaphore(Device *dev, uint64_t initial_value = 0) noexcept : dev_(dev)
	{
		VkSemaphoreTypeCreateInfo timelineCreateInfo;
		timelineCreateInfo.sType = VK_STRUCTURE_TYPE_SEMAPHORE_TYPE_CREATE_INFO;
		timelineCreateInfo.pNext = NULL;
		timelineCreateInfo.semaphoreType = VK_SEMAPHORE_TYPE_TIMELINE;
		timelineCreateInfo.initialValue = initial_value;

		VkSemaphoreCreateInfo ci = {VK_STRUCTURE_TYPE_SEMAPHORE_CREATE_INFO, &timelineCreateInfo, 0};
		VK_CHECK(vkCreateSemaphore(dev->device, &ci, dev->allocator, &sem_));
	}
	~TimelineSemaphore()
	{
		if(sem_)
			vkDestroySemaphore(dev_->device, sem_, dev_->allocator);
	}

	TimelineSemaphore(TimelineSemaphore &&o) noexcept : dev_(o.dev_), sem_(o.sem_)
	{
		o.sem_ = VK_NULL_HANDLE;
	}
	void operator=(TimelineSemaphore &&o) noexcept
	{
		if(sem_)
			vkDestroySemaphore(dev_->device, sem_, dev_->allocator);
		sem_ = o.sem_;
		o.sem_ = nullptr;
	}

	TimelineSemaphore(const TimelineSemaphore &) = delete;
	void operator=(const TimelineSemaphore &) = delete;

	void wait(uint64_t value)
	{
		VkSemaphoreWaitInfo waitInfo;
		waitInfo.sType = VK_STRUCTURE_TYPE_SEMAPHORE_WAIT_INFO;
		waitInfo.pNext = NULL;
		waitInfo.flags = 0;
		waitInfo.semaphoreCount = 1;
		waitInfo.pSemaphores = &sem_;
		waitInfo.pValues = &value;

		VK_CHECK(vkWaitSemaphores(dev_->device, &waitInfo, UINT64_MAX));
	}

	void signal(uint64_t value)
	{
		VkSemaphoreSignalInfo signalInfo;
		signalInfo.sType = VK_STRUCTURE_TYPE_SEMAPHORE_SIGNAL_INFO;
		signalInfo.pNext = NULL;
		signalInfo.semaphore = sem_;
		signalInfo.value = 2;

		VK_CHECK(vkSignalSemaphore(dev_->device, &signalInfo));
	}

	uint64_t query()
	{
		uint64_t value;
		vkGetSemaphoreCounterValue(dev_->device, sem_, &value);
		return value;
	}

	operator VkSemaphore() const noexcept
	{
		return sem_;
	}

private:
	Device *dev_;
	VkSemaphore sem_;
};

class Fence
{
public:
	Fence(Device *dev, bool signalled) noexcept : dev_(dev)
	{
		VkFenceCreateInfo ci = {
		    VK_STRUCTURE_TYPE_FENCE_CREATE_INFO, nullptr, signalled ? VK_FENCE_CREATE_SIGNALED_BIT : 0U};
		VK_CHECK(vkCreateFence(dev->device, &ci, dev->allocator, &fence_));
	}
	~Fence()
	{
		if(fence_)
			vkDestroyFence(dev_->device, fence_, dev_->allocator);
	}

	Fence(const Fence &) = delete;
	void operator=(const Fence &) = delete;

	void Reset()
	{
		VK_CHECK(vkResetFences(dev_->device, 1, &fence_));
	}

	bool Wait(uint64_t timeout)
	{
		VK_CHECK(vkWaitForFences(dev_->device, 1, &fence_, true, timeout));
		return true;
	}

	operator VkFence() const noexcept
	{
		return fence_;
	}

private:
	Device *dev_;
	VkFence fence_;
};


class CommandPoolPool
{
public:
	CommandPoolPool(Device *dev, uint32_t frames, uint32_t threads);
	~CommandPoolPool();

	void NewFrame();

	VkCommandPool GetPool(uint32_t tid);
};


} // namespace gfx
} // namespace lune
