#ifndef AUGUSTA_PACK_READER_BINDING_H_
#define AUGUSTA_PACK_READER_BINDING_H_

#include <pybind11/pybind11.h>

/// \file
/// The cooker's binding of augusta_assets' pack reading (augusta/assets.h,
/// ADR-0030), for augusta-inspect, augusta-verify and augusta-publish
/// (pack_cli.py, publish.py): a tool accepts exactly the packs the client and
/// server load, and no Python code parses a pack. It reads containers only:
/// decoding blobs stays the runtime's. A refusal raises with augusta_assets'
/// LoadError as its reason (failure.h).
namespace augusta::pack {

/// Adds pack reading to module, after BindPackEncoder, whose AssetType the
/// entries name.
void BindPackReader(pybind11::module_& module);

}  // namespace augusta::pack

#endif  // AUGUSTA_PACK_READER_BINDING_H_
