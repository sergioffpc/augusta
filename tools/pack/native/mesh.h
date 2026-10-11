#ifndef AUGUSTA_PACK_MESH_H_
#define AUGUSTA_PACK_MESH_H_

#include <cstdint>
#include <vector>

/// \file
/// The meshoptimizer pass the cooker runs on every mesh (ADR-0016). Nothing
/// here knows of Python.
namespace augusta::pack {

/// A mesh as the pack stores it: points is a flat x0,y0,z0,x1,y1,z1,...
/// buffer (3 floats per vertex); indices is a flat triangle-list index buffer.
struct Mesh {
  std::vector<float> points;
  std::vector<std::uint32_t> indices;
};

/// Welds coincident vertices, simplifies within a tight error budget,
/// optimizes vertex cache and fetch order, then quantizes positions. Either
/// buffer may come back shorter than it went in.
Mesh OptimizeMesh(Mesh mesh);

}  // namespace augusta::pack

#endif  // AUGUSTA_PACK_MESH_H_
