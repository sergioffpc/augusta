#include "content.h"

#include <cstdint>
#include <expected>
#include <format>
#include <optional>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

#include "augusta/assets.h"
#include "augusta/failure.h"
#include "augusta/logging.h"
#include "augusta/map.h"
#include "augusta/parameters.h"
#include "augusta/scripting.h"
#include "parameters_loader.h"
#include "policy_loader.h"

namespace augusta::server {

namespace {

// The pack's content failing: what was wrong, with which part of it, context
// naming the pack first.
failure::Failure ContentFailure(const assets::Pack& pack, std::vector<failure::ContextField> context,
                                std::string detail) {
  context.insert(context.begin(), {.key = "path", .value = pack.Path().string()});
  return {.code = failure::Code::kInvalidContent, .context = std::move(context), .detail = std::move(detail)};
}

// Each of paths as a Character with its hitboxes and its eye, in the same
// order, or the failure of the first that has no hitbox for a body part or no eye.
std::expected<std::vector<Character>, failure::Failure> ReadCharacters(const assets::Pack& pack,
                                                                       std::vector<std::string> paths) {
  std::vector<Character> characters;
  characters.reserve(paths.size());
  for (std::string& path : paths) {
    const std::vector<failure::ContextField> context = {{.key = "character", .value = path}};
    auto hitboxes = pack.ResolveHitboxes(path);
    if (!hitboxes) {
      return std::unexpected(ContentFailure(pack, context, assets::DescribeResolveError(hitboxes.error(), "hitbox")));
    }
    if (const auto missing = assets::FirstMissingBodyPart(*hitboxes)) {
      return std::unexpected(
          ContentFailure(pack, context, std::format("no hitbox for the {}", assets::BodyPartName(*missing))));
    }
    const auto eye = pack.ResolveEye(assets::CharacterEyePath(path));
    if (!eye) {
      return std::unexpected(ContentFailure(pack, context, assets::DescribeResolveError(eye.error(), "eye")));
    }
    characters.push_back({.path = std::move(path), .hitboxes = *std::move(hitboxes), .eye = eye->position});
  }
  return characters;
}

// The scenario's Parameters script, out of the pack (it was cooked into the
// server pack with the map and is signed with it), evaluated once for a server
// ticking at tick_rate_hz. Logs each warning the script gives.
std::expected<parameters::Parameters, failure::Failure> ReadParameters(const assets::Pack& pack,
                                                                       std::uint8_t tick_rate_hz) {
  const std::string_view script_path = assets::kParametersScriptPath;
  const std::vector<failure::ContextField> context = {{.key = "script", .value = std::string(script_path)}};
  const auto script = pack.ResolveScript(script_path);
  if (!script) {
    return std::unexpected(ContentFailure(pack, context, assets::DescribeResolveError(script.error(), "script")));
  }
  auto parameters = LoadParameters(*script, tick_rate_hz, [script_path](std::string_view message) {
    LW("subsystem=server event=parameters_warning script={} message=\"{}\"", script_path, message);
  });
  if (!parameters) {
    return std::unexpected(ContentFailure(pack, context, DescribeParametersLoadError(parameters.error())));
  }
  LI("subsystem=server event=parameters_loaded script={}", script_path);
  return *std::move(parameters);
}

// The scenario's Game policy, its rules (ADR-0022), out of the same pack.
std::expected<scripting::Engine, failure::Failure> ReadPolicy(const assets::Pack& pack) {
  auto policy = LoadPolicy(pack);
  if (!policy) {
    return std::unexpected(ContentFailure(pack, {{.key = "script", .value = std::string(scripting::kRulesScriptPath)}},
                                          DescribePolicyLoadError(policy.error())));
  }
  LI("subsystem=server event=policy_loaded");
  return *std::move(policy);
}

}  // namespace

std::expected<Content, failure::Failure> LoadServerContent(const assets::Pack& pack, std::uint8_t tick_rate_hz) {
  auto collision = map::LoadCollision(pack);
  if (!collision) {
    return std::unexpected(
        ContentFailure(pack, {{.key = "asset", .value = "collision"}}, map::DescribeMapError(collision.error())));
  }
  auto spawn_points = map::LoadSpawnPoints(pack);
  if (!spawn_points) {
    return std::unexpected(
        ContentFailure(pack, {{.key = "asset", .value = "spawn_points"}}, map::DescribeMapError(spawn_points.error())));
  }
  // The scenario's characters, the only ones a player may join as (ADR-0042).
  auto character_paths = pack.ResolveCharacters();
  if (!character_paths) {
    return std::unexpected(ContentFailure(pack, {{.key = "asset", .value = std::string(assets::kCharactersPath)}},
                                          assets::DescribeResolveError(character_paths.error(), "character list")));
  }
  auto characters = ReadCharacters(pack, *std::move(character_paths));
  if (!characters) {
    return std::unexpected(std::move(characters.error()));
  }
  // The client pack cooked with this one, the only one a player may join with.
  const std::optional<assets::PackHash>& client_pack = pack.ClientPackHash();
  if (!client_pack) {
    return std::unexpected(ContentFailure(pack, {}, "names no client pack"));
  }
  LI("subsystem=server event=map_loaded colliders={} spawn_points={} characters={}", collision->size(),
     spawn_points->size(), characters->size());

  auto parameters = ReadParameters(pack, tick_rate_hz);
  if (!parameters) {
    return std::unexpected(std::move(parameters.error()));
  }

  auto policy = ReadPolicy(pack);
  if (!policy) {
    return std::unexpected(std::move(policy.error()));
  }

  return Content{
      .scenario =
          Scenario{
              .collision = *std::move(collision),
              .spawn_points = *std::move(spawn_points),
              .characters = *std::move(characters),
              .client_pack = *client_pack,
          },
      .parameters = *std::move(parameters),
      .policy = *std::move(policy),
  };
}

}  // namespace augusta::server
