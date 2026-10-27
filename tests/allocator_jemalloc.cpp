#include <cstddef>
#include <string_view>

#include <jemalloc/jemalloc.h>

#include "allocator_candidate.h"

// jemalloc, whose malloc replaces the C library's at link time on Linux, and
// which registers itself as the default malloc zone on macOS. Its arenas.lookup
// finds the arena that allocated a block, and fails for a block it did not.
namespace augusta::benchmarks {

extern const std::string_view kCandidate = "jemalloc";

bool CandidateServed(void* block) {
  unsigned arena = 0;
  std::size_t arena_size = sizeof(arena);
  return mallctl("arenas.lookup", &arena, &arena_size, static_cast<void*>(&block), sizeof(block)) == 0;
}

}  // namespace augusta::benchmarks
