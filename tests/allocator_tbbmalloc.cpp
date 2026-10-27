#include <cstddef>
#include <string_view>

#include "allocator_candidate.h"

// tbbmalloc's own answer to whether it allocated a block: its size when it
// did, otherwise 0, given no other msize to fall back on. Exported, but in no
// header.
// NOLINTNEXTLINE(bugprone-reserved-identifier,readability-identifier-naming)
extern "C" std::size_t __TBB_malloc_safer_msize(void* object, std::size_t (*original_msize)(void*));

// oneTBB's scalable allocator, which its proxy (tbbmalloc_proxy) puts in place
// of malloc; augusta_allocator_benchmarks_tbbmalloc_unproxied links this
// without the proxy, as the control the check must fail.
namespace augusta::benchmarks {

extern const std::string_view kCandidate = "tbbmalloc";

bool CandidateServed(void* block) { return __TBB_malloc_safer_msize(block, nullptr) != 0; }

}  // namespace augusta::benchmarks
