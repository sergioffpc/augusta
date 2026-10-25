#include "augusta/version.h"

#include <string>
#include <string_view>

namespace augusta {

std::string_view EngineVersion() {
  // The release's version, which the cut-release runbook bumps. A Development
  // build appends SemVer build metadata, so it and a Shipping build of the same
  // release refuse each other at Join (ADR-0074).
  constexpr std::string_view kRelease = "7.0.0";
#if AUGUSTA_DEBUG_TOOLS
  static const std::string kDevelopment = std::string(kRelease) + "+dev";
  return kDevelopment;
#else
  return kRelease;
#endif
}

}  // namespace augusta
