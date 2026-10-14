#ifndef AUGUSTA_PACK_TEXTURE_H_
#define AUGUSTA_PACK_TEXTURE_H_

#include <cstdint>
#include <filesystem>
#include <string>
#include <vector>

/// \file
/// The KTX2 encoding the cooker runs on every texture (ADR-0017). Nothing here
/// knows of Python.
namespace augusta::pack {

/// An image in 8-bit RGBA, row by row from the top.
struct Image {
  std::uint32_t width = 0;
  std::uint32_t height = 0;
  std::vector<std::uint8_t> rgba;
};

/// What a texture's texels hold, which decides how its levels are filtered.
enum class Content {
  /// sRGB colour, filtered in linear light, alpha weighting the colour.
  kColour,
  /// Linear data, filtered as stored, every channel independent.
  kData,
};

/// What augusta:textureFormat `format` holds, as CompressTexture cooks it.
Content ContentOf(const std::string& format);

/// The full mip chain of base: level 0 is base itself, and level n is
/// max(1, floor(size / 2^n)) on each axis, the KTX2 specification's sizes,
/// down to 1x1. Each level is box-filtered from base, as `content` says.
/// Exposed apart from CompressTexture so the filtering can be checked before
/// the levels are BC-encoded. Throws std::runtime_error on failure.
std::vector<Image> GenerateMipChain(const Image& base, Content content);

/// Loads image_path (PNG/JPEG, via stb_image) and encodes it with libktx to a
/// KTX2 file in `format` ("bc7"/"bc5"/"bc4", augusta:textureFormat; sRGB BC7
/// for any other value) with its full mip chain (GenerateMipChain),
/// Zstandard-supercompressed, returning the file's bytes.
/// Throws std::runtime_error on any failure.
std::vector<std::uint8_t> CompressTexture(const std::filesystem::path& image_path, const std::string& format);

}  // namespace augusta::pack

#endif  // AUGUSTA_PACK_TEXTURE_H_
