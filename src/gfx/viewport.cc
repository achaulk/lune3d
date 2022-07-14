#include "viewport.h"

#include "camera.h"
#include "device.h"

#include "third_party/VkBootstrap/VkBootstrap.h"

#include "logging.h"

namespace lune {
namespace gfx {
namespace {

class VkbSwapchain : public SwapchainScreen
{
public:
	VkbSwapchain(Device *dev, VkSurfaceKHR surf) : dev_(dev), swap_builder(dev->phys_dev, dev->device, surf)
	{
		swap_builder.set_allocation_callbacks(dev->allocator);
		swap_builder.set_desired_present_mode(VK_PRESENT_MODE_MAILBOX_KHR);
		swap_builder.add_fallback_present_mode(VK_PRESENT_MODE_FIFO_KHR);
		VkSurfaceFormatKHR def;
		def.format = VK_FORMAT_R8G8B8A8_SRGB;
		def.colorSpace = VK_COLOR_SPACE_SRGB_NONLINEAR_KHR;
		swap_builder.use_default_format_selection();
	}

	bool BeginFrame() override
	{
		auto f = viewport_->GetOldestFrame();
		VK_CHECK(vkWaitForFences(dev_->device, 1, &f->wait, true, ~0ULL));

		uint32_t idx;
		auto r = vkAcquireNextImageKHR(dev_->device, swapchain_.swapchain, ~0ULL, f->available, VK_NULL_HANDLE, &idx);
		if(r == VK_ERROR_OUT_OF_DATE_KHR) {
			if(!Reinitialize()) {
				abort();
			}
			return false;
		} else if(r != VK_SUCCESS && r != VK_SUBOPTIMAL_KHR) {
			VulkanError(r);
		}

		VK_CHECK(vkResetFences(dev_->device, 1, &f->wait));

		WindowSwapManager::Get()->Begin(swapchain_.swapchain, idx, f->done);

		// We have an image
		viewport_->SetBackbuffer(idx, f->done);

		return true;
	}

	void EndFrame() override {}

	bool Initialize(std::unique_ptr<Window> w, ViewportGraph *g)
	{
		w_ = std::move(w);
		viewport_.reset(new SwapchainViewport(dev_, g));
		return Reinitialize();
	}

	bool Reinitialize()
	{
		auto sz = w_->GetSize();
		size_ = IVec2(sz.width, sz.height);
		swap_builder.set_desired_extent(sz.width, sz.height);
		auto ret = swap_builder.recreate(swapchain_);
		if(!ret)
			return false;
		swapchain_ = ret.value();

		auto views = swapchain_.get_image_views();
		if(!views)
			return false;
		image_views_ = views.value();
		viewport_->Recreate(image_views_, size_);
		return true;
	}

	Device *dev_;
	vkb::SwapchainBuilder swap_builder;
	vkb::Swapchain swapchain_;
	std::unique_ptr<Window> w_;

