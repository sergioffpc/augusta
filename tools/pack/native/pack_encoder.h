#ifndef AUGUSTA_PACK_PACK_ENCODER_H_
#define AUGUSTA_PACK_PACK_ENCODER_H_

#include <pybind11/pybind11.h>

/// \file
/// The cooker's binding of augusta_assets' pack encoder (augusta/pack_encoder.h,
/// ADR-0030): the pack types the cook fills, every Encode*Blob and WritePack,
/// so the cooker writes packs with the code the client and server read them
/// with, and no copy of the wire format exists in Python; and
/// GenerateEd25519KeyPair, which augusta-keygen makes the signing keys with,
/// so the cooker has no crypto of its own. A refused encode or
/// write raises EncodeError (a ValueError) or WriteError (a RuntimeError).
/// Blobs and keys cross as bytes, points and indices as lists.
namespace augusta::pack {

/// Adds the encoder's types, functions and exceptions to module.
void BindPackEncoder(pybind11::module_& module);

}  // namespace augusta::pack

#endif  // AUGUSTA_PACK_PACK_ENCODER_H_
