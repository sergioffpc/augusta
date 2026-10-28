#include <array>
#include <cstddef>
#include <cstdint>
#include <filesystem>
#include <fstream>
#include <ios>
#include <iterator>
#include <random>
#include <span>
#include <string>
#include <system_error>
#include <vector>

#include <gtest/gtest.h>

#include "augusta/assets.h"
#include "augusta/pack_encoder.h"
#include "pack_load_target.h"

// The pack_load fuzz target's seeds (tests/fuzz/README.md): the golden packs
// (tests/fixtures/example-packs) and a pack of one texture, each without its
// trailer, which the target signs itself. The golden seeds are copies, so
// regenerating the golden packs fails here until the seeds are regenerated
// from them too; the texture seed is what WritePack writes.
namespace {

using augusta::pack_load_fuzz::GlobalTestKey;
using augusta::pack_load_fuzz::kTexturePath;
using augusta::pack_load_fuzz::LoadSigned;
using augusta::pack_load_fuzz::Reached;
using augusta::pack_load_fuzz::ResolveEverything;

// A trailer is the pack's BLAKE3 hash and the Ed25519 signature of it (ADR-0031).
constexpr std::size_t kTrailerSize = augusta::assets::kPackHashSize + augusta::assets::kEd25519SignatureSize;

constexpr std::array<const char*, 2> kGoldenPacks = {"client", "server"};
constexpr const char* kTextureSeed = "texture";

const std::filesystem::path kExamplePacks{AUGUSTA_EXAMPLE_PACKS};
const std::filesystem::path kCorpus{AUGUSTA_PACK_LOAD_CORPUS};

// A KTX2 file's identifier, then a VkFormat of VK_FORMAT_BC7_SRGB_BLOCK: as
// much of one as the texture decoder reads (ADR-0017), then a few bytes of
// payload.
constexpr std::array<std::uint8_t, 19> kKtx2 = {0xAB, 0x4B, 0x54, 0x58, 0x20, 0x32, 0x30, 0xBB, 0x0D, 0x0A,
                                                0x1A, 0x0A, 0x92, 0x00, 0x00, 0x00, 0x01, 0xAB, 0xCD};

std::vector<char> ReadFile(const std::filesystem::path& path) {
  std::ifstream in(path, std::ios::binary);
  return {std::istreambuf_iterator<char>(in), std::istreambuf_iterator<char>()};
}

std::vector<char> WithoutTrailer(std::vector<char> pack) {
  pack.resize(pack.size() > kTrailerSize ? pack.size() - kTrailerSize : 0);
  return pack;
}

std::vector<char> GoldenSeed(const std::string& pack) {
  return WithoutTrailer(ReadFile(kExamplePacks / (pack + std::string(augusta::assets::kPackExtension))));
}

// A pack of one texture at kTexturePath, signed with the test key, without
// its trailer.
std::vector<char> TextureSeed() {
  std::vector<std::byte> ktx2;
  for (const std::uint8_t byte : kKtx2) {
    ktx2.push_back(std::byte{byte});
  }
  const auto blob = augusta::assets::EncodeTextureBlob(ktx2);
  EXPECT_TRUE(blob.has_value());
  const std::filesystem::path path = std::filesystem::temp_directory_path() /
                                     ("augusta_pack_load_texture_seed_" + std::to_string(std::random_device{}()) +
                                      std::string(augusta::assets::kPackExtension));
  const std::vector<augusta::assets::AssetEntry> entries = {
      {.type = augusta::assets::AssetType::kTexture,
       .path = std::string(kTexturePath),
       .data = blob.value_or(std::vector<std::byte>{})},
  };
  EXPECT_TRUE(augusta::assets::WritePack(path, entries, GlobalTestKey().private_key).has_value());
  std::vector<char> seed = WithoutTrailer(ReadFile(path));
  std::error_code ignored;
  std::filesystem::remove(path, ignored);
  return seed;
}

// What the target reaches from the seed of that name.
Reached Reach(const std::string& seed) {
  const std::vector<char> bytes = ReadFile(kCorpus / seed);
  const auto pack = LoadSigned(std::span(reinterpret_cast<const std::uint8_t*>(bytes.data()), bytes.size()));
  EXPECT_TRUE(pack.has_value()) << "the seed does not load";
  return pack ? ResolveEverything(*pack) : Reached{};
}

// Requirements: NFR-12
TEST(PackLoadSeedsTest, EveryGoldenSeedIsAGoldenPackWithoutItsTrailer) {
  for (const std::string pack : kGoldenPacks) {
    SCOPED_TRACE(pack);
    const std::vector<char> seed = ReadFile(kCorpus / pack);
    ASSERT_FALSE(seed.empty()) << "missing seed";
    EXPECT_EQ(seed, GoldenSeed(pack)) << "regenerate the seeds (tests/fuzz/README.md)";
  }
}

// Requirements: NFR-12
TEST(PackLoadSeedsTest, TheTextureSeedIsAPackOfOneTextureWithoutItsTrailer) {
  const std::vector<char> seed = ReadFile(kCorpus / kTextureSeed);
  ASSERT_FALSE(seed.empty()) << "missing seed";
  EXPECT_EQ(seed, TextureSeed()) << "regenerate the seeds (tests/fuzz/README.md)";
}

// Requirements: NFR-12
TEST(PackLoadSeedsTest, EveryGoldenSeedReachesItsCharactersClips) {
  for (const std::string pack : kGoldenPacks) {
    SCOPED_TRACE(pack);
    EXPECT_TRUE(Reach(pack).clips_loaded);
  }
}

// Requirements: NFR-12
TEST(PackLoadSeedsTest, TheTextureSeedReachesItsTexture) { EXPECT_TRUE(Reach(kTextureSeed).texture_resolved); }

// Not a check: rewrites the golden seeds from the golden packs after those
// change on purpose, and the texture seed from WritePack. Disabled, so ctest
// never runs it; the augusta_pack_load_seeds build target does
// (tests/fuzz/CMakeLists.txt).
TEST(PackLoadSeedsTest, DISABLED_RegenerateSeeds) {
  const auto write = [](const std::string& name, const std::vector<char>& seed) {
    std::ofstream out(kCorpus / name, std::ios::binary | std::ios::trunc);
    out.write(seed.data(), static_cast<std::streamsize>(seed.size()));
    ASSERT_TRUE(out.good()) << name;
  };
  for (const std::string pack : kGoldenPacks) {
    write(pack, GoldenSeed(pack));
  }
  write(kTextureSeed, TextureSeed());
}

}  // namespace
