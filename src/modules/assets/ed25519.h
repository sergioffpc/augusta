#ifndef AUGUSTA_ASSETS_ED25519_H_
#define AUGUSTA_ASSETS_ED25519_H_

/// \file
/// libsodium's one-time initialization, private to the module: what signing a
/// pack (pack_writer.cpp), verifying one (pack_reader.cpp) and generating a key
/// pair (ed25519.cpp) each call first (ADR-0030/ADR-0031).
namespace augusta::assets {

/// Initializes libsodium once per process; every later call is a no-op.
/// Throws std::runtime_error if it fails to initialize.
void EnsureSodiumInitialized();

}  // namespace augusta::assets

#endif  // AUGUSTA_ASSETS_ED25519_H_
