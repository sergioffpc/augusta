// Asset cooker CLI (ADR-0015 through ADR-0019, ADR-0030 through ADR-0032,
// ROADMAP.md M2): a thin wrapper over augusta::asset_cooking::Cook and
// augusta::assets::GenerateEd25519KeyPair.
#include <array>
#include <cstddef>
#include <filesystem>
#include <fstream>
#include <optional>
#include <print>
#include <string_view>

#include "augusta/asset_cooking.h"
#include "augusta/assets.h"

namespace {

using augusta::assets::Ed25519PrivateKey;
using augusta::assets::Ed25519PublicKey;

template <std::size_t N>
bool WriteKeyFile(const std::filesystem::path& path, const std::array<std::byte, N>& key) {
  std::ofstream out(path, std::ios::binary | std::ios::trunc);
  if (!out) {
    return false;
  }
  out.write(reinterpret_cast<const char*>(key.data()), static_cast<std::streamsize>(key.size()));
  return static_cast<bool>(out);
}

std::optional<Ed25519PrivateKey> ReadPrivateKeyFile(const std::filesystem::path& path) {
  std::ifstream key_file(path, std::ios::binary);
  if (!key_file) {
    return std::nullopt;
  }
  Ed25519PrivateKey key;
  key_file.read(reinterpret_cast<char*>(key.data()), static_cast<std::streamsize>(key.size()));
  if (!key_file || key_file.gcount() != static_cast<std::streamsize>(key.size())) {
    return std::nullopt;
  }
  return key;
}

int RunGenKeypair(std::string_view prefix) {
  const auto pair = augusta::assets::GenerateEd25519KeyPair();
  const auto pub_path = std::filesystem::path(std::string(prefix) + ".pub");
  const auto key_path = std::filesystem::path(std::string(prefix) + ".key");
  if (!WriteKeyFile(pub_path, pair.public_key) || !WriteKeyFile(key_path, pair.private_key)) {
    std::println(stderr, "failed to write keypair to {}.pub / {}.key", prefix, prefix);
    return 1;
  }
  std::println("wrote {} and {}", pub_path.string(), key_path.string());
  return 0;
}

int RunCook(std::string_view stage_path, std::string_view output_path, std::string_view private_key_path) {
  const auto signing_key = ReadPrivateKeyFile(private_key_path);
  if (!signing_key) {
    std::println(stderr, "could not read Ed25519 private key from {}", private_key_path);
    return 1;
  }

  const auto report = augusta::asset_cooking::Cook(stage_path, output_path, *signing_key);
  if (!report) {
    const auto& detail = report.error();
    if (detail.prim_path.empty()) {
      std::println(stderr, "cook failed: {} ({})", detail.message, static_cast<int>(detail.code));
    } else {
      std::println(stderr, "cook failed at {}: {} ({})", detail.prim_path, detail.message,
                   static_cast<int>(detail.code));
    }
    return 1;
  }

  std::println("cooked {} mesh(es), {} node(s) into {}", report->mesh_count, report->node_count, output_path);
  return 0;
}

}  // namespace

int main(int argc, char** argv) {
  if (argc == 3 && std::string_view(argv[1]) == "--gen-keypair") {
    return RunGenKeypair(argv[2]);
  }
  if (argc == 4) {
    return RunCook(argv[1], argv[2], argv[3]);
  }

  std::println(stderr, "usage: augusta_asset_cooking <stage.usd> <output.pack> <signing_key.key>");
  std::println(stderr, "       augusta_asset_cooking --gen-keypair <key_prefix>");
  return 1;
}
