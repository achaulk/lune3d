#include "compress.h"

#define ZSTD_STATIC_LINKING_ONLY
#include "third_party/zstd/lib/zstd.h"

namespace lune {
namespace details {
namespace {

class ZSTDCCTX : public CompressionContext
{
public:
	ZSTDCCTX(Blob *dict) : dict_(dict), cctx_(ZSTD_createCCtx())
	{
		if(dict) {
			auto data = dict->GetContents();
			cdict_ = ZSTD_createCDict_byReference(data.first, data.second, 0);
			ZSTD_CCtx_refCDict(cctx_, cdict_);
		}
	}
	~ZSTDCCTX() override
	{
		ZSTD_freeCCtx(cctx_);
		if(cdict_)
			ZSTD_freeCDict(cdict_);
	}

	RefPtr<Blob> Compress(Blob *b, TaskRunner *runner) final
	{
		RefPtr<DynamicBlob> ret = new DynamicBlob();
		b->AddRef();
		ret->AddRef();
		if(runner) {
			runner->PostTask(std::bind(&ZSTDCCTX::DoCompress, this, b, ret.get()));
		} else {
			DoCompress(b, ret);
		}
		return ret;
	}

private:
	void DoCompress(Blob *in, DynamicBlob *out)
	{
		auto src = in->GetContents();
		auto bound = ZSTD_compressBound(src.second);
		auto p = malloc(bound);
		assert(p);
		auto sz = ZSTD_compress2(cctx_, p, bound, src.first, src.second);
		out->Set(p, sz);

		in->Release();
		out->Release();
	}

	RefPtr<Blob> dict_;
	ZSTD_CCtx *cctx_ = nullptr;
	ZSTD_CDict *cdict_ = nullptr;
};

class ZSTDDCTX : public DecompressionContext
{
public:
	ZSTDDCTX(Blob *dict) : dict_(dict), dctx_(ZSTD_createDCtx())
	{
		if(dict) {
			auto data = dict->GetContents();
			ddict_ = ZSTD_createDDict_byReference(data.first, data.second);
			ZSTD_DCtx_refDDict(dctx_, ddict_);
		}
	}
	~ZSTDDCTX() override
	{
		ZSTD_freeDCtx(dctx_);
		if(ddict_)
			ZSTD_freeDDict(ddict_);
	}

	RefPtr<Blob> Decompress(Blob *b, TaskRunner *runner) final
	{
		RefPtr<DynamicBlob> ret = new DynamicBlob();
		b->AddRef();
		ret->AddRef();
		if(runner) {
			runner->PostTask(std::bind(&ZSTDDCTX::DoDecompress, this, b, ret.get()));
		} else {
			DoDecompress(b, ret);
		}
		return ret;
	}

private:
	void DoDecompress(Blob *in, DynamicBlob *out)
	{
		auto src = in->GetContents();
		auto bound = ZSTD_decompressBound(src.first, src.second);
		if(bound == ZSTD_CONTENTSIZE_ERROR) {
			out->Resolved(true);
		} else {
			auto p = malloc(bound);
			assert(p);
			ZSTD_inBuffer inb = {src.first, src.second, 0};
			ZSTD_outBuffer outb = {p, bound, 0};
			auto sz = ZSTD_decompressStream(dctx_, &outb, &inb);
			if(ZSTD_isError(sz)) {
				out->Resolved(true);
			} else {
				out->Set(p, sz);
			}
		}

		in->Release();
		out->Release();
	}

	RefPtr<Blob> dict_;
	ZSTD_DCtx *dctx_ = nullptr;
	ZSTD_DDict *ddict_ = nullptr;
};

class ZSTD : public CompressionAlgorithm
{
public:
	std::unique_ptr<CompressionContext> CreateCompressor(Blob *dictionary) final
	{
		return std::make_unique<ZSTDCCTX>(dictionary);
	}
	std::unique_ptr<DecompressionContext> CreateDecompressor(Blob *dictionary) final
	{
		return std::make_unique<ZSTDDCTX>(dictionary);
	}
} zstd;

}

CompressionAlgorithm *CompressZstd()
{
	return &zstd;
}
}
} // namespace lune
