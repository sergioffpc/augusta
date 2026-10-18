#include "keys_binding.h"

#include <pybind11/pybind11.h>

#include "augusta/assets.h"
#include "bytes.h"

namespace augusta::pack {

namespace py = pybind11;

void BindKeys(py::module_& module) {
  module.def(
      "generate_ed25519_keypair",
      [] {
        const assets::Ed25519KeyPair pair = assets::GenerateEd25519KeyPair();
        return py::make_tuple(ToBytes(pair.public_key), ToBytes(pair.private_key));
      },
      "Generates a new Ed25519 keypair (ADR-0018). Returns (public_key, private_key): 32 and 64 raw bytes, the "
      "private key the one write_pack signs with, the public key the one load_pack checks against.");
}

}  // namespace augusta::pack
