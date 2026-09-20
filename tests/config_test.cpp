#include "augusta/config.h"

#include <gtest/gtest.h>

#include <expected>
#include <filesystem>
#include <fstream>
#include <string>
#include <string_view>
#include <vector>

// Unit tests for augusta_config's YAML schema and file loading (ADR-0034).
namespace {

using augusta::config::ConfigError;
using augusta::config::ConfigErrorCode;
using augusta::config::DescribeConfigError;
using augusta::config::LoadClientConfig;
using augusta::config::LoadServerConfig;
using augusta::config::ParseClientConfig;
using augusta::config::ParseServerConfig;
using augusta::config::ResolveConfigFile;

// The directory of the (imaginary) config file: what a relative base_dir is
// relative to.
const std::filesystem::path kFileDir = std::filesystem::path("file") / "dir";

// A relative path in a valid client/server config resolves under this one.
const std::filesystem::path kRoot = kFileDir / "content";

bool Contains(const std::string& text, std::string_view part) { return text.find(part) != std::string::npos; }

std::string Absolute(std::string_view name) { return (std::filesystem::absolute(name)).generic_string(); }

TEST(ParseClientConfigTest, ReadsEveryKey) {
  const auto config = ParseClientConfig(
      "base_dir: content\n"
      "pack: packs/level.client.pack\n"
      "public_key: keys/augusta.pub\n"
      "server_address: 10.0.0.5:27016\n",
      kFileDir);

  ASSERT_TRUE(config.has_value());
  EXPECT_EQ(config->pack_path, kRoot / "packs" / "level.client.pack");
  EXPECT_EQ(config->public_key_path, kRoot / "keys" / "augusta.pub");
  EXPECT_EQ(config->server_address, "10.0.0.5:27016");
}

TEST(ParseClientConfigTest, DefaultsTheServerAddress) {
  const auto config = ParseClientConfig("base_dir: content\npack: a.pack\npublic_key: k.pub\n", kFileDir);

  ASSERT_TRUE(config.has_value());
  EXPECT_EQ(config->server_address, augusta::config::kDefaultServerAddress);
}

TEST(ParseClientConfigTest, ResolvesRelativePathsAgainstAnAbsoluteBaseDir) {
  const auto config =
      ParseClientConfig("base_dir: '" + Absolute("content") + "'\npack: packs/a.pack\npublic_key: k.pub\n", kFileDir);

  ASSERT_TRUE(config.has_value());
  EXPECT_EQ(config->pack_path, std::filesystem::absolute("content").lexically_normal() / "packs" / "a.pack");
  EXPECT_EQ(config->public_key_path, std::filesystem::absolute("content").lexically_normal() / "k.pub");
}

TEST(ParseClientConfigTest, ResolvesARelativeBaseDirAgainstTheFilesDirectory) {
  const auto config = ParseClientConfig("base_dir: ../content\npack: a.pack\npublic_key: k.pub\n", kFileDir);

  ASSERT_TRUE(config.has_value());
  EXPECT_EQ(config->pack_path, std::filesystem::path("file") / "content" / "a.pack");
  EXPECT_EQ(config->public_key_path, std::filesystem::path("file") / "content" / "k.pub");
}

TEST(ParseClientConfigTest, ReadsNonAsciiPathsAsUtf8) {
  // "\xC3\xA7\xC3\xA3" is c-cedilla and a-tilde in UTF-8, as YAML text carries
  // them. Escapes rather than the letters themselves: without /utf-8, MSVC
  // reads this file's literals in the system codepage.
  const auto config =
      ParseClientConfig("base_dir: pacotes_\xC3\xA7\xC3\xA3o\npack: a.pack\npublic_key: k.pub\n", kFileDir);

  ASSERT_TRUE(config.has_value());
  EXPECT_EQ(config->pack_path, kFileDir / std::filesystem::path(u8"pacotes_\u00e7\u00e3o") / "a.pack");
}

TEST(ParseClientConfigTest, ADotBaseDirMeansTheFilesDirectory) {
  const auto config = ParseClientConfig("base_dir: .\npack: a.pack\npublic_key: k.pub\n", kFileDir);

  ASSERT_TRUE(config.has_value());
  EXPECT_EQ(config->pack_path, kFileDir / "a.pack");
}

TEST(ParseClientConfigTest, KeepsAnAbsolutePathAsIs) {
  const auto config = ParseClientConfig(
      "base_dir: content\npack: '" + Absolute("elsewhere/a.pack") + "'\npublic_key: k.pub\n", kFileDir);

  ASSERT_TRUE(config.has_value());
  EXPECT_EQ(config->pack_path, std::filesystem::absolute("elsewhere/a.pack").lexically_normal());
  EXPECT_EQ(config->public_key_path, kRoot / "k.pub");
}

TEST(ParseClientConfigTest, NormalizesDotSegments) {
  const auto config = ParseClientConfig("base_dir: content\npack: ./a/../b.pack\npublic_key: k.pub\n", kFileDir);

  ASSERT_TRUE(config.has_value());
  EXPECT_EQ(config->pack_path, kRoot / "b.pack");
}

TEST(ParseClientConfigTest, RejectsAMissingBaseDir) {
  const auto config = ParseClientConfig("pack: a.pack\npublic_key: k.pub\n", kFileDir);

  ASSERT_FALSE(config.has_value());
  EXPECT_EQ(config.error().code, ConfigErrorCode::kMissingKey);
  EXPECT_EQ(config.error().subject, "base_dir");
}

TEST(ParseClientConfigTest, RejectsAnEmptyBaseDir) {
  const auto config = ParseClientConfig("base_dir: ''\npack: a.pack\npublic_key: k.pub\n", kFileDir);

  ASSERT_FALSE(config.has_value());
  EXPECT_EQ(config.error().code, ConfigErrorCode::kEmptyValue);
  EXPECT_EQ(config.error().subject, "base_dir");
}

TEST(ParseClientConfigTest, RejectsAMissingRequiredKey) {
  const auto config = ParseClientConfig("base_dir: content\npublic_key: k.pub\n", kFileDir);

  ASSERT_FALSE(config.has_value());
  EXPECT_EQ(config.error().code, ConfigErrorCode::kMissingKey);
  EXPECT_EQ(config.error().subject, "pack");
}

TEST(ParseClientConfigTest, RejectsAnEmptyRequiredValue) {
  const auto config = ParseClientConfig("base_dir: content\npack: ''\npublic_key: k.pub\n", kFileDir);

  ASSERT_FALSE(config.has_value());
  EXPECT_EQ(config.error().code, ConfigErrorCode::kEmptyValue);
  EXPECT_EQ(config.error().subject, "pack");
}

TEST(ParseClientConfigTest, RejectsAnUnknownKey) {
  // A typo must not silently fall back to the default it was meant to set.
  const auto config =
      ParseClientConfig("base_dir: content\npack: a.pack\npublic_key: k.pub\nserver_adress: x\n", kFileDir);

  ASSERT_FALSE(config.has_value());
  EXPECT_EQ(config.error().code, ConfigErrorCode::kUnknownKey);
  EXPECT_EQ(config.error().subject, "server_adress");
}

TEST(ParseClientConfigTest, RejectsAKeyThatBelongsToTheServer) {
  const auto config =
      ParseClientConfig("base_dir: content\npack: a.pack\npublic_key: k.pub\nlisten_address: x\n", kFileDir);

  ASSERT_FALSE(config.has_value());
  EXPECT_EQ(config.error().code, ConfigErrorCode::kUnknownKey);
  EXPECT_EQ(config.error().subject, "listen_address");
}

TEST(ParseClientConfigTest, RejectsADuplicateKey) {
  const auto config = ParseClientConfig("base_dir: content\npack: a.pack\npack: b.pack\npublic_key: k.pub\n", kFileDir);

  ASSERT_FALSE(config.has_value());
  EXPECT_EQ(config.error().code, ConfigErrorCode::kDuplicateKey);
  EXPECT_EQ(config.error().subject, "pack");
}

TEST(ParseClientConfigTest, RejectsANonScalarValue) {
  const auto config = ParseClientConfig("base_dir: content\npack: [a.pack, b.pack]\npublic_key: k.pub\n", kFileDir);

  ASSERT_FALSE(config.has_value());
  EXPECT_EQ(config.error().code, ConfigErrorCode::kNonStringValue);
  EXPECT_EQ(config.error().subject, "pack");
}

TEST(ParseClientConfigTest, RejectsAKeyWithNoValue) {
  const auto config = ParseClientConfig("base_dir: content\npack:\npublic_key: k.pub\n", kFileDir);

  ASSERT_FALSE(config.has_value());
  EXPECT_EQ(config.error().code, ConfigErrorCode::kNonStringValue);
  EXPECT_EQ(config.error().subject, "pack");
}

TEST(ParseClientConfigTest, RejectsATopLevelThatIsNotAMapping) {
  for (const auto text : {"- a\n- b\n", "just a string\n", ""}) {
    const auto config = ParseClientConfig(text, kFileDir);

    ASSERT_FALSE(config.has_value()) << text;
    EXPECT_EQ(config.error().code, ConfigErrorCode::kNotAMapping) << text;
  }
}

TEST(ParseClientConfigTest, RejectsInvalidYaml) {
  const auto config = ParseClientConfig("pack: [unterminated\n", kFileDir);

  ASSERT_FALSE(config.has_value());
  EXPECT_EQ(config.error().code, ConfigErrorCode::kInvalidYaml);
}

TEST(ParseServerConfigTest, ReadsEveryKey) {
  const auto config = ParseServerConfig(
      "base_dir: content\n"
      "pack: packs/level.server.pack\n"
      "public_key: keys/augusta.pub\n"
      "listen_address: 0.0.0.0:27016\n",
      kFileDir);

  ASSERT_TRUE(config.has_value());
  EXPECT_EQ(config->pack_path, kRoot / "packs" / "level.server.pack");
  EXPECT_EQ(config->public_key_path, kRoot / "keys" / "augusta.pub");
  EXPECT_EQ(config->listen_address, "0.0.0.0:27016");
}

TEST(ParseServerConfigTest, DefaultsTheListenAddress) {
  const auto config = ParseServerConfig("base_dir: content\npack: a.pack\npublic_key: k.pub\n", kFileDir);

  ASSERT_TRUE(config.has_value());
  EXPECT_EQ(config->listen_address, augusta::config::kDefaultListenAddress);
}

TEST(ParseServerConfigTest, RejectsAMissingBaseDir) {
  const auto config = ParseServerConfig("pack: a.pack\npublic_key: k.pub\n", kFileDir);

  ASSERT_FALSE(config.has_value());
  EXPECT_EQ(config.error().code, ConfigErrorCode::kMissingKey);
  EXPECT_EQ(config.error().subject, "base_dir");
}

TEST(ParseServerConfigTest, RejectsAKeyThatBelongsToTheClient) {
  const auto config =
      ParseServerConfig("base_dir: content\npack: a.pack\npublic_key: k.pub\nserver_address: x\n", kFileDir);

  ASSERT_FALSE(config.has_value());
  EXPECT_EQ(config.error().code, ConfigErrorCode::kUnknownKey);
  EXPECT_EQ(config.error().subject, "server_address");
}

std::expected<std::filesystem::path, ConfigError> Resolve(std::vector<const char*> args) {
  args.insert(args.begin(), "augustac");
  return ResolveConfigFile(static_cast<int>(args.size()), args.data(), "augustac", "augustac.yaml");
}

TEST(ResolveConfigFileTest, WithoutArgumentsUsesTheDefaultFileNextToTheExecutable) {
  const auto file = Resolve({});

  ASSERT_TRUE(file.has_value());
  EXPECT_EQ(file->filename(), "augustac.yaml");
  EXPECT_TRUE(std::filesystem::is_directory(file->parent_path())) << file->string();
}

TEST(ResolveConfigFileTest, ConfigArgumentNamesTheFileAsGiven) {
  const auto file = Resolve({"--config", "other/dir/my.yaml"});

  ASSERT_TRUE(file.has_value());
  EXPECT_EQ(*file, std::filesystem::path("other") / "dir" / "my.yaml");
}

TEST(ResolveConfigFileTest, RejectsConfigWithoutAFile) {
  const auto file = Resolve({"--config"});

  ASSERT_FALSE(file.has_value());
  EXPECT_EQ(file.error().code, ConfigErrorCode::kInvalidArguments);
  EXPECT_TRUE(Contains(file.error().subject, "usage: augustac [--config <file>]")) << file.error().subject;
}

TEST(ResolveConfigFileTest, RejectsAnEmptyFileName) { ASSERT_FALSE(Resolve({"--config", ""}).has_value()); }

TEST(ResolveConfigFileTest, RejectsAPositionalArgument) {
  // The old `augustac <pack> <key>` invocation must fail, not be half-honored.
  ASSERT_FALSE(Resolve({"level.pack", "k.pub"}).has_value());
  ASSERT_FALSE(Resolve({"my.yaml"}).has_value());
}

TEST(ResolveConfigFileTest, RejectsAnUnknownOptionAndExtraArguments) {
  ASSERT_FALSE(Resolve({"--conf", "my.yaml"}).has_value());
  ASSERT_FALSE(Resolve({"--config", "a.yaml", "b.yaml"}).has_value());
}

TEST(ResolveConfigFileTest, UsageNamesTheProgramAndTheDefaultFile) {
  const char* const args[] = {"augustad", "nope"};
  const auto file = ResolveConfigFile(2, args, "augustad", "augustad.yaml");

  ASSERT_FALSE(file.has_value());
  EXPECT_EQ(file.error().code, ConfigErrorCode::kInvalidArguments);
  EXPECT_TRUE(Contains(file.error().subject, "usage: augustad [--config <file>]")) << file.error().subject;
  EXPECT_TRUE(Contains(file.error().subject, "augustad.yaml")) << file.error().subject;
}

TEST(DescribeConfigErrorTest, NamesTheKeyAndTheFile) {
  const ConfigError error{.code = ConfigErrorCode::kMissingKey, .subject = "pack", .file = "dir/augustac.yaml"};

  const auto message = DescribeConfigError(error);

  EXPECT_TRUE(Contains(message, "missing required key 'pack'")) << message;
  EXPECT_TRUE(Contains(message, std::filesystem::path("dir/augustac.yaml").string())) << message;
}

TEST(DescribeConfigErrorTest, OmitsTheFileWhenThereIsNone) {
  const auto message = DescribeConfigError({.code = ConfigErrorCode::kUnknownKey, .subject = "typo"});

  EXPECT_EQ(message, "unknown key 'typo'");
}

class LoadConfigTest : public ::testing::Test {
 protected:
  void SetUp() override {
    directory_ = std::filesystem::temp_directory_path() / "augusta_config_test";
    std::filesystem::remove_all(directory_);
    std::filesystem::create_directories(directory_);
  }

