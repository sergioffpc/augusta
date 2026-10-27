#include <cstddef>
#include <cstdint>
#include <cstring>
#include <string_view>

#include <malloc.h>
#include <windows.h>

#include "allocator_candidate.h"

// Windows' Segment Heap as the process heap, which segment_heap.manifest asks
// for: the UCRT's malloc, and the operator new that calls it, allocate from
// the process heap, so the block is the Segment Heap's when that heap is one.
// No API tells the two heaps apart: each keeps a signature 16 bytes into its
// header, the NT heap 0xFFEEFFEE and the Segment Heap 0xDDEEDDEE.
namespace augusta::benchmarks {

extern const std::string_view kCandidate = "segment_heap";

bool CandidateServed(void* /*block*/) {
  constexpr std::size_t kSignatureOffset = 16;
  constexpr std::uint32_t kSegmentHeapSignature = 0xDDEEDDEE;
  HANDLE heap = GetProcessHeap();
  if (reinterpret_cast<HANDLE>(_get_heap_handle()) != heap) {
    return false;
  }
  std::uint32_t signature = 0;
  std::memcpy(&signature, static_cast<const std::byte*>(heap) + kSignatureOffset, sizeof(signature));
  return signature == kSegmentHeapSignature;
}

}  // namespace augusta::benchmarks
