#ifndef AUGUSTA_PACK_ENCODER_BINDING_H_
#define AUGUSTA_PACK_ENCODER_BINDING_H_

#include <pybind11/pybind11.h>

/// \file
/// The cooker's binding of augusta_assets' pack encoder (augusta/pack_encoder.h,
/// ADR-0030), for the cook (cook.py, cli.py): the cooker writes its packs with
/// the code the client and server read them with, so no copy of the wire format
/// exists in Python. What goes into a pack, read off the USD stages, stays the
/// cook's; reading packs back is reader_binding.h's. A refusal raises with
/// augusta_assets' error as its reason (failure.h).
namespace augusta::pack {

/// Adds the encoder to module.
void BindPackEncoder(pybind11::module_& module);

}  // namespace augusta::pack

#endif  // AUGUSTA_PACK_ENCODER_BINDING_H_
