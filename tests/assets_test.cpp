#include "augusta/assets.h"

#include <algorithm>
#include <array>
#include <cstddef>
#include <cstdint>
#include <expected>
#include <filesystem>
#include <fstream>
#include <ios>
#include <limits>
#include <optional>
#include <span>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

#include <gtest/gtest.h>

#include "augusta/math.h"
#include "augusta/pack_decoder.h"
#include "augusta/pack_encoder.h"

// Unit tests for augusta_assets, including its pack encoder
// (augusta/pack_encoder.h's Encode*/WritePack). Links only augusta_assets - no
// USD/libktx/meshoptimizer - so it builds and runs under the plain
// runtime presets (windows/linux/sanitizers). These tests pin down
// augusta_assets' wire format, the one the cooker (tools/pack, ADR-0030)
// writes through its binding of this module.
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
// Requirements: NFR-08
TEST_F(ReadEd25519PublicKeyFileTest, RoundTripsAValidKeyFile) {
  const auto keys = GenerateEd25519KeyPair();
  const auto path = MakePath("augusta_assets_test_valid.pub");
  WriteFileBytes(path, keys.public_key.data(), keys.public_key.size());

  const auto read_key = ReadEd25519PublicKeyFile(path);
  ASSERT_TRUE(read_key.has_value());
  EXPECT_EQ(*read_key, keys.public_key);
}

// Requirements: NFR-08
TEST_F(ReadEd25519PublicKeyFileTest, RejectsMissingFile) {
  const auto result = ReadEd25519PublicKeyFile(MakePath("augusta_assets_test_does_not_exist.pub"));
  ASSERT_FALSE(result.has_value());
  EXPECT_EQ(result.error(), ReadKeyFileError::kIoError);
}

// Requirements: NFR-08
TEST_F(ReadEd25519PublicKeyFileTest, RejectsTruncatedFile) {
  const auto path = MakePath("augusta_assets_test_truncated.pub");
  const std::vector<std::byte> short_bytes(Ed25519PublicKey{}.size() - 1);
  WriteFileBytes(path, short_bytes.data(), short_bytes.size());

  const auto result = ReadEd25519PublicKeyFile(path);
  ASSERT_FALSE(result.has_value());
  EXPECT_EQ(result.error(), ReadKeyFileError::kIoError);
}

