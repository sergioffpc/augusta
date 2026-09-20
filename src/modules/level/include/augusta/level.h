#ifndef AUGUSTA_LEVEL_H_
#define AUGUSTA_LEVEL_H_

#include <expected>
#include <string>
#include <string_view>
#include <vector>

#include "augusta/assets.h"
#include "augusta/physics.h"

// augusta::level turns a verified Pack (ADR-0018) into what a physics::World
// needs to make the level solid. Client and server both call it with their own
// pack (ADR-0019 ships collision data in both), so PredictionWorld and
// SimulationWorld collide against the same geometry built by the same code.
namespace augusta::level {

/// Why a level's collision could not be built from a pack.
enum class LevelErrorCode {
  /// The scene graph could not be resolved; subject is its path.
  kSceneUnresolved,
  /// A node's collider could not be resolved; node is the node's name and subject the collider's path.
  kColliderUnresolved,
  /// A node's collider resolved but is not a usable mesh; node is the node's name,
  /// subject the collider's path, and static_mesh_error says what is wrong with it.
  kInvalidCollider,
  /// The scene has no collider at all, so the level would have no floor or walls.
  kNoCollision,
};

/// A failure to build a level's collision: what went wrong (code) and what it is about.
struct LevelError {
  LevelErrorCode code;
  std::string node{};
  std::string subject{};
  /// Why the pack could not resolve it, for the two kUnresolved codes.
  assets::ResolveError resolve_error{};
  /// What is wrong with the mesh, for kInvalidCollider.
  physics::StaticMeshError static_mesh_error{};
};

/// A message describing error, for whoever runs the process to read.
std::string DescribeLevelError(const LevelError& error);

/// The collision meshes of the scene graph at scene_path in pack, in world space,
/// one per node that references a collider.
std::expected<std::vector<physics::StaticMesh>, LevelError> LoadCollision(
    const assets::Pack& pack, std::string_view scene_path = assets::kScenePath);

}  // namespace augusta::level

#endif  // AUGUSTA_LEVEL_H_
