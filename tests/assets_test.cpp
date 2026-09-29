#include "augusta/assets.h"

#include <algorithm>
#include <cstddef>
#include <cstdint>
#include <filesystem>
#include <fstream>
#include <span>
#include <string>
#include <string_view>
#include <vector>

#include <gtest/gtest.h>

#include "augusta/math.h"
#include "encoder.h"

// Unit tests for augusta_assets, including its own private pack-format
// internals (encoder.h/WritePack). Links only augusta_assets - no USD/
// DirectXTex/meshoptimizer - so it builds and runs under the plain
// runtime presets (windows/linux/sanitizers). These tests pin down
// augusta_assets' wire format, which tools/pack's Python
// WritePack reimplementation (ADR-0030) is validated against.
namespace {

using augusta::assets::Ed25519PublicKey;
using augusta::assets::GenerateEd25519KeyPair;
using augusta::assets::ReadEd25519PublicKeyFile;
using augusta::assets::ReadKeyFileError;
using augusta::math::Vec3;

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
// this kind of file: 32 raw bytes, no framing.
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

class PackTest : public ::testing::Test {
 protected:
  void TearDown() override {
    for (const auto& path : cleanup_) {
      std::filesystem::remove(path);
    }
  }

  std::filesystem::path MakePackPath(std::string_view name) {
    const auto path = std::filesystem::temp_directory_path() / name;
    cleanup_.push_back(path);
    return path;
  }