  void TearDown() override { std::filesystem::remove_all(directory_); }

  std::filesystem::path Write(std::string_view name, std::string_view contents) const {
    const auto path = directory_ / name;
    std::ofstream(path, std::ios::binary) << contents;
    return path;
  }

  std::filesystem::path directory_;
};

TEST_F(LoadConfigTest, ResolvesBaseDirAgainstTheFilesDirectory) {
  const auto file = Write("augustac.yaml", "base_dir: content\npack: level.pack\npublic_key: keys/k.pub\n");

  const auto config = LoadClientConfig(file);

  ASSERT_TRUE(config.has_value());
  EXPECT_EQ(config->pack_path, directory_ / "content" / "level.pack");
  EXPECT_EQ(config->public_key_path, directory_ / "content" / "keys" / "k.pub");
}

TEST_F(LoadConfigTest, LoadsAServerConfig) {
  const auto file =
      Write("augustad.yaml", "base_dir: .\npack: level.pack\npublic_key: k.pub\nlisten_address: 0.0.0.0:1\n");

  const auto config = LoadServerConfig(file);

  ASSERT_TRUE(config.has_value());
  EXPECT_EQ(config->pack_path, directory_ / "level.pack");
  EXPECT_EQ(config->listen_address, "0.0.0.0:1");
}

TEST_F(LoadConfigTest, NamesTheFileWhenItCannotBeOpened) {
  const auto file = directory_ / "missing.yaml";

  const auto config = LoadClientConfig(file);

  ASSERT_FALSE(config.has_value());
  EXPECT_EQ(config.error().code, ConfigErrorCode::kCannotOpenFile);
  EXPECT_EQ(config.error().file, file);
}

TEST_F(LoadConfigTest, NamesTheFileWhenItsContentsAreInvalid) {
  const auto file = Write("augustac.yaml", "base_dir: .\npublic_key: k.pub\n");

  const auto config = LoadClientConfig(file);

  ASSERT_FALSE(config.has_value());
  EXPECT_EQ(config.error().file, file);
  EXPECT_EQ(config.error().code, ConfigErrorCode::kMissingKey);
  EXPECT_EQ(config.error().subject, "pack");
}

}  // namespace
