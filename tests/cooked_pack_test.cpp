#include <cstdlib>
#include <filesystem>
#include <memory>
#include <optional>
#include <string>
#include <vector>

#include <gtest/gtest.h>

#include "augusta/assets.h"

// The C++ half of the contract between the pack's two implementations: the
// Python cooker (tools/pack, ADR-0030) writes it, augusta_assets reads it.
// CI cooks the example scenario with the real cooker and a throwaway key, then
// points AUGUSTA_COOKED_PACKS at a folder holding the client.pack,
// server.pack and augusta.pub it wrote (ADR-0013). Anywhere else the test is
// skipped, so it needs no cooker to pass.
namespace {

using augusta::assets::Pack;

constexpr const char* kCookedPacksVariable = "AUGUSTA_COOKED_PACKS";

std::optional<std::filesystem::path> CookedPacksFolder() {
#ifdef _WIN32
  // getenv is deprecated under MSVC in favor of this.
  char* value = nullptr;
  std::size_t size = 0;
  if (_dupenv_s(&value, &size, kCookedPacksVariable) != 0 || value == nullptr) {
    return std::nullopt;
  }
  const std::unique_ptr<char, decltype(&std::free)> owned(value, &std::free);
  return std::filesystem::path(owned.get());
#else
  const char* value = std::getenv(kCookedPacksVariable);  // NOLINT(concurrency-mt-unsafe): read once, never set
  if (value == nullptr) {
    return std::nullopt;
  }
  return std::filesystem::path(value);
#endif
}

class CookedPackTest : public ::testing::Test {
 protected:
  void SetUp() override {
    const auto folder = CookedPacksFolder();
    if (!folder.has_value()) {
      GTEST_SKIP() << kCookedPacksVariable << " is not set: no cooked packs to load";
    }
    const auto public_key = augusta::assets::ReadEd25519PublicKeyFile(*folder / "augusta.pub");
    ASSERT_TRUE(public_key.has_value());

    auto client = Pack::Load(*folder / "client.pack", *public_key);
    ASSERT_TRUE(client.has_value()) << augusta::assets::DescribeLoadError(client.error());
    client_.emplace(std::move(*client));

    auto server = Pack::Load(*folder / "server.pack", *public_key);
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
  EXPECT_TRUE(client_->ResolveMesh("characters/player/Character/Visual").has_value());

  const auto characters = client_->ResolveCharacters();
  ASSERT_TRUE(characters.has_value());
  EXPECT_EQ(*characters, std::vector<std::string>{"characters/player"});

  const auto eye = client_->ResolveEye("characters/player/Character/Eye");
  ASSERT_TRUE(eye.has_value());
  EXPECT_FLOAT_EQ(eye->position.y, 1.7F);
}

TEST_F(CookedPackTest, TheServerPackHoldsNoVisualContentAndNamesItsClientPack) {
  EXPECT_FALSE(server_->ResolveMesh("Root/Floor/Visual").has_value());
  EXPECT_TRUE(server_->ResolveCollision("Root/Floor/Collider").has_value());
  EXPECT_TRUE(server_->ResolveSpawnPoint("Root/Spawn").has_value());

  const auto client_hash = server_->ResolveClientPackHash();
  ASSERT_TRUE(client_hash.has_value());
  EXPECT_EQ(*client_hash, client_->Hash());

  const auto parameters = server_->ResolveScript("parameters.lua");
  ASSERT_TRUE(parameters.has_value());
  EXPECT_FALSE(parameters->empty());
}

}  // namespace
