#pragma once

#include <vulkan/vulkan.h>

#include <string>
#include <vector>
#include <string_view>
#include <memory>
#include <map>

namespace lune {
namespace gfx {

struct AttachmentInfo
{
	AttachmentInfo(VkFormat fmt = VK_FORMAT_UNDEFINED);

	VkFormat format = VK_FORMAT_UNDEFINED;
	bool target_relative_size = true;
	bool persistent = true;
	float size_x = 1.0f;
	float size_y = 1.0f;
	unsigned samples_shift = 0;
	unsigned levels = 1;
	unsigned layers = 1;

	std::partial_ordering operator<=>(const AttachmentInfo &) const = default;
};

struct SwapchainInfo
{
	AttachmentInfo info;
	std::vector<VkImageView> images;
};

struct BufferInfo
{
	VkDeviceSize size = 0;
	VkBufferUsageFlags usage = 0;
	bool persistent = true;

	std::partial_ordering operator<=>(const BufferInfo &) const = default;
};

enum class FrameGraphNodeType
{
	Graphics,
	Compute,
};

struct FrameGraphPass
{
	// Outputs can be produced exogenously, or update an input (by specifying a source).
	// Output names must be globally unique. If an output updates an input then this pass is
	// sequenced-after all users of the input pass. Source is implicitly added as the appropriate input type.
	FrameGraphPass &AddColorOutput(std::string_view name, const AttachmentInfo &info = AttachmentInfo(),
	    std::string_view source = std::string_view());
	FrameGraphPass &AddBufferOutput(
	    std::string_view name, const BufferInfo &info = BufferInfo(),
	    std::string_view source = std::string_view());
	FrameGraphPass &SetDepthStencilOutput(std::string_view name, const AttachmentInfo &info = AttachmentInfo(),
	    std::string_view source = std::string_view());

	// Add an input. Inputs must either be produced by some pass or be defined externally
	FrameGraphPass &AddColorInput(std::string_view name, const AttachmentInfo &info = AttachmentInfo());
	FrameGraphPass &AddBufferInput(std::string_view name, const BufferInfo &info = BufferInfo());
	FrameGraphPass &SetDepthStencilInput(std::string_view name, const AttachmentInfo &info = AttachmentInfo());

	struct Attachment
	{
		std::string name;
		std::string source;
		AttachmentInfo info;
		FrameGraphPass *producer = nullptr;
	};
	struct Buffer
	{
		std::string name;
		std::string source;
		BufferInfo info;
		FrameGraphPass *producer = nullptr;
	};

	FrameGraphNodeType type;
	std::string name;

	std::vector<Attachment> attachment_inputs;
	std::vector<Buffer> buffer_inputs;

	std::vector<Attachment> attachment_outputs;
	std::vector<Buffer> buffer_outputs;

	Attachment depth_input, depth_output;

	bool pending = false;
	bool complete = false;
	bool resolved = false;
	std::vector<FrameGraphPass *> input_passes;
};

class FrameGraph;

class FrameGraphBuilder
{
public:
	FrameGraphPass &AddPass(std::string_view name, FrameGraphNodeType type = FrameGraphNodeType::Graphics);

	FrameGraphPass *FindPass(std::string_view name);

	// Add an external resource
	void AddResource(std::string_view name, const AttachmentInfo &info);

	// Mark that this buffer persists from one frame to the next and that the dependency is inverted
	void SetPersist(std::string_view name);

	// Set a specific backbuffer to a specific view
	void SetBackbuffer(uint32_t index, std::string_view name);

	std::unique_ptr<FrameGraph> Build(std::string& err);

private:
	friend class FrameGraph;

	bool PopulateProducers(std::string& err);
	bool IsCarriedInput(const std::string &name);
	bool Canonicalize(FrameGraphPass *pass, std::string& err);
	bool Canonicalize(FrameGraphPass::Attachment &a, const AttachmentInfo &b, std::string &err);
	bool Canonicalize(FrameGraphPass::Buffer &a, const BufferInfo &b, std::string &err);

	std::vector<FrameGraphPass::Attachment> attachments_;
	std::vector<FrameGraphPass::Buffer> buffers_;

	std::vector<std::string> persisted_buffers_;

	std::vector<std::unique_ptr<FrameGraphPass>> passes_;
	std::vector<std::string> backbuffers_;
};

class FrameGraph
{
public:
	static std::unique_ptr<FrameGraph> Create(const FrameGraphBuilder *builder);

	struct PhysicalResource
	{
		size_t size;
		VkFormat image_base_format;
	};
};

}
}
