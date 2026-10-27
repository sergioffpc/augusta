#include <algorithm>
#include <cstddef>
#include <filesystem>
#include <optional>
#include <string>
#include <utility>
#include <vector>

#include <gtest/gtest.h>

#include "augusta/assets.h"
#include "augusta/cues.h"
#include "augusta/policy.h"
#include "augusta/policy_loader.h"

// The runtime's end of the cook (tools/pack, ADR-0030), which writes packs with
// augusta_assets' own encoder: these load the golden packs the cooker wrote
// from its example scenario with a committed test key, and what they hold; the
// cooker's own tests check it still writes them byte for byte (ADR-0013).
namespace {

using augusta::assets::Pack;

const std::filesystem::path kExamplePacks{AUGUSTA_EXAMPLE_PACKS};

class CookedPackTest : public ::testing::Test {
 protected:
  void SetUp() override {
    const auto public_key = augusta::assets::ReadEd25519PublicKeyFile(kExamplePacks / "test.pub");
    ASSERT_TRUE(public_key.has_value());

    auto client =
        Pack::Load((kExamplePacks / "client").replace_extension(augusta::assets::kPackExtension), *public_key);
    ASSERT_TRUE(client.has_value()) << augusta::assets::DescribeLoadError(client.error());
    client_.emplace(std::move(*client));

    auto server =
        Pack::Load((kExamplePacks / "server").replace_extension(augusta::assets::kPackExtension), *public_key);
    ASSERT_TRUE(server.has_value()) << augusta::assets::DescribeLoadError(server.error());
    server_.emplace(std::move(*server));
  }

  std::optional<Pack> client_;
  std::optional<Pack> server_;
};

// Requirements: US-22
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
  EXPECT_TRUE(server_->ResolveScript(augusta::policy::kRulesScriptPath).has_value());

  const auto policy = augusta::policy::LoadPolicy(*server_);

  EXPECT_TRUE(policy.has_value()) << augusta::policy::DescribePolicyLoadError(policy.error());
}

// The client puts the camera at it and the server fires Shots from it, so both
// packs hold the example character's eye.
// Requirements: US-22
TEST_F(CookedPackTest, BothPacksHoldTheExampleCharactersEye) {
  for (const Pack* pack : {&*client_, &*server_}) {
    const auto eye = pack->ResolveEye(augusta::assets::CharacterEyePath("soldier"));
    ASSERT_TRUE(eye.has_value());
    EXPECT_FLOAT_EQ(eye->position.y, 1.7F);
  }
}

// The server places every Muzzle from them (ADR-0064), so both packs hold the
// example rifle's Grip, Muzzle, Sight and Fore-end: the Muzzle ahead of the
// Grip, the Fore-end between them.
TEST_F(CookedPackTest, BothPacksHoldTheExampleRiflesMarkers) {
  for (const Pack* pack : {&*client_, &*server_}) {
    const auto rifle = pack->ResolveRifle();
    ASSERT_TRUE(rifle.has_value());
    EXPECT_FLOAT_EQ(rifle->muzzle.translation.z, -0.62F);
    EXPECT_FLOAT_EQ(rifle->sight.translation.y, 0.12F);
    EXPECT_FLOAT_EQ(rifle->fore_end.translation.z, -0.35F);
  }
}

// Both packs pose the example character holding the rifle (ADR-0066), so both
// hold its weapon socket, on its right hand, and name its left hand.
// Requirements: US-11
TEST_F(CookedPackTest, BothPacksHoldTheExampleCharactersWeaponSocket) {
  for (const Pack* pack : {&*client_, &*server_}) {
    const auto socket = pack->ResolveWeaponSocket("soldier");
    ASSERT_TRUE(socket.has_value());
    EXPECT_EQ(socket->right_hand, "Root/Pelvis/Spine/RightShoulder/RightElbow/RightHand");
    EXPECT_EQ(socket->left_hand, "Root/Pelvis/Spine/LeftShoulder/LeftElbow/LeftHand");
  }
}

// The server judges hits against them and the client draws where a Shot lands,
// so both packs hold the example character's hitboxes, one or more per body part.
// Requirements: US-11
TEST_F(CookedPackTest, BothPacksHoldTheExampleCharactersHitboxesForEveryBodyPart) {
  for (const Pack* pack : {&*client_, &*server_}) {
    const auto hitboxes = pack->ResolveHitboxes("soldier");
    ASSERT_TRUE(hitboxes.has_value());
    EXPECT_EQ(hitboxes->size(), 8U);
    EXPECT_EQ(augusta::assets::FirstMissingBodyPart(*hitboxes), std::nullopt);
    for (const augusta::assets::HitboxData& hitbox : *hitboxes) {
      EXPECT_FALSE(hitbox.joint.empty());
    }
  }
}

