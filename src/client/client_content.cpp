#include "client_content.h"

#include <expected>
#include <memory>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

#include "augusta/assets.h"
#include "augusta/clips.h"
#include "augusta/cues.h"
#include "augusta/failure.h"
#include "augusta/logging.h"
#include "augusta/map.h"
#include "augusta/math.h"
#include "augusta/renderer.h"
#include "augusta/weapon.h"
#include "client_character_loader.h"
#include "client_scene_loader.h"

#if AUGUSTA_DEBUG_TOOLS
#include "augusta/hitbox.h"
#endif

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
  LOG_INFO("subsystem=client event=scene_loaded meshes={}", scene->meshes.size());
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

// The Skeleton of character, the one this player asked to play, from pack: what
// its Clips pose.
std::expected<assets::Skeleton, failure::Failure> LoadSkeleton(const assets::Pack& pack, std::string_view character) {
  auto skeleton = pack.ResolveSkeleton(character);
  if (!skeleton) {
    return std::unexpected(ContentFailure(pack, {{.key = "character", .value = std::string(character)}},
                                          assets::DescribeResolveError(skeleton.error(), "skeleton")));
  }
  return *std::move(skeleton);
}

// The Clips of character, the one this player asked to play, of its skeleton,
// from pack: what its predicted Stride advances by and its body holds the rifle
// with (ADR-0066). The server refuses a pack whose Clips do not load, so a client
// refuses it too, naming the part as the server does.
std::expected<std::shared_ptr<const animation::CharacterClips>, failure::Failure> LoadClips(
    const assets::Pack& pack, std::string_view character, const assets::Skeleton& skeleton) {
  std::vector<failure::ContextField> context = {{.key = "character", .value = std::string(character)}};
  auto clips = animation::LoadClips(pack, character, skeleton);
  if (!clips) {
    if (clips.error().part == animation::ClipPart::kClip) {
      context.push_back({.key = "clip", .value = clips.error().role});
    }
    return std::unexpected(ContentFailure(pack, std::move(context), animation::DescribeClipLoadError(clips.error())));
  }
  return std::make_shared<const animation::CharacterClips>(*std::move(clips));
}

#if AUGUSTA_DEBUG_TOOLS
// The Hitboxes of character from pack, for the Hitboxes Debug overlay, in its
// root space where its Skeleton holds them in the Rest pose, or none with a
// warning if they do not load: the server refuses such a pack at startup, and a
// Debug overlay never keeps a character from loading.
std::vector<hitbox::CharacterHitbox> LoadDebugHitboxes(const assets::Pack& pack, std::string_view character) {
  auto hitboxes = LoadCharacterHitboxes(
      character, [&pack](std::string_view name) { return pack.ResolveSkeleton(name); },
      [&pack](std::string_view name) { return pack.ResolveHitboxes(name); });
  if (!hitboxes) {
    LOG_WARN("subsystem=client event=character_hitboxes_skipped character={} reason=\"{}\"", character,
             DescribeCharacterError(hitboxes.error()));
    return {};
  }
  return *std::move(hitboxes);
}
#endif

// Loads a character's mesh and eye, and its Hitboxes in a Development build,
// from pack by its path, which must be one of the scenario's characters
// (ADR-0042); pack must outlive it. Fails if the character list does not load.
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
              .transform([&](const math::Vec3& eye) {
                return LoadedCharacter{
                    .mesh = std::move(mesh),
                    .eye = eye,
#if AUGUSTA_DEBUG_TOOLS
                    .hitboxes = LoadDebugHitboxes(pack, character),
#endif
                };
              });
        });
  };
}

// The scenario's rifle from pack (ADR-0064), whose Muzzle the local player's
// muzzle flash shows at. The server refuses a pack whose rifle does not resolve,
// so a client refuses it too.
std::expected<weapon::Markers, failure::Failure> LoadRifle(const assets::Pack& pack) {
  const auto rifle = pack.ResolveRifle();
  if (!rifle) {
    return std::unexpected(ContentFailure(pack, {{.key = "asset", .value = std::string(assets::kRiflePath)}},
                                          assets::DescribeResolveError(rifle.error(), "rifle")));
  }
  return weapon::MarkersOf(*rifle);
}

// The map's collision from pack.
std::expected<Map, failure::Failure> LoadMap(const assets::Pack& pack) {
  auto collision = map::LoadCollision(pack);
  if (!collision) {
    return std::unexpected(
        ContentFailure(pack, {{.key = "asset", .value = "collision"}}, map::DescribeMapError(collision.error())));
  }
  LOG_INFO("subsystem=client event=map_loaded colliders={}", collision->size());
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
  LOG_INFO("subsystem=client event=cue_sounds_loaded cues={}", sounds->size());
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

  auto skeleton = LoadSkeleton(pack, character);
  if (!skeleton) {
    return std::unexpected(std::move(skeleton.error()));
  }

  auto clips = LoadClips(pack, character, *skeleton);
  if (!clips) {
    return std::unexpected(std::move(clips.error()));
  }

  const auto rifle = LoadRifle(pack);
  if (!rifle) {
    return std::unexpected(rifle.error());
  }

  return Content{.eye = *eye,
                 .scene = *std::move(scene),
                 .map = *std::move(map),
                 .load_character = *std::move(load_character),
                 .cue_sounds = *std::move(cue_sounds),
                 .skeleton = *std::move(skeleton),
                 .clips = *std::move(clips),
                 .rifle = *rifle};
}

}  // namespace augusta::client