// Requirements: NFR-08
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

  // Named after the running test too: ctest runs every test in a process of
  // its own, possibly in parallel, so no other test removes it while in use.
  std::filesystem::path MakePackPath(std::string_view name) {
    const ::testing::TestInfo& test = *::testing::UnitTest::GetInstance()->current_test_info();
    std::string unique = std::string("augusta_assets_test_") + test.test_suite_name() + "_" + test.name() + "_" +
                         std::string(name) + std::string(augusta::assets::kPackExtension);
    std::ranges::replace(unique, '/', '_');
    const auto path = std::filesystem::temp_directory_path() / unique;
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

// Requirements: US-22
TEST_F(PackTest, WritePackRejectsDuplicatePaths) {
  const auto blob = MakeTriangleMeshBlob();
  ASSERT_FALSE(blob.empty());

  const std::vector<augusta::assets::AssetEntry> entries = {
      augusta::assets::AssetEntry{augusta::assets::AssetType::kMesh, "Dup", blob},
      augusta::assets::AssetEntry{augusta::assets::AssetType::kMesh, "Dup", blob},
  };

  const auto keys = GenerateEd25519KeyPair();
  const auto pack_path = MakePackPath("augusta_assets_test_dup");
  const auto result = augusta::assets::WritePack(pack_path, entries, keys.private_key);
  ASSERT_FALSE(result.has_value());
  EXPECT_EQ(result.error(), augusta::assets::WriteError::kDuplicatePath);
}

// The KTX2 file identifier (ADR-0031): the twelve bytes every KTX2 file starts
// with.
constexpr std::array<std::uint8_t, 12> kKtx2Identifier = {0xAB, 0x4B, 0x54, 0x58, 0x20, 0x32,
                                                          0x30, 0xBB, 0x0D, 0x0A, 0x1A, 0x0A};

// The VkFormats a texture blob's KTX2 may carry (ADR-0017), and one it may not.
constexpr std::uint32_t kVkFormatBc4UnormBlock = 139;
constexpr std::uint32_t kVkFormatBc5UnormBlock = 141;
constexpr std::uint32_t kVkFormatBc7UnormBlock = 145;
constexpr std::uint32_t kVkFormatBc7SrgbBlock = 146;

// The start of a KTX2 file: its identifier, then vk_format as the header's
// first field, then a few bytes standing in for the rest of the file.
std::vector<std::byte> MakeKtx2(std::uint32_t vk_format) {
  std::vector<std::byte> bytes;
  for (const std::uint8_t byte : kKtx2Identifier) {
    bytes.push_back(std::byte{byte});
  }
  for (std::size_t shift = 0; shift < 32; shift += 8) {
    bytes.push_back(static_cast<std::byte>((vk_format >> shift) & 0xFFU));
  }
  bytes.insert(bytes.end(), {std::byte{0x01}, std::byte{0xAB}, std::byte{0xCD}});
  return bytes;
}

// Exercises augusta_assets' own texture-blob encode/write/resolve seam
// directly (WritePack/Pack::Load/ResolveTexture) - actually encoding an image
// to KTX2 (libktx) happens in tools/pack, which this module has no dependency
// on.
class TextureBlobTest : public PackTest {
 protected:
  // Writes a pack holding ktx2 as the texture "Tex" and resolves it.
  std::expected<augusta::assets::TextureData, augusta::assets::ResolveError> WriteAndResolve(
      const std::vector<std::byte>& ktx2) {
    const auto pack_path = MakePackPath("augusta_assets_test_texture_blob");
    const auto keys = GenerateEd25519KeyPair();
    const auto blob = augusta::assets::EncodeTextureBlob(ktx2);
    EXPECT_TRUE(blob.has_value());
    const std::vector<augusta::assets::AssetEntry> entries = {
        augusta::assets::AssetEntry{.type = augusta::assets::AssetType::kTexture, .path = "Tex", .data = *blob},
    };
    EXPECT_TRUE(augusta::assets::WritePack(pack_path, entries, keys.private_key).has_value());
    auto pack = augusta::assets::Pack::Load(pack_path, keys.public_key);
    EXPECT_TRUE(pack.has_value());
    return pack->ResolveTexture("Tex");
  }
};

// Requirements: US-22
TEST_F(TextureBlobTest, ResolvesEachSupportedVkFormatToItsBlockFormat) {
  const std::array<std::pair<std::uint32_t, augusta::assets::TextureFormat>, 3> cases = {{
      {kVkFormatBc7SrgbBlock, augusta::assets::TextureFormat::kBC7},
      {kVkFormatBc5UnormBlock, augusta::assets::TextureFormat::kBC5},
      {kVkFormatBc4UnormBlock, augusta::assets::TextureFormat::kBC4},
  }};
  for (const auto& [vk_format, format] : cases) {
    SCOPED_TRACE(vk_format);
    const std::vector<std::byte> ktx2 = MakeKtx2(vk_format);

    const auto resolved = WriteAndResolve(ktx2);

    ASSERT_TRUE(resolved.has_value());
    EXPECT_EQ(resolved->format, format);
    EXPECT_EQ(resolved->ktx2_bytes, ktx2);
  }
}

// Requirements: NFR-12
TEST_F(TextureBlobTest, RefusesABlobWithoutTheKtx2Identifier) {
  std::vector<std::byte> dds = MakeKtx2(kVkFormatBc7SrgbBlock);
  std::ranges::copy(std::array{std::byte{'D'}, std::byte{'D'}, std::byte{'S'}, std::byte{' '}}, dds.begin());

  EXPECT_EQ(WriteAndResolve(dds).error(), augusta::assets::ResolveError::kCorruptBlob);
}

// Requirements: NFR-12
TEST_F(TextureBlobTest, RefusesABlobTooShortForAKtx2Header) {
  std::vector<std::byte> truncated = MakeKtx2(kVkFormatBc7SrgbBlock);
  truncated.resize(kKtx2Identifier.size() + 3);

  EXPECT_EQ(WriteAndResolve(truncated).error(), augusta::assets::ResolveError::kCorruptBlob);
}

// Requirements: NFR-12
TEST_F(TextureBlobTest, RefusesAKtx2OfAnotherVkFormat) {
  EXPECT_EQ(WriteAndResolve(MakeKtx2(kVkFormatBc7UnormBlock)).error(), augusta::assets::ResolveError::kCorruptBlob);
}

// A scenario's Lua scripts ride in its server pack (ADR-0031, ADR-0039) as text,
// addressed by their path relative to the scenario's folder.
// Requirements: US-22
TEST_F(PackTest, EncodesAndResolvesAScriptBlob) {
  const auto pack_path = MakePackPath("augusta_assets_test_script_blob");
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

// Requirements: US-22
TEST_F(PackTest, AnEmptyScriptResolvesToEmptyText) {
  const auto pack_path = MakePackPath("augusta_assets_test_empty_script");
  const auto keys = GenerateEd25519KeyPair();
  const std::vector<augusta::assets::AssetEntry> entries = {
      augusta::assets::AssetEntry{.type = augusta::assets::AssetType::kScript, .path = "empty.lua", .data = {}},
  };
  ASSERT_TRUE(augusta::assets::WritePack(pack_path, entries, keys.private_key).has_value());

  auto pack = augusta::assets::Pack::Load(pack_path, keys.public_key);
  ASSERT_TRUE(pack.has_value());

  EXPECT_EQ(pack->ResolveScript("empty.lua").value(), "");
}

// Requirements: US-22
TEST_F(PackTest, AScriptThatIsNotThereOrIsSomethingElseIsAResolveError) {
  const auto pack_path = MakePackPath("augusta_assets_test_script_errors");
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

// Requirements: NFR-12
TEST_F(PackTest, AScriptLargerThanTheLimitIsTooLargeToEncodeAndCorruptToResolve) {
  const std::string huge(2U * 1024 * 1024, 'x');
  const auto blob = augusta::assets::EncodeScriptBlob(huge);
  ASSERT_FALSE(blob.has_value());
  EXPECT_EQ(blob.error(), augusta::assets::EncodeError::kTooLarge);

  // A hostile pack can still carry one: the reader refuses it too.
  const auto pack_path = MakePackPath("augusta_assets_test_huge_script");
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
// Requirements: US-16, US-22
TEST_F(PackTest, EncodesAndResolvesTheCharacterListInOrder) {
  const auto pack_path = MakePackPath("augusta_assets_test_characters");
  const auto keys = GenerateEd25519KeyPair();
  const std::vector<std::string> characters = {"sniper", "soldier", "medic"};

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

// A cue's sound is mono PCM (ADR-0020): resolved by path with its sample rate,
// its sample width and its samples as the WAV file held them, and only as audio.
// Requirements: US-22
TEST_F(PackTest, EncodesAndResolvesASound) {
  const auto pack_path = MakePackPath("augusta_assets_test_audio");
  const auto keys = GenerateEd25519KeyPair();
  const std::vector<std::byte> samples = {std::byte{0x01}, std::byte{0x00}, std::byte{0xFF}, std::byte{0x7F}};

  const auto blob = augusta::assets::EncodeAudioBlob({.sample_rate = 22050, .bits_per_sample = 16, .samples = samples});
  ASSERT_TRUE(blob.has_value());
  const std::vector<augusta::assets::AssetEntry> entries = {
      augusta::assets::AssetEntry{
          .type = augusta::assets::AssetType::kAudio, .path = "sounds/test/gunshot", .data = *blob},
  };
  ASSERT_TRUE(augusta::assets::WritePack(pack_path, entries, keys.private_key).has_value());
  auto pack = augusta::assets::Pack::Load(pack_path, keys.public_key);
  ASSERT_TRUE(pack.has_value());

  const auto sound = pack->ResolveAudio("sounds/test/gunshot");
  ASSERT_TRUE(sound.has_value());
  EXPECT_EQ(sound->sample_rate, 22050U);
  EXPECT_EQ(sound->bits_per_sample, 16U);
  EXPECT_EQ(sound->samples, samples);
  EXPECT_EQ(pack->ResolveMesh("sounds/test/gunshot").error(), augusta::assets::ResolveError::kTypeMismatch);
  EXPECT_EQ(pack->ResolveAudio("sounds/test/death").error(), augusta::assets::ResolveError::kNotFound);
}

// Only whole samples of a width PCM has are a sound: anything else is corrupt.
// Requirements: NFR-12
TEST_F(PackTest, ASoundWithAnUnknownSampleWidthOrAPartialSampleIsCorrupt) {
  const auto pack_path = MakePackPath("augusta_assets_test_corrupt_audio");
  const auto keys = GenerateEd25519KeyPair();
  // sample rate 22050, then the bits per sample, then 3 sample bytes.
  const auto blob = [](std::uint8_t bits_per_sample) {
    return std::vector<std::byte>{
        std::byte{0x22}, std::byte{0x56}, std::byte{0x00}, std::byte{0x00}, std::byte{bits_per_sample},
        std::byte{0x03}, std::byte{0x00}, std::byte{0x00}, std::byte{0x00}, std::byte{0x01},
        std::byte{0x02}, std::byte{0x03}};
  };
  const std::vector<augusta::assets::AssetEntry> entries = {
      augusta::assets::AssetEntry{.type = augusta::assets::AssetType::kAudio, .path = "width12", .data = blob(12)},
      augusta::assets::AssetEntry{.type = augusta::assets::AssetType::kAudio, .path = "partial16", .data = blob(16)},
      augusta::assets::AssetEntry{.type = augusta::assets::AssetType::kAudio, .path = "whole24", .data = blob(24)},
  };
  ASSERT_TRUE(augusta::assets::WritePack(pack_path, entries, keys.private_key).has_value());
  auto pack = augusta::assets::Pack::Load(pack_path, keys.public_key);
  ASSERT_TRUE(pack.has_value());

  EXPECT_EQ(pack->ResolveAudio("width12").error(), augusta::assets::ResolveError::kCorruptBlob);
  EXPECT_EQ(pack->ResolveAudio("partial16").error(), augusta::assets::ResolveError::kCorruptBlob);
  EXPECT_TRUE(pack->ResolveAudio("whole24").has_value());
}

// The encoder refuses the sounds the decoder would find corrupt, so no cook
// writes one.
// Requirements: US-22
TEST(EncodeAudioBlobTest, RefusesASampleWidthPcmDoesNotHave) {
  const std::vector<std::byte> samples(6);

  const auto blob = augusta::assets::EncodeAudioBlob({.sample_rate = 22050, .bits_per_sample = 12, .samples = samples});

  ASSERT_FALSE(blob.has_value());
  EXPECT_EQ(blob.error(), augusta::assets::EncodeError::kUnsupportedSampleWidth);
}

// Requirements: US-22
TEST(EncodeAudioBlobTest, RefusesSamplesThatAreNotAWholeNumberOfSamples) {
  const std::vector<std::byte> samples(3);

  const auto partial =
      augusta::assets::EncodeAudioBlob({.sample_rate = 22050, .bits_per_sample = 16, .samples = samples});
  const auto whole =
      augusta::assets::EncodeAudioBlob({.sample_rate = 22050, .bits_per_sample = 24, .samples = samples});

  ASSERT_FALSE(partial.has_value());
  EXPECT_EQ(partial.error(), augusta::assets::EncodeError::kPartialSample);
  EXPECT_TRUE(whole.has_value());
}

// A cue's sound and each of its variants are addressed under the sounds prefix
// (ADR-0031), which the client and the cooker share.
// Requirements: US-22
TEST(CueSoundPathTest, ACuesSoundAndItsVariantsAreAddressedUnderTheSoundsPrefix) {
  EXPECT_EQ(augusta::assets::CueSoundPath("gunshot"), "sounds/gunshot");
  EXPECT_EQ(augusta::assets::CueVariantPath("gunshot", 0), "sounds/gunshot/0");
  EXPECT_EQ(augusta::assets::CueVariantPath("hit_marker", 12), "sounds/hit_marker/12");
}

// A cue's sound (ADR-0041) is its gain and its variants' paths, resolved by
// path and only as a cue sound.
// Requirements: US-22
TEST_F(PackTest, EncodesAndResolvesACueSound) {
  const auto pack_path = MakePackPath("augusta_assets_test_cue_sound");
  const auto keys = GenerateEd25519KeyPair();
  const augusta::assets::CueSoundData sound{.gain_db = -6.5F, .variants = {"sounds/gunshot/0", "sounds/gunshot/1"}};

  const auto blob = augusta::assets::EncodeCueSoundBlob(sound);
  ASSERT_TRUE(blob.has_value());
  const std::vector<augusta::assets::AssetEntry> entries = {
      augusta::assets::AssetEntry{
          .type = augusta::assets::AssetType::kCueSound, .path = "sounds/gunshot", .data = *blob},
  };
  ASSERT_TRUE(augusta::assets::WritePack(pack_path, entries, keys.private_key).has_value());
  auto pack = augusta::assets::Pack::Load(pack_path, keys.public_key);
  ASSERT_TRUE(pack.has_value());

  const auto resolved = pack->ResolveCueSound("sounds/gunshot");
  ASSERT_TRUE(resolved.has_value());
  EXPECT_EQ(resolved->gain_db, -6.5F);
  EXPECT_EQ(resolved->variants, sound.variants);
  EXPECT_EQ(pack->ResolveAudio("sounds/gunshot").error(), augusta::assets::ResolveError::kTypeMismatch);
  EXPECT_EQ(pack->ResolveCueSound("sounds/death").error(), augusta::assets::ResolveError::kNotFound);
}

// Every cue needs a sound (ADR-0041): one with no variant, or a gain that is
// not a number, is refused when encoded and corrupt when decoded.
// Requirements: US-22
TEST(EncodeCueSoundBlobTest, RefusesACueSoundWithNoVariantOrANonFiniteGain) {
  const auto no_variant = augusta::assets::EncodeCueSoundBlob({.gain_db = 0.0F, .variants = {}});
  const auto infinite = augusta::assets::EncodeCueSoundBlob(
      {.gain_db = std::numeric_limits<float>::infinity(), .variants = {"sounds/gunshot/0"}});
  const auto not_a_number = augusta::assets::EncodeCueSoundBlob(
      {.gain_db = std::numeric_limits<float>::quiet_NaN(), .variants = {"sounds/gunshot/0"}});

  ASSERT_FALSE(no_variant.has_value());
  EXPECT_EQ(no_variant.error(), augusta::assets::EncodeError::kInvalidCueSound);
  ASSERT_FALSE(infinite.has_value());
  EXPECT_EQ(infinite.error(), augusta::assets::EncodeError::kInvalidCueSound);
  ASSERT_FALSE(not_a_number.has_value());
  EXPECT_EQ(not_a_number.error(), augusta::assets::EncodeError::kInvalidCueSound);
}

// Requirements: NFR-12
TEST_F(PackTest, ACueSoundWithNoVariantOrANonFiniteGainIsCorrupt) {
  const auto pack_path = MakePackPath("augusta_assets_test_corrupt_cue_sound");
  const auto keys = GenerateEd25519KeyPair();
  // A gain (f32) of 0 dB, then the variant count, and no variant.
  const std::vector<std::byte> no_variant(8, std::byte{0x00});
  // A gain of +infinity (0x7F800000), then one variant: the path "a".
  const std::vector<std::byte> infinite = {std::byte{0x00}, std::byte{0x00}, std::byte{0x80}, std::byte{0x7F},
                                           std::byte{0x01}, std::byte{0x00}, std::byte{0x00}, std::byte{0x00},
                                           std::byte{0x01}, std::byte{0x00}, std::byte{0x00}, std::byte{0x00},
                                           std::byte{'a'}};
  const std::vector<augusta::assets::AssetEntry> entries = {
      augusta::assets::AssetEntry{.type = augusta::assets::AssetType::kCueSound, .path = "none", .data = no_variant},
      augusta::assets::AssetEntry{.type = augusta::assets::AssetType::kCueSound, .path = "infinite", .data = infinite},
  };
  ASSERT_TRUE(augusta::assets::WritePack(pack_path, entries, keys.private_key).has_value());
  auto pack = augusta::assets::Pack::Load(pack_path, keys.public_key);
  ASSERT_TRUE(pack.has_value());

  EXPECT_EQ(pack->ResolveCueSound("none").error(), augusta::assets::ResolveError::kCorruptBlob);
  EXPECT_EQ(pack->ResolveCueSound("infinite").error(), augusta::assets::ResolveError::kCorruptBlob);
}

// A character's eye is where the local player's camera sits (ADR-0040): a bare
// point, resolved by path, and only as an eye.
// Requirements: US-22
TEST(CharacterEyePathTest, ACharactersEyeIsTheEyeChildOfItsRootPrim) {
  EXPECT_EQ(augusta::assets::CharacterEyePath("soldier"), "soldier/Character/Eye");
}

// Requirements: US-17, US-22
TEST(CharacterMeshPathTest, ACharactersMeshIsTheVisualChildOfItsRootPrim) {
  EXPECT_EQ(augusta::assets::CharacterMeshPath("soldier"), "soldier/Character/Visual");
}

// Requirements: US-22
TEST_F(PackTest, EncodesAndResolvesACharactersEye) {
  const auto pack_path = MakePackPath("augusta_assets_test_eye");
  const auto keys = GenerateEd25519KeyPair();

  const auto blob = augusta::assets::EncodeEyeBlob({.position = Vec3(0.0F, 1.6F, 0.1F)});
  ASSERT_TRUE(blob.has_value());
  const std::vector<augusta::assets::AssetEntry> entries = {
      augusta::assets::AssetEntry{
          .type = augusta::assets::AssetType::kEye, .path = "soldier/Character/Eye", .data = *blob},
  };
  ASSERT_TRUE(augusta::assets::WritePack(pack_path, entries, keys.private_key).has_value());
  auto pack = augusta::assets::Pack::Load(pack_path, keys.public_key);
  ASSERT_TRUE(pack.has_value());

  const auto eye = pack->ResolveEye("soldier/Character/Eye");
  ASSERT_TRUE(eye.has_value());
  EXPECT_EQ(eye->position, Vec3(0.0F, 1.6F, 0.1F));
  EXPECT_EQ(pack->ResolveMesh("soldier/Character/Eye").error(), augusta::assets::ResolveError::kTypeMismatch);
  EXPECT_EQ(pack->ResolveEye("medic/Character/Eye").error(), augusta::assets::ResolveError::kNotFound);
}

// The rifle's markers (ADR-0064, ADR-0066): its Grip, Muzzle, Sight and
// Fore-end, each a frame in the rifle's space, found at the one path every
// scenario's rifle has.
// Requirements: US-22
TEST_F(PackTest, EncodesAndResolvesTheRiflesMarkers) {
  const auto pack_path = MakePackPath("augusta_assets_test_rifle");
  const auto keys = GenerateEd25519KeyPair();
  const augusta::math::Quat quarter_turn(0.70710677F, 0.0F, 0.70710677F, 0.0F);
  const augusta::assets::RifleData rifle{
      .grip = {.translation = Vec3(0.0F, -0.1F, 0.0F)},
      .muzzle = {.translation = Vec3(0.0F, 0.0F, -0.8F), .rotation = quarter_turn},
      .sight = {.translation = Vec3(0.0F, 0.07F, -0.2F)},
      .fore_end = {.translation = Vec3(0.0F, -0.05F, -0.4F)},
  };

  const auto blob = augusta::assets::EncodeRifleBlob(rifle);
  ASSERT_TRUE(blob.has_value());
  ASSERT_TRUE(augusta::assets::WritePack(pack_path,
                                         {{.type = augusta::assets::AssetType::kRifle,
                                           .path = std::string(augusta::assets::kRiflePath),
                                           .data = *blob}},
                                         keys.private_key)
                  .has_value());
  auto pack = augusta::assets::Pack::Load(pack_path, keys.public_key);
  ASSERT_TRUE(pack.has_value());

  const auto resolved = pack->ResolveRifle();
  ASSERT_TRUE(resolved.has_value());
  EXPECT_EQ(*resolved, rifle);
  EXPECT_EQ(pack->ResolveEye(augusta::assets::kRiflePath).error(), augusta::assets::ResolveError::kTypeMismatch);
}

// Requirements: US-22
TEST_F(PackTest, APackWithoutARifleResolvesNone) {
  const auto pack_path = MakePackPath("augusta_assets_test_no_rifle");
  const auto keys = GenerateEd25519KeyPair();
  ASSERT_TRUE(augusta::assets::WritePack(pack_path, {}, keys.private_key).has_value());
  auto pack = augusta::assets::Pack::Load(pack_path, keys.public_key);
  ASSERT_TRUE(pack.has_value());

  EXPECT_EQ(pack->ResolveRifle().error(), augusta::assets::ResolveError::kNotFound);
}

// Requirements: NFR-12
TEST_F(PackTest, ARifleMissingAMarkerIsCorrupt) {
  const auto pack_path = MakePackPath("augusta_assets_test_rifle_truncated");
  const auto keys = GenerateEd25519KeyPair();
  auto blob = augusta::assets::EncodeRifleBlob({}).value();
  // The Fore-end's rotation, the last of its fields, cut off.
  blob.resize(blob.size() - (4 * sizeof(float)));
  ASSERT_TRUE(augusta::assets::WritePack(pack_path,
                                         {{.type = augusta::assets::AssetType::kRifle,
                                           .path = std::string(augusta::assets::kRiflePath),
                                           .data = blob}},
                                         keys.private_key)
                  .has_value());
  auto pack = augusta::assets::Pack::Load(pack_path, keys.public_key);
  ASSERT_TRUE(pack.has_value());

  EXPECT_EQ(pack->ResolveRifle().error(), augusta::assets::ResolveError::kCorruptBlob);
}

// A character's weapon socket (ADR-0066): its right hand's Joint and its frame
// there, and its left hand's Joint, found under the character's path.
// Requirements: US-11, US-22
TEST_F(PackTest, EncodesAndResolvesAWeaponSocket) {
  const auto pack_path = MakePackPath("augusta_assets_test_weapon_socket");
  const auto keys = GenerateEd25519KeyPair();
  const augusta::assets::WeaponSocketData socket{
      .right_hand = "Root/Spine/RightArm/RightHand",
      .translation = Vec3(0.0F, -0.05F, -0.02F),
      .rotation = augusta::math::Quat(0.70710677F, 0.0F, 0.70710677F, 0.0F),
      .left_hand = "Root/Spine/LeftArm/LeftForearm/LeftHand",
  };

  const auto blob = augusta::assets::EncodeWeaponSocketBlob(socket);
  ASSERT_TRUE(blob.has_value());
  ASSERT_TRUE(augusta::assets::WritePack(pack_path,
                                         {{.type = augusta::assets::AssetType::kWeaponSocket,
                                           .path = augusta::assets::CharacterWeaponSocketPath("soldier"),
                                           .data = *blob}},
                                         keys.private_key)
                  .has_value());
  auto pack = augusta::assets::Pack::Load(pack_path, keys.public_key);
  ASSERT_TRUE(pack.has_value());

  const auto resolved = pack->ResolveWeaponSocket("soldier");
  ASSERT_TRUE(resolved.has_value());
  EXPECT_EQ(*resolved, socket);
  EXPECT_EQ(pack->ResolveWeaponSocket("medic").error(), augusta::assets::ResolveError::kNotFound);
}

// Requirements: NFR-12
TEST_F(PackTest, AWeaponSocketWithoutItsLeftHandIsCorrupt) {
  const auto pack_path = MakePackPath("augusta_assets_test_weapon_socket_truncated");
  const auto keys = GenerateEd25519KeyPair();
  auto blob = augusta::assets::EncodeWeaponSocketBlob({.right_hand = "Root", .left_hand = "Root/Arm/Hand"}).value();
  // The left hand's name cut short.
  blob.resize(blob.size() - 2);
  ASSERT_TRUE(augusta::assets::WritePack(pack_path,
                                         {{.type = augusta::assets::AssetType::kWeaponSocket,
                                           .path = augusta::assets::CharacterWeaponSocketPath("soldier"),
                                           .data = blob}},
                                         keys.private_key)
                  .has_value());
  auto pack = augusta::assets::Pack::Load(pack_path, keys.public_key);
  ASSERT_TRUE(pack.has_value());

  EXPECT_EQ(pack->ResolveWeaponSocket("soldier").error(), augusta::assets::ResolveError::kCorruptBlob);
}

// A character's hitboxes (ADR-0040, ADR-0061): each a body part, its Joint and
// its geometry, found together under the character's path, in path order.
class HitboxPackTest : public PackTest {
 protected:
  static augusta::assets::AssetEntry Hitbox(std::string path, augusta::assets::BodyPart part, float height) {
    const augusta::assets::HitboxData hitbox{
        .part = part,
        .joint = "Root/Spine",
        .mesh = {.points = {Vec3(0.0F, height, 0.0F), Vec3(1.0F, height, 0.0F), Vec3(0.0F, height, 1.0F)},
                 .indices = {0, 1, 2}},
    };
    const auto blob = augusta::assets::EncodeHitboxBlob(hitbox);
    EXPECT_TRUE(blob.has_value());
    return {.type = augusta::assets::AssetType::kHitbox,
            .path = std::move(path),
            .data = blob.value_or(std::vector<std::byte>{})};
  }

  // The Skeleton of the character named character: a root at its feet, and the
  // Root/Spine joint every Hitbox above moves with at spine.
  static augusta::assets::AssetEntry Skeleton(std::string_view character, Vec3 spine) {
    const augusta::assets::SkeletonData skeleton{
        .joints = {{.name = "Root"}, {.name = "Root/Spine", .parent = 0, .translation = spine}}};
    const auto blob = augusta::assets::EncodeSkeletonBlob(skeleton);
    EXPECT_TRUE(blob.has_value());
    return {.type = augusta::assets::AssetType::kSkeleton,
            .path = augusta::assets::CharacterSkeletonPath(character),
            .data = blob.value_or(std::vector<std::byte>{})};
  }

  augusta::assets::Pack Write(const std::vector<augusta::assets::AssetEntry>& entries) {
    // One file per test: ctest runs each in its own process, possibly at once.
    const auto pack_path = MakePackPath(std::string("augusta_assets_test_hitboxes_") +
                                        ::testing::UnitTest::GetInstance()->current_test_info()->name());
    const auto keys = GenerateEd25519KeyPair();
    EXPECT_TRUE(augusta::assets::WritePack(pack_path, entries, keys.private_key).has_value());
    auto pack = augusta::assets::Pack::Load(pack_path, keys.public_key);
    EXPECT_TRUE(pack.has_value());
    return *std::move(pack);
  }
};

// Requirements: US-11, US-22
TEST_F(HitboxPackTest, AHitboxResolvesToItsBodyPartJointAndGeometry) {
  const auto pack = Write({Hitbox("soldier/Character/Head", augusta::assets::BodyPart::kHead, 1.8F)});

  const auto hitbox = pack.ResolveHitbox("soldier/Character/Head");

  ASSERT_TRUE(hitbox.has_value());
  EXPECT_EQ(hitbox->part, augusta::assets::BodyPart::kHead);
  EXPECT_EQ(hitbox->joint, "Root/Spine");
  EXPECT_EQ(hitbox->mesh.points.size(), 3U);
  EXPECT_EQ(hitbox->mesh.points[0], Vec3(0.0F, 1.8F, 0.0F));
  EXPECT_EQ(hitbox->mesh.indices, (std::vector<std::uint32_t>{0, 1, 2}));
}

// Requirements: US-11, US-22
TEST_F(HitboxPackTest, ACharactersHitboxesResolveTogetherInPathOrderAndNoOtherCharacters) {
  const auto pack = Write({
      Hitbox("soldier/Character/Torso", augusta::assets::BodyPart::kTorso, 1.3F),
      Hitbox("medic/Character/Head", augusta::assets::BodyPart::kHead, 1.7F),
      Hitbox("soldier/Character/Head", augusta::assets::BodyPart::kHead, 1.8F),
      Hitbox("soldier/Character/Leg", augusta::assets::BodyPart::kLimb, 0.5F),
      // A character whose path the other's is a prefix of is another character.
      Hitbox("soldier2/Character/Head", augusta::assets::BodyPart::kHead, 1.9F),
  });

  const auto hitboxes = pack.ResolveHitboxes("soldier");

  ASSERT_TRUE(hitboxes.has_value());
  ASSERT_EQ(hitboxes->size(), 3U);
  // Head, Leg, Torso: the order of their paths.
  EXPECT_EQ((*hitboxes)[0].part, augusta::assets::BodyPart::kHead);
  EXPECT_EQ((*hitboxes)[0].mesh.points[0].y, 1.8F);
  EXPECT_EQ((*hitboxes)[1].part, augusta::assets::BodyPart::kLimb);
  EXPECT_EQ((*hitboxes)[2].part, augusta::assets::BodyPart::kTorso);
  EXPECT_TRUE(pack.ResolveHitboxes("sniper").value().empty());
}

// Requirements: NFR-12
TEST_F(HitboxPackTest, AHitboxOfAnUnknownBodyPartIsCorrupt) {
  auto entry = Hitbox("soldier/Character/Tail", augusta::assets::BodyPart::kLimb, 0.9F);
  entry.data.front() = std::byte{3};
  const auto pack = Write({entry});

  EXPECT_EQ(pack.ResolveHitbox("soldier/Character/Tail").error(), augusta::assets::ResolveError::kCorruptBlob);
  EXPECT_EQ(pack.ResolveHitboxes("soldier").error(), augusta::assets::ResolveError::kCorruptBlob);
}

// A character's hitbox is cooked in its Joint's space (ADR-0061), and resolves
// as it was cooked: placing it is its Joint's business.
// Requirements: US-11, US-22
TEST_F(HitboxPackTest, ACharactersHitboxesResolveInTheirJointsSpace) {
  const auto pack = Write({
      Skeleton("soldier", Vec3(0.0F, 1.0F, 0.0F)),
      Hitbox("soldier/Character/Head", augusta::assets::BodyPart::kHead, 0.8F),
  });

  const auto hitboxes = pack.ResolveHitboxes("soldier");

  ASSERT_TRUE(hitboxes.has_value());
  ASSERT_EQ(hitboxes->size(), 1U);
  EXPECT_EQ((*hitboxes)[0].mesh.points[1], Vec3(1.0F, 0.8F, 0.0F));
}

// A character's Skeleton (ADR-0061): Root at its feet, Root/Spine a metre up and
// turned a quarter about +Y, Root/Spine/Head half a metre along the Spine's +X.
class SkeletonPackTest : public HitboxPackTest {
 protected:
  static constexpr float kHalfSqrt2 = 0.70710678F;

  static augusta::assets::AssetEntry Soldier() {
    const augusta::assets::SkeletonData skeleton{
        .joints = {
            {.name = "Root"},
            {.name = "Root/Spine",
             .parent = 0,
             .translation = Vec3(0.0F, 1.0F, 0.0F),
             .rotation = augusta::math::Quat(kHalfSqrt2, 0.0F, kHalfSqrt2, 0.0F)},
            {.name = "Root/Spine/Head", .parent = 1, .translation = Vec3(0.5F, 0.0F, 0.0F)},
        }};
    const auto blob = augusta::assets::EncodeSkeletonBlob(skeleton);
    EXPECT_TRUE(blob.has_value());
    return {.type = augusta::assets::AssetType::kSkeleton,
            .path = augusta::assets::CharacterSkeletonPath("soldier"),
            .data = blob.value_or(std::vector<std::byte>{})};
  }

  // A Hitbox of part on the Joint named joint, a small triangle at its origin.
  static augusta::assets::HitboxData HitboxOn(std::string joint, augusta::assets::BodyPart part) {
    return {.part = part,
            .joint = std::move(joint),
            .mesh = {.points = {Vec3(0.0F), Vec3(0.1F, 0.0F, 0.0F), Vec3(0.0F, 0.1F, 0.0F)}, .indices = {0, 1, 2}}};
  }

  // The soldier's Skeleton, out of a pack holding it.
  augusta::assets::Skeleton ResolveSoldier() {
    const auto pack = Write({Soldier()});
    auto skeleton = pack.ResolveSkeleton("soldier");
    EXPECT_TRUE(skeleton.has_value());
    return skeleton.value_or(augusta::assets::Skeleton{});
  }
};

void ExpectNear(const Vec3& actual, const Vec3& expected) {
  constexpr float kTolerance = 1e-5F;
  EXPECT_NEAR(actual.x, expected.x, kTolerance);
  EXPECT_NEAR(actual.y, expected.y, kTolerance);
  EXPECT_NEAR(actual.z, expected.z, kTolerance);
}

// Requirements: US-11, US-22
TEST_F(SkeletonPackTest, ACharactersSkeletonResolvesItsJointsAndTheirParents) {
  const auto pack = Write({Soldier()});

  const auto skeleton = pack.ResolveSkeleton("soldier");

  ASSERT_TRUE(skeleton.has_value());
  ASSERT_EQ(skeleton->joints.size(), 3U);
  EXPECT_EQ(skeleton->joints[0].parent, augusta::assets::kJointNoParent);
  EXPECT_EQ(skeleton->joints[2].name, "Root/Spine/Head");
  EXPECT_EQ(skeleton->joints[2].parent, 1U);
}

// Each Joint's world transform is composed once, at load, from the local ones
// the pack stores: the Head sits where the turned Spine's +X points, -Z.
// Requirements: US-11, US-22
TEST_F(SkeletonPackTest, ACharactersSkeletonResolvesEachJointsRestTransformInTheCharactersSpace) {
  const auto skeleton = ResolveSoldier();

  ASSERT_EQ(skeleton.rest_transforms.size(), 3U);
  ExpectNear(augusta::math::TransformPoint(skeleton.rest_transforms[2], Vec3(0.0F)), Vec3(0.0F, 1.0F, -0.5F));
  ExpectNear(augusta::math::TransformPoint(skeleton.rest_transforms[2], Vec3(0.1F, 0.0F, 0.0F)),
             Vec3(0.0F, 1.0F, -0.6F));
}

// Requirements: US-22
TEST_F(SkeletonPackTest, ACharacterWithoutASkeletonResolvesNone) {
  const auto pack = Write({Soldier()});

  EXPECT_EQ(pack.ResolveSkeleton("medic").error(), augusta::assets::ResolveError::kNotFound);
}

// Every Joint name is resolved once, at load, into the Joint it names.
// Requirements: US-11, US-22
TEST_F(SkeletonPackTest, EachHitboxIsPairedWithTheJointItNames) {
  const auto skeleton = ResolveSoldier();

  const auto bound =
      augusta::assets::BindHitboxes(skeleton, {HitboxOn("Root/Spine/Head", augusta::assets::BodyPart::kHead),
                                               HitboxOn("Root/Spine", augusta::assets::BodyPart::kTorso)});

  ASSERT_TRUE(bound.has_value());
  ASSERT_EQ(bound->size(), 2U);
  EXPECT_EQ(skeleton.joints.at((*bound)[0].joint).name, "Root/Spine/Head");
  EXPECT_EQ((*bound)[0].hitbox.part, augusta::assets::BodyPart::kHead);
  EXPECT_EQ(skeleton.joints.at((*bound)[1].joint).name, "Root/Spine");
  EXPECT_EQ((*bound)[1].hitbox.part, augusta::assets::BodyPart::kTorso);
}

// A Joint name is its path within the Skeleton: a bare "Head" names no Joint.
// Requirements: US-22
TEST_F(SkeletonPackTest, AHitboxNamingAJointTheSkeletonLacksIsRefusedNamingThatJoint) {
  const auto skeleton = ResolveSoldier();

  const auto bound = augusta::assets::BindHitboxes(skeleton, {HitboxOn("Root/Spine", augusta::assets::BodyPart::kTorso),
                                                              HitboxOn("Head", augusta::assets::BodyPart::kHead)});

  ASSERT_FALSE(bound.has_value());
  EXPECT_EQ(bound.error().joint, "Head");
}

// Requirements: US-11, US-22
TEST(FirstMissingBodyPartTest, NamesTheFirstBodyPartNoHitboxStandsFor) {
  using augusta::assets::BodyPart;
  using augusta::assets::FirstMissingBodyPart;
  using augusta::assets::HitboxData;
  const std::vector<HitboxData> every{{.part = BodyPart::kLimb}, {.part = BodyPart::kHead}, {.part = BodyPart::kTorso}};
  const std::vector<HitboxData> no_torso{{.part = BodyPart::kLimb}, {.part = BodyPart::kHead}};
  const std::vector<HitboxData> only_limbs{{.part = BodyPart::kLimb}, {.part = BodyPart::kLimb}};

  EXPECT_EQ(FirstMissingBodyPart(every), std::nullopt);
  EXPECT_EQ(FirstMissingBodyPart(no_torso), BodyPart::kTorso);
  EXPECT_EQ(FirstMissingBodyPart(only_limbs), BodyPart::kHead);
  EXPECT_EQ(FirstMissingBodyPart({}), BodyPart::kHead);
}

// Requirements: US-11, US-22
TEST(CharacterAnimationPathsTest, ACharactersClipsAreAddressedUnderItsClipsPrim) {
  EXPECT_EQ(augusta::assets::CharacterAnimationSkeletonPath("soldier"), "soldier/Character/Skeleton.ozz");
  EXPECT_EQ(augusta::assets::CharacterClipPath("soldier", "standing/forward"),
            "soldier/Character/Clips/standing/forward");
  EXPECT_EQ(augusta::assets::CharacterStanceTransitionsPath("soldier"), "soldier/Character/Clips");
}

// Some bytes standing in for an ozz-animation archive, which augusta_assets
// carries without reading.
std::vector<std::byte> MakeArchive() { return {std::byte{0x6F}, std::byte{0x7A}, std::byte{0x7A}, std::byte{0x00}}; }

// Requirements: US-11, US-22
TEST(ClipBlobTest, AClipDecodesToItsStrideLengthFootContactsAndArchive) {
  using augusta::assets::Foot;
  const augusta::assets::ClipData clip{
      .stride_length = 1.4F,
      .foot_contacts = {{.foot = Foot::kLeft, .ratio = 0.0F}, {.foot = Foot::kRight, .ratio = 0.5F}},
      .archive = MakeArchive()};

  const auto blob = augusta::assets::EncodeClipBlob(clip);
  ASSERT_TRUE(blob.has_value());
  const auto decoded = augusta::assets::DecodeClipBlob(*blob);

  ASSERT_TRUE(decoded.has_value());
  EXPECT_FLOAT_EQ(decoded->stride_length, 1.4F);
  ASSERT_EQ(decoded->foot_contacts.size(), 2U);
  EXPECT_EQ(decoded->foot_contacts[0].foot, Foot::kLeft);
  EXPECT_FLOAT_EQ(decoded->foot_contacts[0].ratio, 0.0F);
  EXPECT_EQ(decoded->foot_contacts[1].foot, Foot::kRight);
  EXPECT_FLOAT_EQ(decoded->foot_contacts[1].ratio, 0.5F);
  EXPECT_EQ(decoded->archive, MakeArchive());
}

// Requirements: US-22, NFR-12
TEST(ClipBlobTest, AClipWithANegativeStrideOrAFootContactOutsideItIsRefused) {
  using augusta::assets::EncodeError;
  using augusta::assets::Foot;
  const augusta::assets::ClipData backwards{.stride_length = -1.0F, .foot_contacts = {}, .archive = MakeArchive()};
  const augusta::assets::ClipData late{
      .stride_length = 1.0F, .foot_contacts = {{.foot = Foot::kLeft, .ratio = 1.5F}}, .archive = MakeArchive()};
  const augusta::assets::ClipData infinite{
      .stride_length = std::numeric_limits<float>::infinity(), .foot_contacts = {}, .archive = MakeArchive()};

  EXPECT_EQ(augusta::assets::EncodeClipBlob(backwards).error(), EncodeError::kInvalidAnimation);
  EXPECT_EQ(augusta::assets::EncodeClipBlob(late).error(), EncodeError::kInvalidAnimation);
  EXPECT_EQ(augusta::assets::EncodeClipBlob(infinite).error(), EncodeError::kInvalidAnimation);
}

// Requirements: NFR-12
TEST(ClipBlobTest, AClipOfAnUnknownFootIsCorrupt) {
  auto blob =
      augusta::assets::EncodeClipBlob({.stride_length = 1.0F,
                                       .foot_contacts = {{.foot = augusta::assets::Foot::kLeft, .ratio = 0.25F}},
                                       .archive = MakeArchive()});
  ASSERT_TRUE(blob.has_value());
  // The stride length's f32, the contact count's u8, then the contact's foot.
  (*blob)[5] = std::byte{7};

  EXPECT_FALSE(augusta::assets::DecodeClipBlob(*blob).has_value());
}

// Requirements: US-11, US-22
TEST(StanceTransitionsBlobTest, EachPairOfStancesDecodesToItsDuration) {
  const auto blob = augusta::assets::EncodeStanceTransitionsBlob(
      {.standing_crouching = 0.25F, .standing_prone = 0.75F, .crouching_prone = 0.5F});
  ASSERT_TRUE(blob.has_value());

  const auto decoded = augusta::assets::DecodeStanceTransitionsBlob(*blob);

  ASSERT_TRUE(decoded.has_value());
  EXPECT_FLOAT_EQ(decoded->standing_crouching, 0.25F);
  EXPECT_FLOAT_EQ(decoded->standing_prone, 0.75F);
  EXPECT_FLOAT_EQ(decoded->crouching_prone, 0.5F);
}

// Requirements: US-22, NFR-12
TEST(StanceTransitionsBlobTest, ADurationThatIsNotPositiveIsRefused) {
  const auto blob = augusta::assets::EncodeStanceTransitionsBlob(
      {.standing_crouching = 0.25F, .standing_prone = 0.0F, .crouching_prone = 0.5F});

  EXPECT_EQ(blob.error(), augusta::assets::EncodeError::kInvalidAnimation);
}

// Requirements: US-11, US-22
TEST(AnimationSkeletonBlobTest, AnAnimationSkeletonDecodesToItsArchive) {
  const auto blob = augusta::assets::EncodeAnimationSkeletonBlob(MakeArchive());
  ASSERT_TRUE(blob.has_value());

  EXPECT_EQ(augusta::assets::DecodeAnimationSkeletonBlob(*blob), MakeArchive());
}

// A pack holding a character's animation loads, and resolves each part of it
// by the character's name and, for a Clip, its role.
// Requirements: US-11, US-22
TEST_F(PackTest, APackHoldingACharactersAnimationResolvesItsSkeletonClipsAndTransitions) {
  const auto pack_path = MakePackPath("augusta_assets_test_animation");
  const auto keys = GenerateEd25519KeyPair();
  const auto skeleton = augusta::assets::EncodeAnimationSkeletonBlob(MakeArchive());
  const auto clip =
      augusta::assets::EncodeClipBlob({.stride_length = 1.25F, .foot_contacts = {}, .archive = MakeArchive()});
  const auto transitions = augusta::assets::EncodeStanceTransitionsBlob(
      {.standing_crouching = 0.25F, .standing_prone = 0.75F, .crouching_prone = 0.5F});
  ASSERT_TRUE(skeleton.has_value() && clip.has_value() && transitions.has_value());
  const std::vector<augusta::assets::AssetEntry> entries = {
      {.type = augusta::assets::AssetType::kAnimationSkeleton,
       .path = augusta::assets::CharacterAnimationSkeletonPath("soldier"),
       .data = *skeleton},
      {.type = augusta::assets::AssetType::kClip,
       .path = augusta::assets::CharacterClipPath("soldier", "standing/idle"),
       .data = *clip},
      {.type = augusta::assets::AssetType::kStanceTransitions,
       .path = augusta::assets::CharacterStanceTransitionsPath("soldier"),
       .data = *transitions},
  };
  ASSERT_TRUE(augusta::assets::WritePack(pack_path, entries, keys.private_key).has_value());

  const auto pack = augusta::assets::Pack::Load(pack_path, keys.public_key);

  ASSERT_TRUE(pack.has_value());
  EXPECT_EQ(pack->Entries().size(), 3U);
  EXPECT_EQ(pack->ResolveAnimationSkeleton("soldier"), MakeArchive());
  const auto idle = pack->ResolveClip("soldier", "standing/idle");
  ASSERT_TRUE(idle.has_value());
  EXPECT_FLOAT_EQ(idle->stride_length, 1.25F);
  EXPECT_EQ(idle->archive, MakeArchive());
  const auto transitions_resolved = pack->ResolveStanceTransitions("soldier");
  ASSERT_TRUE(transitions_resolved.has_value());
  EXPECT_FLOAT_EQ(transitions_resolved->standing_prone, 0.75F);
  EXPECT_EQ(pack->ResolveClip("soldier", "air").error(), augusta::assets::ResolveError::kNotFound);
  EXPECT_EQ(pack->ResolveAnimationSkeleton("medic").error(), augusta::assets::ResolveError::kNotFound);
  EXPECT_EQ(pack->ResolveStanceTransitions("medic").error(), augusta::assets::ResolveError::kNotFound);
}

// Requirements: US-16, US-22
TEST_F(PackTest, AnEmptyCharacterListResolvesAsEmpty) {
  const auto pack_path = MakePackPath("augusta_assets_test_no_characters");
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

// Requirements: US-22
TEST_F(PackTest, APackWithoutTheCharacterListIsAResolveError) {
  const auto pack_path = MakePackPath("augusta_assets_test_missing_characters");
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

// kMaxCharacters bounds the character list a pack is read with (ADR-0042).
// Requirements: NFR-12
TEST_F(PackTest, ACharacterListLongerThanTheLimitIsTooLargeToEncodeAndCorruptToResolve) {
  const std::vector<std::string> too_many(augusta::assets::kMaxCharacters + 1, "soldier");
  const auto blob = augusta::assets::EncodeCharactersBlob(too_many);
  ASSERT_FALSE(blob.has_value());
  EXPECT_EQ(blob.error(), augusta::assets::EncodeError::kTooLarge);

  // A hostile pack can still carry one: the reader refuses it too. Built from a
  // one-character blob: its u32 count patched to kMaxCharacters + 1, then its one
  // encoded string repeated that many times.
  const auto one = augusta::assets::EncodeCharactersBlob(std::vector<std::string>{"soldier"});
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
  const auto pack_path = MakePackPath("augusta_assets_test_too_many_characters");
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
// Requirements: NFR-08
TEST_F(PackTest, APacksHashIsItsTrailersHash) {
  const auto pack_path = MakePackPath("augusta_assets_test_hash");
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

// The executables name the pack in every error about its content.
// Requirements: NFR-08
TEST_F(PackTest, APackRemembersThePathItWasLoadedFrom) {
  const auto pack_path = MakePackPath("augusta_assets_test_path");
  const auto keys = GenerateEd25519KeyPair();
  const std::vector<augusta::assets::AssetEntry> entries = {
      augusta::assets::AssetEntry{
          .type = augusta::assets::AssetType::kMesh, .path = "Mesh", .data = MakeTriangleMeshBlob()},
  };
  ASSERT_TRUE(augusta::assets::WritePack(pack_path, entries, keys.private_key).has_value());
  auto pack = augusta::assets::Pack::Load(pack_path, keys.public_key);
  ASSERT_TRUE(pack.has_value());

  EXPECT_EQ(pack->Path(), pack_path);
}

// Requirements: NFR-08
TEST_F(PackTest, LoadVerifiedPackLoadsAPackAgainstTheKeyInAFile) {
  const auto pack_path = MakePackPath("augusta_assets_test_verified");
  const auto key_path = MakePackPath("augusta_assets_test_verified.pub");
  const auto keys = GenerateEd25519KeyPair();
  const std::vector<augusta::assets::AssetEntry> entries = {
      augusta::assets::AssetEntry{
          .type = augusta::assets::AssetType::kMesh, .path = "Mesh", .data = MakeTriangleMeshBlob()},
  };
  ASSERT_TRUE(augusta::assets::WritePack(pack_path, entries, keys.private_key).has_value());
  WriteFileBytes(key_path, keys.public_key.data(), keys.public_key.size());

  const auto pack = augusta::assets::LoadVerifiedPack(pack_path, key_path);

  ASSERT_TRUE(pack.has_value());
  EXPECT_TRUE(pack->ResolveMesh("Mesh").has_value());
}

// Requirements: NFR-08
TEST_F(PackTest, LoadVerifiedPackRefusesAnUnreadableKey) {
  const auto pack = augusta::assets::LoadVerifiedPack(MakePackPath("augusta_assets_test_unread"),
                                                      MakePackPath("augusta_assets_test_missing.pub"));

  ASSERT_FALSE(pack.has_value());
  EXPECT_EQ(pack.error().failure, augusta::assets::VerifiedPackFailure::kPublicKeyUnreadable);
}

// Requirements: NFR-08
TEST_F(PackTest, LoadVerifiedPackRefusesAPackSignedByAnotherKey) {
  const auto pack_path = MakePackPath("augusta_assets_test_other_key");
  const auto key_path = MakePackPath("augusta_assets_test_other_key.pub");
  const auto keys = GenerateEd25519KeyPair();
  const auto other_keys = GenerateEd25519KeyPair();
  const std::vector<augusta::assets::AssetEntry> entries = {
      augusta::assets::AssetEntry{
          .type = augusta::assets::AssetType::kMesh, .path = "Mesh", .data = MakeTriangleMeshBlob()},
  };
  ASSERT_TRUE(augusta::assets::WritePack(pack_path, entries, keys.private_key).has_value());
  WriteFileBytes(key_path, other_keys.public_key.data(), other_keys.public_key.size());

  const auto pack = augusta::assets::LoadVerifiedPack(pack_path, key_path);

  ASSERT_FALSE(pack.has_value());
  EXPECT_EQ(pack.error().failure, augusta::assets::VerifiedPackFailure::kPackRejected);
  EXPECT_EQ(pack.error().load_error.code, augusta::assets::LoadErrorCode::kSignatureInvalid);
}

// Requirements: NFR-08
TEST_F(PackTest, APackNamesTheClientPackItWasWrittenWith) {
  const auto keys = GenerateEd25519KeyPair();
  const auto client_path = MakePackPath("augusta_assets_test_client");
  ASSERT_TRUE(augusta::assets::WritePack(
                  client_path,
                  {augusta::assets::AssetEntry{
                      .type = augusta::assets::AssetType::kMesh, .path = "Mesh", .data = MakeTriangleMeshBlob()}},
                  keys.private_key)
                  .has_value());
  const auto client = augusta::assets::Pack::Load(client_path, keys.public_key);
  ASSERT_TRUE(client.has_value());

  const auto server_path = MakePackPath("augusta_assets_test_server");
  ASSERT_TRUE(augusta::assets::WritePack(server_path, {}, keys.private_key, client->Hash()).has_value());
  const auto server = augusta::assets::Pack::Load(server_path, keys.public_key);
  ASSERT_TRUE(server.has_value());

  EXPECT_EQ(server->ClientPackHash(), client->Hash());
  EXPECT_EQ(client->ClientPackHash(), std::nullopt);
}

// The cooker names a client pack in its server pack by the hash WritePack
// returns, so it must be the one Load verifies.
// Requirements: NFR-08
TEST_F(PackTest, WritePackReturnsTheHashLoadVerifies) {
  const auto keys = GenerateEd25519KeyPair();
  const auto pack_path = MakePackPath("augusta_assets_test_returned_hash");
  const auto written = augusta::assets::WritePack(
      pack_path,
      {augusta::assets::AssetEntry{
          .type = augusta::assets::AssetType::kMesh, .path = "Mesh", .data = MakeTriangleMeshBlob()}},
      keys.private_key);
  ASSERT_TRUE(written.has_value());

  const auto pack = augusta::assets::Pack::Load(pack_path, keys.public_key);
  ASSERT_TRUE(pack.has_value());
  EXPECT_EQ(*written, pack->Hash());
}

// A pack's own bytes, as the layout read must find them: pack_format.h's
// kHeaderSize, and the 64-byte Ed25519 signature that ends the file.
constexpr std::uint64_t kWrittenHeaderSize = 4 + 4 + 8 + 8 + 4 + 1 + 32;
constexpr std::size_t kWrittenSignatureSize = 64;

// What a tool shows of a pack without a key (augusta-inspect): every field of
// its header, index and trailer, as WritePack wrote them.
// Requirements: NFR-08
TEST_F(PackTest, TheLayoutReadReturnsTheHeaderEntriesHashAndSignatureWritePackWrote) {
  const auto keys = GenerateEd25519KeyPair();
  const auto pack_path = MakePackPath("augusta_assets_test_layout");
  const auto mesh = MakeTriangleMeshBlob();
  const std::vector<std::byte> script = {std::byte{'x'}};
  const auto written = augusta::assets::WritePack(
      pack_path,
      {augusta::assets::AssetEntry{.type = augusta::assets::AssetType::kMesh, .path = "Mesh", .data = mesh},
       augusta::assets::AssetEntry{.type = augusta::assets::AssetType::kScript, .path = "rules.lua", .data = script}},
      keys.private_key);
  ASSERT_TRUE(written.has_value());
  const auto bytes = ReadFileBytes(pack_path);

  const auto layout = augusta::assets::ReadPackLayout(pack_path);

  ASSERT_TRUE(layout.has_value());
  EXPECT_EQ(layout->version, 7U);
  EXPECT_EQ(layout->file_size, bytes.size());
  EXPECT_EQ(layout->data_offset, kWrittenHeaderSize);
  EXPECT_EQ(layout->index_offset, kWrittenHeaderSize + mesh.size() + script.size());
  EXPECT_EQ(layout->client_pack, std::nullopt);
  ASSERT_EQ(layout->entries.size(), 2U);
  EXPECT_EQ(layout->entries[0].type, augusta::assets::AssetType::kMesh);
  EXPECT_EQ(layout->entries[0].path, "Mesh");
  EXPECT_EQ(layout->entries[0].offset, kWrittenHeaderSize);
  EXPECT_EQ(layout->entries[0].size, mesh.size());
  EXPECT_EQ(layout->entries[1].type, augusta::assets::AssetType::kScript);
  EXPECT_EQ(layout->entries[1].path, "rules.lua");
  EXPECT_EQ(layout->entries[1].offset, kWrittenHeaderSize + mesh.size());
  EXPECT_EQ(layout->entries[1].size, script.size());
  EXPECT_EQ(layout->hash, *written);
  EXPECT_TRUE(std::ranges::equal(layout->signature, std::span(bytes).last(kWrittenSignatureSize)));
}

// Requirements: NFR-08
TEST_F(PackTest, TheLayoutReadReturnsTheClientPackAServerPackNames) {
  const auto keys = GenerateEd25519KeyPair();
  const auto client_path = MakePackPath("augusta_assets_test_layout_client");
  const auto client_hash = augusta::assets::WritePack(client_path, {}, keys.private_key);
  ASSERT_TRUE(client_hash.has_value());
  const auto server_path = MakePackPath("augusta_assets_test_layout_server");
  ASSERT_TRUE(augusta::assets::WritePack(server_path, {}, keys.private_key, *client_hash).has_value());

  const auto layout = augusta::assets::ReadPackLayout(server_path);

  ASSERT_TRUE(layout.has_value());
  EXPECT_EQ(layout->client_pack, *client_hash);
  EXPECT_TRUE(layout->entries.empty());
}

// augusta-verify reports what it verified.
// Requirements: NFR-08
TEST_F(PackTest, APackListsTheEntriesItWasWrittenWith) {
  const auto keys = GenerateEd25519KeyPair();
  const auto pack_path = MakePackPath("augusta_assets_test_entries");
  const auto mesh = MakeTriangleMeshBlob();
  ASSERT_TRUE(augusta::assets::WritePack(
                  pack_path,
                  {augusta::assets::AssetEntry{.type = augusta::assets::AssetType::kMesh, .path = "Mesh", .data = mesh},
                   augusta::assets::AssetEntry{
                       .type = augusta::assets::AssetType::kCollision, .path = "Collider", .data = mesh}},
                  keys.private_key)
                  .has_value());

  const auto pack = augusta::assets::Pack::Load(pack_path, keys.public_key);

  ASSERT_TRUE(pack.has_value());
  const auto entries = pack->Entries();
  ASSERT_EQ(entries.size(), 2U);
  EXPECT_EQ(entries[0].type, augusta::assets::AssetType::kMesh);
  EXPECT_EQ(entries[0].path, "Mesh");
  EXPECT_EQ(entries[0].size, mesh.size());
  EXPECT_EQ(entries[1].type, augusta::assets::AssetType::kCollision);
  EXPECT_EQ(entries[1].path, "Collider");
  EXPECT_EQ(entries[1].offset, entries[0].offset + mesh.size());
}

// augusta-publish names the format a verified server pack was cooked in, and
// augusta-verify the size it verified.
// Requirements: NFR-08
TEST_F(PackTest, APackKnowsItsFormatVersionAndSize) {
  const auto keys = GenerateEd25519KeyPair();
  const auto pack_path = MakePackPath("augusta_assets_test_version");
  ASSERT_TRUE(augusta::assets::WritePack(pack_path, {}, keys.private_key).has_value());

  const auto pack = augusta::assets::Pack::Load(pack_path, keys.public_key);

  ASSERT_TRUE(pack.has_value());
  EXPECT_EQ(pack->Version(), 7U);
  EXPECT_EQ(pack->FileSize(), ReadFileBytes(pack_path).size());
}

// The remaining tests exercise Pack::Load's fail-closed parsing directly,
// by mutating the bytes of an otherwise validly written and signed pack -
// every case here is one ParsePackHeader (or the top-level size check)
// rejects before Load() ever computes/verifies the trailer, so none of
// these need a re-signed file to behave deterministically.
class PackLoadNegativeTest : public PackTest {
 protected:
  // pack_format.h's kHeaderSize and kTrailerSize: magic, version, data offset,
  // index offset, index count, flags and client pack hash; hash and signature.
  static constexpr std::size_t kHeaderSize = 4 + 4 + 8 + 8 + 4 + 1 + 32;
  static constexpr std::size_t kTrailerSize = 32 + 64;

  void SetUp() override {
    keys_ = GenerateEd25519KeyPair();
    const std::vector<augusta::assets::AssetEntry> entries = {
        augusta::assets::AssetEntry{augusta::assets::AssetType::kMesh, "TestMesh", MakeTriangleMeshBlob()},
    };
    const auto valid_path = MakePackPath("augusta_assets_test_valid_source");
    const auto written = augusta::assets::WritePack(valid_path, entries, keys_.private_key);
    ASSERT_TRUE(written.has_value());
    valid_bytes_ = ReadFileBytes(valid_path);
    ASSERT_FALSE(valid_bytes_.empty());
  }

  augusta::assets::Ed25519KeyPair keys_;
  std::vector<std::byte> valid_bytes_;
};

// Requirements: NFR-08, NFR-12
TEST_F(PackLoadNegativeTest, RejectsBadMagic) {
  auto bytes = valid_bytes_;
  bytes[0] = std::byte{'X'};
  const auto path = MakePackPath("augusta_assets_test_bad_magic");
  WriteFileBytes(path, bytes);

  const auto pack = augusta::assets::Pack::Load(path, keys_.public_key);
  ASSERT_FALSE(pack.has_value());
  EXPECT_EQ(pack.error().code, augusta::assets::LoadErrorCode::kBadMagic);
}

// A version-2 pack holds DDS textures (ADR-0031): refused, not misread, and
// named by both versions, so whoever reads it can tell whether the pack or the
// build is the stale one.
// Requirements: NFR-08, NFR-12
TEST_F(PackLoadNegativeTest, RejectsAVersion2Pack) {
  auto bytes = valid_bytes_;
  bytes[4] = std::byte{2};
  const auto path = MakePackPath("augusta_assets_test_version_2");
  WriteFileBytes(path, bytes);

  const auto pack = augusta::assets::Pack::Load(path, keys_.public_key);
  ASSERT_FALSE(pack.has_value());
  EXPECT_EQ(pack.error().code, augusta::assets::LoadErrorCode::kUnsupportedVersion);
  EXPECT_EQ(augusta::assets::DescribeLoadError(pack.error()), "is pack format 2; this build reads format 7");
}

// A version-3 pack has no Skeleton (ADR-0061): refused at load by the version
// error, not loaded only to fail at a character's first Hitbox.
// Requirements: NFR-08, NFR-12
TEST_F(PackLoadNegativeTest, RejectsAVersion3Pack) {
  auto bytes = valid_bytes_;
  bytes[4] = std::byte{3};
  const auto path = MakePackPath("augusta_assets_test_version_3");
  WriteFileBytes(path, bytes);

  const auto pack = augusta::assets::Pack::Load(path, keys_.public_key);
  ASSERT_FALSE(pack.has_value());
  EXPECT_EQ(pack.error().code, augusta::assets::LoadErrorCode::kUnsupportedVersion);
  EXPECT_EQ(augusta::assets::DescribeLoadError(pack.error()), "is pack format 3; this build reads format 7");
}

// A version-4 pack has no Clips (ADR-0066): refused at load by the version
// error, so a server never starts on a Character it cannot pose.
// Requirements: NFR-08, NFR-12
TEST_F(PackLoadNegativeTest, RejectsAVersion4Pack) {
  auto bytes = valid_bytes_;
  bytes[4] = std::byte{4};
  const auto path = MakePackPath("augusta_assets_test_version_4");
  WriteFileBytes(path, bytes);

  const auto pack = augusta::assets::Pack::Load(path, keys_.public_key);
  ASSERT_FALSE(pack.has_value());
  EXPECT_EQ(pack.error().code, augusta::assets::LoadErrorCode::kUnsupportedVersion);
  EXPECT_EQ(augusta::assets::DescribeLoadError(pack.error()), "is pack format 4; this build reads format 7");
}

// A version-5 pack has no rifle (ADR-0064): refused at load by the version
// error, not loaded only to fail when the server resolves its rifle.
// Requirements: NFR-08, NFR-12
TEST_F(PackLoadNegativeTest, RejectsAVersion5Pack) {
  auto bytes = valid_bytes_;
  bytes[4] = std::byte{5};
  const auto path = MakePackPath("augusta_assets_test_version_5");
  WriteFileBytes(path, bytes);

  const auto pack = augusta::assets::Pack::Load(path, keys_.public_key);
  ASSERT_FALSE(pack.has_value());
  EXPECT_EQ(pack.error().code, augusta::assets::LoadErrorCode::kUnsupportedVersion);
  EXPECT_EQ(augusta::assets::DescribeLoadError(pack.error()), "is pack format 5; this build reads format 7");
}

// A version-6 pack has no weapon socket or Fore-end (ADR-0066): refused at load
// by the version error, not loaded only to fail when the server resolves them.
// Requirements: NFR-08, NFR-12
TEST_F(PackLoadNegativeTest, RejectsAVersion6Pack) {
  auto bytes = valid_bytes_;
  bytes[4] = std::byte{6};
  const auto path = MakePackPath("augusta_assets_test_version_6");
  WriteFileBytes(path, bytes);

  const auto pack = augusta::assets::Pack::Load(path, keys_.public_key);
  ASSERT_FALSE(pack.has_value());
  EXPECT_EQ(pack.error().code, augusta::assets::LoadErrorCode::kUnsupportedVersion);
  EXPECT_EQ(augusta::assets::DescribeLoadError(pack.error()), "is pack format 6; this build reads format 7");
}

// Requirements: NFR-08, NFR-12
TEST_F(PackLoadNegativeTest, RejectsUnsupportedVersion) {
  auto bytes = valid_bytes_;
  // Version is the u32 right after the 4-byte magic, little-endian.
  bytes[4] = std::byte{99};
  const auto path = MakePackPath("augusta_assets_test_bad_version");
  WriteFileBytes(path, bytes);

  const auto pack = augusta::assets::Pack::Load(path, keys_.public_key);
  ASSERT_FALSE(pack.has_value());
  EXPECT_EQ(pack.error().code, augusta::assets::LoadErrorCode::kUnsupportedVersion);
}

// Requirements: NFR-08, NFR-12
TEST_F(PackLoadNegativeTest, RejectsFileTooShortForHeaderAndTrailer) {
  const std::vector<std::byte> bytes(valid_bytes_.begin(), valid_bytes_.begin() + 10);
  const auto path = MakePackPath("augusta_assets_test_truncated_tiny");
  WriteFileBytes(path, bytes);

  const auto pack = augusta::assets::Pack::Load(path, keys_.public_key);
  ASSERT_FALSE(pack.has_value());
  EXPECT_EQ(pack.error().code, augusta::assets::LoadErrorCode::kTruncated);
}

// Requirements: NFR-08, NFR-12
TEST_F(PackLoadNegativeTest, RejectsIndexOffsetPastTruncatedContent) {
  // Short enough that the header's own (unchanged) index_offset now
  // claims to point past this file's much smaller hashed_length.
  const std::vector<std::byte> bytes(valid_bytes_.begin(), valid_bytes_.begin() + kHeaderSize + kTrailerSize + 1);
  const auto path = MakePackPath("augusta_assets_test_truncated_mid");
  WriteFileBytes(path, bytes);

  const auto pack = augusta::assets::Pack::Load(path, keys_.public_key);
  ASSERT_FALSE(pack.has_value());
  EXPECT_EQ(pack.error().code, augusta::assets::LoadErrorCode::kTruncated);
}

// Requirements: NFR-08, NFR-12
TEST_F(PackLoadNegativeTest, RejectsCorruptedContentAsHashMismatch) {
  auto bytes = valid_bytes_;
  // Flip a byte inside the data section (right after the fixed-size
  // header) - the trailer's stored hash still reflects the original
  // content, so this must be caught as a mismatch rather than silently
  // loading corrupted mesh data.
  bytes[kHeaderSize + 2] ^= std::byte{0xFF};
  const auto path = MakePackPath("augusta_assets_test_corrupted");
  WriteFileBytes(path, bytes);

  const auto pack = augusta::assets::Pack::Load(path, keys_.public_key);
  ASSERT_FALSE(pack.has_value());
  EXPECT_EQ(pack.error().code, augusta::assets::LoadErrorCode::kHashMismatch);
}

// Requirements: NFR-08, NFR-12
TEST_F(PackLoadNegativeTest, RejectsAClientPackHashItsHeaderDoesNotFlag) {
  auto bytes = valid_bytes_;
  // The hash's first byte, right after the flags byte, which is clear.
  bytes[kHeaderSize - 32] = std::byte{1};
  const auto path = MakePackPath("augusta_assets_test_unflagged_hash");
  WriteFileBytes(path, bytes);

  const auto pack = augusta::assets::Pack::Load(path, keys_.public_key);
  ASSERT_FALSE(pack.has_value());
  EXPECT_EQ(pack.error().code, augusta::assets::LoadErrorCode::kTruncated);
}

// Requirements: NFR-08, NFR-12
TEST_F(PackLoadNegativeTest, RejectsAHeaderFlagNoWriterSets) {
  auto bytes = valid_bytes_;
  bytes[kHeaderSize - 33] = std::byte{0x02};
  const auto path = MakePackPath("augusta_assets_test_unknown_flag");
  WriteFileBytes(path, bytes);

  const auto pack = augusta::assets::Pack::Load(path, keys_.public_key);
  ASSERT_FALSE(pack.has_value());
  EXPECT_EQ(pack.error().code, augusta::assets::LoadErrorCode::kTruncated);
}

// Requirements: NFR-08, NFR-12
TEST_F(PackLoadNegativeTest, RejectsWrongPublicKeyAsSignatureInvalid) {
  const auto path = MakePackPath("augusta_assets_test_wrong_key");
  WriteFileBytes(path, valid_bytes_);

  const auto other_keys = GenerateEd25519KeyPair();
  const auto pack = augusta::assets::Pack::Load(path, other_keys.public_key);
  ASSERT_FALSE(pack.has_value());
  EXPECT_EQ(pack.error().code, augusta::assets::LoadErrorCode::kSignatureInvalid);
}

// The layout read refuses what Pack::Load refuses, with the same LoadError.
class PackLayoutNegativeTest : public PackLoadNegativeTest {
 protected:
  std::expected<augusta::assets::PackLayout, augusta::assets::LoadError> ReadLayoutOf(
      std::span<const std::byte> bytes) {
    const auto path = MakePackPath("augusta_assets_test_layout_negative");
    WriteFileBytes(path, bytes);
    return augusta::assets::ReadPackLayout(path);
  }
};

// Requirements: NFR-12
TEST_F(PackLayoutNegativeTest, RejectsBadMagic) {
  auto bytes = valid_bytes_;
  bytes[0] = std::byte{'X'};

  const auto layout = ReadLayoutOf(bytes);

  ASSERT_FALSE(layout.has_value());
  EXPECT_EQ(layout.error().code, augusta::assets::LoadErrorCode::kBadMagic);
}

// Requirements: NFR-12
TEST_F(PackLayoutNegativeTest, RejectsUnsupportedVersion) {
  auto bytes = valid_bytes_;
  bytes[4] = std::byte{99};

  const auto layout = ReadLayoutOf(bytes);

  ASSERT_FALSE(layout.has_value());
  EXPECT_EQ(layout.error().code, augusta::assets::LoadErrorCode::kUnsupportedVersion);
}

// Requirements: NFR-12
TEST_F(PackLayoutNegativeTest, RejectsATruncatedFile) {
  const std::vector<std::byte> bytes(valid_bytes_.begin(), valid_bytes_.begin() + kHeaderSize + kTrailerSize + 1);

  const auto layout = ReadLayoutOf(bytes);

  ASSERT_FALSE(layout.has_value());
  EXPECT_EQ(layout.error().code, augusta::assets::LoadErrorCode::kTruncated);
}

// Requirements: NFR-12
TEST_F(PackLayoutNegativeTest, RejectsAnIndexEntryOfAnUnknownType) {
  auto bytes = valid_bytes_;
  // The only entry's type byte opens the index, after its one mesh blob.
  bytes[kHeaderSize + MakeTriangleMeshBlob().size()] = std::byte{0xFF};

  const auto layout = ReadLayoutOf(bytes);

  ASSERT_FALSE(layout.has_value());
  EXPECT_EQ(layout.error().code, augusta::assets::LoadErrorCode::kInvalidIndex);
}

// No key, no verification: a pack modified after signing still shows its
// layout, which is how augusta-inspect looks inside a pack that fails to verify.
// Requirements: NFR-08
TEST_F(PackLayoutNegativeTest, ReadsAPackWhoseContentNoLongerMatchesItsHash) {
  auto bytes = valid_bytes_;
  bytes[kHeaderSize + 2] ^= std::byte{0xFF};

  const auto layout = ReadLayoutOf(bytes);

  ASSERT_TRUE(layout.has_value());
  ASSERT_EQ(layout->entries.size(), 1U);
  EXPECT_EQ(layout->entries[0].path, "TestMesh");
}

// Requirements: US-22
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

// Requirements: US-22
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
