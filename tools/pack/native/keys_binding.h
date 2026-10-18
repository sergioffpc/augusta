#ifndef AUGUSTA_PACK_KEYS_BINDING_H_
#define AUGUSTA_PACK_KEYS_BINDING_H_

#include <pybind11/pybind11.h>

/// \file
/// The cooker's binding of augusta_assets' Ed25519 key generation
/// (augusta/assets.h, ADR-0018), for augusta-keygen (keys.py): the keys the
/// cook signs packs with come from the library that verifies them, so the
/// cooker has no crypto of its own. Reading and writing key files stays in
/// keys.py.
namespace augusta::pack {

/// Adds key generation to module.
void BindKeys(pybind11::module_& module);

}  // namespace augusta::pack

#endif  // AUGUSTA_PACK_KEYS_BINDING_H_
