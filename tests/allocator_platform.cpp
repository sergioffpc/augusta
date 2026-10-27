#include <string_view>

#include "allocator_candidate.h"

// The baseline: the platform's own allocator (the UCRT's, glibc's, libmalloc),
// which serves every allocation of an executable that links no other.
namespace augusta::benchmarks {

extern const std::string_view kCandidate = "platform";

bool CandidateServed(void* /*block*/) { return true; }

}  // namespace augusta::benchmarks
