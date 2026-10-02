#include "policy_loader.h"

#include <expected>
#include <optional>
#include <string>
#include <utility>

#include "augusta/assets.h"
#include "augusta/scripting.h"

namespace augusta::server {
namespace {

// The text of script in pack, or nullopt if the pack has none.
std::expected<std::optional<std::string>, PolicyLoadError> ReadScript(const assets::Pack& pack,
                                                                      scripting::Script script) {
  auto text = pack.ResolveScript(scripting::ScriptPath(script));
  if (text) {
    return *std::move(text);
  }
  if (text.error() == assets::ResolveError::kNotFound) {
    return std::nullopt;
  }
  return std::unexpected(PolicyLoadError{.code = PolicyLoadErrorCode::kUnreadable,
                                         .script = script,
                                         .subject = assets::DescribeResolveError(text.error(), "script")});
}

}  // namespace

std::string DescribePolicyLoadError(const PolicyLoadError& error) {
  const std::string script(scripting::ScriptPath(error.script));
  switch (error.code) {
    case PolicyLoadErrorCode::kUnreadable:
      return "the policy script " + script + " is unreadable: " + error.subject;
    case PolicyLoadErrorCode::kScriptError:
      return "the policy script " + script + " failed to load: " + error.subject;
  }
  return "the policy script " + script + " is invalid";
}

std::expected<scripting::Engine, PolicyLoadError> LoadPolicy(const assets::Pack& pack) {
  auto objectives = ReadScript(pack, scripting::Script::kObjectives);
  if (!objectives) {
    return std::unexpected(objectives.error());
  }
  auto behaviours = ReadScript(pack, scripting::Script::kBehaviours);
  if (!behaviours) {
    return std::unexpected(behaviours.error());
  }
  auto engine = scripting::Engine::Load({.objectives = *std::move(objectives), .behaviours = *std::move(behaviours)});
  if (!engine) {
    return std::unexpected(PolicyLoadError{
        .code = PolicyLoadErrorCode::kScriptError, .script = engine.error().script, .subject = engine.error().message});
  }
  return *std::move(engine);
}

}  // namespace augusta::server
