#include <algorithm>
#include <cstddef>
#include <filesystem>
#include <fstream>
#include <ios>
#include <iterator>
#include <string>
#include <string_view>
#include <vector>

#include <gtest/gtest.h>

#include "augusta/assets.h"
#include "augusta/clips.h"
#include "augusta/content.h"
#include "augusta/failure.h"
#include "augusta/pack_encoder.h"
#include "augusta/physics.h"

// What the server loads from its verified pack at startup, on the example
// scenario's golden server pack (ADR-0013): its characters' Clips among the rest,
// and the character it cannot pose refused before any player is admitted.
namespace {

using augusta::failure::Code;
using augusta::failure::Failure;

const std::filesystem::path kPacks{AUGUSTA_EXAMPLE_PACKS};
constexpr std::uint8_t kTickRateHz = 60;

std::filesystem::path ServerPackPath() {
  return (kPacks / "server").replace_extension(augusta::assets::kPackExtension);
}

augusta::assets::Pack LoadPack(const std::filesystem::path& path, const augusta::assets::Ed25519PublicKey& key) {
  auto pack = augusta::assets::Pack::Load(path, key);
  EXPECT_TRUE(pack.has_value());
  return *std::move(pack);
}

// The value of the failure's context under key, or empty if it has none.
std::string ContextOf(const Failure& failure, std::string_view key) {
  for (const auto& field : failure.context) {
    if (field.key == key) {
      return field.value;
    }
  }
  return {};
}

TEST(ServerContentTest, EachCharacterLoadsWithItsClips) {
  const auto key = augusta::assets::ReadEd25519PublicKeyFile(kPacks / "test.pub");
  ASSERT_TRUE(key.has_value());
  const auto pack = LoadPack(ServerPackPath(), *key);

  const auto content = augusta::scenario::LoadServerContent(pack, kTickRateHz);

  ASSERT_TRUE(content.has_value()) << content.error().detail;
  ASSERT_FALSE(content->scenario.characters.empty());
  for (const augusta::scenario::Character& character : content->scenario.characters) {
    ASSERT_NE(character.clips, nullptr) << character.path;
    EXPECT_GT(character.clips->Of(augusta::physics::Stance::kStanding).forward.stride_length, 0.0F) << character.path;
    EXPECT_EQ(character.clips->joints.size(), character.skeleton.joints.size()) << character.path;
  }
}

// The golden server pack again, re-signed with a key of the test's own, without
// the entry at path.
class ServerContentWithoutTest : public ::testing::Test {
 protected:
  void TearDown() override { std::filesystem::remove(path_); }

  augusta::assets::Pack Without(std::string_view path) {
    const auto key = augusta::assets::ReadEd25519PublicKeyFile(kPacks / "test.pub");
    EXPECT_TRUE(key.has_value());
    const auto golden = LoadPack(ServerPackPath(), *key);
    std::ifstream file(ServerPackPath(), std::ios::binary);
    const std::vector<char> bytes{std::istreambuf_iterator<char>(file), std::istreambuf_iterator<char>()};
    std::vector<augusta::assets::AssetEntry> entries;
    for (const augusta::assets::PackEntry& entry : golden.Entries()) {
      if (entry.path == path) {
        continue;
      }
      const auto* const begin = reinterpret_cast<const std::byte*>(bytes.data()) + entry.offset;
      entries.push_back({.type = entry.type, .path = entry.path, .data = {begin, begin + entry.size}});
    }
    EXPECT_EQ(entries.size() + 1, golden.Entries().size()) << path << " is not in the golden server pack";
    const auto keys = augusta::assets::GenerateEd25519KeyPair();
    const ::testing::TestInfo& test = *::testing::UnitTest::GetInstance()->current_test_info();
    path_ = std::filesystem::temp_directory_path() /
            (std::string("augusta_server_content_test_") + test.name() + std::string(augusta::assets::kPackExtension));
    EXPECT_TRUE(augusta::assets::WritePack(path_, entries, keys.private_key, golden.ClientPackHash()).has_value());
    return LoadPack(path_, keys.public_key);
  }

  std::filesystem::path path_;
};

// Requirements: US-26 (spec #823)
TEST_F(ServerContentWithoutTest, ACharacterMissingAClipIsRefusedNamingTheCharacterAndTheClip) {
  const auto pack = Without(augusta::assets::CharacterClipPath("soldier", "crouching/left"));

  const auto content = augusta::scenario::LoadServerContent(pack, kTickRateHz);

  ASSERT_FALSE(content.has_value());
  EXPECT_EQ(content.error().code, Code::kInvalidContent);
  EXPECT_EQ(ContextOf(content.error(), "character"), "soldier");
  EXPECT_EQ(ContextOf(content.error(), "clip"), "crouching/left");
  EXPECT_EQ(content.error().detail, "clip crouching/left: not found");
}

TEST_F(ServerContentWithoutTest, ACharacterWithoutItsAnimationSkeletonIsRefused) {
  const auto pack = Without(augusta::assets::CharacterAnimationSkeletonPath("soldier"));

  const auto content = augusta::scenario::LoadServerContent(pack, kTickRateHz);

  ASSERT_FALSE(content.has_value());
  EXPECT_EQ(content.error().code, Code::kInvalidContent);
  EXPECT_EQ(ContextOf(content.error(), "character"), "soldier");
  EXPECT_EQ(ContextOf(content.error(), "clip"), "");
  EXPECT_EQ(content.error().detail, "animation skeleton: not found");
}

}  // namespace
