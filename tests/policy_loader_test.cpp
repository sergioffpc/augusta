#include "policy_loader.h"

#include <filesystem>
#include <string>
#include <string_view>
#include <utility>
#include <variant>
#include <vector>

#include <gtest/gtest.h>

#include "augusta/assets.h"
#include "augusta/scripting.h"
#include "encoder.h"

// The server's startup read of a scenario's Game policy out of its pack: a real
// signed pack in a temp file, loaded back the way the server loads it.
namespace {

using augusta::assets::AssetEntry;
using augusta::assets::AssetType;
using augusta::assets::Pack;
using augusta::server::LoadPolicy;
using augusta::server::LoadPolicyMaker;
using augusta::server::PolicyLoadErrorCode;

AssetEntry ScriptEntry(std::string path, std::string_view text) {
  return AssetEntry{
      .type = AssetType::kScript, .path = std::move(path), .data = augusta::assets::EncodeScriptBlob(text).value()};
}

// What hook returns, as a string; empty if it returns anything else.
std::string Returned(augusta::scripting::Engine& engine, std::string_view hook) {
  const auto value = engine.Call(hook, {});
  if (!value || !std::holds_alternative<std::string>(value->data)) {
    return {};
  }
  return std::get<std::string>(value->data);
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
    const auto path = std::filesystem::temp_directory_path() / ("augusta_policy_loader_test_" + name + ".pack");
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
  const Pack pack = MakePack("rules", {ScriptEntry("rules.lua", "function probe() return 'rules' end")});

  auto engine = LoadPolicy(pack);

  ASSERT_TRUE(engine.has_value()) << augusta::server::DescribePolicyLoadError(engine.error());
  EXPECT_EQ(Returned(*engine, "probe"), "rules");
}

// Every Replay's World runs a policy of its own (ADR-0051): what one keeps
// between its hooks' calls is none of another's.
TEST_F(PolicyLoaderTest, EachPolicyTheMakerMakesStartsAfresh) {
  const Pack pack = MakePack(
      "maker",
      {ScriptEntry("rules.lua", "local calls = 0 function probe() calls = calls + 1 return tostring(calls) end")});

  const auto make = LoadPolicyMaker(pack);

  ASSERT_TRUE(make.has_value()) << augusta::server::DescribePolicyLoadError(make.error());
  augusta::scripting::Engine first = (*make)();
  augusta::scripting::Engine second = (*make)();
  EXPECT_EQ(Returned(first, "probe"), "1");
  EXPECT_EQ(Returned(first, "probe"), "2");
  EXPECT_EQ(Returned(second, "probe"), "1");
}

TEST_F(PolicyLoaderTest, TheMakerOfRulesThatDoNotLoadIsTheirError) {
  const Pack pack = MakePack("maker_broken", {ScriptEntry("rules.lua", "function probe(")});

  const auto make = LoadPolicyMaker(pack);

  ASSERT_FALSE(make.has_value());
  EXPECT_EQ(make.error().code, PolicyLoadErrorCode::kScriptError);
}

TEST_F(PolicyLoaderTest, ThePolicyMadeForAPackWithoutRulesIsNone) {
  const Pack pack = MakePack("maker_none", {ScriptEntry("parameters.lua", "function probe() return 'here' end")});

  const auto make = LoadPolicyMaker(pack);

  ASSERT_TRUE(make.has_value());
  augusta::scripting::Engine none = (*make)();
  EXPECT_EQ(Returned(none, "probe"), "");
}

// Requirements: US-22
TEST_F(PolicyLoaderTest, APackWithoutRulesHasNoPolicy) {
  const Pack pack = MakePack("no_rules", {ScriptEntry("parameters.lua", "function probe() return 'here' end")});

  auto engine = LoadPolicy(pack);

  ASSERT_TRUE(engine.has_value()) << augusta::server::DescribePolicyLoadError(engine.error());
  const auto returned = engine->Call("probe", {});
  ASSERT_TRUE(returned.has_value());
  EXPECT_TRUE(std::holds_alternative<std::monostate>(returned->data));
}

// Requirements: US-22
TEST_F(PolicyLoaderTest, RulesWithASyntaxErrorAreAnErrorNamingTheScript) {
  const Pack pack = MakePack("syntax_error", {ScriptEntry("rules.lua", "function assign_spawns(")});

  const auto engine = LoadPolicy(pack);

  ASSERT_FALSE(engine.has_value());
  EXPECT_EQ(engine.error().code, PolicyLoadErrorCode::kScriptError);
  EXPECT_NE(augusta::server::DescribePolicyLoadError(engine.error()).find("rules.lua"), std::string::npos);
}

TEST_F(PolicyLoaderTest, AnEntryAtTheRulesPathThatIsNotAScriptIsAnError) {
  const Pack pack = MakePack(
      "not_a_script",
      {AssetEntry{.type = AssetType::kEye, .path = "rules.lua", .data = augusta::assets::EncodeEyeBlob({}).value()}});

  const auto engine = LoadPolicy(pack);

  ASSERT_FALSE(engine.has_value());
  EXPECT_EQ(engine.error().code, PolicyLoadErrorCode::kUnreadable);
  EXPECT_NE(augusta::server::DescribePolicyLoadError(engine.error()).find("rules.lua"), std::string::npos);
}

}  // namespace
