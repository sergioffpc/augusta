#include <array>
#include <cstddef>
#include <filesystem>
#include <fstream>
#include <iterator>
#include <string>
#include <vector>

#include <gtest/gtest.h>

#include "augusta/assets.h"

// The pack_load fuzz target's seeds (tests/fuzz/README.md): the golden packs
// (tests/fixtures/example-packs) without their trailer, which the target signs
// itself. They are copies, so regenerating the golden packs fails here until
// the seeds are regenerated from them too.
namespace {

// A trailer is the pack's BLAKE3 hash and the Ed25519 signature of it (ADR-0031).
constexpr std::size_t kTrailerSize = augusta::assets::kPackHashSize + 64;

constexpr std::array<const char*, 2> kGoldenPacks = {"client", "server"};

const std::filesystem::path kExamplePacks{AUGUSTA_EXAMPLE_PACKS};
const std::filesystem::path kCorpus{AUGUSTA_PACK_LOAD_CORPUS};

std::vector<char> ReadFile(const std::filesystem::path& path) {
  std::ifstream in(path, std::ios::binary);
  return {std::istreambuf_iterator<char>(in), std::istreambuf_iterator<char>()};
}

std::vector<char> WithoutTrailer(const std::string& pack) {
  std::vector<char> bytes = ReadFile(kExamplePacks / (pack + ".pack"));
  bytes.resize(bytes.size() > kTrailerSize ? bytes.size() - kTrailerSize : 0);
  return bytes;
}

TEST(PackLoadSeedsTest, EverySeedIsAGoldenPackWithoutItsTrailer) {
  for (const std::string pack : kGoldenPacks) {
    SCOPED_TRACE(pack);
    const std::vector<char> seed = ReadFile(kCorpus / pack);
    ASSERT_FALSE(seed.empty()) << "missing seed";
    EXPECT_EQ(seed, WithoutTrailer(pack)) << "regenerate the seeds (tests/fuzz/README.md)";
  }
}

// Not a check: rewrites the seeds from the golden packs after those change on
// purpose. Disabled, so ctest never runs it; the augusta_pack_load_seeds build
// target does (tests/fuzz/CMakeLists.txt).
TEST(PackLoadSeedsTest, DISABLED_RegenerateSeeds) {
  for (const std::string pack : kGoldenPacks) {
    const std::vector<char> seed = WithoutTrailer(pack);
    std::ofstream out(kCorpus / pack, std::ios::binary | std::ios::trunc);
    out.write(seed.data(), static_cast<std::streamsize>(seed.size()));
    ASSERT_TRUE(out.good()) << pack;
  }
}

}  // namespace
