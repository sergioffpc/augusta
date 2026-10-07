#ifndef AUGUSTA_VERSION_H_
#define AUGUSTA_VERSION_H_

#include <string_view>

/// \file
/// The engine's version, shared by both executables: each prints it for
/// `--version` and logs it at startup, a client presents it when it joins, and
/// the server admits only clients of its own version.
namespace augusta {

/// The engine's semantic version, tracked by hand until releases exist (see
/// docs/ROADMAP.md).
std::string_view EngineVersion();

}  // namespace augusta

#endif  // AUGUSTA_VERSION_H_
