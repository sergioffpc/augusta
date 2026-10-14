// augusta_pack._native: the pybind11 bindings of the two cooking steps with no
// Python equivalent, the mesh pass (mesh.h) and the texture compression
// (texture.h), called from augusta_pack.cook.

#include <cstdint>
#include <filesystem>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

#include <pybind11/pybind11.h>
#include <pybind11/stl.h>
#include <pybind11/stl/filesystem.h>

#include "mesh.h"
#include "texture.h"

namespace py = pybind11;

PYBIND11_MODULE(_native, module) {
  module.doc() =
      "The cooker's native steps (ADR-0030): meshoptimizer's weld/simplify/optimize-cache/optimize-fetch/quantize "
      "(ADR-0016), and stb_image + stb_image_resize2 + libktx's PNG/JPEG load, mip chain, BC7/BC5/BC4 encode and KTX2 "
      "write (ADR-0017).";
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
        return py::bytes(reinterpret_cast<const char*>(ktx2.data()), ktx2.size());
      },
      py::arg("image_path"), py::arg("format") = "bc7",
      "Loads image_path (PNG/JPEG), encodes it to a KTX2 in `format` (bc7/bc5/bc4) with its full mip chain, returns "
      "the KTX2's bytes.");
  module.def(
      "generate_mip_chain",
      [](const py::bytes& rgba, std::uint32_t width, std::uint32_t height, const std::string& format) {
        const std::string_view pixels(rgba);
        const std::vector<augusta::pack::Image> levels = augusta::pack::GenerateMipChain(
            {.width = width, .height = height, .rgba = {pixels.begin(), pixels.end()}}, format);
        py::list chain;
        for (const augusta::pack::Image& level : levels) {
          chain.append(py::make_tuple(level.width, level.height,
                                      py::bytes(reinterpret_cast<const char*>(level.rgba.data()), level.rgba.size())));
        }
        return chain;
      },
      py::arg("rgba"), py::arg("width"), py::arg("height"), py::arg("format") = "bc7",
      "rgba: a width x height 8-bit RGBA image, row by row from the top. Returns the mip chain compress_texture "
      "encodes for `format`, as [(width, height, rgba)], level 0 (the image itself) first. Exposed so the filtering "
      "can be checked before BC encoding.");
}
