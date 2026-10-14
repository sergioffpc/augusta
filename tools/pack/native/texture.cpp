#include "texture.h"

#include <algorithm>
#include <cstddef>
#include <cstdint>
#include <cstdlib>
#include <filesystem>
#include <format>
#include <fstream>
#include <ios>
#include <iterator>
#include <memory>
#include <span>
#include <stdexcept>
#include <string>
#include <string_view>
#include <thread>
#include <vector>

#include <ktx.h>

#define STB_IMAGE_IMPLEMENTATION
#define STBI_NO_STDIO
#define STBI_ONLY_PNG
#define STBI_ONLY_JPEG
#include <stb_image.h>

namespace augusta::pack {

namespace {

constexpr int kRgbaChannels = 4;

// The VkFormats an image's pixels are handed to libktx as: libktx's header
// carries no Vulkan enums, so their values are spelled out.
constexpr ktx_uint32_t kVkFormatR8G8B8A8Unorm = 37;
constexpr ktx_uint32_t kVkFormatR8G8B8A8Srgb = 43;

// The Zstandard level the levels are supercompressed with (ADR-0017): fixed, so
// a cook is reproducible and no author-facing knob exists.
constexpr ktx_uint32_t kZstdLevel = 18;

// What augusta:textureFormat (ADR-0017) cooks a texture to: the RGBA format its
// pixels are handed to libktx as (sRGB for colour), the swizzle that places
// the channels the BC format keeps where its transcode reads them, and the BC
// format the UASTC is transcoded to.
struct Target {
  ktx_uint32_t source_format;
  std::string_view input_swizzle;
  ktx_transcode_fmt_e transcode_format;
};

// bc7 is colour; bc5 keeps two linear channels, which the transcode reads from
// red and alpha; bc4 keeps one, read from red. Any other value is bc7.
Target ToTarget(const std::string& format) {
  if (format == "bc5") {
    return {.source_format = kVkFormatR8G8B8A8Unorm, .input_swizzle = "rrrg", .transcode_format = KTX_TTF_BC5_RG};
  }
  if (format == "bc4") {
    return {.source_format = kVkFormatR8G8B8A8Unorm, .input_swizzle = "rrr1", .transcode_format = KTX_TTF_BC4_R};
  }
  return {.source_format = kVkFormatR8G8B8A8Srgb, .input_swizzle = "", .transcode_format = KTX_TTF_BC7_RGBA};
}

// An image decoded to 8-bit RGBA, row by row from the top.
struct Image {
  std::uint32_t width = 0;
  std::uint32_t height = 0;
  std::unique_ptr<stbi_uc, decltype(&stbi_image_free)> pixels{nullptr, &stbi_image_free};
};

// Decodes the PNG/JPEG at image_path with stb_image (ADR-0017) into 8-bit RGBA,
// whatever its channel count or bit depth. Read into memory first so
// std::filesystem opens the path (wide on Windows), not stb_image's narrow
// fopen.
Image LoadImageRgba8(const std::filesystem::path& image_path) {
  std::ifstream file(image_path, std::ios::binary);
  if (!file) {
    throw std::runtime_error(std::format("failed to open texture image {}", image_path.string()));
  }
  const std::vector<char> encoded{std::istreambuf_iterator<char>(file), std::istreambuf_iterator<char>()};

  int width = 0;
  int height = 0;
  int channels_in_file = 0;
  Image image;
  image.pixels.reset(stbi_load_from_memory(reinterpret_cast<const stbi_uc*>(encoded.data()),
                                           static_cast<int>(encoded.size()), &width, &height, &channels_in_file,
                                           kRgbaChannels));
  if (!image.pixels) {
    throw std::runtime_error(
        std::format("failed to load texture image {}: {}", image_path.string(), stbi_failure_reason()));
  }
  image.width = static_cast<std::uint32_t>(width);
  image.height = static_cast<std::uint32_t>(height);
  return image;
}

// Throws unless result is KTX_SUCCESS, naming the step and the image.
void ThrowIfKtxFailed(KTX_error_code result, std::string_view step, const std::filesystem::path& image_path) {
  if (result != KTX_SUCCESS) {
    throw std::runtime_error(
        std::format("failed to {} texture image {}: {}", step, image_path.string(), ktxErrorString(result)));
  }
}

struct Ktx2Deleter {
  void operator()(ktxTexture2* texture) const { ktxTexture2_Destroy(texture); }
};
using Ktx2Texture = std::unique_ptr<ktxTexture2, Ktx2Deleter>;

// A single-level 2D KTX2 texture of image, in vk_format.
Ktx2Texture CreateTexture(const Image& image, ktx_uint32_t vk_format, const std::filesystem::path& image_path) {
  ktxTextureCreateInfo create_info{};
  create_info.vkFormat = vk_format;
  create_info.baseWidth = image.width;
  create_info.baseHeight = image.height;
  create_info.baseDepth = 1;
  create_info.numDimensions = 2;
  create_info.numLevels = 1;
  create_info.numLayers = 1;
  create_info.numFaces = 1;
  create_info.isArray = KTX_FALSE;
  create_info.generateMipmaps = KTX_FALSE;

  ktxTexture2* created = nullptr;
  ThrowIfKtxFailed(ktxTexture2_Create(&create_info, KTX_TEXTURE_CREATE_ALLOC_STORAGE, &created), "create a KTX2 for",
                   image_path);
  Ktx2Texture texture(created);

  // Names the cooker as the file's writer, NUL included, as KTX2 requires.
  constexpr std::string_view kWriter{"augusta-pack", sizeof("augusta-pack")};
  ThrowIfKtxFailed(ktxHashList_AddKVPair(&texture->kvDataHead, KTX_WRITER_KEY,
                                         static_cast<unsigned int>(kWriter.size()), kWriter.data()),
                   "name the writer of", image_path);

  const std::size_t size = std::size_t{image.width} * image.height * kRgbaChannels;
  ThrowIfKtxFailed(ktxTexture_SetImageFromMemory(ktxTexture(texture.get()), 0, 0, 0, image.pixels.get(), size),
                   "set the pixels of", image_path);
  return texture;
}

// Encodes texture to UASTC at quality level 2 with no RDO (ADR-0017), swizzled
// by input_swizzle unless it is empty.
void EncodeUastc(ktxTexture2* texture, std::string_view input_swizzle, const std::filesystem::path& image_path) {
  ktxBasisParams params{};
  params.structSize = sizeof(params);
  params.uastc = KTX_TRUE;
  params.uastcFlags = KTX_PACK_UASTC_LEVEL_DEFAULT;
  params.uastcRDO = KTX_FALSE;
  params.threadCount = std::max(1U, std::thread::hardware_concurrency());
  std::ranges::copy(input_swizzle, std::begin(params.inputSwizzle));
  ThrowIfKtxFailed(ktxTexture2_CompressBasisEx(texture, &params), "encode to UASTC", image_path);
}

}  // namespace

std::vector<std::uint8_t> CompressTexture(const std::filesystem::path& image_path, const std::string& format) {
  const Target target = ToTarget(format);
  const Image image = LoadImageRgba8(image_path);

  const Ktx2Texture texture = CreateTexture(image, target.source_format, image_path);
  EncodeUastc(texture.get(), target.input_swizzle, image_path);
  ThrowIfKtxFailed(ktxTexture2_TranscodeBasis(texture.get(), target.transcode_format, 0), "transcode", image_path);
  ThrowIfKtxFailed(ktxTexture2_DeflateZstd(texture.get(), kZstdLevel), "supercompress", image_path);

  ktx_uint8_t* written = nullptr;
  ktx_size_t written_size = 0;
  ThrowIfKtxFailed(ktxTexture_WriteToMemory(ktxTexture(texture.get()), &written, &written_size), "write the KTX2 of",
                   image_path);
  const std::unique_ptr<ktx_uint8_t, decltype(&std::free)> owned(written, &std::free);
  const std::span<const std::uint8_t> ktx2(owned.get(), written_size);
  return {ktx2.begin(), ktx2.end()};
}

}  // namespace augusta::pack
