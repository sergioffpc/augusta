#ifndef AUGUSTA_CLIENT_CONTENT_H_
#define AUGUSTA_CLIENT_CONTENT_H_

#include <expected>
#include <functional>
#include <memory>
#include <string_view>
#include <vector>

#include "augusta/assets.h"
#include "augusta/clips.h"
#include "augusta/cues.h"
#include "augusta/failure.h"
#include "augusta/math.h"
#include "augusta/physics.h"
#include "augusta/renderer.h"
#include "client_character_loader.h"

#if AUGUSTA_DEBUG_TOOLS
#include "augusta/hitbox.h"
#endif

/// \file
/// What the client loads from its verified pack at startup (ADR-0018): the scene
/// it draws, the map it predicts against, every cue's sound, the Clips of the
/// character it asked to play, which its predicted Stride advances by, and how to load the
/// characters other players bring once the Lobby names them (ADR-0043). Where
/// content comes from is the executable's business, not the orchestrator's:
/// ClientRuntime (client.h) is handed it already loaded.
namespace augusta::client {

/// The map's collision, the same the server builds from its own pack, so the
/// client's prediction and the server's simulation agree on where the walls are.
struct Map {
  std::vector<physics::CollisionMesh> collision;
};

/// What the client loads of a character another player brings: its visual mesh,
/// which the renderer draws, its eye, which a spectator watches it from, and in
/// a Development build (ADR-0074) its Hitboxes, which the Hitboxes Debug overlay
/// draws.
struct LoadedCharacter {
  renderer::SceneMesh mesh{};
  math::Vec3 eye{};
#if AUGUSTA_DEBUG_TOOLS
  /// None for a character that has none, or whose Hitboxes do not load: a
  /// Debug overlay never keeps a character from loading.
  std::vector<hitbox::CharacterHitbox> hitboxes;
#endif
};

/// Loads the character with the given path from the client pack
/// (LoadCharacterMesh and LoadCharacterEye, and LoadCharacterHitboxes in a
/// Development build), or says why it could not.
using CharacterLoader = std::function<std::expected<LoadedCharacter, CharacterError>(std::string_view)>;

/// Everything LoadClientContent loads, for ClientRuntime's constructor.
struct Content {
  /// Where the local player's camera sits, from the character it asked to play.
  math::Vec3 eye;
  renderer::Scene scene;
  Map map;
  CharacterLoader load_character;
  audio::CueSounds cue_sounds;
  /// The Clips of the character it asked to play (ADR-0066): what PredictionWorld
  /// advances its Stride by, as the server's World does.
  std::shared_ptr<const animation::CharacterClips> clips;
};

/// Loads startup content from the verified pack for the local player's
/// character, or returns what is wrong with it: failure::Code::kInvalidContent,
/// with the pack's path and the part of it that failed as context, for the
/// application boundary to report. The pack must outlive the returned content
/// and the ClientRuntime constructed from it.
[[nodiscard]] std::expected<Content, failure::Failure> LoadClientContent(const assets::Pack& pack,
                                                                         std::string_view character);

}  // namespace augusta::client

#endif  // AUGUSTA_CLIENT_CONTENT_H_