// Each Hitbox moves with a Joint of it (ADR-0061), so both packs hold the
// example character's Skeleton.
// Requirements: US-11
TEST_F(CookedPackTest, BothPacksHoldTheExampleCharactersSkeleton) {
  for (const Pack* pack : {&*client_, &*server_}) {
    const auto entries = pack->Entries();
    EXPECT_TRUE(std::ranges::any_of(entries, [](const augusta::assets::PackEntry& entry) {
      return entry.type == augusta::assets::AssetType::kSkeleton &&
             entry.path == augusta::assets::CharacterSkeletonPath("soldier");
    }));
  }
}

// The example character's pose comes from its Clips (ADR-0066), so both packs
// hold its animation Skeleton, its Clips and its stance transitions.
// Requirements: US-11
TEST_F(CookedPackTest, BothPacksHoldTheExampleCharactersAnimation) {
  using augusta::assets::AssetType;
  const std::vector<std::pair<AssetType, std::string>> expected = {
      {AssetType::kAnimationSkeleton, augusta::assets::CharacterAnimationSkeletonPath("soldier")},
      {AssetType::kClip, augusta::assets::CharacterClipPath("soldier", "standing/forward")},
      {AssetType::kClip, augusta::assets::CharacterClipPath("soldier", "prone/idle")},
      {AssetType::kClip, augusta::assets::CharacterClipPath("soldier", "air")},
      {AssetType::kStanceTransitions, augusta::assets::CharacterStanceTransitionsPath("soldier")},
  };
  for (const Pack* pack : {&*client_, &*server_}) {
    const auto entries = pack->Entries();
    for (const auto& [type, path] : expected) {
      EXPECT_TRUE(std::ranges::any_of(entries, [&](const augusta::assets::PackEntry& entry) {
        return entry.type == type && entry.path == path;
      })) << path;
    }
  }
}

// Both packs carry the same Skeleton (ADR-0061), and it loads with every one of
// the example character's Hitboxes on a Joint of it.
// Requirements: US-11
TEST_F(CookedPackTest, BothPacksLoadTheSameExampleSkeletonAndEveryHitboxResolvesItsJoint) {
  const auto client_skeleton = client_->ResolveSkeleton("soldier");
  const auto server_skeleton = server_->ResolveSkeleton("soldier");
  ASSERT_TRUE(client_skeleton.has_value());
  ASSERT_TRUE(server_skeleton.has_value());
  ASSERT_EQ(client_skeleton->joints.size(), server_skeleton->joints.size());
  for (std::size_t joint = 0; joint < client_skeleton->joints.size(); ++joint) {
    EXPECT_EQ(client_skeleton->joints[joint].name, server_skeleton->joints[joint].name);
    EXPECT_EQ(client_skeleton->joints[joint].parent, server_skeleton->joints[joint].parent);
  }
  for (const Pack* pack : {&*client_, &*server_}) {
    auto hitboxes = pack->ResolveHitboxes("soldier");
    ASSERT_TRUE(hitboxes.has_value());
    const auto bound = augusta::assets::BindHitboxes(*server_skeleton, *std::move(hitboxes));
    ASSERT_TRUE(bound.has_value()) << bound.error().joint;
    EXPECT_EQ(bound->size(), 8U);
  }
}

// The client loads every cue's variants and gain at startup (ADR-0020,
// ADR-0041), so a missing one is found before a Match; the headless server plays
// none. The example names each cue's sound by its file alone: one variant at
// 0 dB.
TEST_F(CookedPackTest, TheClientPackHoldsASoundForEveryCueAndTheServerPackNone) {
  const auto sounds = augusta::audio::LoadCueSounds(*client_);
  ASSERT_TRUE(sounds.has_value()) << augusta::audio::DescribeCueSoundError(sounds.error());
  for (const augusta::audio::CueSound& sound : *sounds) {
    EXPECT_EQ(sound.gain_db, 0.0F);
    ASSERT_EQ(sound.variants.size(), 1U);
    EXPECT_EQ(sound.variants[0].sample_rate, 22050U);
    EXPECT_EQ(sound.variants[0].bits_per_sample, 16U);
    EXPECT_FALSE(sound.variants[0].samples.empty());
  }

  const auto server_sounds = augusta::audio::LoadCueSounds(*server_);
  ASSERT_FALSE(server_sounds.has_value());
  EXPECT_EQ(server_sounds.error().path, "sounds/gunshot");
  EXPECT_FALSE(server_->ResolveAudio("sounds/gunshot/0").has_value());
}

}  // namespace
