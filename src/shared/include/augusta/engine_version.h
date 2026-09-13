#ifndef AUGUSTA_ENGINE_VERSION_H_
#define AUGUSTA_ENGINE_VERSION_H_

#include <string_view>

namespace augusta {

// The engine's semantic version, tracked by hand until releases exist (see
// docs/ROADMAP.md).
std::string_view EngineVersion();

}  // namespace augusta

#endif  // AUGUSTA_ENGINE_VERSION_H_
