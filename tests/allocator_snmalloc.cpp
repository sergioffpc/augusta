#include <string_view>

// snmalloc's malloc and operator new, compiled into the executable as its own
// shim library compiles them, without a prefix, so they replace the C
// library's and the C++ runtime's at link time. The port is header-only: it
// installs these sources beside the headers instead of building the shim.
#include <snmalloc/override/malloc.cc>  // IWYU pragma: keep
#include <snmalloc/override/new.cc>     // IWYU pragma: keep
#include <snmalloc/snmalloc.h>

#include "allocator_candidate.h"

// snmalloc, on Linux and macOS: its pagemap knows every block it owns.
namespace augusta::benchmarks {

extern const std::string_view kCandidate = "snmalloc";

bool CandidateServed(void* block) { return snmalloc::is_owned(block); }

}  // namespace augusta::benchmarks
