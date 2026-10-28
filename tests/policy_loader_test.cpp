#include "augusta/policy_loader.h"

#include <filesystem>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

#include <gtest/gtest.h>

#include "augusta/assets.h"
#include "augusta/pack_encoder.h"
#include "augusta/policy.h"

// The server's startup read of a scenario's Game policy out of its pack: a real
// signed pack in a temp file, loaded back the way the server loads it.
namespace {

using augusta::assets::AssetEntry;
using augusta::assets::AssetType;
using augusta::assets::Pack;
using augusta::policy::LoadPolicy;
using augusta::policy::LoadPolicyMaker;
using augusta::policy::PolicyLoadErrorCode;
using augusta::policy::PolicyMakerOf;

AssetEntry ScriptEntry(std::string path, std::string_view text) {
  return AssetEntry{
      .type = AssetType::kScript, .path = std::move(path), .data = augusta::assets::EncodeScriptBlob(text).value()};
}

// Whether policy's on_tick, called once on a Match with no players, ends it.
bool EndsTheMatch(augusta::policy::Policy& policy) {
  const auto action = policy.OnTick(augusta::policy::MatchView{}, 1);
  return action.has_value() && action->has_value();
}

class PolicyLoaderTest : public ::testing::Test {
 protected:
  void TearDown() override {
    for (const auto& path : cleanup_) {
      std::filesystem::remove(path);
    }
  }

  // Writes entries into a signed pack and loads it.
  Pack MakePack(const std::string& name, const std::vector<AssetEntry>& entries) {
    const auto path = std::filesystem::temp_directory_path() /
                      ("augusta_policy_loader_test_" + name + std::string(augusta::assets::kPackExtension));
    cleanup_.push_back(path);
    const auto keys = augusta::assets::GenerateEd25519KeyPair();
    EXPECT_TRUE(augusta::assets::WritePack(path, entries, keys.private_key).has_value());
    auto pack = Pack::Load(path, keys.public_key);
    EXPECT_TRUE(pack.has_value());
    return std::move(*pack);
  }

 private:
  std::vector<std::filesystem::path> cleanup_;
};

// Requirements: US-22
TEST_F(PolicyLoaderTest, LoadsTheRulesFromThePack) {
  const Pack pack = MakePack("rules", {ScriptEntry("rules.lua", "function on_tick() return {draw = true} end")});

  auto engine = LoadPolicy(pack);

  ASSERT_TRUE(engine.has_value()) << augusta::policy::DescribePolicyLoadError(engine.error());
  EXPECT_TRUE(EndsTheMatch(*engine));
}

// Every Replay's World runs a policy of its own (ADR-0051): what one keeps
// between its hooks' calls is none of another's.
// Requirements: NFR-09
TEST_F(PolicyLoaderTest, EachPolicyTheMakerMakesStartsAfresh) {
  const Pack pack = MakePack(
      "maker",
      {ScriptEntry(
          "rules.lua",
          "local calls = 0 function on_tick() calls = calls + 1 if calls == 2 then return {draw = true} end end")});

  const auto make = LoadPolicyMaker(pack);

  ASSERT_TRUE(make.has_value()) << augusta::policy::DescribePolicyLoadError(make.error());
  auto first = (*make)();
  auto second = (*make)();
  ASSERT_TRUE(first.has_value() && second.has_value());
  EXPECT_FALSE(EndsTheMatch(*first));
  EXPECT_TRUE(EndsTheMatch(*first));
  EXPECT_FALSE(EndsTheMatch(*second));
}

// Requirements: US-22
TEST_F(PolicyLoaderTest, ThePolicyMakerOfThatDoNotLoadIsTheirError) {
  const Pack pack = MakePack("maker_broken", {ScriptEntry("rules.lua", "function on_tick(")});

  const auto make = LoadPolicyMaker(pack);

  ASSERT_FALSE(make.has_value());
  EXPECT_EQ(make.error().code, PolicyLoadErrorCode::kScriptError);
}

// Each policy a maker makes loads its rules again, so each load can fail: a
// load that fails is that call's error, never a policy taken for loaded.
// Requirements: US-21
TEST_F(PolicyLoaderTest, ALoadThatFailsIsTheErrorOfTheCallThatMadeIt) {
  const augusta::policy::PolicyMaker make = PolicyMakerOf("function on_tick(");

  const auto made = make();

  ASSERT_FALSE(made.has_value());
  EXPECT_EQ(made.error().code, PolicyLoadErrorCode::kScriptError);
  EXPECT_FALSE(made.error().subject.empty());
}

// Control for the case above: the same maker of rules that load makes their policy.
// Requirements: US-21
TEST_F(PolicyLoaderTest, ThePolicyMakerOfThatLoadMakesTheirPolicy) {
  const augusta::policy::PolicyMaker make = PolicyMakerOf("function on_tick() return {draw = true} end");

  auto made = make();

  ASSERT_TRUE(made.has_value()) << augusta::policy::DescribePolicyLoadError(made.error());
  EXPECT_TRUE(EndsTheMatch(*made));
}

// Requirements: US-22
TEST_F(PolicyLoaderTest, ThePolicyMadeForAPackWithoutRulesIsNone) {
  const Pack pack =
      MakePack("maker_none", {ScriptEntry("parameters.lua", "function on_tick() return {draw = true} end")});

  const auto make = LoadPolicyMaker(pack);

  ASSERT_TRUE(make.has_value());
  auto none = (*make)();
  ASSERT_TRUE(none.has_value());
  EXPECT_FALSE(EndsTheMatch(*none));
}

// Requirements: US-22
TEST_F(PolicyLoaderTest, APackWithoutRulesHasNoPolicy) {
  const Pack pack =
      MakePack("no_rules", {ScriptEntry("parameters.lua", "function on_tick() return {draw = true} end")});

  auto engine = LoadPolicy(pack);

  ASSERT_TRUE(engine.has_value()) << augusta::policy::DescribePolicyLoadError(engine.error());
  EXPECT_FALSE(EndsTheMatch(*engine));
}

// Requirements: US-22
TEST_F(PolicyLoaderTest, RulesWithASyntaxErrorAreAnErrorNamingTheScript) {
  const Pack pack = MakePack("syntax_error", {ScriptEntry("rules.lua", "function assign_spawns(")});

  const auto engine = LoadPolicy(pack);

  ASSERT_FALSE(engine.has_value());
  EXPECT_EQ(engine.error().code, PolicyLoadErrorCode::kScriptError);
  EXPECT_NE(augusta::policy::DescribePolicyLoadError(engine.error()).find("rules.lua"), std::string::npos);
}

// Requirements: US-22
TEST_F(PolicyLoaderTest, AnEntryAtTheRulesPathThatIsNotAScriptIsAnError) {
  const Pack pack = MakePack(
      "not_a_script",
      {AssetEntry{.type = AssetType::kEye, .path = "rules.lua", .data = augusta::assets::EncodeEyeBlob({}).value()}});

  const auto engine = LoadPolicy(pack);

  ASSERT_FALSE(engine.has_value());
  EXPECT_EQ(engine.error().code, PolicyLoadErrorCode::kUnreadable);
  EXPECT_NE(augusta::policy::DescribePolicyLoadError(engine.error()).find("rules.lua"), std::string::npos);
}

}  // namespace
