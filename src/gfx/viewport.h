#pragma once

#include "types.h"
#include "refptr.h"
#include "window.h"
#include "device.h"
#include "texture.h"

#include <functional>
#include <vector>

namespace lune {
namespace gfx {

struct Device;

struct RenderTargetInfo
{
	VkImageView view = VK_NULL_HANDLE;
};

struct FramebufferInfo
{
	IVec2 size;
	VkFramebuffer fb = VK_NULL_HANDLE;
	RenderTargetInfo color[4];
	RenderTargetInfo depth_stencil;

	std::vector<VkImageView> attachments;
};

class Camera;

struct ViewportProperties
{
	bool has_depth = true;
	bool has_stencil = false;
	enum class Type
	{
		SRGB,
		Linear,
		FloatingPoint
	} type = Type::SRGB;
};

struct ViewportDraw
{
};

enum class ViewportElementType
{
	FullColor,
	DiffuseColor,
	SpecularColor,
	DepthBuffer,
	SurfaceNormal,
};

enum ViewportElementLoad
{
	Clear,
	Load,
	DontCare
};

struct ViewportElement
{
	ViewportElementType logical_type = ViewportElementType::FullColor;
	TextureFormat format = TextureFormat::RGBA8;
	TextureElement element = TextureElement::UnsignedNormalized;
	uint32_t num_samples_shift = 0; // [0,6]
	uint32_t downsample_shift = 0;
	ViewportElementLoad load = ViewportElementLoad::Clear;
};

struct ViewportDesc
{
	std::vector<ViewportElement> viewport_component;
};

struct RenderPassRef : public Refcounted
{
	static RefPtr<RenderPassRef> GetFor(const ViewportDesc &desc);
	vulkan_ptr<VkRenderPass> pass;
};

class Viewport;
struct ViewportGraph
{
	bool dirty = false;

	std::vector<Viewport *> viewports_in_draw_order_;

	void Clear() {}
	void AddRoot(Viewport *vp) {}
};

// A Viewport is a drawable region that can contain a Camera view, script drawn elements, other viewports
class Viewport : public Refcounted
{
public:
	struct InFlight
	{
		VkSemaphore available = VK_NULL_HANDLE, done = VK_NULL_HANDLE;
		VkFence wait = VK_NULL_HANDLE;
	};

	Viewport(Device *dev, ViewportGraph *graph);
	~Viewport();

	virtual void SetHasDepth(bool depth, bool stencil) = 0;

	void AddViewport(RefPtr<Viewport> vp, IVec2 location, int z, int id, const ViewportDraw& draw);
	void RemoveViewport(uint32_t id);
	void MoveViewport(IVec2 location, int z);

//	void CmdBegin(VkCommandBuffer)

	void SetBackbuffer(uint32_t index, VkSemaphore target);
	bool NeedDraw()
	{
		return need_draw_;
	}

protected:
	void RecreateElement(uint32_t fb, uint32_t elem);

	void CreateFramebuffer();

	ViewportGraph *graph_;

	InFlight a, b;
	InFlight *tminus0 = &a, *tminus1 = &b;

	struct ViewportInfo
	{
		RefPtr<Viewport> vp;
		IVec2 location;
		int z;
		int id;
	};
	std::vector<ViewportInfo> vp_in_draw_order_;

	RefPtr<Camera> camera_;

	std::function<void(VkCommandBuffer)> custom_draw_;

	struct Framebuffer
	{
		std::vector<VkImageView> image_views;
		std::vector<RefPtr<Texture>> texture_refs;
	};
	Device *dev_;
	std::vector<Framebuffer> framebuffers_;
	ViewportDesc viewport_desc_;
	vulkan_ptr<VkFramebuffer> fb_;
	IVec2 size_ = IVec2(0, 0);
	RefPtr<RenderPassRef> pass_;
	uint32_t back_ = 0;
	VkFence prev_fence_ = VK_NULL_HANDLE;
	VkSemaphore prev_sem_ = VK_NULL_HANDLE;

	bool need_draw_ = false;
	bool is_timeline_ = false;
};

class ForwardRenderingViewport : public Viewport
{
public:
};

// A viewport that draws into a swapchain. Can resize by itself
class SwapchainViewport : public Viewport
{
public:
	SwapchainViewport(Device *dev, ViewportGraph *graph);
	virtual ~SwapchainViewport() = default;

	void Recreate(const std::vector<VkImageView>& v, IVec2 size);

	void SetHasDepth(bool depth, bool stencil) override;

	InFlight *GetOldestFrame();

	TextureFormat depth_format_ = TextureFormat::Default;

	BinarySemaphore s1, s2, s3, s4;
	Fence f1, f2;
};

class WindowSwapManager
{
public:
	static WindowSwapManager *Get();

	void Begin(VkSwapchainKHR swap, uint32_t index, VkSemaphore done);
	void Present(VkQueue queue);

private:
	std::vector<VkSwapchainKHR> swapchains;
	std::vector<uint32_t> indices;
	std::vector<VkSemaphore> semaphores;
	std::vector<VkResult> results;
};

// A Screen is the entire drawable area of a swapchain
class Screen
{
public:
	static std::unique_ptr<Screen> Create(std::unique_ptr<Window> w);

	class Observer
	{
	public:
		virtual ~Observer() = default;

		virtual void Resized() = 0;
	};
	Viewport *viewport() const
	{
		return viewport_.get();
	}

	IVec2 size() const
	{
		return size_;
	}

	void AddObserver(Observer *o)
	{
		observers_.push_back(o);
	}
	void RemoveObserver(Observer *o)
	{
		for(auto it = observers_.begin(); it != observers_.end(); ++it) {
			if(*it == o) {
				observers_.erase(it);
				return;
			}
		}
	}

	virtual bool BeginFrame() = 0;
	virtual void EndFrame() = 0;

//	virtual int Swap() = 0;
//	virtual void SysUpdate() = 0;

	virtual bool ShouldAlwaysUpdate() const
	{
		return false;
	}

protected:
	void OnResized(IVec2 sz)
	{
		size_ = sz;
		for(auto *o : observers_) o->Resized();
	}
	IVec2 size_;
	RefPtr<SwapchainViewport> viewport_;
	std::vector<Observer *> observers_;
};

class SwapchainScreen : public Screen
{
public:
	bool ShouldAlwaysUpdate() const override
	{
		return true;
	}

	static std::unique_ptr<Screen> Create(Device *dev, VkSurfaceKHR sc, std::unique_ptr<Window> w);
};

class OffscreenScreen : public Screen
{
public:
};

}
}
