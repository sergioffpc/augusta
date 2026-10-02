#include "content.h"

#include <format>
#include <optional>
#include <string_view>
#include <utility>

#include "augusta/logging.h"
#include "augusta/map.h"
#include "scene_loader.h"

namespace augusta::client {

namespace {

// Only the scene graph and its meshes are consumed so far (what the renderer
// draws); collision/hitbox/texture resolution waits for the ECS
// component shapes and gameplay code that will use them. Reports what is
// wrong and returns nullopt.
std::optional<renderer::Scene> LoadScene(const assets::Pack& pack, const math::Vec3& eye) {
  auto scene = LoadRenderScene(pack, eye);
  if (!scene) {
    LE("subsystem=client event=scene_loading_failed path={} error={}", pack.Path().string(),
       DescribeSceneError(scene.error()));
    return std::nullopt;
  }
  LI("subsystem=client event=scene_loaded meshes={}", scene->meshes.size());
  return *std::move(scene);
}

// The eye of character, the one this player asked to play, from pack: where its
// camera sits. Reports what is wrong and returns nullopt.
std::optional<math::Vec3> LoadEye(const assets::Pack& pack, std::string_view character) {
  auto eye = LoadCharacterEye(character, [&pack](std::string_view path) { return pack.ResolveEye(path); });
  if (!eye) {
    LE("subsystem=client event=character_eye_loading_failed path={} error={}", pack.Path().string(),
       DescribeCharacterError(eye.error()));
    return std::nullopt;
  }
  return *eye;
}

// Loads a character's mesh and eye from pack by its path, which must be one of
// the scenario's characters (ADR-0042); pack must outlive it. Reports what is
// wrong with the character list and returns nullopt.
std::optional<CharacterLoader> CharacterLoaderFor(const assets::Pack& pack) {
  auto characters = pack.ResolveCharacters();
  if (!characters) {
    LE("subsystem=client event=character_loading_failed path={} error={}", pack.Path().string(),
       std::format("{} {}", assets::kCharactersPath,
                   assets::DescribeResolveError(characters.error(), "character list")));
    return std::nullopt;
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

// The map's collision from pack. Reports what is wrong and returns nullopt.
std::optional<Map> LoadMap(const assets::Pack& pack) {
  auto collision = map::LoadCollision(pack);
  if (!collision) {
    LE("subsystem=client event=map_loading_failed path={} error={}", pack.Path().string(),
       map::DescribeMapError(collision.error()));
    return std::nullopt;
  }
  LI("subsystem=client event=map_loaded colliders={}", collision->size());
  return Map{.collision = *std::move(collision)};
}

// Every cue's sound (ADR-0020), loaded at startup so a pack missing one is found
// before a Match rather than during one. Reports what is wrong and returns nullopt.
std::optional<audio::CueSounds> LoadCueSounds(const assets::Pack& pack) {
  auto sounds = audio::LoadCueSounds(pack);
  if (!sounds) {
    LE("subsystem=client event=cue_sounds_loading_failed path={} error={}", pack.Path().string(),
       audio::DescribeCueSoundError(sounds.error()));
    return std::nullopt;
  }
  LI("subsystem=client event=cue_sounds_loaded cues={}", sounds->size());
  return *std::move(sounds);
}

}  // namespace

std::string_view DescribeContentError(ContentError error) {
  switch (error) {
    case ContentError::kEyeLoading:
      return "character eye loading failed";
    case ContentError::kSceneLoading:
      return "scene loading failed";
    case ContentError::kCharacterLoading:
      return "character loading failed";
    case ContentError::kMapLoading:
      return "map loading failed";
    case ContentError::kCueSoundsLoading:
      return "cue sounds loading failed";
  }
  return "unknown content error";
}

std::expected<Content, ContentError> LoadClientContent(const assets::Pack& pack, std::string_view character) {
  // The camera is attached to the character this player asked to play, at its eye.
  const auto eye = LoadEye(pack, character);
  if (!eye) {
    return std::unexpected(ContentError::kEyeLoading);
  }

  auto scene = LoadScene(pack, *eye);
  if (!scene) {
    return std::unexpected(ContentError::kSceneLoading);
  }

  // A character is loaded only once another player in the Lobby brings it (ADR-0043).
  auto load_character = CharacterLoaderFor(pack);
  if (!load_character) {
    return std::unexpected(ContentError::kCharacterLoading);
  }

  auto map = LoadMap(pack);
  if (!map) {
    return std::unexpected(ContentError::kMapLoading);
  }

  auto cue_sounds = LoadCueSounds(pack);
  if (!cue_sounds) {
    return std::unexpected(ContentError::kCueSoundsLoading);
  }

  return Content{.eye = *eye,
                 .scene = *std::move(scene),
                 .map = *std::move(map),
                 .load_character = *std::move(load_character),
                 .cue_sounds = *std::move(cue_sounds)};
}

}  // namespace augusta::client
