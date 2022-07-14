#pragma once

#include "refptr.h"
#include "types.h"

#include "memory.h"

#include <map>

namespace lune {
namespace gfx {

enum class TextureUsage
{
	// A sampleable texture
	Texture,
	// A sampleable texture that is frequently changed
	DynamicTexture,
	// A generic render target that can be sampled from or used as an input
	RenderTarget,
	// A render target that can be used as an input
	IntermediateRenderTarget,
	// A render target that can be sampled from. Depth cannot be sampled
	TextureRenderTarget,
	// A sampleable depth buffer
	DepthBuffer,
};

enum class TextureFormat : uint8_t
{
	Default,

	// These default to SRGB
	RGBA8,
	ABGR8,
	BGRA8,

	R8,
	RGB8,
	BGR8,

	// These default to Float
	RGBA16,
	RGBA32,

	// These ignore TextureElement
	R10G11B11_F,

	Stencil8,
	Depth16,
	Depth24,
	Depth32,
	Depth16_Stencil8,
	Depth24_Stencil8,
	Depth32_Stencil8,
};

enum class TextureElement
{
	Default,
	SRGB,
	SignedNormalized,
	UnsignedNormalized,
	SignedScaled,
	UnsignedScaled,
	Float,
};

struct TextureInfo
{
	IVec2 size;
	TextureFormat format = TextureFormat::RGBA8;
	TextureElement element = TextureElement::UnsignedNormalized;
	TextureUsage usage = TextureUsage::Texture;
	uint32_t num_samples_shift = 0; // [0,6]
	uint32_t num_mipmaps = 1;

	VkFormat GetFormat() const
	{
		return GetFormat(format, element);
	}
	VkImageUsageFlags GetUsage() const;

	static VkFormat GetFormat(TextureFormat format, TextureElement element);
};

namespace Swizzle {
static constexpr uint32_t identity = 0x0000;
} // namespace Swizzle

class Texture;
class TextureData;

class TextureGroup
{
public:
};


// TextureData refers to a buffer of device data
class TextureData : public MemoryArea
{
public:
	~TextureData() override;

	static RefPtr<TextureData> Construct2D(Device *dev, const TextureInfo &info, MemoryPriority priority);

	RefPtr<Texture> CreateView(TextureFormat format = TextureFormat::Default,
	    TextureElement element = TextureElement::Default, uint32_t swizzle = Swizzle::identity);

private:
	friend class Texture;

	TextureData(Device *dev, VmaAllocation mem, VkImage image, const TextureInfo &info)
	    : MemoryArea(dev, mem), image_(image), info_(info)
	{
	}

	void OnLost() override;

	std::unique_ptr<VkImage_T, VulkanEnsureDeleted> image_;
	TextureInfo info_;
};

namespace sampler {
static constexpr uint32_t kFilterNearest = 0;
static constexpr uint32_t kFilterLinear = 1;

static constexpr uint32_t kMipMapNearest = 0;
static constexpr uint32_t kMipMapLinear = 1;

static constexpr uint32_t kModeRepeat = 0;
static constexpr uint32_t kModeMirroredRepeat = 1;
static constexpr uint32_t kModeClampToEdge = 2;
static constexpr uint32_t kModeClampToBorder = 3;
static constexpr uint32_t kModeMirrorClampToEdge = 4;
} // namespace sampler

struct SamplerInfo
{
	SamplerInfo(uint32_t min_filter = sampler::kFilterLinear, uint32_t mag_filter = sampler::kFilterLinear,
		uint32_t mipmode = sampler::kMipMapLinear, uint32_t u_mode = sampler::kModeClampToEdge, uint32_t v_mode = sampler::kModeClampToEdge, uint32_t w_mode = sampler::kModeClampToEdge, float lod_bias = 0.0f,
	    bool anisotropic = false, float min_lod = 0.0f, float max_lod = VK_LOD_CLAMP_NONE,
	    VkBorderColor border = VK_BORDER_COLOR_FLOAT_TRANSPARENT_BLACK, bool unnormalized = false,
		float max_anisotropic = 0.0f);

	bool operator<(const SamplerInfo &o) const;

	size_t hash() const;

	VkFilter min() const;
	VkFilter mag() const;
	VkSamplerMipmapMode mip() const;
	VkSamplerAddressMode u() const;
	VkSamplerAddressMode v() const;
	VkSamplerAddressMode w() const;
	VkBool32 anisotropic() const;
	VkBorderColor border() const;
	VkBool32 unnormalized() const;

	uint64_t info;
	float lod_bias, lod_min, lod_max;
	float max_aniso;
};

class Texture : public Refcounted
{
public:
	~Texture() override;

	VkSampler GetSampler(const SamplerInfo& info);
	VkImageView GetView()
	{
		return imageview_.get();
	}

private:
	friend class TextureData;

	Texture(VkImageView view, TextureData *data) : imageview_(view), data_(data) {}

	vulkan_ptr<VkImageView> imageview_;
	RefPtr<TextureData> data_;
};

class SamplerCache
{
public:
	~SamplerCache();

	void Clean(VkDevice dev, VkAllocationCallbacks *cb);

	VkSampler GetSampler(const SamplerInfo &id);
	void AddSampler(const SamplerInfo& id, VkSampler s);

private:
	std::map<SamplerInfo, VkSampler> cache_;
};


}
}
