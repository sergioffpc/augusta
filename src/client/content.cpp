#include "content.h"

#include <expected>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

#include "augusta/assets.h"
#include "augusta/cues.h"
#include "augusta/failure.h"
#include "augusta/logging.h"
#include "augusta/map.h"
#include "augusta/math.h"
#include "augusta/renderer.h"
#include "character_loader.h"
#include "scene_loader.h"

namespace augusta::client {

namespace {

// The pack's content failing: what was wrong, with which part of it, context
// naming the pack first.
failure::Failure ContentFailure(const assets::Pack& pack, std::vector<failure::ContextField> context,
                                std::string detail) {
  context.insert(context.begin(), {.key = "path", .value = pack.Path().string()});
  return {.code = failure::Code::kInvalidContent, .context = std::move(context), .detail = std::move(detail)};
}

// Only the scene graph and its meshes are consumed so far (what the renderer
// draws); collision/hitbox/texture resolution waits for the ECS
// component shapes and gameplay code that will use them.
std::expected<renderer::Scene, failure::Failure> LoadScene(const assets::Pack& pack, const math::Vec3& eye) {
  auto scene = LoadRenderScene(pack, eye);
  if (!scene) {
    return std::unexpected(
        ContentFailure(pack, {{.key = "asset", .value = "scene"}}, DescribeSceneError(scene.error())));
  }
  LI("subsystem=client event=scene_loaded meshes={}", scene->meshes.size());
  return *std::move(scene);
}

// The eye of character, the one this player asked to play, from pack: where its
// camera sits.
std::expected<math::Vec3, failure::Failure> LoadEye(const assets::Pack& pack, std::string_view character) {
  auto eye = LoadCharacterEye(character, [&pack](std::string_view path) { return pack.ResolveEye(path); });
  if (!eye) {
    return std::unexpected(ContentFailure(pack, {{.key = "character", .value = std::string(character)}},
                                          DescribeCharacterError(eye.error())));
  }
  return *eye;
}

// Loads a character's mesh and eye from pack by its path, which must be one of
// the scenario's characters (ADR-0042); pack must outlive it. Fails if the
// character list does not load.
std::expected<CharacterLoader, failure::Failure> CharacterLoaderFor(const assets::Pack& pack) {
  auto characters = pack.ResolveCharacters();
  if (!characters) {
    return std::unexpected(ContentFailure(pack, {{.key = "asset", .value = std::string(assets::kCharactersPath)}},
                                          assets::DescribeResolveError(characters.error(), "character list")));
  }
  return [&pack, characters = *std::move(characters)](std::string_view character) {
    return LoadCharacterMesh(characters, character, [&pack](std::string_view path) { return pack.ResolveMesh(path); })
        .and_then([&](renderer::SceneMesh mesh) {
          return LoadCharacterEye(character, [&pack](std::string_view path) { return pack.ResolveEye(path); })
              .transform(
                  [&mesh](const math::Vec3& eye) { return LoadedCharacter{.mesh = std::move(mesh), .eye = eye}; });
        });
  };
}

// The map's collision from pack.
std::expected<Map, failure::Failure> LoadMap(const assets::Pack& pack) {
  auto collision = map::LoadCollision(pack);
  if (!collision) {
    return std::unexpected(
        ContentFailure(pack, {{.key = "asset", .value = "collision"}}, map::DescribeMapError(collision.error())));
  }
  LI("subsystem=client event=map_loaded colliders={}", collision->size());
  return Map{.collision = *std::move(collision)};
}

// Every cue's sound (ADR-0020), loaded at startup so a pack missing one is found
// before a Match rather than during one.
std::expected<audio::CueSounds, failure::Failure> LoadCueSounds(const assets::Pack& pack) {
  auto sounds = audio::LoadCueSounds(pack);
  if (!sounds) {
    return std::unexpected(
        ContentFailure(pack, {{.key = "asset", .value = "cue_sounds"}}, audio::DescribeCueSoundError(sounds.error())));
  }
  LI("subsystem=client event=cue_sounds_loaded cues={}", sounds->size());
  return *std::move(sounds);
}

}  // namespace

std::expected<Content, failure::Failure> LoadClientContent(const assets::Pack& pack, std::string_view character) {
  // The camera is attached to the character this player asked to play, at its eye.
  const auto eye = LoadEye(pack, character);
  if (!eye) {
    return std::unexpected(eye.error());
  }

  auto scene = LoadScene(pack, *eye);
  if (!scene) {
    return std::unexpected(std::move(scene.error()));
  }

  // A character is loaded only once another player in the Lobby brings it (ADR-0043).
  auto load_character = CharacterLoaderFor(pack);
  if (!load_character) {
    return std::unexpected(std::move(load_character.error()));
  }

  auto map = LoadMap(pack);
  if (!map) {
    return std::unexpected(std::move(map.error()));
  }

  auto cue_sounds = LoadCueSounds(pack);
  if (!cue_sounds) {
    return std::unexpected(std::move(cue_sounds.error()));
  }

  return Content{.eye = *eye,
                 .scene = *std::move(scene),
                 .map = *std::move(map),
                 .load_character = *std::move(load_character),
                 .cue_sounds = *std::move(cue_sounds)};
}

}  // namespace augusta::client
