#include "augusta/assets.h"

#include <gtest/gtest.h>

#include <cstddef>
#include <filesystem>
#include <fstream>
#include <vector>

// Unit tests for augusta_assets functionality that doesn't need the
// offline cooker (tools/asset-cooking) - unlike asset_pipeline_test.cpp,
// this links only augusta_assets, so it builds and runs under the plain
// runtime presets (windows/linux/sanitizers), not just windows-tools.
namespace {

using augusta::assets::Ed25519PublicKey;
using augusta::assets::GenerateEd25519KeyPair;
using augusta::assets::ReadEd25519PublicKeyFile;
using augusta::assets::ReadKeyFileError;

class ReadEd25519PublicKeyFileTest : public ::testing::Test {
 protected:
  void TearDown() override {
    for (const auto& path : cleanup_) {
      std::filesystem::remove(path);
    }
  }

  std::filesystem::path MakePath(std::string_view name) {
    const auto path = std::filesystem::temp_directory_path() / name;
    cleanup_.push_back(path);
    return path;
  }

 private:
  std::vector<std::filesystem::path> cleanup_;
};

void WriteFileBytes(const std::filesystem::path& path, const void* data, std::size_t size) {
  std::ofstream file(path, std::ios::binary | std::ios::trunc);
  file.write(reinterpret_cast<const char*>(data), static_cast<std::streamsize>(size));
}

// Client/server startup (issue #60) reads its public key from exactly
// this kind of file - the same 32 raw bytes the cooker's --gen-keypair
// mode writes.
TEST_F(ReadEd25519PublicKeyFileTest, RoundTripsAValidKeyFile) {
  const auto keys = GenerateEd25519KeyPair();
  const auto path = MakePath("augusta_assets_test_valid.pub");
  WriteFileBytes(path, keys.public_key.data(), keys.public_key.size());

  const auto read_key = ReadEd25519PublicKeyFile(path);
  ASSERT_TRUE(read_key.has_value());
  EXPECT_EQ(*read_key, keys.public_key);
}

TEST_F(ReadEd25519PublicKeyFileTest, RejectsMissingFile) {
  const auto result = ReadEd25519PublicKeyFile(MakePath("augusta_assets_test_does_not_exist.pub"));
  ASSERT_FALSE(result.has_value());
  EXPECT_EQ(result.error(), ReadKeyFileError::kIoError);
}

TEST_F(ReadEd25519PublicKeyFileTest, RejectsTruncatedFile) {
  const auto path = MakePath("augusta_assets_test_truncated.pub");
  const std::vector<std::byte> short_bytes(Ed25519PublicKey{}.size() - 1);
  WriteFileBytes(path, short_bytes.data(), short_bytes.size());

  const auto result = ReadEd25519PublicKeyFile(path);
  ASSERT_FALSE(result.has_value());
  EXPECT_EQ(result.error(), ReadKeyFileError::kIoError);
}

TEST_F(ReadEd25519PublicKeyFileTest, RejectsOversizedFile) {
  const auto keys = GenerateEd25519KeyPair();
  const auto path = MakePath("augusta_assets_test_oversized.pub");
  std::vector<std::byte> oversized(keys.public_key.begin(), keys.public_key.end());
  oversized.push_back(std::byte{0xFF});
  WriteFileBytes(path, oversized.data(), oversized.size());

  const auto result = ReadEd25519PublicKeyFile(path);
  ASSERT_FALSE(result.has_value());
  EXPECT_EQ(result.error(), ReadKeyFileError::kIoError);
}

}  // namespace
