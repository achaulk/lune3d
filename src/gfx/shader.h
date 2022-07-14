#pragma once

#include "io/file.h"
#include "types.h"
#include "sys/thread.h"

namespace lune {
namespace gfx {

struct Device;

enum class ShaderType
{
	Unknown,

	Vertex,
	Fragment,
	Compute,

	RayGen,
	AnyHit,
	ClosestHit,
	Miss,
	Intersection,
	Callable,
};

class Shader : public Refcounted, public Promisable<RefPtr<Shader>>
{
public:
	explicit Shader(Device *dev);
	Shader(Device *dev, VkShaderModule m);
	Shader(Device *dev, const void *bytes, size_t size);
	~Shader();

private:
	friend class ShaderSPIRV;

	Device *dev_; 
	vulkan_ptr<VkShaderModule> shader_;
};
typedef RefPtr<Shader> ShaderPtr;

class ShaderSPIRV : public Refcounted
{
public:
	ShaderSPIRV(Device *dev, BlobPtr blob);

	ShaderPtr GetShader();
	BlobPtr GetBytes()
	{
		return bytes_;
	}

private:
	void OnReady();

	Device *dev_;
	BlobPtr bytes_;
	ShaderPtr shader_;
};

class CompilerPipeline;

struct CompileContext;
struct CompileOptions : public Refcounted
{
	CompileOptions();
	~CompileOptions();

	RefPtr<CompileOptions> Copy() const;

	std::unique_ptr<CompileContext> context;

private:
	CompileOptions(CompileContext *c);
};

class ShaderSource
{
public:
	ShaderSource(Device *dev, ShaderType type, BlobPtr source, const char *filename);

	RefPtr<ShaderSPIRV> GetSPIRV(CompilerPipeline *pipeline, const CompileOptions *ctx);
	ShaderPtr Compile(CompilerPipeline *pipeline, const CompileOptions *ctx);

private:
	Device *dev_;
	ShaderType type_;
	BlobPtr text_;
	RefPtr<ShaderSPIRV> spirv_;
	std::string filename_;
};

class CompilerCache;

class CompilerPipeline
{
public:
	CompilerPipeline(TaskRunner *runner);
	~CompilerPipeline();

	void AsyncCompile(ShaderType type, Blob *src, DynamicBlob *dest, const CompileOptions *ctx, std::string filename);

private:
	void CompileOnThread(
	    ShaderType type, Blob *src, DynamicBlob *dest, const CompileOptions *ctx, const std::string& filename);

	TaskRunner *compile_runner_ = nullptr;
	std::unique_ptr<CompilerCache> cache_;
};

} // namespace gfx
} // namespace lune
