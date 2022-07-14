#include "texture.h"
#include "device.h"

namespace lune {
namespace gfx {
namespace {
#define GET_FORMAT_SRGB(x, extra)                                                     \
	switch(element) {                                                                 \
	case TextureElement::Default:                                                     \
	case TextureElement::SRGB:                                                        \
		return LUNE_CONCAT(LUNE_CONCAT(VK_FORMAT_, LUNE_CONCAT(x, _SRGB)), extra);    \
	case TextureElement::SignedNormalized:                                            \
		return LUNE_CONCAT(LUNE_CONCAT(VK_FORMAT_, LUNE_CONCAT(x, _SNORM)), extra);   \
	case TextureElement::UnsignedNormalized:                                          \
		return LUNE_CONCAT(LUNE_CONCAT(VK_FORMAT_, LUNE_CONCAT(x, _UNORM)), extra);   \
	case TextureElement::SignedScaled:                                                \
		return LUNE_CONCAT(LUNE_CONCAT(VK_FORMAT_, LUNE_CONCAT(x, _SSCALED)), extra); \
	case TextureElement::UnsignedScaled:                                              \
		return LUNE_CONCAT(LUNE_CONCAT(VK_FORMAT_, LUNE_CONCAT(x, _USCALED)), extra); \
	}

#define GET_FORMAT_F(x, extra)                                                        \
	switch(element) {                                                                 \
	case TextureElement::Default:                                                     \
	case TextureElement::Float:                                                       \
		return LUNE_CONCAT(LUNE_CONCAT(VK_FORMAT_, LUNE_CONCAT(x, _SFLOAT)), extra);  \
	case TextureElement::SignedNormalized:                                            \
		return LUNE_CONCAT(LUNE_CONCAT(VK_FORMAT_, LUNE_CONCAT(x, _SNORM)), extra);   \
	case TextureElement::UnsignedNormalized:                                          \
		return LUNE_CONCAT(LUNE_CONCAT(VK_FORMAT_, LUNE_CONCAT(x, _UNORM)), extra);   \
	case TextureElement::SignedScaled:                                                \
		return LUNE_CONCAT(LUNE_CONCAT(VK_FORMAT_, LUNE_CONCAT(x, _SSCALED)), extra); \
	case TextureElement::UnsignedScaled:                                              \
		return LUNE_CONCAT(LUNE_CONCAT(VK_FORMAT_, LUNE_CONCAT(x, _USCALED)), extra); \
	}

} // namespace

VkImageUsageFlags TextureInfo::GetUsage() const
{
	bool is_depth = false;
	switch(format) {
	case TextureFormat::Stencil8:
	case TextureFormat::Depth16:
	case TextureFormat::Depth24:
	case TextureFormat::Depth32:
	case TextureFormat::Depth16_Stencil8:
	case TextureFormat::Depth24_Stencil8:
	case TextureFormat::Depth32_Stencil8:
		is_depth = true;
	}
	switch(usage) {
	case TextureUsage::Texture:
		return VK_IMAGE_USAGE_TRANSFER_DST_BIT | VK_IMAGE_USAGE_SAMPLED_BIT;
	case TextureUsage::DynamicTexture:
		return VK_IMAGE_USAGE_TRANSFER_SRC_BIT | VK_IMAGE_USAGE_TRANSFER_DST_BIT | VK_IMAGE_USAGE_SAMPLED_BIT;
	case TextureUsage::RenderTarget:
		if(is_depth)
			return VK_IMAGE_USAGE_DEPTH_STENCIL_ATTACHMENT_BIT | VK_IMAGE_USAGE_INPUT_ATTACHMENT_BIT |
			       VK_IMAGE_USAGE_SAMPLED_BIT;
		return VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT | VK_IMAGE_USAGE_INPUT_ATTACHMENT_BIT | VK_IMAGE_USAGE_SAMPLED_BIT;
	case TextureUsage::IntermediateRenderTarget:
		if(is_depth)
			return VK_IMAGE_USAGE_DEPTH_STENCIL_ATTACHMENT_BIT | VK_IMAGE_USAGE_INPUT_ATTACHMENT_BIT;
		return VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT | VK_IMAGE_USAGE_INPUT_ATTACHMENT_BIT;
	case TextureUsage::TextureRenderTarget:
		if(is_depth)
			return VK_IMAGE_USAGE_DEPTH_STENCIL_ATTACHMENT_BIT;
		return VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT | VK_IMAGE_USAGE_SAMPLED_BIT;
	case TextureUsage::DepthBuffer:
		return VK_IMAGE_USAGE_DEPTH_STENCIL_ATTACHMENT_BIT | VK_IMAGE_USAGE_SAMPLED_BIT;
	}
	return VK_IMAGE_USAGE_TRANSFER_DST_BIT | VK_IMAGE_USAGE_SAMPLED_BIT;
}

VkFormat TextureInfo::GetFormat(TextureFormat format, TextureElement element)
{
	switch(format) {
	case TextureFormat::Default:
	case TextureFormat::RGBA8:
		GET_FORMAT_SRGB(R8G8B8A8, )
		break;
	case TextureFormat::ABGR8:
		GET_FORMAT_SRGB(A8B8G8R8, _PACK32)
		break;
	case TextureFormat::BGRA8:
		GET_FORMAT_SRGB(B8G8R8A8, )
		break;
	case TextureFormat::R8:
		GET_FORMAT_SRGB(R8, )
		break;
	case TextureFormat::RGB8:
		GET_FORMAT_SRGB(R8G8B8, )
		break;
	case TextureFormat::BGR8:
		GET_FORMAT_SRGB(B8G8R8, )
		break;
	case TextureFormat::RGBA16:
		GET_FORMAT_F(R16G16B16A16, )
		break;
	case TextureFormat::RGBA32:
		switch(element) {
		case TextureElement::Default:
		case TextureElement::Float:
			return VK_FORMAT_R32G32B32A32_SFLOAT;
		}
		break;
	case TextureFormat::R10G11B11_F:
		return VK_FORMAT_B10G11R11_UFLOAT_PACK32; 
	case TextureFormat::Stencil8:
		return VK_FORMAT_S8_UINT;
	case TextureFormat::Depth16:
		return VK_FORMAT_D16_UNORM;
	case TextureFormat::Depth24:
		return VK_FORMAT_X8_D24_UNORM_PACK32;
	case TextureFormat::Depth32:
		return VK_FORMAT_D32_SFLOAT;
	case TextureFormat::Depth16_Stencil8:
		return VK_FORMAT_D16_UNORM_S8_UINT;
	case TextureFormat::Depth24_Stencil8:
		return VK_FORMAT_D24_UNORM_S8_UINT;
	case TextureFormat::Depth32_Stencil8:
		return VK_FORMAT_D32_SFLOAT_S8_UINT;
	}
	return VK_FORMAT_UNDEFINED;
}

TextureData::~TextureData()
{
	dev_->delete_queue.Enqueue(used_, &vkDestroyImage, image_.release());
}

RefPtr<TextureData> TextureData::Construct2D(Device *dev, const TextureInfo &info, MemoryPriority priority)
{
	VkImageCreateInfo image = {VK_STRUCTURE_TYPE_IMAGE_CREATE_INFO, nullptr, 0, VK_IMAGE_TYPE_2D, info.GetFormat(),
	    VkExtent3D{(unsigned)info.size.x, (unsigned)info.size.y, 1U}, info.num_mipmaps, 1,
	    (VkSampleCountFlagBits)(1U << info.num_samples_shift), VK_IMAGE_TILING_OPTIMAL, info.GetUsage(),
	    VK_SHARING_MODE_EXCLUSIVE, 0, nullptr, VK_IMAGE_LAYOUT_UNDEFINED};
	VmaAllocationCreateInfo createinfo = {};
	createinfo.usage = VmaMemoryUsage::VMA_MEMORY_USAGE_GPU_ONLY;
	switch(priority) {
	case MemoryPriority::Critical:
		createinfo.flags = VMA_ALLOCATION_CREATE_CAN_MAKE_OTHER_LOST_BIT;
		break;
	case MemoryPriority::Important:
		createinfo.flags = VMA_ALLOCATION_CREATE_CAN_MAKE_OTHER_LOST_BIT | VMA_ALLOCATION_CREATE_CAN_BECOME_LOST_BIT;
		break;
	case MemoryPriority::Normal:
		createinfo.flags = VMA_ALLOCATION_CREATE_CAN_MAKE_OTHER_LOST_BIT | VMA_ALLOCATION_CREATE_CAN_BECOME_LOST_BIT | VMA_ALLOCATION_CREATE_WITHIN_BUDGET_BIT;
		break;
	case MemoryPriority::Low:
		createinfo.flags = VMA_ALLOCATION_CREATE_CAN_BECOME_LOST_BIT | VMA_ALLOCATION_CREATE_WITHIN_BUDGET_BIT |
		                   VMA_ALLOCATION_CREATE_NEVER_ALLOCATE_BIT;
		break;
	}

	VkImage i;
	VmaAllocation a;
	auto r = vmaCreateImage(dev->alloc, &image, &createinfo, &i, &a, nullptr);
	if(r != VK_SUCCESS)
		return nullptr;

	return new TextureData(dev, a, i, info);
}

RefPtr<Texture> TextureData::CreateView(TextureFormat format, TextureElement element, uint32_t swizzle)
{
	VkImageAspectFlags aspect = VK_IMAGE_ASPECT_COLOR_BIT;
	VkImageViewCreateInfo ci = {VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO, nullptr, 0, image_.get(),
	    VK_IMAGE_VIEW_TYPE_2D, TextureInfo::GetFormat(format, element),
	    {(VkComponentSwizzle)((swizzle >> 12) & 0xF), (VkComponentSwizzle)((swizzle >> 8) & 0xF),
	        (VkComponentSwizzle)((swizzle >> 4) & 0xF), (VkComponentSwizzle)(swizzle & 0xF)},
	    {aspect, 0, VK_REMAINING_MIP_LEVELS, 0, VK_REMAINING_ARRAY_LAYERS}};
	VkImageView v;
	auto r = vkCreateImageView(dev_->device, &ci, dev_->allocator, &v);
	if(r != VK_SUCCESS)
		return nullptr;

	return new Texture(v, this);
}

void TextureData::OnLost()
{
}

SamplerInfo::SamplerInfo(uint32_t min_filter, uint32_t mag_filter, uint32_t mipmode, uint32_t u_mode, uint32_t v_mode,
    uint32_t w_mode, float lod_bias, bool anisotropic, float min_lod, float max_lod, VkBorderColor border,
    bool unnormalized, float max_anisotropic)
    : lod_bias(lod_bias), lod_min(min_lod), lod_max(max_lod), max_aniso(anisotropic ? max_anisotropic : 0.0f)
{
	info = 0;
}

bool SamplerInfo::operator<(const SamplerInfo &o) const
{
	return std::tie(info, lod_bias, lod_min, lod_max, max_aniso) <
	       std::tie(o.info, o.lod_bias, o.lod_min, o.lod_max, o.max_aniso);
}

template<class T>
inline void hash_combine(std::size_t &seed, const T &v)
{
	std::hash<T> hasher;
	seed ^= hasher(v) + 0x9e3779b9 + (seed << 6) + (seed >> 2);
}

size_t SamplerInfo::hash() const
{
	size_t h = 0;
	hash_combine(h, info);
	hash_combine(h, lod_bias);
	hash_combine(h, lod_min);
	hash_combine(h, lod_max);
	hash_combine(h, max_aniso);
	return h;
}

VkFilter SamplerInfo::min() const
{
	return (VkFilter)((info)&0x1UL);
}
VkFilter SamplerInfo::mag() const
{
	return (VkFilter)((info >> 1) & 0x1UL);
}
VkSamplerMipmapMode SamplerInfo::mip() const
{
	return (VkSamplerMipmapMode)((info >> 2) & 0x1UL);
}
VkSamplerAddressMode SamplerInfo::u() const
{
	return (VkSamplerAddressMode)((info >> 3) & 0x7UL);
}
VkSamplerAddressMode SamplerInfo::v() const
{
	return (VkSamplerAddressMode)((info >> 6) & 0x7UL);
}
VkSamplerAddressMode SamplerInfo::w() const
{
	return (VkSamplerAddressMode)((info >> 9) & 0x7UL);
}

VkBool32 SamplerInfo::anisotropic() const
{
	return (VkBool32)((info >> 12) & 0x1UL);
}

VkBorderColor SamplerInfo::border() const
{
	return (VkBorderColor)((info >> 14) & 0x7UL);
}
VkBool32 SamplerInfo::unnormalized() const
{
	return (VkBool32)((info >> 13) & 0x1UL);
}

Texture::~Texture()
{
	data_->dev_->delete_queue.Enqueue(data_->used_, &vkDestroyImageView, imageview_.release());
}

VkSampler Texture::GetSampler(const SamplerInfo &info)
{
	size_t id = info.hash();
	auto c = data_->dev_->samplercache.get();
	auto sampler = c->GetSampler(id);
	if(sampler != VK_NULL_HANDLE)
		return sampler;

	VkSamplerCreateInfo ci = {VK_STRUCTURE_TYPE_SAMPLER_CREATE_INFO, nullptr, 0, info.min(), info.mag(), info.mip(),
	    info.u(), info.v(), info.w(), info.lod_bias, info.anisotropic(), info.max_aniso, VK_FALSE, VK_COMPARE_OP_NEVER,
	    info.lod_min, info.lod_max, info.border(), info.unnormalized()};
	auto r = vkCreateSampler(data_->dev_->device, &ci, data_->dev_->allocator, &sampler);
	if(r != VK_SUCCESS)
		return VK_NULL_HANDLE;

	c->AddSampler(id, sampler);
	return sampler;
}

SamplerCache::~SamplerCache()
{
	assert(cache_.empty());
}

void SamplerCache::Clean(VkDevice dev, VkAllocationCallbacks *cb)
{
	for(auto &e : cache_) vkDestroySampler(dev, e.second, cb);
	cache_.clear();
}

VkSampler SamplerCache::GetSampler(const SamplerInfo &id)
{
	auto it = cache_.find(id);
	return (it == cache_.end()) ? VK_NULL_HANDLE : it->second;
}

void SamplerCache::AddSampler(const SamplerInfo &id, VkSampler s)
{
	cache_[id] = s;
}

} // namespace gfx
} // namespace lune
