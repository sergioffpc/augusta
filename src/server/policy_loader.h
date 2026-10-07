#ifndef AUGUSTA_SERVER_POLICY_LOADER_H_
#define AUGUSTA_SERVER_POLICY_LOADER_H_

#include <expected>
#include <string>

#include "augusta/assets.h"
#include "augusta/scripting.h"

/// \file
/// augusta::server::LoadPolicy reads a scenario's Game policy, its rules script,
/// out of the server pack (ADR-0022, ADR-0039) and loads it into the engine
/// SimulationWorld runs it in. The server calls it at startup, before it opens a socket, so a
/// pack whose policy does not load exits like a bad pack does. Server-only.
namespace augusta::server {

/// Why a pack's Game policy did not load.
enum class PolicyLoadErrorCode {
  /// The pack has an entry at the rules' path that is not a readable script;
  /// subject is how it is not.
  kUnreadable,
  /// The script does not compile, raises an error at its top level or reaches
  /// for what the sandbox leaves out; subject is Lua's message.
  kScriptError,
};

/// A policy that did not load: what went wrong, and what it is about.
struct PolicyLoadError {
  PolicyLoadErrorCode code = PolicyLoadErrorCode::kScriptError;
  std::string subject;
};

/// A message for error fit to log, naming the script.
std::string DescribePolicyLoadError(const PolicyLoadError& error);

/// The engine loaded with the rules.lua pack holds. A pack without one is no
/// error: the scenario has no Game policy, and the mechanism decides alone.
std::expected<scripting::Engine, PolicyLoadError> LoadPolicy(const assets::Pack& pack);

}  // namespace augusta::server

#endif  // AUGUSTA_SERVER_POLICY_LOADER_H_
