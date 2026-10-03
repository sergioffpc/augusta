#include "content.h"

#include <cstdint>
#include <expected>
#include <optional>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

#include "augusta/assets.h"
#include "augusta/logging.h"
#include "augusta/map.h"
#include "augusta/parameters.h"
#include "augusta/scripting.h"
#include "parameters_loader.h"
#include "policy_loader.h"

namespace augusta::server {

namespace {

// Each of paths as a Character with its hitboxes and its eye, in the same
// order. Reports the first that has no hitbox for a body part or no eye and
// returns nullopt.
std::optional<std::vector<Character>> ReadCharacters(const assets::Pack& pack, std::vector<std::string> paths) {
  std::vector<Character> characters;
  characters.reserve(paths.size());
  for (std::string& path : paths) {
    auto hitboxes = pack.ResolveHitboxes(path);
    if (!hitboxes) {
      LE("subsystem=server event=hitboxes_loading_failed path={} character={} error={}", pack.Path().string(), path,
         assets::DescribeResolveError(hitboxes.error(), "hitbox"));
      return std::nullopt;
    }
    if (const auto missing = assets::FirstMissingBodyPart(*hitboxes)) {
      LE("subsystem=server event=hitboxes_loading_failed path={} character={} error=\"no hitbox for the {}\"",
         pack.Path().string(), path, assets::BodyPartName(*missing));
      return std::nullopt;
    }
    const auto eye = pack.ResolveEye(assets::CharacterEyePath(path));
    if (!eye) {
      LE("subsystem=server event=eye_loading_failed path={} character={} error={}", pack.Path().string(), path,
         assets::DescribeResolveError(eye.error(), "eye"));
      return std::nullopt;
    }
    characters.push_back({.path = std::move(path), .hitboxes = *std::move(hitboxes), .eye = eye->position});
  }
  return characters;
}

// The scenario's Parameters script, out of the pack (it was cooked into the
// server pack with the map and is signed with it), evaluated once for a server
// ticking at tick_rate_hz. Logs each warning the script gives; reports what is
// wrong and returns nullopt.
std::optional<parameters::Parameters> ReadParameters(const assets::Pack& pack, std::uint8_t tick_rate_hz) {
  const std::string_view script_path = assets::kParametersScriptPath;
  const auto script = pack.ResolveScript(script_path);
  if (!script) {
    LE("subsystem=server event=parameters_script_loading_failed path={} script={} error={}", pack.Path().string(),
       script_path, assets::DescribeResolveError(script.error(), "script"));
    return std::nullopt;
  }
  auto parameters = LoadParameters(*script, tick_rate_hz, [script_path](std::string_view message) {
    LW("subsystem=server event=parameters_warning script={} message=\"{}\"", script_path, message);
  });
  if (!parameters) {
    LE("subsystem=server event=parameters_loading_failed path={} script={} error={}", pack.Path().string(), script_path,
       DescribeParametersLoadError(parameters.error()));
    return std::nullopt;
  }
  LI("subsystem=server event=parameters_loaded script={}", script_path);
  return *std::move(parameters);
}

// The scenario's Game policy scripts (ADR-0022), out of the same pack. Reports
// what is wrong and returns nullopt.
std::optional<scripting::Engine> ReadPolicy(const assets::Pack& pack) {
  auto policy = LoadPolicy(pack);
  if (!policy) {
    LE("subsystem=server event=policy_loading_failed path={} script={} error=\"{}\"", pack.Path().string(),
       scripting::ScriptPath(policy.error().script), DescribePolicyLoadError(policy.error()));
    return std::nullopt;
  }
  LI("subsystem=server event=policy_loaded");
  return *std::move(policy);
}

}  // namespace

std::string_view DescribeContentError(ContentError error) {
  switch (error) {
    case ContentError::kCollisionLoading:
      return "collision loading failed";
    case ContentError::kSpawnPointsLoading:
      return "spawn points loading failed";
    case ContentError::kCharactersLoading:
      return "characters loading failed";
    case ContentError::kClientPackHashLoading:
      return "client pack hash loading failed";
    case ContentError::kParametersLoading:
      return "parameters loading failed";
    case ContentError::kPolicyLoading:
      return "policy loading failed";
  }
  return "unknown content error";
}

std::expected<Content, ContentError> LoadServerContent(const assets::Pack& pack, std::uint8_t tick_rate_hz) {
  auto collision = map::LoadCollision(pack);
  if (!collision) {
    LE("subsystem=server event=collision_loading_failed path={} error={}", pack.Path().string(),
       map::DescribeMapError(collision.error()));
    return std::unexpected(ContentError::kCollisionLoading);
  }
  auto spawn_points = map::LoadSpawnPoints(pack);
  if (!spawn_points) {
    LE("subsystem=server event=spawn_points_loading_failed path={} error={}", pack.Path().string(),
       map::DescribeMapError(spawn_points.error()));
    return std::unexpected(ContentError::kSpawnPointsLoading);
  }
  // The scenario's characters, the only ones a player may join as (ADR-0042).
  auto character_paths = pack.ResolveCharacters();
  if (!character_paths) {
    LE("subsystem=server event=characters_loading_failed path={} asset={} error={}", pack.Path().string(),
       assets::kCharactersPath, assets::DescribeResolveError(character_paths.error(), "character list"));
    return std::unexpected(ContentError::kCharactersLoading);
  }
  auto characters = ReadCharacters(pack, *std::move(character_paths));
  if (!characters) {
    return std::unexpected(ContentError::kCharactersLoading);
  }
  // The client pack cooked with this one, the only one a player may join with.
  const auto client_pack = pack.ResolveClientPackHash();
  if (!client_pack) {
    LE("subsystem=server event=client_pack_hash_loading_failed path={} asset={} error={}", pack.Path().string(),
       assets::kClientPackPath, assets::DescribeResolveError(client_pack.error(), "client pack hash"));
    return std::unexpected(ContentError::kClientPackHashLoading);
  }
  LI("subsystem=server event=map_loaded colliders={} spawn_points={} characters={}", collision->size(),
     spawn_points->size(), characters->size());

  auto parameters = ReadParameters(pack, tick_rate_hz);
  if (!parameters) {
    return std::unexpected(ContentError::kParametersLoading);
  }

  auto policy = ReadPolicy(pack);
  if (!policy) {
    return std::unexpected(ContentError::kPolicyLoading);
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
