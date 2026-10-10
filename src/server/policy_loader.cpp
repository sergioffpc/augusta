#include "policy_loader.h"

#include <expected>
#include <string>
#include <utility>

#include "augusta/assets.h"
#include "augusta/scripting.h"

namespace augusta::server {

std::string DescribePolicyLoadError(const PolicyLoadError& error) {
  const std::string script(scripting::kRulesScriptPath);
  switch (error.code) {
    case PolicyLoadErrorCode::kUnreadable:
      return "the policy script " + script + " is unreadable: " + error.subject;
    case PolicyLoadErrorCode::kScriptError:
      return "the policy script " + script + " failed to load: " + error.subject;
  }
  return "the policy script " + script + " is invalid";
}

std::expected<scripting::Engine, PolicyLoadError> LoadPolicy(const assets::Pack& pack) {
  const auto rules = pack.ResolveScript(scripting::kRulesScriptPath);
  if (!rules) {
    if (rules.error() == assets::ResolveError::kNotFound) {
      return scripting::Engine{};
    }
    return std::unexpected(PolicyLoadError{.code = PolicyLoadErrorCode::kUnreadable,
                                           .subject = assets::DescribeResolveError(rules.error(), "script")});
  }
  auto engine = scripting::Engine::Load(*rules);
  if (!engine) {
    return std::unexpected(
        PolicyLoadError{.code = PolicyLoadErrorCode::kScriptError, .subject = std::move(engine.error().message)});
  }
  return *std::move(engine);
}

std::expected<PolicyMaker, PolicyLoadError> LoadPolicyMaker(const assets::Pack& pack) {
  const auto rules = pack.ResolveScript(scripting::kRulesScriptPath);
  if (!rules && rules.error() == assets::ResolveError::kNotFound) {
    return PolicyMaker([] { return scripting::Engine{}; });
  }
  if (auto checked = LoadPolicy(pack); !checked) {
    return std::unexpected(std::move(checked.error()));
  }
  // Loaded once already, from the same text: Load cannot fail now.
  return PolicyMaker([text = *rules] { return *scripting::Engine::Load(text); });
}

}  // namespace augusta::server
