#include <pybind11/pybind11.h>
#include <pybind11/stl/filesystem.h>

// DirectXTex.h pulls in <windows.h> (via d3d11.h) - CoInitializeEx below
// comes from that same transitive include.
#include <DirectXTex.h>

#include <filesystem>
#include <format>
#include <stdexcept>
#include <string>

namespace py = pybind11;

namespace {

// DirectXTex's WIC-backed loaders (LoadFromWICFile) need COM initialized
// on the calling thread - they don't do this themselves. thread_local
// since COM apartment state is per-thread. Returns false if COM is
// unusable on this thread (e.g. RPC_E_CHANGED_MODE, because something
// else already initialized it with an incompatible apartment model).
bool EnsureComInitialized() {
  thread_local const bool kInitialized = [] {
    // S_FALSE (already initialized on this thread) is success too - only
    // a hard FAILED() return means WIC calls on this thread won't work.
    const HRESULT init_result = CoInitializeEx(nullptr, COINIT_MULTITHREADED);
    return SUCCEEDED(init_result) || init_result == S_FALSE;
  }();
  return kInitialized;
}

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

}  // namespace

// Loads image_path via WIC and block-compresses it (ADR-0017) to `format`
// ("bc7"/"bc5"/"bc4"), returning DirectXTex's own SaveToDDSMemory output
// (DDS header included) as bytes. Raises RuntimeError on any failure.
py::bytes CompressTexture(const std::filesystem::path& image_path, const std::string& format) {
  if (!EnsureComInitialized()) {
    throw std::runtime_error(
        "COM could not be initialized on this thread (CoInitializeEx failed) - WIC texture "
        "loading is unavailable");
  }

  DirectX::TexMetadata metadata;
  DirectX::ScratchImage image;
  if (FAILED(DirectX::LoadFromWICFile(image_path.wstring().c_str(), DirectX::WIC_FLAGS_NONE, &metadata, image))) {
    throw std::runtime_error(std::format("failed to load texture image {}", image_path.string()));
  }

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
      "Thin bindings over DirectXTex (ADR-0017), called from pack.cook - WIC image load + "
      "BC7/BC5/BC4 block compression + DDS encode.";
  m.def("compress_texture", &CompressTexture, py::arg("image_path"), py::arg("format") = "bc7",
        "Loads image_path via WIC, BC-compresses to `format` (bc7/bc5/bc4), returns SaveToDDSMemory bytes.");
}