	std::vector<VkImageView> image_views_;
};

} // namespace


RefPtr<RenderPassRef> RenderPassRef::GetFor(const ViewportDesc &desc)
{
	return nullptr;
}


std::unique_ptr<Screen> SwapchainScreen::Create(Device *dev, VkSurfaceKHR surf, std::unique_ptr<Window> w)
{
	std::unique_ptr<VkbSwapchain> p(new VkbSwapchain(dev, surf));
	if(!p->Initialize(std::move(w), dev->viewport_graph.get()))
		return nullptr;
	return p;
}

SwapchainViewport::SwapchainViewport(Device *dev, ViewportGraph *graph)
    : Viewport(dev, graph), s1(dev), s2(dev), s3(dev), s4(dev), f1(dev, true), f2(dev, true)
{
	tminus0->available = s1;
	tminus0->done = s2;
	tminus0->wait = f1;
	tminus1->available = s3;
	tminus1->done = s4;
	tminus1->wait = f2;
}

SwapchainViewport::InFlight *SwapchainViewport::GetOldestFrame()
{
	return tminus1;
}

void SwapchainViewport::Recreate(const std::vector<VkImageView> &views, IVec2 size)
{
	if(size != size_) {
		size_ = size;
		framebuffers_.resize(views.size());
		for(uint32_t i = 0; i < views.size(); i++) {
			framebuffers_[i].image_views.resize(viewport_desc_.viewport_component.size());
			framebuffers_[i].texture_refs.resize(viewport_desc_.viewport_component.size() - 1);
			framebuffers_[i].image_views[0] = views[i];
			for(uint32_t j = 1; j < viewport_desc_.viewport_component.size(); j++) RecreateElement(i, j);
		}

		dev_->delete_queue.Enqueue(fb_.release());
	}
}

void SwapchainViewport::SetHasDepth(bool depth, bool stencil) {}

Viewport::Viewport(Device *dev, ViewportGraph *graph) : graph_(graph), dev_(dev) {}
Viewport::~Viewport()
{
	graph_->dirty = true;
}

void Viewport::AddViewport(RefPtr<Viewport> vp, IVec2 location, int z, int id, const ViewportDraw &draw)
{
	graph_->dirty = true;

	vp_in_draw_order_.emplace_back(ViewportInfo{std::move(vp), location, z, id});
}

void Viewport::RemoveViewport(uint32_t id)
{

}

void Viewport::MoveViewport(IVec2 location, int z)
{

}

void Viewport::SetBackbuffer(uint32_t index, VkSemaphore sem)
{
	need_draw_ = true;
	;
}

void Viewport::RecreateElement(uint32_t fb, uint32_t elem)
{
	auto &e = viewport_desc_.viewport_component[elem];
	auto &f = framebuffers_[fb];

	TextureInfo ti;
	ti.size = IVec2(size_.x >> e.downsample_shift, size_.y >> e.downsample_shift);
	ti.format = e.format;
	ti.element = e.element;
	ti.num_mipmaps = 0;
	ti.usage = TextureUsage::RenderTarget;
	ti.num_samples_shift = e.num_samples_shift;
	auto td = TextureData::Construct2D(dev_, ti, MemoryPriority::Critical);

	auto ref = td->CreateView();
	f.image_views[elem] = ref->GetView();
	f.texture_refs[elem] = std::move(ref);
}

void Viewport::CreateFramebuffer()
{
	pass_ = RenderPassRef::GetFor(viewport_desc_);

	VkFramebufferCreateInfo ci = {
		VK_STRUCTURE_TYPE_FRAMEBUFFER_CREATE_INFO, nullptr, VK_FRAMEBUFFER_CREATE_IMAGELESS_BIT, pass_->pass.get(),
		(uint32_t)framebuffers_[0].image_views.size(), nullptr, (uint32_t)size_.x, (uint32_t)size_.y, 1U
	};
	VkFramebuffer fb;
	VK_CHECK(vkCreateFramebuffer(dev_->device, &ci, dev_->allocator, &fb));
	fb_.reset(fb);
}

WindowSwapManager *WindowSwapManager::Get()
{
	static WindowSwapManager m;
	return &m;
}

void WindowSwapManager::Begin(VkSwapchainKHR swap, uint32_t index, VkSemaphore sem)
{
	swapchains.push_back(swap);
	indices.push_back(index);
	semaphores.push_back(sem);
}

void WindowSwapManager::Present(VkQueue queue)
{
	if(swapchains.empty())
		return;
	results.resize(swapchains.size());
	VkPresentInfoKHR pi = {VK_STRUCTURE_TYPE_PRESENT_INFO_KHR, nullptr, (uint32_t)semaphores.size(), semaphores.data(),
	    (uint32_t)swapchains.size(), swapchains.data(), indices.data(), results.data()};
	vkQueuePresentKHR(queue, &pi);

	swapchains.resize(0);
	indices.resize(0);
	semaphores.resize(0);
}

} // namespace gfx
} // namespace lune
