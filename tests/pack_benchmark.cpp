#include <filesystem>
#include <string>

#include <benchmark/benchmark.h>

#include "augusta/assets.h"

// Pack::Load of the golden packs (ADR-0013): mapping the file and verifying its
// hash and signature, which the client and the server do with every pack they
// start on.
namespace {

using augusta::assets::Pack;

const std::filesystem::path kExamplePacks{AUGUSTA_EXAMPLE_PACKS};

void BM_PackLoad(benchmark::State& state, const std::string& pack) {
  const auto public_key = augusta::assets::ReadEd25519PublicKeyFile(kExamplePacks / "test.pub");
  if (!public_key.has_value()) {
    state.SkipWithError("the test public key does not read");
    return;
  }
  const std::filesystem::path path = kExamplePacks / pack;
  for (auto _ : state) {
    auto loaded = Pack::Load(path, *public_key);
    if (!loaded.has_value()) {
      state.SkipWithError(std::string(augusta::assets::DescribeLoadError(loaded.error())));
      return;
    }
    benchmark::DoNotOptimize(loaded);
  }
}
BENCHMARK_CAPTURE(BM_PackLoad, client, std::string("client.pack"));
BENCHMARK_CAPTURE(BM_PackLoad, server, std::string("server.pack"));

}  // namespace
