#include <cstddef>
#include <cstdint>
#include <span>

#include "pack_load_target.h"

// The fuzz target for pack loading (ADR-0013): a pack file is the other input
// that arrives from outside. The fuzzer's bytes are a pack without its trailer,
// which the target signs before loading (pack_load_target.h); a pack that loads
// then has every asset the engine reads from one resolved, so the blob decoders
// and ozz-animation's archive parsing are fuzzed too.
extern "C" int LLVMFuzzerTestOneInput(const std::uint8_t* data, std::size_t size) {
  const auto pack = augusta::pack_load_fuzz::LoadSigned(std::span(data, size));
  if (pack.has_value()) {
    (void)augusta::pack_load_fuzz::ResolveEverything(*pack);
  }
  return 0;
}
