#include "augusta/cues.h"

#include <algorithm>
#include <cstddef>
#include <filesystem>
#include <string>
#include <string_view>
#include <vector>

#include <gtest/gtest.h>

#include "augusta/assets.h"
#include "encoder.h"

// The client's cue sounds (ADR-0020): a sound for every cue in the catalogue,
// found in the client pack under the sounds prefix it names.
namespace {

using augusta::assets::AssetEntry;
using augusta::assets::AssetType;
using augusta::assets::Pack;
using augusta::audio::Cue;
using augusta::audio::CueName;

class CueSoundsTest : public ::testing::Test {
 protected:
  void TearDown() override { std::filesystem::remove(path_); }

  // A pack naming sounds/test as its sounds prefix, with a sound for every cue
  // but missing.
  Pack PackWithoutCue(std::string_view missing) {
    std::vector<AssetEntry> entries = {
        AssetEntry{.type = AssetType::kSounds,
                   .path = std::string(augusta::assets::kSoundsPath),
                   .data = *augusta::assets::EncodeSoundsBlob("sounds/test")},
    };
    const auto sound = augusta::assets::EncodeAudioBlob(
        {.sample_rate = 22050, .bits_per_sample = 16, .samples = {std::byte{0x01}, std::byte{0x00}}});
    for (const Cue cue : augusta::audio::kCues) {
      if (CueName(cue) != missing) {
        entries.push_back(
            AssetEntry{.type = AssetType::kAudio, .path = "sounds/test/" + std::string(CueName(cue)), .data = *sound});
      }
    }
    const auto keys = augusta::assets::GenerateEd25519KeyPair();
    EXPECT_TRUE(augusta::assets::WritePack(path_, entries, keys.private_key).has_value());
    return *Pack::Load(path_, keys.public_key);
  }

 private:
  // Named after the running test: ctest runs every test in a process of its
  // own, possibly in parallel, so no other test removes it while in use.
  static std::filesystem::path TestPackPath() {
    const ::testing::TestInfo& test = *::testing::UnitTest::GetInstance()->current_test_info();
    std::string name = std::string("augusta_cues_test_") + test.test_suite_name() + "_" + test.name() + ".pack";
    std::ranges::replace(name, '/', '_');
    return std::filesystem::temp_directory_path() / name;
  }

  std::filesystem::path path_ = TestPackPath();
};

TEST(CueNameTest, EachCueIsNamedAsItsSoundFileIs) {
  const std::vector<std::string_view> names = {"gunshot", "hit_marker", "hit_taken",
                                               "death",   "match_won",  "match_lost"};
  std::vector<std::string_view> catalogue;
  for (const Cue cue : augusta::audio::kCues) {
    catalogue.push_back(CueName(cue));
  }
  EXPECT_EQ(catalogue, names);
}

TEST_F(CueSoundsTest, EveryCueLoadsInCatalogueOrder) {
  const Pack pack = PackWithoutCue("");

  const auto sounds = augusta::audio::LoadCueSounds(pack);

  ASSERT_TRUE(sounds.has_value()) << augusta::audio::DescribeCueSoundError(sounds.error());
  EXPECT_EQ(sounds->size(), augusta::audio::kCues.size());
  EXPECT_EQ((*sounds)[0].sample_rate, 22050U);
}

TEST_F(CueSoundsTest, AMissingCueIsNamed) {
  const Pack pack = PackWithoutCue("hit_taken");

  const auto sounds = augusta::audio::LoadCueSounds(pack);

  ASSERT_FALSE(sounds.has_value());
  EXPECT_EQ(sounds.error().path, "sounds/test/hit_taken");
  EXPECT_EQ(sounds.error().resolve_error, augusta::assets::ResolveError::kNotFound);
  EXPECT_NE(augusta::audio::DescribeCueSoundError(sounds.error()).find("hit_taken"), std::string::npos);
}

}  // namespace
