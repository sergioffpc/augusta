#ifndef AUGUSTA_VERSION_H_
#define AUGUSTA_VERSION_H_

#include <string>
#include <string_view>

/// \file
/// The engine's version, shared by both executables: each prints it for
/// `--version` and logs it at startup, a client presents it when it joins, and
/// the server admits only clients of its own version. And which build of it is
/// running, what ties a Crash dump of the executable to that build's symbols:
/// its PDB's GUID and age on Windows, its GNU build ID on Linux (ADR-0055).
namespace augusta {

/// The engine's semantic version, tracked by hand until releases exist (see
/// docs/ROADMAP.md).
std::string_view EngineVersion();

/// Which build of it is running, as logfmt fields (ADR-0029) for the client's
/// first line: what matches a Crash dump of this executable to its symbols
/// (ADR-0055). On Windows its PDB's GUID, uppercase and hyphenated as WinDbg and
/// dumpbin write it, and age: `pdb_guid=<guid> pdb_age=<n>`. On Linux its GNU
/// build ID, lowercase hex: `build_id=<hex>`. A value the executable was linked
/// without is `unknown`.
std::string DescribeBuild();

}  // namespace augusta

#endif  // AUGUSTA_VERSION_H_
