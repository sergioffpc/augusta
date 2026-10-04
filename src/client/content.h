#ifndef AUGUSTA_CLIENT_CONTENT_H_
#define AUGUSTA_CLIENT_CONTENT_H_

#include <expected>
#include <functional>
#include <string_view>
#include <vector>

#include "augusta/assets.h"
#include "augusta/cues.h"
#include "augusta/math.h"
#include "augusta/physics.h"
#include "augusta/renderer.h"
#include "character_loader.h"

// What the client loads from its verified pack at startup (ADR-0018): the scene
// it draws, the map it predicts against, every cue's sound, and how to load the
// characters other players bring once the Lobby names them (ADR-0043). Where
// content comes from is the executable's business, not the orchestrator's:
// ClientRuntime (runtime.h) is handed it already loaded.
namespace augusta::client {

/// The map's collision, the same the server builds from its own pack, so the
/// client's prediction and the server's simulation agree on where the walls are.
struct Map {
  std::vector<physics::CollisionMesh> collision;
};

/// What the client loads of a character another player brings: its visual mesh,
/// which the renderer draws, and its eye, which a spectator watches it from.
struct LoadedCharacter {
  renderer::SceneMesh mesh{};
  math::Vec3 eye{};
};

/// Loads the character with the given path from the client pack
/// (LoadCharacterMesh and LoadCharacterEye), or says why it could not.
using CharacterLoader = std::function<std::expected<LoadedCharacter, CharacterError>(std::string_view)>;

/// Everything LoadClientContent loads, for ClientRuntime's constructor.
struct Content {
  /// Where the local player's camera sits, from the character it asked to play.
  math::Vec3 eye;
  renderer::Scene scene;
  Map map;
  CharacterLoader load_character;
  audio::CueSounds cue_sounds;
};

/// Which part of the startup content could not be loaded; what was wrong with
/// it is logged where it failed.
enum class ContentError {
  kEyeLoading,
  kSceneLoading,
  kCharacterLoading,
  kMapLoading,
  kCueSoundsLoading,
};

[[nodiscard]] std::string_view DescribeContentError(ContentError error);

/// Loads startup content from the verified pack for the local player's
/// character, logging what is wrong with it; the pack must outlive the returned
/// content and the ClientRuntime constructed from it.
[[nodiscard]] std::expected<Content, ContentError> LoadClientContent(const assets::Pack& pack,
                                                                     std::string_view character);

}  // namespace augusta::client

#endif  // AUGUSTA_CLIENT_CONTENT_H_
