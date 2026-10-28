#ifndef AUGUSTA_POLICY_LOADER_H_
#define AUGUSTA_POLICY_LOADER_H_

#include <expected>
#include <functional>
#include <string>

#include "augusta/assets.h"
#include "augusta/policy.h"

/// \file
/// augusta::policy::LoadPolicy reads a scenario's Game policy, its rules script,
/// out of the server pack (ADR-0022, ADR-0039) and loads it as the Game policy
/// SimulationWorld runs. The server calls it at startup, before it opens a socket, so a
/// pack whose policy does not load exits like a bad pack does. Server-only.
namespace augusta::policy {

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

/// The Game policy loaded with the rules.lua pack holds. A pack without one is no
/// error: the scenario has no Game policy, and the mechanism decides alone.
std::expected<Policy, PolicyLoadError> LoadPolicy(const assets::Pack& pack);

/// Makes a scenario's Game policy afresh each time it is called: every
/// World that runs the policy needs one of its own (a replay server's
/// every Replay, ADR-0051). Each call loads the rules again, so each can fail
/// as LoadPolicy does: rules that loaded once can still not load later, when
/// their state cannot get the memory it needs.
using PolicyMaker = std::function<std::expected<Policy, PolicyLoadError>()>;

/// A PolicyMaker that loads rules, a rules script's text, on each call, unchecked
/// beforehand: a call whose load fails gives that load's error.
PolicyMaker PolicyMakerOf(std::string rules);

/// What makes the policy LoadPolicy loads, afresh for each World that runs it
/// (a replay server's every Replay, ADR-0051), or the error LoadPolicy gives:
/// the rules are checked once, here, so that rules that never load stop the
/// server at startup.
std::expected<PolicyMaker, PolicyLoadError> LoadPolicyMaker(const assets::Pack& pack);

}  // namespace augusta::policy

#endif  // AUGUSTA_POLICY_LOADER_H_
