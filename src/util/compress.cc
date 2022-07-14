#include "compress.h"

namespace lune {
namespace details {
CompressionAlgorithm *CompressZstd();
}

CompressionAlgorithm *CompressionAlgorithm::Get(CompressionAlgorithmType type)
{
	switch(type) {
	case CompressionAlgorithmType::zstd:
		return details::CompressZstd();
	default:
		return nullptr;
	}
}

}
