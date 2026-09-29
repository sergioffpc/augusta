#include <cstddef>
#include <cstdint>
#include <utility>
#include <vector>

#include <meshoptimizer.h>
#include <pybind11/pybind11.h>
#include <pybind11/stl.h>

namespace py = pybind11;

namespace {

constexpr std::size_t kVertexStride = 3 * sizeof(float);
// Reduces stored-value entropy for later compression without changing the
// pack's on-disk vertex format, which stays plain float32.
constexpr int kQuantizationMantissaBits = 12;

}  // namespace

// Runs the meshoptimizer pass ADR-0016 calls for: weld coincident
// vertices, simplify within a tight error budget, optimize vertex cache/
// fetch order, then quantize positions. points is a flat
// x0,y0,z0,x1,y1,z1,... buffer (3 floats per vertex); indices is a flat
// triangle-list index buffer. Returns the optimized (points, indices),
// each possibly shorter than the input.
py::tuple OptimizeMesh(std::vector<float> points, std::vector<std::uint32_t> indices) {
  const std::size_t vertex_count = points.size() / 3;
  const std::size_t index_count = indices.size();
  if (vertex_count == 0 || index_count == 0) {
    return py::make_tuple(std::move(points), std::move(indices));
  }

  // Weld vertices that share the exact same position first - every later
  // pass assumes the vertex buffer has no redundant entries.
  std::vector<unsigned int> remap(vertex_count);
  const std::size_t unique_vertex_count = meshopt_generateVertexRemap(remap.data(), indices.data(), index_count,
                                                                      points.data(), vertex_count, kVertexStride);

  std::vector<std::uint32_t> welded_indices(index_count);
  meshopt_remapIndexBuffer(welded_indices.data(), indices.data(), index_count, remap.data());

  std::vector<float> welded_points(unique_vertex_count * 3);
  meshopt_remapVertexBuffer(welded_points.data(), points.data(), vertex_count, kVertexStride, remap.data());

  indices = std::move(welded_indices);
  points = std::move(welded_points);

  // Collapse degenerate/redundant triangles within a tight 1%-of-extents
  // error budget. target_index_count 0 means "simplify as far as
  // target_error allows".
  std::vector<std::uint32_t> simplified_indices(indices.size());
  float simplify_error = 0.0F;
  const std::size_t simplified_index_count =
      meshopt_simplify(simplified_indices.data(), indices.data(), indices.size(), points.data(), points.size() / 3,
                       kVertexStride, /*target_index_count=*/0, /*target_error=*/0.01F, /*options=*/0, &simplify_error);
  simplified_indices.resize(simplified_index_count);
  indices = std::move(simplified_indices);

  // Vertex cache optimization: reorder indices for GPU post-transform
  // cache locality, without touching the vertex buffer.
  meshopt_optimizeVertexCache(indices.data(), indices.data(), indices.size(), points.size() / 3);

  // Vertex fetch optimization: reorder the vertex buffer itself (indices
  // are remapped in place to match) for cache-friendly fetch order.
  std::vector<float> fetch_optimized_points(points.size());
  const std::size_t fetch_vertex_count = meshopt_optimizeVertexFetch(
      fetch_optimized_points.data(), indices.data(), indices.size(), points.data(), points.size() / 3, kVertexStride);
  fetch_optimized_points.resize(fetch_vertex_count * 3);
  points = std::move(fetch_optimized_points);

  for (float& component : points) {
    component = meshopt_quantizeFloat(component, kQuantizationMantissaBits);
  }

  return py::make_tuple(std::move(points), std::move(indices));
}

PYBIND11_MODULE(_meshoptimizer, m) {
  m.doc() =
      "Thin bindings over meshoptimizer (ADR-0016), called from pack.cook - weld/simplify/"
      "optimize-cache/optimize-fetch/quantize.";
  m.def("optimize_mesh", &OptimizeMesh, py::arg("points"), py::arg("indices"),
        "points: flat [x0,y0,z0,x1,y1,z1,...] float list. indices: flat triangle-list uint32 list. Returns "
        "(points, indices) after welding/simplifying/reordering/quantizing.");
}
