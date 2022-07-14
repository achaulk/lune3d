#include "shader.h"

#include "shaderc/shaderc.h"

#include "device.h"

namespace lune {
namespace gfx {
namespace {
VkShaderModule CreateShader(Device *dev, const void *bytes, size_t size)
{
	VkShaderModuleCreateInfo ci = {
	    VK_STRUCTURE_TYPE_SHADER_MODULE_CREATE_INFO, nullptr, 0, size, (const uint32_t *)bytes};

	VkShaderModule sm;
	VK_CHECK(vkCreateShaderModule(dev->device, &ci, dev->allocator, &sm));
	return sm;
}
}

Shader::Shader(Device *dev) : dev_(dev) {}

Shader::Shader(Device *dev, VkShaderModule m) : dev_(dev), shader_(m) {}

Shader::Shader(Device *dev, const void *bytes, size_t size) : dev_(dev), shader_(CreateShader(dev, bytes, size))
{
}

Shader::~Shader() = default;

ShaderSPIRV::ShaderSPIRV(Device *dev, BlobPtr blob) : dev_(dev), bytes_(blob), shader_(new Shader(dev))
{
	AddRef();
	bytes_->Then(std::bind(&ShaderSPIRV::OnReady, this));
}

ShaderPtr ShaderSPIRV::GetShader()
{
	return shader_;
}

void ShaderSPIRV::OnReady()
{
	if(bytes_->errored()) {
		shader_->Resolved(true);
		return;
	}
	auto x = bytes_->GetContents();
	shader_->shader_.reset(CreateShader(dev_, x.first, x.second));
	shader_->Resolved(false);
	Release();
}

ShaderSource::ShaderSource(Device *dev, ShaderType type, BlobPtr source, const char *filename)
    : dev_(dev), type_(type), text_(source), filename_(filename)
{
}

RefPtr<ShaderSPIRV> ShaderSource::GetSPIRV(CompilerPipeline *pipeline, const CompileOptions *ctx)
{
	if(!spirv_) {
		auto b = new DynamicBlob();
		spirv_ = new ShaderSPIRV(dev_, b);

		b->AddRef();
		text_->AddRef();
		ctx->AddRef();

		text_->Then([type = type_, dst = b, src = text_.get(), pipeline, ctx, file = std::move(filename_)](Blob*, bool) {
			pipeline->AsyncCompile(type, src, dst, ctx, std::move(file));
			src->Release();
			dst->Release();
			ctx->Release();
		});
	}
	return spirv_;
}

ShaderPtr ShaderSource::Compile(CompilerPipeline *pipeline, const CompileOptions *ctx)
{
	return GetSPIRV(pipeline, ctx)->GetShader();
}


struct CompilerState
{
#if LUNE_SHADER_COMPILER
	CompilerState() : compiler(shaderc_compiler_initialize()) {}
	~CompilerState()
	{
		shaderc_compiler_release(compiler);
	}
#endif

	shaderc_compiler_t compiler = nullptr;
};

class CompilerCache
{
public:
	~CompilerCache()
	{
		for(auto &c : cache) delete c;
	}
	CompilerState *Get()
	{
		CompilerState *s = nullptr;
		lock.lock();
		if(!cache.empty()) {
			s = cache.back();
			cache.pop_back();
		}
		lock.unlock();

		return s ? s : new CompilerState();
	}
	void Put(CompilerState *c)
	{
		lock.lock();
		if(cache.size() < 2) {
			cache.push_back(c);
			c = nullptr;
		}
		lock.unlock();
		delete c;
	}

	CriticalSection lock;
	std::vector<CompilerState *> cache;
};

struct CompileContext
{
#if LUNE_SHADER_COMPILER
	CompileContext() : options(shaderc_compile_options_initialize()) {}
	CompileContext(shaderc_compile_options_t o) : options(o) {}
	~CompileContext()
	{
		shaderc_compile_options_release(options);
	}
#endif

	shaderc_compile_options_t options = nullptr;
};


CompileOptions::CompileOptions() : context(std::make_unique<CompileContext>()) {}
CompileOptions::~CompileOptions() = default;

CompileOptions::CompileOptions(CompileContext *c) : context(c) {}


RefPtr<CompileOptions> CompileOptions::Copy() const
{
#if LUNE_SHADER_COMPILER
	auto copy = shaderc_compile_options_clone(context->options);
	return new CompileOptions(new CompileContext(copy));
#else
	return new CompileOptions();
#endif
}

CompilerPipeline::CompilerPipeline(TaskRunner *runner) : compile_runner_(runner) {}
CompilerPipeline::~CompilerPipeline() = default;

void CompilerPipeline::AsyncCompile(
    ShaderType type, Blob *src, DynamicBlob *dest, const CompileOptions *ctx, std::string filename)
{
	compile_runner_->PostTask(std::bind(&CompilerPipeline::CompileOnThread, this, type, src, dest, ctx, std::move(filename)));
}

void CompilerPipeline::CompileOnThread(
    ShaderType type, Blob *src, DynamicBlob *dest, const CompileOptions *ctx, const std::string& filename)
{
#if LUNE_SHADER_COMPILER
	auto s = cache_->Get();

	auto txt = src->GetContents();

	const char *entry;
	shaderc_shader_kind sc_type;
	switch(type) {
	case ShaderType::Vertex:
		sc_type = shaderc_vertex_shader;
		entry = "vertexmain";
		break;
	case ShaderType::Fragment:
		sc_type = shaderc_fragment_shader;
		entry = "fragmain";
		break;
	case ShaderType::Compute:
		sc_type = shaderc_compute_shader;
		entry = "main";
		break;
	case ShaderType::RayGen:
		sc_type = shaderc_fragment_shader;
		entry = "raygen";
		break;
	case ShaderType::AnyHit:
		sc_type = shaderc_fragment_shader;
		entry = "anyhit";
		break;
	case ShaderType::ClosestHit:
		sc_type = shaderc_fragment_shader;
		entry = "closesthit";
		break;
	case ShaderType::Miss:
		sc_type = shaderc_fragment_shader;
		entry = "miss";
		break;
	case ShaderType::Intersection:
		sc_type = shaderc_fragment_shader;
		entry = "intersection";
		break;
	case ShaderType::Callable:
		sc_type = shaderc_fragment_shader;
		entry = "callable";
		break;
	default:
		sc_type = shaderc_glsl_infer_from_source;
		entry = "main";
		break;
	}

	shaderc_compile_options_t opts = nullptr;
	if(ctx && ctx->context)
		opts = ctx->context->options;

	shaderc_compilation_result_t result = shaderc_compile_into_spv(
	    s->compiler, (const char *)txt.first, txt.second, sc_type, filename.c_str(), entry, opts);
	if(!result) {
		dest->Set("No compiler result!", true);
	} else if(shaderc_result_get_compilation_status(result) != shaderc_compilation_status_success) {
		dest->Set(shaderc_result_get_error_message(result), true);
	} else {
		dest->Copy(shaderc_result_get_bytes(result), shaderc_result_get_length(result));
	}
	if(result)
		shaderc_result_release(result);

	cache_->Put(s);
#else
	dest->Set("No compiler included!");
#endif
}

} // namespace gfx
} // namespace lune