 private:
  std::vector<std::filesystem::path> cleanup_;
};

std::vector<std::byte> ReadFileBytes(const std::filesystem::path& path) {
  std::ifstream file(path, std::ios::binary | std::ios::ate);
  const auto size = static_cast<std::size_t>(file.tellg());
  file.seekg(0);
  std::vector<std::byte> bytes(size);
  file.read(reinterpret_cast<char*>(bytes.data()), static_cast<std::streamsize>(size));
  return bytes;
}

void WriteFileBytes(const std::filesystem::path& path, std::span<const std::byte> bytes) {
  std::ofstream file(path, std::ios::binary | std::ios::trunc);
  file.write(reinterpret_cast<const char*>(bytes.data()), static_cast<std::streamsize>(bytes.size()));
}

// A single-triangle mesh blob, ready to embed as an AssetEntry - shared by
// every test below that just needs *some* validly-encoded blob rather than
// exercising mesh content itself.
std::vector<std::byte> MakeTriangleMeshBlob() {
  augusta::assets::MeshData mesh;
  mesh.points = {Vec3(0.0F, 0.0F, 0.0F), Vec3(1.0F, 0.0F, 0.0F), Vec3(0.0F, 1.0F, 0.0F)};
  mesh.indices = {0, 1, 2};
  const auto blob = augusta::assets::EncodeMeshBlob(mesh);
  return blob.has_value() ? *blob : std::vector<std::byte>{};
}

TEST_F(PackTest, WritePackRejectsDuplicatePaths) {
  const auto blob = MakeTriangleMeshBlob();
  ASSERT_FALSE(blob.empty());

  const std::vector<augusta::assets::AssetEntry> entries = {
      augusta::assets::AssetEntry{augusta::assets::AssetType::kMesh, "Dup", blob},
      augusta::assets::AssetEntry{augusta::assets::AssetType::kMesh, "Dup", blob},
  };

  const auto keys = GenerateEd25519KeyPair();
  const auto pack_path = MakePackPath("augusta_assets_test_dup.pack");
  const auto result = augusta::assets::WritePack(pack_path, entries, keys.private_key);
  ASSERT_FALSE(result.has_value());
  EXPECT_EQ(result.error(), augusta::assets::WriteError::kDuplicatePath);
}

// Exercises augusta_assets' own texture-blob encode/write/resolve seam
// directly (WritePack/Pack::Load/ResolveTexture) - actually compressing an
// image (DirectXTex) happens in tools/pack (Python), which this
// module has no dependency on.
TEST_F(PackTest, EncodesAndResolvesTextureBlob) {
  const auto pack_path = MakePackPath("augusta_assets_test_texture_blob.pack");
  const auto keys = GenerateEd25519KeyPair();

  const std::vector<std::byte> dds_bytes = {std::byte{0x44}, std::byte{0x44}, std::byte{0x53},
                                            std::byte{0x20}, std::byte{0xAB}, std::byte{0xCD}};
  const augusta::assets::TextureData texture{.dds_bytes = dds_bytes, .format = augusta::assets::TextureFormat::kBC5};

  const auto blob = augusta::assets::EncodeTextureBlob(texture);
  ASSERT_TRUE(blob.has_value());

  const std::vector<augusta::assets::AssetEntry> entries = {
      augusta::assets::AssetEntry{.type = augusta::assets::AssetType::kTexture, .path = "Tex", .data = *blob},
  };
  const auto written = augusta::assets::WritePack(pack_path, entries, keys.private_key);
  ASSERT_TRUE(written.has_value());

  auto pack = augusta::assets::Pack::Load(pack_path, keys.public_key);
  ASSERT_TRUE(pack.has_value());

  const auto resolved = pack->ResolveTexture("Tex");
  ASSERT_TRUE(resolved.has_value());
  EXPECT_EQ(resolved->format, augusta::assets::TextureFormat::kBC5);
  EXPECT_EQ(resolved->dds_bytes, dds_bytes);
}

// A scenario's Lua scripts ride in its server pack (ADR-0031, ADR-0039) as text,
// addressed by their path relative to the scenario's folder.
TEST_F(PackTest, EncodesAndResolvesAScriptBlob) {
  const auto pack_path = MakePackPath("augusta_assets_test_script_blob.pack");
  const auto keys = GenerateEd25519KeyPair();
  const std::string script = "return { stamina = {} }\n-- utf-8: \xC3\xA7\xC3\xA3o\n";

  const auto blob = augusta::assets::EncodeScriptBlob(script);
  ASSERT_TRUE(blob.has_value());
  const std::vector<augusta::assets::AssetEntry> entries = {
      augusta::assets::AssetEntry{.type = augusta::assets::AssetType::kScript, .path = "parameters.lua", .data = *blob},
      augusta::assets::AssetEntry{
          .type = augusta::assets::AssetType::kScript, .path = "rules/round.lua", .data = *blob},
  };
  ASSERT_TRUE(augusta::assets::WritePack(pack_path, entries, keys.private_key).has_value());

  auto pack = augusta::assets::Pack::Load(pack_path, keys.public_key);
  ASSERT_TRUE(pack.has_value());

  EXPECT_EQ(pack->ResolveScript(augusta::assets::kParametersScriptPath).value(), script);
  EXPECT_EQ(pack->ResolveScript("rules/round.lua").value(), script);
}

TEST_F(PackTest, AnEmptyScriptResolvesToEmptyText) {
  const auto pack_path = MakePackPath("augusta_assets_test_empty_script.pack");
  const auto keys = GenerateEd25519KeyPair();
  const std::vector<augusta::assets::AssetEntry> entries = {
      augusta::assets::AssetEntry{.type = augusta::assets::AssetType::kScript, .path = "empty.lua", .data = {}},
  };
  ASSERT_TRUE(augusta::assets::WritePack(pack_path, entries, keys.private_key).has_value());

  auto pack = augusta::assets::Pack::Load(pack_path, keys.public_key);
  ASSERT_TRUE(pack.has_value());

  EXPECT_EQ(pack->ResolveScript("empty.lua").value(), "");
}

TEST_F(PackTest, AScriptThatIsNotThereOrIsSomethingElseIsAResolveError) {
  const auto pack_path = MakePackPath("augusta_assets_test_script_errors.pack");
  const auto keys = GenerateEd25519KeyPair();
  const std::vector<augusta::assets::AssetEntry> entries = {
      augusta::assets::AssetEntry{
          .type = augusta::assets::AssetType::kMesh, .path = "parameters.lua", .data = MakeTriangleMeshBlob()},
  };
  ASSERT_TRUE(augusta::assets::WritePack(pack_path, entries, keys.private_key).has_value());
  auto pack = augusta::assets::Pack::Load(pack_path, keys.public_key);
  ASSERT_TRUE(pack.has_value());

  EXPECT_EQ(pack->ResolveScript("missing.lua").error(), augusta::assets::ResolveError::kNotFound);
  EXPECT_EQ(pack->ResolveScript("parameters.lua").error(), augusta::assets::ResolveError::kTypeMismatch);
}

TEST_F(PackTest, AScriptLargerThanTheLimitIsTooLargeToEncodeAndCorruptToResolve) {
  const std::string huge(2U * 1024 * 1024, 'x');
  const auto blob = augusta::assets::EncodeScriptBlob(huge);
  ASSERT_FALSE(blob.has_value());
  EXPECT_EQ(blob.error(), augusta::assets::EncodeError::kTooLarge);

  // A hostile pack can still carry one: the reader refuses it too.
  const auto pack_path = MakePackPath("augusta_assets_test_huge_script.pack");
  const auto keys = GenerateEd25519KeyPair();
  const std::vector<augusta::assets::AssetEntry> entries = {
      augusta::assets::AssetEntry{.type = augusta::assets::AssetType::kScript,
                                  .path = "huge.lua",
                                  .data = std::vector<std::byte>(huge.size(), std::byte{'x'})},
  };
  ASSERT_TRUE(augusta::assets::WritePack(pack_path, entries, keys.private_key).has_value());
  auto pack = augusta::assets::Pack::Load(pack_path, keys.public_key);
  ASSERT_TRUE(pack.has_value());

  EXPECT_EQ(pack->ResolveScript("huge.lua").error(), augusta::assets::ResolveError::kCorruptBlob);
}

// A scenario's character list rides in both packs (ADR-0042): the character
// index N names element N-1, so the order the cooker wrote is the order resolved.
TEST_F(PackTest, EncodesAndResolvesTheCharacterListInOrder) {
  const auto pack_path = MakePackPath("augusta_assets_test_characters.pack");
  const auto keys = GenerateEd25519KeyPair();
  const std::vector<std::string> characters = {"characters/sniper", "characters/player", "characters/medic"};

  const auto blob = augusta::assets::EncodeCharactersBlob(characters);
  ASSERT_TRUE(blob.has_value());
  const std::vector<augusta::assets::AssetEntry> entries = {
      augusta::assets::AssetEntry{.type = augusta::assets::AssetType::kCharacters,
                                  .path = std::string(augusta::assets::kCharactersPath),
                                  .data = *blob},
  };
  ASSERT_TRUE(augusta::assets::WritePack(pack_path, entries, keys.private_key).has_value());
  auto pack = augusta::assets::Pack::Load(pack_path, keys.public_key);
  ASSERT_TRUE(pack.has_value());

  EXPECT_EQ(pack->ResolveCharacters().value(), characters);
}

// A character's eye is where the local player's camera sits (ADR-0040): a bare
// point, resolved by path, and only as an eye.
TEST_F(PackTest, EncodesAndResolvesACharactersEye) {
  const auto pack_path = MakePackPath("augusta_assets_test_eye.pack");
  const auto keys = GenerateEd25519KeyPair();

  const auto blob = augusta::assets::EncodeEyeBlob({.position = Vec3(0.0F, 1.6F, 0.1F)});
  ASSERT_TRUE(blob.has_value());
  const std::vector<augusta::assets::AssetEntry> entries = {
      augusta::assets::AssetEntry{
          .type = augusta::assets::AssetType::kEye, .path = "characters/player/Character/Eye", .data = *blob},
  };
  ASSERT_TRUE(augusta::assets::WritePack(pack_path, entries, keys.private_key).has_value());
  auto pack = augusta::assets::Pack::Load(pack_path, keys.public_key);
  ASSERT_TRUE(pack.has_value());

  const auto eye = pack->ResolveEye("characters/player/Character/Eye");
  ASSERT_TRUE(eye.has_value());
  EXPECT_EQ(eye->position, Vec3(0.0F, 1.6F, 0.1F));
  EXPECT_EQ(pack->ResolveMesh("characters/player/Character/Eye").error(), augusta::assets::ResolveError::kTypeMismatch);
  EXPECT_EQ(pack->ResolveEye("characters/medic/Character/Eye").error(), augusta::assets::ResolveError::kNotFound);
}

TEST_F(PackTest, AnEmptyCharacterListResolvesAsEmpty) {
  const auto pack_path = MakePackPath("augusta_assets_test_no_characters.pack");
  const auto keys = GenerateEd25519KeyPair();

  const auto blob = augusta::assets::EncodeCharactersBlob({});
  ASSERT_TRUE(blob.has_value());
  const std::vector<augusta::assets::AssetEntry> entries = {
      augusta::assets::AssetEntry{.type = augusta::assets::AssetType::kCharacters,
                                  .path = std::string(augusta::assets::kCharactersPath),
                                  .data = *blob},
  };
  ASSERT_TRUE(augusta::assets::WritePack(pack_path, entries, keys.private_key).has_value());
  auto pack = augusta::assets::Pack::Load(pack_path, keys.public_key);
  ASSERT_TRUE(pack.has_value());

  const auto resolved = pack->ResolveCharacters();
  ASSERT_TRUE(resolved.has_value());
  EXPECT_TRUE(resolved->empty());
}

TEST_F(PackTest, APackWithoutTheCharacterListIsAResolveError) {
  const auto pack_path = MakePackPath("augusta_assets_test_missing_characters.pack");
  const auto keys = GenerateEd25519KeyPair();
  const std::vector<augusta::assets::AssetEntry> entries = {
      augusta::assets::AssetEntry{
          .type = augusta::assets::AssetType::kMesh, .path = "Mesh", .data = MakeTriangleMeshBlob()},
  };
  ASSERT_TRUE(augusta::assets::WritePack(pack_path, entries, keys.private_key).has_value());
  auto pack = augusta::assets::Pack::Load(pack_path, keys.public_key);
  ASSERT_TRUE(pack.has_value());

  EXPECT_EQ(pack->ResolveCharacters().error(), augusta::assets::ResolveError::kNotFound);
}

// A character index is one byte with zero never valid (ADR-0042), so a list
// longer than kMaxCharacters could name a character no index can reach.
TEST_F(PackTest, ACharacterListLongerThanTheLimitIsTooLargeToEncodeAndCorruptToResolve) {
  const std::vector<std::string> too_many(augusta::assets::kMaxCharacters + 1, "characters/player");
  const auto blob = augusta::assets::EncodeCharactersBlob(too_many);
  ASSERT_FALSE(blob.has_value());
  EXPECT_EQ(blob.error(), augusta::assets::EncodeError::kTooLarge);

  // A hostile pack can still carry one: the reader refuses it too. Built from a
  // one-character blob: its u32 count patched to kMaxCharacters + 1, then its one
  // encoded string repeated that many times.
  const auto one = augusta::assets::EncodeCharactersBlob(std::vector<std::string>{"characters/player"});
  ASSERT_TRUE(one.has_value());
  const std::span<const std::byte> count_prefix = std::span(*one).first(sizeof(std::uint32_t));
  const std::span<const std::byte> encoded_string = std::span(*one).subspan(sizeof(std::uint32_t));
  std::vector<std::byte> hostile(count_prefix.begin(), count_prefix.end());
  static_assert(augusta::assets::kMaxCharacters + 1 == 0x100);
  hostile[0] = std::byte{0};  // Little-endian 0x100.
  hostile[1] = std::byte{1};
  for (std::size_t i = 0; i < too_many.size(); ++i) {
    hostile.insert(hostile.end(), encoded_string.begin(), encoded_string.end());
  }
  const auto pack_path = MakePackPath("augusta_assets_test_too_many_characters.pack");
  const auto keys = GenerateEd25519KeyPair();
  const std::vector<augusta::assets::AssetEntry> entries = {
      augusta::assets::AssetEntry{.type = augusta::assets::AssetType::kCharacters,
                                  .path = std::string(augusta::assets::kCharactersPath),
                                  .data = hostile},
  };
  ASSERT_TRUE(augusta::assets::WritePack(pack_path, entries, keys.private_key).has_value());
  auto pack = augusta::assets::Pack::Load(pack_path, keys.public_key);
  ASSERT_TRUE(pack.has_value());

  EXPECT_EQ(pack->ResolveCharacters().error(), augusta::assets::ResolveError::kCorruptBlob);
}

// A pack's hash names one cook of it: the trailer's BLAKE3 hash, which the
// server pack carries for the client pack cooked with it.
TEST_F(PackTest, APacksHashIsItsTrailersHash) {
  const auto pack_path = MakePackPath("augusta_assets_test_hash.pack");
  const auto keys = GenerateEd25519KeyPair();
  const std::vector<augusta::assets::AssetEntry> entries = {
      augusta::assets::AssetEntry{
          .type = augusta::assets::AssetType::kMesh, .path = "Mesh", .data = MakeTriangleMeshBlob()},
  };
  ASSERT_TRUE(augusta::assets::WritePack(pack_path, entries, keys.private_key).has_value());
  auto pack = augusta::assets::Pack::Load(pack_path, keys.public_key);
  ASSERT_TRUE(pack.has_value());

  // The trailer is the hash, then its 64-byte Ed25519 signature, at the end of the file.
  constexpr std::size_t kSignatureSize = 64;
  const auto bytes = ReadFileBytes(pack_path);
  const auto trailer_hash =
      std::span(bytes).last(augusta::assets::kPackHashSize + kSignatureSize).first(augusta::assets::kPackHashSize);
  EXPECT_TRUE(std::ranges::equal(pack->Hash(), trailer_hash));
}

TEST_F(PackTest, AServerPackResolvesTheHashOfItsClientPack) {
  const auto keys = GenerateEd25519KeyPair();
  const auto client_path = MakePackPath("augusta_assets_test_client.pack");
  ASSERT_TRUE(augusta::assets::WritePack(
                  client_path,
                  {augusta::assets::AssetEntry{
                      .type = augusta::assets::AssetType::kMesh, .path = "Mesh", .data = MakeTriangleMeshBlob()}},
                  keys.private_key)
                  .has_value());
  const auto client = augusta::assets::Pack::Load(client_path, keys.public_key);
  ASSERT_TRUE(client.has_value());

  const auto server_path = MakePackPath("augusta_assets_test_server.pack");
  const std::vector<augusta::assets::AssetEntry> entries = {
      augusta::assets::AssetEntry{.type = augusta::assets::AssetType::kClientPack,
                                  .path = std::string(augusta::assets::kClientPackPath),
                                  .data = std::vector<std::byte>(client->Hash().begin(), client->Hash().end())},
  };
  ASSERT_TRUE(augusta::assets::WritePack(server_path, entries, keys.private_key).has_value());
  const auto server = augusta::assets::Pack::Load(server_path, keys.public_key);
  ASSERT_TRUE(server.has_value());

  EXPECT_EQ(server->ResolveClientPackHash().value(), client->Hash());
}

TEST_F(PackTest, AClientPackHashThatIsMissingOrOfAnotherSizeIsAResolveError) {
  const auto keys = GenerateEd25519KeyPair();
  const auto missing_path = MakePackPath("augusta_assets_test_no_client_pack.pack");
  ASSERT_TRUE(augusta::assets::WritePack(
                  missing_path,
                  {augusta::assets::AssetEntry{
                      .type = augusta::assets::AssetType::kMesh, .path = "Mesh", .data = MakeTriangleMeshBlob()}},
                  keys.private_key)
                  .has_value());
  const auto missing = augusta::assets::Pack::Load(missing_path, keys.public_key);
  ASSERT_TRUE(missing.has_value());
  EXPECT_EQ(missing->ResolveClientPackHash().error(), augusta::assets::ResolveError::kNotFound);

  for (const std::size_t size : {augusta::assets::kPackHashSize - 1, augusta::assets::kPackHashSize + 1}) {
    const auto path = MakePackPath("augusta_assets_test_short_client_pack.pack");
    ASSERT_TRUE(
        augusta::assets::WritePack(path,
                                   {augusta::assets::AssetEntry{.type = augusta::assets::AssetType::kClientPack,
                                                                .path = std::string(augusta::assets::kClientPackPath),
                                                                .data = std::vector<std::byte>(size)}},
                                   keys.private_key)
            .has_value());
    const auto pack = augusta::assets::Pack::Load(path, keys.public_key);
    ASSERT_TRUE(pack.has_value());
    EXPECT_EQ(pack->ResolveClientPackHash().error(), augusta::assets::ResolveError::kCorruptBlob) << size;
  }
}

// The remaining tests exercise Pack::Load's fail-closed parsing directly,
// by mutating the bytes of an otherwise validly written and signed pack -
// every case here is one ParsePackHeader (or the top-level size check)
// rejects before Load() ever computes/verifies the trailer, so none of
// these need a re-signed file to behave deterministically.
class PackLoadNegativeTest : public PackTest {
 protected:
  void SetUp() override {
    keys_ = GenerateEd25519KeyPair();
    const std::vector<augusta::assets::AssetEntry> entries = {
        augusta::assets::AssetEntry{augusta::assets::AssetType::kMesh, "TestMesh", MakeTriangleMeshBlob()},
    };
    const auto valid_path = MakePackPath("augusta_assets_test_valid_source.pack");
    const auto written = augusta::assets::WritePack(valid_path, entries, keys_.private_key);
    ASSERT_TRUE(written.has_value());
    valid_bytes_ = ReadFileBytes(valid_path);
    ASSERT_FALSE(valid_bytes_.empty());
  }

