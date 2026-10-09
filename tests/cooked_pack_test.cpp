#include <filesystem>
#include <optional>
#include <string>
#include <utility>
#include <vector>

#include <gtest/gtest.h>

#include "augusta/assets.h"
#include "augusta/cues.h"
#include "augusta/scripting.h"
#include "policy_loader.h"

// The C++ half of the contract between the pack's two implementations: the
// Python cooker (tools/pack, ADR-0030) writes it, augusta_assets reads it.
// These load golden packs the cooker wrote from its example scenario with a
// committed test key; the cooker's own tests check it still writes them byte
// for byte (ADR-0013).
namespace {

using augusta::assets::Pack;

const std::filesystem::path kExamplePacks{AUGUSTA_EXAMPLE_PACKS};

class CookedPackTest : public ::testing::Test {
 protected:
  void SetUp() override {
    const auto public_key = augusta::assets::ReadEd25519PublicKeyFile(kExamplePacks / "test.pub");
    ASSERT_TRUE(public_key.has_value());

    auto client = Pack::Load(kExamplePacks / "client.pack", *public_key);
    ASSERT_TRUE(client.has_value()) << augusta::assets::DescribeLoadError(client.error());
    client_.emplace(std::move(*client));

    auto server = Pack::Load(kExamplePacks / "server.pack", *public_key);
    ASSERT_TRUE(server.has_value()) << augusta::assets::DescribeLoadError(server.error());
    server_.emplace(std::move(*server));
  }

  std::optional<Pack> client_;
  std::optional<Pack> server_;
};

TEST_F(CookedPackTest, TheClientPackResolvesTheExampleScenariosContent) {
  EXPECT_TRUE(client_->ResolveScene("Scene").has_value());
  EXPECT_TRUE(client_->ResolveMesh("Root/Floor/Visual").has_value());
  EXPECT_TRUE(client_->ResolveCollision("Root/Floor/Collider").has_value());
  EXPECT_TRUE(client_->ResolveSpawnPoint("Root/Spawn").has_value());
  EXPECT_TRUE(client_->ResolveMesh("soldier/Character/Visual").has_value());

  const auto characters = client_->ResolveCharacters();
  ASSERT_TRUE(characters.has_value());
  EXPECT_EQ(*characters, std::vector<std::string>{"soldier"});
}

// Requirements: NFR-08
TEST_F(CookedPackTest, TheServerPackHoldsNoVisualContentAndNamesItsClientPack) {
  EXPECT_FALSE(server_->ResolveMesh("Root/Floor/Visual").has_value());
  EXPECT_TRUE(server_->ResolveCollision("Root/Floor/Collider").has_value());
  EXPECT_TRUE(server_->ResolveSpawnPoint("Root/Spawn").has_value());

  EXPECT_EQ(server_->ClientPackHash(), client_->Hash());
  EXPECT_EQ(client_->ClientPackHash(), std::nullopt);

  const auto parameters = server_->ResolveScript("parameters.lua");
  ASSERT_TRUE(parameters.has_value());
  EXPECT_FALSE(parameters->empty());
}

// The server loads them at startup as it does any scenario's: an example whose
// policy did not load would stop every server run on it.
// Requirements: US-22
TEST_F(CookedPackTest, TheServerPackHoldsTheExamplesRulesAndTheyLoad) {
  EXPECT_TRUE(server_->ResolveScript(augusta::scripting::kRulesScriptPath).has_value());

  const auto policy = augusta::server::LoadPolicy(*server_);

  EXPECT_TRUE(policy.has_value()) << augusta::server::DescribePolicyLoadError(policy.error());
}

// The client puts the camera at it and the server fires Shots from it, so both
// packs hold the example character's eye.
TEST_F(CookedPackTest, BothPacksHoldTheExampleCharactersEye) {
  for (const Pack* pack : {&*client_, &*server_}) {
    const auto eye = pack->ResolveEye(augusta::assets::CharacterEyePath("soldier"));
    ASSERT_TRUE(eye.has_value());
    EXPECT_FLOAT_EQ(eye->position.y, 1.7F);
  }
}

// The server judges hits against them and the client draws where a Shot lands,
// so both packs hold the example character's hitboxes, one or more per body part.
TEST_F(CookedPackTest, BothPacksHoldTheExampleCharactersHitboxesForEveryBodyPart) {
  for (const Pack* pack : {&*client_, &*server_}) {
    const auto hitboxes = pack->ResolveHitboxes("soldier");
    ASSERT_TRUE(hitboxes.has_value());
    EXPECT_EQ(hitboxes->size(), 6U);
    EXPECT_EQ(augusta::assets::FirstMissingBodyPart(*hitboxes), std::nullopt);
  }
}

// The client loads a sound for every cue at startup (ADR-0020), so a missing one
// is found before a Match; the headless server plays none.
TEST_F(CookedPackTest, TheClientPackHoldsASoundForEveryCueAndTheServerPackNone) {
  const auto sounds = augusta::audio::LoadCueSounds(*client_);
  ASSERT_TRUE(sounds.has_value()) << augusta::audio::DescribeCueSoundError(sounds.error());
  for (const augusta::assets::AudioData& sound : *sounds) {
    EXPECT_EQ(sound.sample_rate, 22050U);
    EXPECT_EQ(sound.bits_per_sample, 16U);
    EXPECT_FALSE(sound.samples.empty());
  }

  const auto server_sounds = augusta::audio::LoadCueSounds(*server_);
  ASSERT_FALSE(server_sounds.has_value());
  EXPECT_EQ(server_sounds.error().path, augusta::assets::kSoundsPath);
  EXPECT_FALSE(server_->ResolveAudio("sounds/gunshot").has_value());
}

}  // namespace
