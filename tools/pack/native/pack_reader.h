#ifndef AUGUSTA_PACK_PACK_READER_H_
#define AUGUSTA_PACK_PACK_READER_H_

#include <pybind11/pybind11.h>

/// \file
/// The cooker's binding of augusta_assets' pack reading (augusta/assets.h,
/// ADR-0030): ReadPackLayout, for augusta-inspect, and Pack::Load with its
/// entries, for augusta-verify and augusta-publish, so a tool accepts exactly
/// the packs the client and server load and no Python code parses a pack. A
/// refusal raises LoadError (a ValueError), its reason the LoadError value and
/// its message DescribeLoadError's phrase. No blob is decoded: neither Decode*Blob
/// nor Pack's Resolve* is exposed.
namespace augusta::pack {

/// Adds the reader's types, functions and exception to module, after
/// BindPackEncoder, whose AssetType the entries name.
void BindPackReader(pybind11::module_& module);

}  // namespace augusta::pack

#endif  // AUGUSTA_PACK_PACK_READER_H_
