#include <cstddef>
#include <vector>

#include "augusta/debug_kept_shapes.h"
#include "augusta/debug_shapes.h"

namespace augusta::debug {

namespace {

template <typename Shape>
void Append(std::vector<Shape>& to, const std::vector<Shape>& shapes) {
  to.insert(to.end(), shapes.begin(), shapes.end());
}

}  // namespace

KeptShapes::KeptShapes(std::size_t capacity) : capacity_(capacity) {}

void KeptShapes::Keep(const Shapes& shapes, Clock::time_point until) {
  if (capacity_ == 0) {
    return;
  }
  if (kept_.size() == capacity_) {
    kept_.erase(kept_.begin());
  }
  kept_.push_back({.shapes = shapes, .until = until});
}

Shapes KeptShapes::Alive(Clock::time_point now) {
  std::erase_if(kept_, [now](const Kept& kept) { return kept.until <= now; });
  Shapes alive;
  for (const Kept& kept : kept_) {
    Append(alive.segments, kept.shapes.segments);
    Append(alive.triangles, kept.shapes.triangles);
    Append(alive.boxes, kept.shapes.boxes);
    Append(alive.spheres, kept.shapes.spheres);
    Append(alive.capsules, kept.shapes.capsules);
    Append(alive.voxels, kept.shapes.voxels);
  }
  return alive;
}

std::size_t KeptShapes::Count() const { return kept_.size(); }

}  // namespace augusta::debug
