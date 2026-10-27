#include <string_view>

#include <mimalloc.h>
#ifdef _WIN32
// operator new and delete as mimalloc's own, in this one file of the
// executable, as mimalloc asks of a program its DLL overrides malloc for. Its
// static library, on Linux and macOS, defines them itself.
#include <mimalloc-new-delete.h>  // IWYU pragma: keep
#endif

#include "allocator_candidate.h"

// mimalloc: its malloc replaces the C library's at link time on Linux and
// macOS; on Windows, its DLL's mimalloc-redirect.dll redirects the CRT's to it
// as the process starts, which only happens when the executable loads
// mimalloc.dll first of its DLLs, and mi_is_redirected() tells.
namespace augusta::benchmarks {

extern const std::string_view kCandidate = "mimalloc";

bool CandidateServed(void* block) {
#ifdef _WIN32
  if (!mi_is_redirected()) {
    return false;
  }
#endif
  return mi_is_in_heap_region(block);
}

}  // namespace augusta::benchmarks
