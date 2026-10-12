#ifndef AUGUSTA_PACK_TEXTURE_H_
#define AUGUSTA_PACK_TEXTURE_H_

#include <cstdint>
#include <filesystem>
#include <string>
#include <vector>

/// \file
/// The block compression the cooker runs on every texture (ADR-0017). Nothing
/// here knows of Python.
namespace augusta::pack {

/// Loads image_path (PNG/JPEG, via stb_image) and block-compresses it to
/// `format` ("bc7"/"bc5"/"bc4", augusta:textureFormat; BC7 for any other
/// value), returning DirectXTex's SaveToDDSMemory output, DDS header included.
/// Throws std::runtime_error on any failure.
std::vector<std::uint8_t> CompressTexture(const std::filesystem::path& image_path, const std::string& format);

}  // namespace augusta::pack

#endif  // AUGUSTA_PACK_TEXTURE_H_
