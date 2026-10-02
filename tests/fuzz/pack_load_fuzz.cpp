#include <array>
#include <cstddef>
#include <cstdint>
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <random>
#include <string>
#include <string_view>
#include <system_error>
#include <vector>

#include <blake3.h>
#include <sodium.h>

#include "augusta/assets.h"

// The fuzz target for pack loading (ADR-0013): a pack file is the other input
// that arrives from outside. The fuzzer's bytes are a pack without its trailer;
// the target signs them with the golden packs' committed test key before
// loading, since a trailer the fuzzer wrote would fail verification and stop
// Load before the index or any blob is parsed. A pack that loads then has every
// asset of the example scenario resolved, so the blob decoders are fuzzed too.
namespace {

using augusta::assets::Ed25519PrivateKey;
using augusta::assets::Ed25519PublicKey;
using augusta::assets::Pack;

// The example scenario's paths (tests/fixtures/example-packs), each resolved as
// every type: a resolver that meets a path of another type is exercised too.
constexpr std::array<std::string_view, 12> kPaths = {
    "Scene",
    "Root/Floor/Visual",
    "Root/Floor/Collider",
    "Root/Spawn",
    "characters/player/Character/Visual",
    "characters/player/Character/Collider",
    "characters/player/Character/Eye",
    "characters/player/Character/HeadHitbox",
    augusta::assets::kParametersScriptPath,
    "behaviours.lua",
    "sounds/augusta/gunshot",
    augusta::assets::kSoundsPath,
};

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

struct TestKey {
  Ed25519PublicKey public_key = ReadKey<Ed25519PublicKey>(std::filesystem::path(AUGUSTA_EXAMPLE_PACKS) / "test.pub");
  Ed25519PrivateKey private_key = ReadKey<Ed25519PrivateKey>(std::filesystem::path(AUGUSTA_EXAMPLE_PACKS) / "test.key");
};

// The file Load reads: one per process, rewritten for every input, since
// libFuzzer's -fork and -jobs run several processes side by side.
struct ScratchFile {
  ScratchFile() {
    std::random_device random;
    path = std::filesystem::temp_directory_path() / ("augusta_pack_load_fuzz_" + std::to_string(random()) + ".pack");
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
std::vector<std::byte> Sign(const std::uint8_t* data, std::size_t size, const Ed25519PrivateKey& key) {
  std::array<std::uint8_t, augusta::assets::kPackHashSize> hash{};
  blake3_hasher hasher;
  blake3_hasher_init(&hasher);
  blake3_hasher_update(&hasher, data, size);
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

void ResolveEverything(const Pack& pack) {
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
  }
  (void)pack.ResolveHitboxes("characters/player");
  (void)pack.ResolveCharacters();
  (void)pack.ResolveClientPackHash();
  (void)pack.ResolveSoundsPath();
}

}  // namespace

extern "C" int LLVMFuzzerTestOneInput(const std::uint8_t* data, std::size_t size) {
  static const TestKey key;
  static const ScratchFile scratch;
  if (sodium_init() < 0) {
    std::abort();
  }

  const std::vector<std::byte> trailer = Sign(data, size, key.private_key);
  {
    std::ofstream out(scratch.path, std::ios::binary | std::ios::trunc);
    out.write(reinterpret_cast<const char*>(data), static_cast<std::streamsize>(size));
    out.write(reinterpret_cast<const char*>(trailer.data()), static_cast<std::streamsize>(trailer.size()));
    if (!out) {
      std::abort();
    }
  }

  const auto pack = Pack::Load(scratch.path, key.public_key);
  if (pack.has_value()) {
    ResolveEverything(*pack);
  }
  return 0;
}
