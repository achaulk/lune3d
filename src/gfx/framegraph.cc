#include "framegraph.h"

#include <deque>

namespace lune {
namespace gfx {

AttachmentInfo::AttachmentInfo(VkFormat fmt) : format(fmt) {}

FrameGraphPass &FrameGraphPass::AddColorOutput(
    std::string_view name, const AttachmentInfo &info, std::string_view source)
{
	attachment_outputs.emplace_back(Attachment{std::string(name), std::string(source), info});
	return *this;
}
FrameGraphPass &FrameGraphPass::AddBufferOutput(std::string_view name, const BufferInfo &info, std::string_view source)
{
	buffer_outputs.emplace_back(Buffer{std::string(name), std::string(source), info});
	return *this;
}
FrameGraphPass &FrameGraphPass::SetDepthStencilOutput(
    std::string_view name, const AttachmentInfo &info, std::string_view source)
{
	depth_output.name = name;
	depth_output.source = source;
	depth_output.info = info;
	return *this;
}

// Add an input. Inputs must either be produced by some pass or be defined externally
FrameGraphPass &FrameGraphPass::AddColorInput(std::string_view name, const AttachmentInfo &info)
{
	attachment_inputs.emplace_back(Attachment{std::string(name), std::string(name), info});
	return *this;
}
FrameGraphPass &FrameGraphPass::AddBufferInput(std::string_view name, const BufferInfo &info)
{
	buffer_inputs.emplace_back(Buffer{std::string(name), std::string(name), info});
	return *this;
}
FrameGraphPass &FrameGraphPass::SetDepthStencilInput(std::string_view name, const AttachmentInfo &info)
{
	depth_input.name = name;
	depth_input.info = info;
	return *this;
}

FrameGraphPass &FrameGraphBuilder::AddPass(std::string_view name, FrameGraphNodeType type)
{
	auto pass = std::make_unique<FrameGraphPass>();
	auto ptr = pass.get();
	ptr->name = name;
	ptr->type = type;
	passes_.emplace_back(std::move(pass));
	return *ptr;
}

void FrameGraphBuilder::AddResource(std::string_view name, const AttachmentInfo &info)
{
	attachments_.emplace_back(FrameGraphPass::Attachment{std::string(name), std::string(), info});
}

void FrameGraphBuilder::SetPersist(std::string_view name)
{
	persisted_buffers_.emplace_back(name);
}

void FrameGraphBuilder::SetBackbuffer(uint32_t index, std::string_view name)
{
	if(index >= backbuffers_.size())
		backbuffers_.resize(index + 1);
	backbuffers_[index] = name;
}

FrameGraphPass *FrameGraphBuilder::FindPass(std::string_view name)
{
	for(auto &p : passes_)
		if(p->name == name)
			return p.get();
	return nullptr;
}

bool FrameGraphBuilder::IsCarriedInput(const std::string &name)
{
	return std::find(persisted_buffers_.begin(), persisted_buffers_.end(), name) != persisted_buffers_.end();
}

bool FrameGraphBuilder::PopulateProducers(std::string &err)
{
	std::map<std::string_view, FrameGraphPass *> producers;

	auto check = [&producers, this](const std::string &name, auto &v) -> bool {
		auto it = producers.find(name);
		if(it != producers.end()) {
			v.producer = it->second;
			return true;
		}
		for(auto &a : attachments_) {
			if(a.name == name)
				return true;
		}
		for(auto &a : buffers_) {
			if(a.name == name)
				return true;
		}
		return false;
	};

	auto push = [](FrameGraphPass *p1, FrameGraphPass *p2) {
		if(!p2 || p1 == p1)
			return;
		for(auto &p : p1->input_passes)
			if(p == p2)
				return;
		p1->input_passes.push_back(p2);
	};

	for(auto &p : passes_) {
		p->pending = false;
		for(auto &o : p->attachment_outputs) {
			if(!producers.emplace(o.name, p.get()).second) {
				err = "Non-unique output " + o.name;
				return false;
			}
		}
		for(auto &o : p->buffer_outputs) {
			if(!producers.emplace(o.name, p.get()).second) {
				err = "Non-unique output " + o.name;
				return false;
			}
		}
		if(!p->depth_output.name.empty() && !producers.emplace(p->depth_output.name, p.get()).second) {
			err = "Non-unique output " + p->depth_output.name;
			return false;
		}
	}
	for(auto &p : passes_) {
		if(!p->depth_input.name.empty() && !check(p->depth_input.name, p->depth_input)) {
			err = "No such input " + p->depth_input.name;
			return false;
		}
		if(!p->depth_output.source.empty() && !check(p->depth_output.source, p->depth_output)) {
			err = "No such input " + p->depth_output.source;
			return false;
		}
		for(auto &o : p->attachment_outputs) {
			if(!o.source.empty() && !check(o.source, o)) {
				err = "No such input " + o.source;
				return false;
			}
			if(!IsCarriedInput(o.source))
				push(p.get(), o.producer);
		}
		for(auto &o : p->buffer_outputs) {
			if(!o.source.empty() && !check(o.source, o)) {
				err = "No such input " + o.source;
				return false;
			}
			if(!IsCarriedInput(o.source))
				push(p.get(), o.producer);
		}
		for(auto &o : p->attachment_inputs) {
			if(!check(o.name, o)) {
				err = "No such input " + o.name;
				return false;
			}
			if(!IsCarriedInput(o.name))
				push(p.get(), o.producer);
		}
		for(auto &o : p->buffer_inputs) {
			if(!check(o.name, o)) {
				err = "No such input " + o.name;
				return false;
			}
			if(!IsCarriedInput(o.name))
				push(p.get(), o.producer);
		}
	}
	return true;
}

bool FrameGraphBuilder::Canonicalize(FrameGraphPass *pass, std::string &err)
{
	for(auto &o : pass->attachment_outputs) {
		for(auto i : pass->input_passes) {
			for(auto &ii : i->attachment_inputs) {
				if(ii.name == o.name && !Canonicalize(o, ii.info, err))
					return false;
			}
		}
		for(auto &a : attachments_)
			if(a.name == o.name && !Canonicalize(o, a.info, err))
				return false;
	}
	for(auto &o : pass->buffer_outputs) {
		for(auto i : pass->input_passes) {
			for(auto &ii : i->buffer_inputs) {
				if(ii.name == o.name && !Canonicalize(o, ii.info, err))
					return false;
			}
		}
		for(auto &a : buffers_)
			if(a.name == o.name && !Canonicalize(o, a.info, err))
				return false;
	}
	for(auto i : pass->input_passes) {
		if(pass->depth_output.name == i->depth_input.name && !Canonicalize(pass->depth_output, i->depth_input.info, err))
			return false;
	}
	return true;
}

bool FrameGraphBuilder::Canonicalize(FrameGraphPass::Attachment &a, const AttachmentInfo &b, std::string &err)
{
	if(b.format == VK_FORMAT_UNDEFINED)
		return true;
	if(a.info.format == VK_FORMAT_UNDEFINED) {
		a.info = b;
		return true;
	}
	if(a.info == b)
		return true;
	err = "Format mismatch for " + a.name;
	return false;
}

bool FrameGraphBuilder::Canonicalize(FrameGraphPass::Buffer &a, const BufferInfo &b, std::string &err)
{
	if(b.size == 0)
		return true;
	if(a.info.size == 0) {
		a.info = b;
		return true;
	}
	if(a.info == b)
		return true;
	err = "Format mismatch for " + a.name;
	return false;
}

std::unique_ptr<FrameGraph> FrameGraphBuilder::Build(std::string &err)
{
	if(!PopulateProducers(err))
		return nullptr;
	for(auto &p : passes_) {
		if(!Canonicalize(p.get(), err))
			return nullptr;
	}

	std::deque<FrameGraphPass *> pending;
	std::vector<FrameGraphPass *> order;

	auto push = [&pending](FrameGraphPass *p) {
		if(p->pending)
			return false;
		p->pending = true;
		pending.push_back(p);
		return true;
	};

	auto recursive_push = [&push](FrameGraphPass *p, auto &recurse) -> void {
		if(push(p)) {
			for(auto *p : p->input_passes) recurse(p, recurse);
		}
	};

	auto is_complete = [](FrameGraphPass *p) {
		for(auto *i : p->input_passes)
			if(!i->complete)
				return false;
		return true;
	};

	order.reserve(passes_.size());

	std::vector<FrameGraphPass *> targets;
	for(auto &bb : backbuffers_) {
		if(bb.empty())
			continue;
		FrameGraphPass *pass = nullptr;
		for(auto &p : passes_) {
			for(auto &o : p->attachment_outputs) {
				if(o.name == bb) {
					pass = p.get();
					targets.push_back(p.get());
					break;
				}
			}
			if(pass)
				break;
		}
		if(!pass) {
			err = "No such backbuffer: " + bb;
			return nullptr;
		}
		recursive_push(pass, recursive_push);
	}
	if(targets.empty()) {
		err = "No backbuffers";
		return nullptr;
	}

	while(!pending.empty()) {
		auto *p = pending.back();
		p->pending = false;
		pending.pop_back();
		if(is_complete(p)) {
			// Okay this is ready
			order.push_back(p);
			p->complete = true;
			// Insert every pass that depends on this pass
			for(auto &pp : passes_) {
				if(!pp->pending &&
				    std::find(pp->input_passes.begin(), pp->input_passes.end(), p) != pp->input_passes.end())
					push(p);
			}
		}
	}

	for(auto &t : targets) {
		if(std::find(order.begin(), order.end(), t) == order.end()) {
			err = "Undecidable / unreachable target pass " + t->name;
			return nullptr;
		}
	}

	return FrameGraph::Create(this);
}

std::unique_ptr<FrameGraph> FrameGraph::Create(const FrameGraphBuilder *builder)
{
	auto g = std::make_unique<FrameGraph>();
	g;
	return g;
}

} // namespace gfx
} // namespace lune
