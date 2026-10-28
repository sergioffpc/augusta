#include "augusta/policy_loader.h"

#include <expected>
#include <string>
#include <string_view>
#include <utility>

#include "augusta/assets.h"
#include "augusta/policy.h"

namespace augusta::policy {

std::string DescribePolicyLoadError(const PolicyLoadError& error) {
  const std::string script(kRulesScriptPath);
  switch (error.code) {
    case PolicyLoadErrorCode::kUnreadable:
      return "the policy script " + script + " is unreadable: " + error.subject;
    case PolicyLoadErrorCode::kScriptError:
      return "the policy script " + script + " failed to load: " + error.subject;
  }
  return "the policy script " + script + " is invalid";
}

namespace {

// rules loaded as the Game policy, or why they did not load.
std::expected<Policy, PolicyLoadError> LoadRules(std::string_view rules) {
  auto loaded = Policy::Load(rules);
  if (!loaded) {
    return std::unexpected(
        PolicyLoadError{.code = PolicyLoadErrorCode::kScriptError, .subject = std::move(loaded.error().message)});
  }
  return *std::move(loaded);
}

}  // namespace

std::expected<Policy, PolicyLoadError> LoadPolicy(const assets::Pack& pack) {
  const auto rules = pack.ResolveScript(kRulesScriptPath);
  if (!rules) {
    if (rules.error() == assets::ResolveError::kNotFound) {
      return Policy{};
    }
    return std::unexpected(PolicyLoadError{.code = PolicyLoadErrorCode::kUnreadable,
                                           .subject = assets::DescribeResolveError(rules.error(), "script")});
  }
  return LoadRules(*rules);
}

PolicyMaker PolicyMakerOf(std::string rules) {
  return [text = std::move(rules)] { return LoadRules(text); };
}

std::expected<PolicyMaker, PolicyLoadError> LoadPolicyMaker(const assets::Pack& pack) {
  const auto rules = pack.ResolveScript(kRulesScriptPath);
  if (!rules && rules.error() == assets::ResolveError::kNotFound) {
    return PolicyMaker([] { return Policy{}; });
  }
  if (auto checked = LoadPolicy(pack); !checked) {
    return std::unexpected(std::move(checked.error()));
  }
  return PolicyMakerOf(*rules);
}

}  // namespace augusta::policy
