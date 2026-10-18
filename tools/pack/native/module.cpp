// augusta_pack._native: the pybind11 bindings of the cooker's native steps,
// called from augusta_pack.cook: the mesh pass (mesh.h), the texture
// compression (texture.h) and augusta_assets' pack encoder
// (encoder_binding.h); from augusta-keygen: augusta_assets' key generation
// (keys_binding.h); and from augusta-inspect, augusta-verify and
// augusta-publish: augusta_assets' pack reading (reader_binding.h).

#include <cstddef>
#include <cstdint>
#include <filesystem>
#include <format>
#include <span>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

#include <pybind11/pybind11.h>
#include <pybind11/stl.h>
#include <pybind11/stl/filesystem.h>

#include "bytes.h"
#include "encoder_binding.h"
#include "keys_binding.h"
#include "mesh.h"
#include "reader_binding.h"
#include "texture.h"

namespace py = pybind11;

PYBIND11_MODULE(_native, module) {
  module.doc() =
      "The cooker's native steps (ADR-0030): meshoptimizer's weld/simplify/optimize-cache/optimize-fetch/quantize "
      "(ADR-0016), and stb_image + stb_image_resize2 + libktx's PNG/JPEG load, mip chain, BC7/BC5/BC4 encode and KTX2 "
      "write (ADR-0017), augusta_assets' pack encoder: the blobs and the signed pack the client and server load "
      "(ADR-0031) and the Ed25519 keys it signs them with (ADR-0018), and its pack reader: a pack's layout, and the "
      "pack the client and server would load.";
  module.def(
      "optimize_mesh",
      [](std::vector<float> points, std::vector<std::uint32_t> indices) {
        augusta::pack::Mesh mesh =
            augusta::pack::OptimizeMesh({.points = std::move(points), .indices = std::move(indices)});
        return py::make_tuple(std::move(mesh.points), std::move(mesh.indices));
      },
      py::arg("points"), py::arg("indices"),
      "points: flat [x0,y0,z0,x1,y1,z1,...] float list. indices: flat triangle-list uint32 list. Returns "
      "(points, indices) after welding/simplifying/reordering/quantizing.");
  module.def(
      "compress_texture",
      [](const std::filesystem::path& image_path, const std::string& format) {
        const std::vector<std::uint8_t> ktx2 = augusta::pack::CompressTexture(image_path, format);
        return augusta::pack::ToBytes(std::as_bytes(std::span(ktx2)));
      },
      py::arg("image_path"), py::arg("format") = "bc7",
      "Loads image_path (PNG/JPEG), encodes it to a KTX2 in `format` (bc7/bc5/bc4) with its full mip chain, returns "
      "the KTX2's bytes.");
  module.def(
      "generate_mip_chain",
      [](const py::bytes& rgba, std::uint32_t width, std::uint32_t height, const std::string& format) {
        const std::string_view pixels(rgba);
        // A buffer shorter than its dimensions would be read past its end.
        if (width == 0 || height == 0 || pixels.size() != std::size_t{width} * height * 4) {
          throw py::value_error(std::format("rgba holds {} bytes, not the {}x{}x4 a {}x{} RGBA image needs",
                                            pixels.size(), width, height, width, height));
        }
        const std::vector<augusta::pack::Image> levels =
            augusta::pack::GenerateMipChain({.width = width, .height = height, .rgba = {pixels.begin(), pixels.end()}},
                                            augusta::pack::ContentOf(format));
        py::list chain;
        for (const augusta::pack::Image& level : levels) {
          chain.append(
              py::make_tuple(level.width, level.height, augusta::pack::ToBytes(std::as_bytes(std::span(level.rgba)))));
        }
        return chain;
      },
      py::arg("rgba"), py::arg("width"), py::arg("height"), py::arg("format") = "bc7",
      "rgba: a width x height 8-bit RGBA image, row by row from the top. Returns the mip chain compress_texture "
      "encodes for `format`, as [(width, height, rgba)], level 0 (the image itself) first. Exposed so the filtering "
      "can be checked before BC encoding.");
  augusta::pack::BindPackEncoder(module);
  augusta::pack::BindPackReader(module);
  augusta::pack::BindKeys(module);
}
