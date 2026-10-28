#include "pack_load_target.h"

#include <array>
#include <cstddef>
#include <cstdint>
#include <cstdlib>
#include <expected>
#include <filesystem>
#include <fstream>
#include <ios>
#include <random>
#include <span>
#include <string>
#include <string_view>
#include <system_error>
#include <utility>
#include <vector>

#include <blake3.h>
#include <sodium.h>

#include "augusta/assets.h"
#include "augusta/clips.h"

namespace augusta::pack_load_fuzz {

namespace {

// The example scenario's paths (tests/fixtures/example-packs) and the texture
// seed's, each resolved as every type: a resolver that meets a path of another
// type is exercised too.
constexpr std::array<std::string_view, 13> kPaths = {
    "Scene",
    "Root/Floor/Visual",
    "Root/Floor/Collider",
    "Root/Spawn",
    "soldier/Character/Visual",
    "soldier/Character/Collider",
    "soldier/Character/Eye",
    "soldier/Character/HeadHitbox",
    assets::kParametersScriptPath,
    "rules.lua",
    "sounds/gunshot",
    "sounds/gunshot/0",
    kTexturePath,
};

// The example scenario's Character, read when the pack's Characters list does
// not resolve.
constexpr std::string_view kCharacter = "soldier";

// Clips resolved on their own, beside the ones LoadClips resolves, so the
// clip-blob decoder is reached even when the ozz Skeleton before them is broken.
constexpr std::array<std::string_view, 2> kClipRoles = {"standing/idle", "air"};

template <typename Key>
Key ReadKey(const std::filesystem::path& path) {
  Key key{};
  std::ifstream in(path, std::ios::binary);
  in.read(reinterpret_cast<char*>(key.data()), static_cast<std::streamsize>(key.size()));
  if (!in) {
    std::abort();
  }
  return key;
}

// The file Load reads: one per process, rewritten for every input, since
// libFuzzer's -fork and -jobs run several processes side by side.
struct ScratchFile {
  ScratchFile() {
    std::random_device random;
    path = std::filesystem::temp_directory_path() /
           ("augusta_pack_load_fuzz_" + std::to_string(random()) + std::string(assets::kPackExtension));
  }
  ScratchFile(const ScratchFile&) = delete;
  ScratchFile& operator=(const ScratchFile&) = delete;
  ~ScratchFile() {
    std::error_code ignored;
    std::filesystem::remove(path, ignored);
  }

  std::filesystem::path path;
};

// The pack's trailer (ADR-0031): the BLAKE3 hash of everything before it, then
// the Ed25519 signature of that hash.
std::vector<std::byte> Sign(std::span<const std::uint8_t> data, const assets::Ed25519PrivateKey& key) {
  std::array<std::uint8_t, assets::kPackHashSize> hash{};
  blake3_hasher hasher;
  blake3_hasher_init(&hasher);
  blake3_hasher_update(&hasher, data.data(), data.size());
  blake3_hasher_finalize(&hasher, hash.data(), hash.size());

  std::array<std::uint8_t, crypto_sign_BYTES> signature{};
  crypto_sign_detached(signature.data(), nullptr, hash.data(), hash.size(),
                       reinterpret_cast<const unsigned char*>(key.data()));

  std::vector<std::byte> trailer;
  for (const std::uint8_t byte : hash) {
    trailer.push_back(static_cast<std::byte>(byte));
  }
  for (const std::uint8_t byte : signature) {
    trailer.push_back(static_cast<std::byte>(byte));
  }
  return trailer;
}

// Reads the Character named character out of pack as the server's content
// loading does (scenario's ReadCharacters): its Hitboxes bound to its
// Skeleton, its eye, and its Clips through ozz-animation's archives. Each
// animation part is resolved on its own too, since LoadClips stops at the first
// that does not load. True when its Clips loaded.
bool LoadCharacter(const assets::Pack& pack, std::string_view character) {
  (void)pack.ResolveAnimationSkeleton(character);
  for (const std::string_view role : kClipRoles) {
    (void)pack.ResolveClip(character, role);
  }
  (void)pack.ResolveStanceTransitions(character);
  (void)pack.ResolveWeaponSocket(character);
  (void)pack.ResolveEye(assets::CharacterEyePath(character));

  auto hitboxes = pack.ResolveHitboxes(character);
  if (hitboxes) {
    (void)assets::FirstMissingBodyPart(*hitboxes);
  }
  const auto skeleton = pack.ResolveSkeleton(character);
  if (!skeleton) {
    return false;
  }
  if (hitboxes) {
    (void)assets::BindHitboxes(*skeleton, *std::move(hitboxes));
  }
  return animation::LoadClips(pack, character, *skeleton).has_value();
}

}  // namespace

const TestKey& GlobalTestKey() {
  static const TestKey key{
      .public_key = ReadKey<assets::Ed25519PublicKey>(std::filesystem::path(AUGUSTA_EXAMPLE_PACKS) / "test.pub"),
      .private_key = ReadKey<assets::Ed25519PrivateKey>(std::filesystem::path(AUGUSTA_EXAMPLE_PACKS) / "test.key"),
  };
  return key;
}

std::expected<assets::Pack, assets::LoadError> LoadSigned(std::span<const std::uint8_t> unsigned_pack) {
  static const ScratchFile scratch;
  if (sodium_init() < 0) {
    std::abort();
  }

  const std::vector<std::byte> trailer = Sign(unsigned_pack, GlobalTestKey().private_key);
  {
    std::ofstream out(scratch.path, std::ios::binary | std::ios::trunc);
    out.write(reinterpret_cast<const char*>(unsigned_pack.data()), static_cast<std::streamsize>(unsigned_pack.size()));
    out.write(reinterpret_cast<const char*>(trailer.data()), static_cast<std::streamsize>(trailer.size()));
    if (!out) {
      std::abort();
    }
  }
  return assets::Pack::Load(scratch.path, GlobalTestKey().public_key);
}

Reached ResolveEverything(const assets::Pack& pack) {
  for (const std::string_view path : kPaths) {
    (void)pack.ResolveMesh(path);
    (void)pack.ResolveScene(path);
    (void)pack.ResolveTexture(path);
    (void)pack.ResolveCollision(path);
    (void)pack.ResolveHitbox(path);
    (void)pack.ResolveSpawnPoint(path);
    (void)pack.ResolveEye(path);
    (void)pack.ResolveScript(path);
    (void)pack.ResolveAudio(path);
    (void)pack.ResolveCueSound(path);
  }
  (void)pack.ResolveRifle();
  (void)pack.ClientPackHash();

  // The Characters the server loads, or the example's when the list does not
  // resolve, so a broken list still leaves the animation blobs reachable.
  const std::vector<std::string> characters =
      pack.ResolveCharacters().value_or(std::vector<std::string>{std::string(kCharacter)});
  bool every_clips_loaded = !characters.empty();
  for (const std::string& character : characters) {
    every_clips_loaded = LoadCharacter(pack, character) && every_clips_loaded;
  }
  return Reached{.clips_loaded = every_clips_loaded, .texture_resolved = pack.ResolveTexture(kTexturePath).has_value()};
}

}  // namespace augusta::pack_load_fuzz
