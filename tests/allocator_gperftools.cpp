#include <string_view>

#include <gperftools/malloc_extension.h>

#include "allocator_candidate.h"

// gperftools' TCMalloc, the older one, whose malloc replaces the C library's
// at link time on Linux, and which registers itself as the default malloc zone
// on macOS.
namespace augusta::benchmarks {

extern const std::string_view kCandidate = "gperftools";

bool CandidateServed(void* block) {
  return MallocExtension::instance()->GetOwnership(block) == MallocExtension::kOwned;
}

}  // namespace augusta::benchmarks
