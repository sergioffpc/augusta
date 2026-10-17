#include "augusta/policy_loader.h"

#include <expected>
#include <string>
#include <utility>

#include "augusta/assets.h"
#include "augusta/policy.h"

namespace augusta::server {

std::string DescribePolicyLoadError(const PolicyLoadError& error) {
  const std::string script(policy::kRulesScriptPath);
  switch (error.code) {
    case PolicyLoadErrorCode::kUnreadable:
      return "the policy script " + script + " is unreadable: " + error.subject;
    case PolicyLoadErrorCode::kScriptError:
      return "the policy script " + script + " failed to load: " + error.subject;
  }
  return "the policy script " + script + " is invalid";
}

std::expected<policy::Policy, PolicyLoadError> LoadPolicy(const assets::Pack& pack) {
  const auto rules = pack.ResolveScript(policy::kRulesScriptPath);
  if (!rules) {
    if (rules.error() == assets::ResolveError::kNotFound) {
      return policy::Policy{};
    }
    return std::unexpected(PolicyLoadError{.code = PolicyLoadErrorCode::kUnreadable,
                                           .subject = assets::DescribeResolveError(rules.error(), "script")});
  }
  auto loaded = policy::Policy::Load(*rules);
  if (!loaded) {
    return std::unexpected(
        PolicyLoadError{.code = PolicyLoadErrorCode::kScriptError, .subject = std::move(loaded.error().message)});
  }
  return *std::move(loaded);
}

std::expected<PolicyMaker, PolicyLoadError> LoadPolicyMaker(const assets::Pack& pack) {
  const auto rules = pack.ResolveScript(policy::kRulesScriptPath);
  if (!rules && rules.error() == assets::ResolveError::kNotFound) {
    return PolicyMaker([] { return policy::Policy{}; });
  }
  if (auto checked = LoadPolicy(pack); !checked) {
    return std::unexpected(std::move(checked.error()));
  }
  // Loaded once already, from the same text: Load cannot fail now.
  return PolicyMaker([text = *rules] { return *policy::Policy::Load(text); });
}

}  // namespace augusta::server