  augusta::assets::Ed25519KeyPair keys_;
  std::vector<std::byte> valid_bytes_;
};

TEST_F(PackLoadNegativeTest, RejectsBadMagic) {
  auto bytes = valid_bytes_;
  bytes[0] = std::byte{'X'};
  const auto path = MakePackPath("augusta_assets_test_bad_magic.pack");
  WriteFileBytes(path, bytes);

  const auto pack = augusta::assets::Pack::Load(path, keys_.public_key);
  ASSERT_FALSE(pack.has_value());
  EXPECT_EQ(pack.error(), augusta::assets::LoadError::kBadMagic);
}

TEST_F(PackLoadNegativeTest, RejectsUnsupportedVersion) {
  auto bytes = valid_bytes_;
  // Version is the u32 right after the 4-byte magic, little-endian.
  bytes[4] = std::byte{99};
  const auto path = MakePackPath("augusta_assets_test_bad_version.pack");
  WriteFileBytes(path, bytes);

  const auto pack = augusta::assets::Pack::Load(path, keys_.public_key);
  ASSERT_FALSE(pack.has_value());
  EXPECT_EQ(pack.error(), augusta::assets::LoadError::kUnsupportedVersion);
}

TEST_F(PackLoadNegativeTest, RejectsFileTooShortForHeaderAndTrailer) {
  const std::vector<std::byte> bytes(valid_bytes_.begin(), valid_bytes_.begin() + 10);
  const auto path = MakePackPath("augusta_assets_test_truncated_tiny.pack");
  WriteFileBytes(path, bytes);

  const auto pack = augusta::assets::Pack::Load(path, keys_.public_key);
  ASSERT_FALSE(pack.has_value());
  EXPECT_EQ(pack.error(), augusta::assets::LoadError::kTruncated);
}

TEST_F(PackLoadNegativeTest, RejectsIndexOffsetPastTruncatedContent) {
  // Short enough that the header's own (unchanged) index_offset now
  // claims to point past this file's much smaller hashed_length.
  const std::vector<std::byte> bytes(valid_bytes_.begin(), valid_bytes_.begin() + 40);
  const auto path = MakePackPath("augusta_assets_test_truncated_mid.pack");
  WriteFileBytes(path, bytes);

  const auto pack = augusta::assets::Pack::Load(path, keys_.public_key);
  ASSERT_FALSE(pack.has_value());
  EXPECT_EQ(pack.error(), augusta::assets::LoadError::kTruncated);
}

TEST_F(PackLoadNegativeTest, RejectsCorruptedContentAsHashMismatch) {
  auto bytes = valid_bytes_;
  // Flip a byte inside the data section (right after the fixed-size
  // header) - the trailer's stored hash still reflects the original
  // content, so this must be caught as a mismatch rather than silently
  // loading corrupted mesh data.
  bytes[30] ^= std::byte{0xFF};
  const auto path = MakePackPath("augusta_assets_test_corrupted.pack");
  WriteFileBytes(path, bytes);

  const auto pack = augusta::assets::Pack::Load(path, keys_.public_key);
  ASSERT_FALSE(pack.has_value());
  EXPECT_EQ(pack.error(), augusta::assets::LoadError::kHashMismatch);
}

TEST_F(PackLoadNegativeTest, RejectsWrongPublicKeyAsSignatureInvalid) {
  const auto path = MakePackPath("augusta_assets_test_wrong_key.pack");
  WriteFileBytes(path, valid_bytes_);

  const auto other_keys = GenerateEd25519KeyPair();
  const auto pack = augusta::assets::Pack::Load(path, other_keys.public_key);
  ASSERT_FALSE(pack.has_value());
  EXPECT_EQ(pack.error(), augusta::assets::LoadError::kSignatureInvalid);
}

TEST(ComputeWorldTransformsTest, ChainsEachNodeThroughItsParents) {
  augusta::assets::SceneData scene;
  augusta::assets::SceneNode root;
  root.name = "Root";
  root.translation = augusta::math::Vec3(1.0F, 0.0F, 0.0F);
  augusta::assets::SceneNode child;
  child.name = "Root/Child";
  child.parent_index = 0;
  child.translation = augusta::math::Vec3(0.0F, 2.0F, 0.0F);
  augusta::assets::SceneNode grandchild;
  grandchild.name = "Root/Child/Grandchild";
  grandchild.parent_index = 1;
  grandchild.scale = augusta::math::Vec3(2.0F, 2.0F, 2.0F);
  scene.nodes = {root, child, grandchild};

  const auto world = augusta::assets::ComputeWorldTransforms(scene);

  ASSERT_EQ(world.size(), 3U);
  const augusta::math::Vec3 origin(0.0F, 0.0F, 0.0F);
  const augusta::math::Vec3 one(1.0F, 1.0F, 1.0F);
  EXPECT_NEAR(augusta::math::Length(augusta::math::TransformPoint(world[0], origin) - augusta::math::Vec3(1, 0, 0)),
              0.0F, 1e-5F);
  EXPECT_NEAR(augusta::math::Length(augusta::math::TransformPoint(world[1], origin) - augusta::math::Vec3(1, 2, 0)),
              0.0F, 1e-5F);
  // The grandchild's own scale applies to its points, on top of its parents' offsets.
  EXPECT_NEAR(augusta::math::Length(augusta::math::TransformPoint(world[2], one) - augusta::math::Vec3(3, 4, 2)), 0.0F,
              1e-5F);
}

TEST(ComputeWorldTransformsTest, ANodeWithNoParentIsItsOwnRoot) {
  augusta::assets::SceneData scene;
  augusta::assets::SceneNode node;
  node.name = "Only";
  node.translation = augusta::math::Vec3(0.0F, 5.0F, 0.0F);
  scene.nodes = {node};

  const auto world = augusta::assets::ComputeWorldTransforms(scene);

  ASSERT_EQ(world.size(), 1U);
  EXPECT_NEAR(augusta::math::TranslationOf(world[0]).y, 5.0F, 1e-5F);
}

}  // namespace
