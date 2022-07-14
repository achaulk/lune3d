#include "device.h"
#include "texture.h"
#include "gfx.h"
#include "engine.h"
#include "viewport.h"

#include "third_party/VkBootstrap/VkBootstrap.h"

#include "logging.h"

LUNE_MODULE()

namespace lune {
namespace gfx {

struct DeviceStorage
{
	~DeviceStorage()
	{
		vkb::destroy_device(device);
		vkb::destroy_instance(instance);
	}

	vkb::Instance instance;
	vkb::Device device;
};

Device *g_device;

Device::Device() : delete_queue(this), storage(new DeviceStorage()), viewport_graph(new ViewportGraph()) {}

Device::~Device()
{
	vkDeviceWaitIdle(device);
	delete_queue.DoneFrame(device, allocator, ~0ULL);
	storage.reset();
}

void Device::Free(VmaAllocation mem)
{
	alloc_cs.lock();
	vmaFreeMemory(alloc, mem);
	alloc_cs.unlock();
}

Device *Device::Get()
{
	return g_device;
}



uint32_t g_vulkan_log_level = VK_DEBUG_UTILS_MESSAGE_SEVERITY_ERROR_BIT_EXT |
                              VK_DEBUG_UTILS_MESSAGE_SEVERITY_WARNING_BIT_EXT |
                              VK_DEBUG_UTILS_MESSAGE_SEVERITY_INFO_BIT_EXT;

static VKAPI_ATTR VkBool32 VKAPI_CALL VulkanDebugCallback(VkDebugUtilsMessageSeverityFlagBitsEXT messageSeverity,
    VkDebugUtilsMessageTypeFlagsEXT messageType, const VkDebugUtilsMessengerCallbackDataEXT *pCallbackData,
    void *pUserData)
{
	if(g_vulkan_log_level & messageSeverity)
		LOG("%s %s", pCallbackData->pMessageIdName, pCallbackData->pMessage);
	return VK_FALSE;
}


bool InitializeGraphicsContext(const char *name, std::string &err)
{
	vkb::InstanceBuilder ib;

	assert(!g_device);
	g_device = new Device();

	ib.set_engine_name("Lune").set_engine_version(0, 0, 0).set_app_name(name);
	ib.require_api_version(1, 2, 0);
	ib.request_validation_layers();
	ib.set_debug_callback(&VulkanDebugCallback);

	auto inst = ib.build();
	if(!inst.has_value()) {
		err = vkb::to_string(inst.error().type);
		return false;
	}

	g_device->storage->instance = inst.value();
	

	vkb::PhysicalDeviceSelector pds(g_device->storage->instance);
	pds.prefer_gpu_device_type();
	pds.defer_surface_initialization();
	pds.add_required_extension("VK_EXT_descriptor_indexing");
	pds.add_desired_extensions({"VK_KHR_acceleration_structure", "VK_KHR_ray_tracing_pipeline", "VK_KHR_ray_query",
	    "VK_KHR_pipeline_library", "VK_KHR_deferred_host_operations", "VK_EXT_memory_budget", "VK_EXT_memory_priority"});
	pds.set_minimum_version(1, 2).require_present();
	VkPhysicalDeviceFeatures f = {};
	f.shaderUniformBufferArrayDynamicIndexing = 1;
	f.shaderSampledImageArrayDynamicIndexing = 1;
	f.shaderStorageBufferArrayDynamicIndexing = 1;
	f.shaderStorageImageArrayDynamicIndexing = 1;
	f.sparseBinding = 1;
	pds.set_required_features(f);
	auto pd = pds.select();
	if(!pd) {
		err = vkb::to_string(pd.error().type);
		return false;
	}

	vkb::DeviceBuilder db(pd.value());
	db.set_allocation_callbacks(g_device->allocator);
	VkPhysicalDeviceDescriptorIndexingFeatures indexing_features{
	    VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_DESCRIPTOR_INDEXING_FEATURES_EXT, nullptr};
	VkPhysicalDeviceFeatures2 device_features{VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_FEATURES_2, &indexing_features};

	vkGetPhysicalDeviceFeatures2(pd.value().physical_device, &device_features);
	db.add_pNext(&device_features);

	auto dev = db.build();
	if(!dev) {
		err = vkb::to_string(dev.error().type);
		return false;
	}

	g_device->device = dev->device;
	g_device->phys_dev = dev->physical_device.physical_device;
	g_device->instance = inst.value().instance;
	g_device->storage->device = dev.value();

	VmaAllocatorCreateInfo vma = {
		VMA_ALLOCATOR_CREATE_EXTERNALLY_SYNCHRONIZED_BIT,
		g_device->phys_dev,
		g_device->device,
		0,
		g_device->allocator,
		nullptr,
		2,
	};
	vma.instance = g_device->storage->instance.instance;
	vma.vulkanApiVersion = VK_API_VERSION_1_2;

	if(dev->physical_device.has_extension("VK_EXT_memory_budget"))
		vma.flags |= VMA_ALLOCATOR_CREATE_EXT_MEMORY_BUDGET_BIT;

	auto r = vmaCreateAllocator(&vma, &g_device->alloc);
	if(r != VK_SUCCESS) {
		err = "vk allocator failure";
		return false;
	}

	gEngine->SetDevice(g_device);

	return true;
}

void DestroyGraphicsContext()
{
	gEngine->SetDevice(nullptr);
	delete g_device;
	g_device = nullptr;
}


void VulkanError(VkResult err)
{
	LOGF("Vulkan error %d", err);
}


} // namespace gfx
} // namespace lune
