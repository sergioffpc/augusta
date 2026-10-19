#include "augusta/cues.h"

#include <algorithm>
#include <cstddef>
#include <cstdint>
#include <filesystem>
#include <optional>
#include <random>
#include <set>
#include <string>
#include <string_view>
#include <vector>

#include <gtest/gtest.h>

#include "augusta/assets.h"
#include "augusta/pack_encoder.h"

// The client's cue sounds (ADR-0020, ADR-0041): a sound for every cue in the
// catalogue, found in the client pack under the shared sounds prefix, each with
// its variants and its gain.
namespace {

using augusta::assets::AssetEntry;
using augusta::assets::AssetType;
using augusta::assets::Pack;
using augusta::audio::ChooseVariant;
using augusta::audio::Cue;
using augusta::audio::CueName;

// What PackOf leaves out of a pack whose cues are otherwise all whole.
struct Missing {
  // A cue with no sound blob at all.
  std::string_view cue;
  // A cue whose sound names a second variant that is not in the pack.
  std::string_view second_variant_of;
};

class CueSoundsTest : public ::testing::Test {
 protected:
  void TearDown() override { std::filesystem::remove(path_); }

  // A pack with a sound for every cue: two variants at -3 dB, the first one at
  // 22050 Hz and the second at 44100 Hz, but for what missing leaves out.
  Pack PackOf(const Missing& missing) {
    std::vector<AssetEntry> entries;
    for (const Cue cue : augusta::audio::kCues) {
      const std::string_view name = CueName(cue);
      if (name == missing.cue) {
        continue;
      }
      const augusta::assets::CueSoundData sound{
          .gain_db = -3.0F,
          .variants = {augusta::assets::CueVariantPath(name, 0), augusta::assets::CueVariantPath(name, 1)}};
      entries.push_back(AssetEntry{.type = AssetType::kCueSound,
                                   .path = augusta::assets::CueSoundPath(name),
                                   .data = *augusta::assets::EncodeCueSoundBlob(sound)});
      for (std::size_t i = 0; i < sound.variants.size(); ++i) {
        if (name == missing.second_variant_of && i == 1) {
          continue;
        }
        const auto audio = augusta::assets::EncodeAudioBlob({.sample_rate = i == 0 ? 22050U : 44100U,
                                                             .bits_per_sample = 16,
                                                             .samples = {std::byte{0x01}, std::byte{0x00}}});
        entries.push_back(AssetEntry{.type = AssetType::kAudio, .path = sound.variants[i], .data = *audio});
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
    std::string name = std::string("augusta_cues_test_") + test.test_suite_name() + "_" + test.name() +
                       std::string(augusta::assets::kPackExtension);
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

TEST_F(CueSoundsTest, EveryCueLoadsInCatalogueOrderWithItsVariantsAndGain) {
  const Pack pack = PackOf({});

  const auto sounds = augusta::audio::LoadCueSounds(pack);

  ASSERT_TRUE(sounds.has_value()) << augusta::audio::DescribeCueSoundError(sounds.error());
  for (const augusta::audio::CueSound& sound : *sounds) {
    EXPECT_EQ(sound.gain_db, -3.0F);
    ASSERT_EQ(sound.variants.size(), 2U);
    EXPECT_EQ(sound.variants[0].sample_rate, 22050U);
    EXPECT_EQ(sound.variants[1].sample_rate, 44100U);
  }
}

TEST_F(CueSoundsTest, ACueWithNoSoundIsNamed) {
  const Pack pack = PackOf({.cue = "hit_taken", .second_variant_of = ""});

  const auto sounds = augusta::audio::LoadCueSounds(pack);

  ASSERT_FALSE(sounds.has_value());
  EXPECT_EQ(sounds.error().path, "sounds/hit_taken");
  EXPECT_EQ(sounds.error().resolve_error, augusta::assets::ResolveError::kNotFound);
  EXPECT_NE(augusta::audio::DescribeCueSoundError(sounds.error()).find("sounds/hit_taken"), std::string::npos);
}

TEST_F(CueSoundsTest, AMissingVariantIsNamed) {
  const Pack pack = PackOf({.cue = "", .second_variant_of = "death"});

  const auto sounds = augusta::audio::LoadCueSounds(pack);

  ASSERT_FALSE(sounds.has_value());
  EXPECT_EQ(sounds.error().path, "sounds/death/1");
  EXPECT_EQ(sounds.error().resolve_error, augusta::assets::ResolveError::kNotFound);
}

// A cue's variant is chosen at random each time it plays, never the one it
// played last (ADR-0041).
TEST(ChooseVariantTest, ACueWithOneVariantAlwaysPlaysIt) {
  for (std::uint32_t random = 0; random < 8; ++random) {
    EXPECT_EQ(ChooseVariant(1, std::nullopt, random), 0U);
    EXPECT_EQ(ChooseVariant(1, 0, random), 0U);
  }
}

TEST(ChooseVariantTest, NoVariantIsChosenTwiceInARow) {
  for (std::size_t count = 2; count <= 5; ++count) {
    for (std::size_t last = 0; last < count; ++last) {
      for (std::uint32_t random = 0; random < 20; ++random) {
        const std::size_t chosen = ChooseVariant(count, last, random);
        EXPECT_LT(chosen, count);
        EXPECT_NE(chosen, last) << count << " variants, random " << random;
      }
    }
  }
}

TEST(ChooseVariantTest, EveryVariantIsChosenOverEnoughPlays) {
  constexpr std::size_t kCount = 4;
  std::set<std::size_t> chosen;
  std::optional<std::size_t> last;
  // Seeded, so the test always sees the same plays.
  std::mt19937 random(812);
  for (int play = 0; play < 100; ++play) {
    last = ChooseVariant(kCount, last, random());
    chosen.insert(*last);
  }
  EXPECT_EQ(chosen, (std::set<std::size_t>{0, 1, 2, 3}));
}

TEST(ChooseVariantTest, TheFirstPlayMayChooseAnyVariant) {
  EXPECT_EQ(ChooseVariant(3, std::nullopt, 0), 0U);
  EXPECT_EQ(ChooseVariant(3, std::nullopt, 1), 1U);
  EXPECT_EQ(ChooseVariant(3, std::nullopt, 2), 2U);
}

}  // namespace
