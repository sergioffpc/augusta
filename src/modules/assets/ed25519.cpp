#include "ed25519.h"

#include <expected>
#include <filesystem>
#include <fstream>
#include <ios>
#include <stdexcept>

#include <sodium.h>

#include "augusta/assets.h"

namespace augusta::assets {

void EnsureSodiumInitialized() {
  static const bool kInitialized = [] {
    if (sodium_init() < 0) {
      // Unrecoverable startup error (ENGINEERING.md Design Philosophy):
      // the crypto library failed to initialize, so nothing downstream
      // (signing or verification) can be trusted to run at all.
      throw std::runtime_error("augusta::assets: sodium_init() failed");
    }
    return true;
  }();
  (void)kInitialized;
}

Ed25519KeyPair GenerateEd25519KeyPair() {
  EnsureSodiumInitialized();
  Ed25519KeyPair pair;
  crypto_sign_keypair(reinterpret_cast<unsigned char*>(pair.public_key.data()),
                      reinterpret_cast<unsigned char*>(pair.private_key.data()));
  return pair;
}

std::expected<Ed25519PublicKey, ReadKeyFileError> ReadEd25519PublicKeyFile(const std::filesystem::path& path) {
  std::ifstream key_file(path, std::ios::binary);
  if (!key_file) {
    return std::unexpected(ReadKeyFileError::kIoError);
  }
  Ed25519PublicKey key;
  key_file.read(reinterpret_cast<char*>(key.data()), static_cast<std::streamsize>(key.size()));
  if (!key_file || key_file.gcount() != static_cast<std::streamsize>(key.size())) {
    return std::unexpected(ReadKeyFileError::kIoError);
  }
  // A short read (file smaller than 32 bytes) is already caught above by
  // gcount(); this catches the opposite - a file with trailing bytes past
  // the key, which would otherwise go silently unnoticed - by checking
  // there's nothing left to read.
  key_file.peek();
  if (!key_file.eof()) {
    return std::unexpected(ReadKeyFileError::kIoError);
  }
  return key;
}

}  // namespace augusta::assets
