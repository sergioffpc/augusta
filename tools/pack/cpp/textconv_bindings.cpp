#include <cstddef>
#include <cstring>
#include <filesystem>
#include <format>
#include <fstream>
#include <iterator>
#include <memory>
#include <stdexcept>
#include <string>
#include <vector>

#include <DirectXTex.h>
#include <pybind11/pybind11.h>
#include <pybind11/stl/filesystem.h>

#define STB_IMAGE_IMPLEMENTATION
#define STBI_NO_STDIO
#define STBI_ONLY_PNG
#define STBI_ONLY_JPEG
#include <stb_image.h>

namespace py = pybind11;

namespace {

// augusta:textureFormat (ADR-0017/issue #49): selects which BC format a
// texture compresses to. Defaults to BC7 (the common sRGB color-texture
// case) for an unrecognized value.
DXGI_FORMAT ToDxgiFormat(const std::string& format) {
  if (format == "bc5") {
    return DXGI_FORMAT_BC5_UNORM;
  }
  if (format == "bc4") {
    return DXGI_FORMAT_BC4_UNORM;
  }
  return DXGI_FORMAT_BC7_UNORM;
}

// Decodes the PNG/JPEG at image_path with stb_image (ADR-0017) into an
// R8G8B8A8_UNORM image, whatever its channel count or bit depth. Read into
// memory first so std::filesystem opens the path (wide on Windows), not
// stb_image's narrow fopen.
DirectX::ScratchImage LoadImageRgba8(const std::filesystem::path& image_path) {
  std::ifstream file(image_path, std::ios::binary);
  if (!file) {
    throw std::runtime_error(std::format("failed to open texture image {}", image_path.string()));
  }
  const std::vector<char> encoded{std::istreambuf_iterator<char>(file), std::istreambuf_iterator<char>()};

  constexpr int kRgba = 4;
  int width = 0;
  int height = 0;
  int channels_in_file = 0;
  const std::unique_ptr<stbi_uc, decltype(&stbi_image_free)> pixels(
      stbi_load_from_memory(reinterpret_cast<const stbi_uc*>(encoded.data()), static_cast<int>(encoded.size()), &width,
                            &height, &channels_in_file, kRgba),
      &stbi_image_free);
  if (!pixels) {
    throw std::runtime_error(
        std::format("failed to load texture image {}: {}", image_path.string(), stbi_failure_reason()));
  }

  DirectX::ScratchImage image;
  if (FAILED(image.Initialize2D(DXGI_FORMAT_R8G8B8A8_UNORM, static_cast<size_t>(width), static_cast<size_t>(height), 1,
                                1))) {
    throw std::runtime_error(std::format("failed to allocate texture image {}", image_path.string()));
  }
  std::memcpy(image.GetPixels(), pixels.get(), image.GetPixelsSize());
  return image;
}

}  // namespace

// Loads image_path (PNG/JPEG, via stb_image) and block-compresses it
// (ADR-0017) to `format` ("bc7"/"bc5"/"bc4"), returning DirectXTex's own
// SaveToDDSMemory output (DDS header included) as bytes. Raises RuntimeError
// on any failure.
py::bytes CompressTexture(const std::filesystem::path& image_path, const std::string& format) {
  const DirectX::ScratchImage image = LoadImageRgba8(image_path);

  DirectX::ScratchImage compressed;
  if (FAILED(DirectX::Compress(image.GetImages(), image.GetImageCount(), image.GetMetadata(), ToDxgiFormat(format),
                               DirectX::TEX_COMPRESS_DEFAULT, DirectX::TEX_THRESHOLD_DEFAULT, compressed))) {
    throw std::runtime_error(std::format("failed to BC-compress texture image {}", image_path.string()));
  }

  DirectX::Blob dds_blob;
  if (FAILED(DirectX::SaveToDDSMemory(compressed.GetImages(), compressed.GetImageCount(), compressed.GetMetadata(),
                                      DirectX::DDS_FLAGS_NONE, dds_blob))) {
    throw std::runtime_error(std::format("failed to encode DDS for texture image {}", image_path.string()));
  }

  return py::bytes(reinterpret_cast<const char*>(dds_blob.GetBufferPointer()), dds_blob.GetBufferSize());
}

PYBIND11_MODULE(_textconv, m) {
  m.doc() =
      "Thin bindings over stb_image and DirectXTex (ADR-0017), called from pack.cook - PNG/JPEG load + "
      "BC7/BC5/BC4 block compression + DDS encode.";
  m.def("compress_texture", &CompressTexture, py::arg("image_path"), py::arg("format") = "bc7",
        "Loads image_path (PNG/JPEG), BC-compresses to `format` (bc7/bc5/bc4), returns SaveToDDSMemory bytes.");
}
