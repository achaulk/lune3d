#pragma once

#include "blob.h"
#include "sys/thread.h"

namespace lune {

enum class CompressionAlgorithmType
{
	zstd,
};

// Compression and decompression using a single context is safe so long as the same task runner is
// used for each call AND it is a single threaded task tunner

class CompressionContext
{
public:
	virtual ~CompressionContext() = default;

	virtual RefPtr<Blob> Compress(Blob *b, TaskRunner *runner = nullptr) = 0;
};

class DecompressionContext
{
public:
	virtual ~DecompressionContext() = default;

	virtual RefPtr<Blob> Decompress(Blob *b, TaskRunner *runner = nullptr) = 0;
};

class CompressionAlgorithm
{
public:
	static CompressionAlgorithm *Get(CompressionAlgorithmType type);


	virtual std::unique_ptr<CompressionContext> CreateCompressor(Blob *dictionary = nullptr) = 0;
	virtual std::unique_ptr<DecompressionContext> CreateDecompressor(Blob *dictionary = nullptr) = 0;

protected:
	virtual ~CompressionAlgorithm() = default;
};

}
