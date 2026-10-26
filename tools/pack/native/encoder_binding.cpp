#include "encoder_binding.h"

#include <cstddef>
#include <cstdint>
#include <expected>
#include <filesystem>
#include <format>
#include <optional>
#include <span>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

#include <pybind11/pybind11.h>
#include <pybind11/stl.h>
#include <pybind11/stl/filesystem.h>

#include "augusta/assets.h"
#include "augusta/math.h"
#include "augusta/pack_encoder.h"
#include "bytes.h"
#include "failure.h"
#include "math_caster.h"  // IWYU pragma: keep - the Vec3/Quat conversions

namespace augusta::pack {

namespace {

namespace py = pybind11;

// what names the blob, e.g. "mesh", in the EncodeError a refusal raises.
py::bytes BlobOrThrow(const std::expected<std::vector<std::byte>, assets::EncodeError>& blob, std::string_view what) {
  if (!blob) {
    throw Failure(blob.error(), std::format("{} {}", what, assets::DescribeEncodeError(blob.error())));
  }
  return ToBytes(*blob);
}

py::bytes WritePack(const std::filesystem::path& output_path, const std::vector<assets::AssetEntry>& entries,
                    const py::bytes& signing_key, const std::optional<py::bytes>& client_pack_hash) {
  std::optional<assets::PackHash> client_pack;
  if (client_pack_hash) {
    client_pack = FixedBytesOf<assets::kPackHashSize>(*client_pack_hash, "client_pack_hash");
  }
  const auto written = assets::WritePack(
      output_path, entries, FixedBytesOf<assets::kEd25519PrivateKeySize>(signing_key, "signing_key"), client_pack);
  if (!written) {
    throw Failure(written.error(),
                  std::format("{} {}", output_path.string(), assets::DescribeWriteError(written.error())));
  }
  return ToBytes(*written);
}

void BindErrors(py::module_& module) {
  py::enum_<assets::EncodeError>(module, "EncodeErrorReason", "Why a blob was refused: an EncodeError's reason.")
      .value("TOO_LARGE", assets::EncodeError::kTooLarge)
      .value("UNSUPPORTED_SAMPLE_WIDTH", assets::EncodeError::kUnsupportedSampleWidth)
      .value("PARTIAL_SAMPLE", assets::EncodeError::kPartialSample)
      .value("INVALID_CUE_SOUND", assets::EncodeError::kInvalidCueSound)
      .value("INVALID_ANIMATION", assets::EncodeError::kInvalidAnimation);
  py::enum_<assets::WriteError>(module, "WriteErrorReason", "Why a pack was not written: a WriteError's reason.")
      .value("IO_ERROR", assets::WriteError::kIoError)
      .value("DUPLICATE_PATH", assets::WriteError::kDuplicatePath)
      .value("TOO_LARGE", assets::WriteError::kTooLarge);
  // PyExc_*: Python.h's, which pybind11.h includes for its users.
  RegisterFailure<assets::EncodeError>(module, "EncodeError", PyExc_ValueError);  // NOLINT(misc-include-cleaner)
  RegisterFailure<assets::WriteError>(module, "WriteError", PyExc_RuntimeError);  // NOLINT(misc-include-cleaner)
}

void BindEnums(py::module_& module) {
  py::enum_<assets::AssetType>(module, "AssetType", "An index entry's type tag (ADR-0031).")
      .value("MESH", assets::AssetType::kMesh)
      .value("TEXTURE", assets::AssetType::kTexture)
      .value("AUDIO", assets::AssetType::kAudio)
      .value("COLLISION", assets::AssetType::kCollision)
      .value("SPAWN_POINT", assets::AssetType::kSpawnPoint)
      .value("HITBOX", assets::AssetType::kHitbox)
      .value("SCENE", assets::AssetType::kScene)
      .value("SCRIPT", assets::AssetType::kScript)
      .value("CHARACTERS", assets::AssetType::kCharacters)
      .value("EYE", assets::AssetType::kEye)
      .value("CUE_SOUND", assets::AssetType::kCueSound)
      .value("SKELETON", assets::AssetType::kSkeleton)
      .value("ANIMATION_SKELETON", assets::AssetType::kAnimationSkeleton)
      .value("CLIP", assets::AssetType::kClip)
      .value("STANCE_TRANSITIONS", assets::AssetType::kStanceTransitions)
      .value("RIFLE", assets::AssetType::kRifle)
      .value("WEAPON_SOCKET", assets::AssetType::kWeaponSocket);
  py::enum_<assets::BodyPart>(module, "BodyPart", "Where on a player a hitbox is (US-11).")
      .value("HEAD", assets::BodyPart::kHead)
      .value("TORSO", assets::BodyPart::kTorso)
      .value("LIMB", assets::BodyPart::kLimb);
  py::enum_<assets::Foot>(module, "Foot", "The foot a Foot contact lands (ADR-0062).")
      .value("LEFT", assets::Foot::kLeft)
      .value("RIGHT", assets::Foot::kRight);
}

void BindConstants(py::module_& module) {
  module.attr("SCENE_PATH") = std::string(assets::kScenePath);
  module.attr("CHARACTERS_PATH") = std::string(assets::kCharactersPath);
  module.attr("RIFLE_PATH") = std::string(assets::kRiflePath);
  module.attr("SOUNDS_PREFIX") = std::string(assets::kSoundsPrefix);
  module.attr("SCENE_NODE_NO_PARENT") = assets::kSceneNodeNoParent;
  module.attr("MAX_CHARACTERS") = assets::kMaxCharacters;
  module.attr("MAX_JOINTS") = assets::kMaxJoints;
  module.attr("JOINT_NO_PARENT") = assets::kJointNoParent;
}

void BindGeometryTypes(py::module_& module) {
  py::class_<assets::MeshData>(module, "MeshData",
                               "A triangle mesh: its points, and three indices per triangle. Reading points or "
                               "indices gives a copy, so a change to it, such as append, is lost: assign the whole "
                               "list instead.")
      .def(py::init([](std::vector<math::Vec3> points, std::vector<std::uint32_t> indices) {
             return assets::MeshData{.points = std::move(points), .indices = std::move(indices)};
           }),
           py::arg("points"), py::arg("indices"))
      .def_readwrite("points", &assets::MeshData::points)
      .def_readwrite("indices", &assets::MeshData::indices);
  py::class_<assets::HitboxData>(
      module, "HitboxData",
      "A hitbox: the body part it stands for, the name of the Joint it moves with, and its geometry in that "
      "Joint's space (ADR-0061).")
      .def(py::init([](assets::BodyPart part, std::string joint, assets::MeshData mesh) {
             return assets::HitboxData{.part = part, .joint = std::move(joint), .mesh = std::move(mesh)};
           }),
           py::arg("part"), py::arg("joint"), py::arg("mesh"))
      .def_readwrite("part", &assets::HitboxData::part)
      .def_readwrite("joint", &assets::HitboxData::joint)
      .def_readwrite("mesh", &assets::HitboxData::mesh);
  py::class_<assets::JointData>(module, "JointData",
                                "One Joint of a Skeleton: its name, the path within the Skeleton; its parent's index "
                                "(JOINT_NO_PARENT for a root); its rest transform relative to that parent (ADR-0061).")
      .def(py::init([](std::string name, std::uint32_t parent, math::Vec3 translation, math::Quat rotation,
                       math::Vec3 scale) {
             return assets::JointData{.name = std::move(name),
                                      .parent = parent,
                                      .translation = translation,
                                      .rotation = rotation,
                                      .scale = scale};
           }),
           py::arg("name"), py::arg("parent") = assets::kJointNoParent,
           py::arg("translation") = math::Vec3(0.0F, 0.0F, 0.0F),
           py::arg("rotation") = math::Quat(1.0F, 0.0F, 0.0F, 0.0F), py::arg("scale") = math::Vec3(1.0F, 1.0F, 1.0F))
      .def_readwrite("name", &assets::JointData::name)
      .def_readwrite("parent", &assets::JointData::parent)
      .def_readwrite("translation", &assets::JointData::translation)
      .def_readwrite("rotation", &assets::JointData::rotation)
      .def_readwrite("scale", &assets::JointData::scale);
  py::class_<assets::SpawnPointData>(module, "SpawnPointData", "A spawn point's transform (ADR-0032).")
      .def(py::init([](math::Vec3 translation, math::Quat rotation) {
             return assets::SpawnPointData{.translation = translation, .rotation = rotation};
           }),
           py::arg("translation"), py::arg("rotation"))
      .def_readwrite("translation", &assets::SpawnPointData::translation)
      .def_readwrite("rotation", &assets::SpawnPointData::rotation);
  py::class_<assets::EyeData>(module, "EyeData", "A character's eye, in its own root space (ADR-0040).")
      .def(py::init([](math::Vec3 position) { return assets::EyeData{.position = position}; }), py::arg("position"))
      .def_readwrite("position", &assets::EyeData::position);
}

void BindRifleTypes(py::module_& module) {
  py::class_<assets::RifleMarker>(module, "RifleMarker",
                                  "One of the rifle's markers: a frame in the rifle's space, pointing along its -Z "
                                  "(ADR-0064).")
      .def(py::init([](math::Vec3 translation, math::Quat rotation) {
             return assets::RifleMarker{.translation = translation, .rotation = rotation};
           }),
           py::arg("translation"), py::arg("rotation"))
      .def_readwrite("translation", &assets::RifleMarker::translation)
      .def_readwrite("rotation", &assets::RifleMarker::rotation);
  py::class_<assets::RifleData>(module, "RifleData", "The rifle's Grip, Muzzle, Sight and Fore-end (ADR-0064).")
      .def(py::init([](assets::RifleMarker grip, assets::RifleMarker muzzle, assets::RifleMarker sight,
                       assets::RifleMarker fore_end) {
             return assets::RifleData{.grip = grip, .muzzle = muzzle, .sight = sight, .fore_end = fore_end};
           }),
           py::arg("grip"), py::arg("muzzle"), py::arg("sight"), py::arg("fore_end"))
      .def_readwrite("grip", &assets::RifleData::grip)
      .def_readwrite("muzzle", &assets::RifleData::muzzle)
      .def_readwrite("sight", &assets::RifleData::sight)
      .def_readwrite("fore_end", &assets::RifleData::fore_end);
  py::class_<assets::WeaponSocketData>(module, "WeaponSocketData",
                                       "A character's weapon socket on its right hand's Joint, and its left hand's "
                                       "Joint (ADR-0066).")
      .def(py::init([](std::string right_hand, math::Vec3 translation, math::Quat rotation, std::string left_hand) {
             return assets::WeaponSocketData{.right_hand = std::move(right_hand),
                                             .translation = translation,
                                             .rotation = rotation,
                                             .left_hand = std::move(left_hand)};
           }),
           py::arg("right_hand"), py::arg("translation"), py::arg("rotation"), py::arg("left_hand"))
      .def_readwrite("right_hand", &assets::WeaponSocketData::right_hand)
      .def_readwrite("translation", &assets::WeaponSocketData::translation)
      .def_readwrite("rotation", &assets::WeaponSocketData::rotation)
      .def_readwrite("left_hand", &assets::WeaponSocketData::left_hand);
}

void BindSceneNode(py::module_& module) {
  py::class_<assets::SceneNode>(module, "SceneNode",
                                "One node of a scene blob, its transform local (ADR-0032). Reading properties gives "
                                "a copy, so a change to it, such as append, is lost: assign the whole list instead.")
      .def(py::init([](std::string name, std::uint32_t parent_index, math::Vec3 translation, math::Quat rotation,
                       math::Vec3 scale, std::optional<std::string> mesh_path, std::optional<std::string> material_path,
                       std::optional<std::string> collider_path, std::optional<std::string> hitbox_path,
                       bool is_spawn_point, std::vector<std::pair<std::string, std::string>> properties) {
             return assets::SceneNode{.name = std::move(name),
                                      .parent_index = parent_index,
                                      .translation = translation,
                                      .rotation = rotation,
                                      .scale = scale,
                                      .mesh_path = std::move(mesh_path),
                                      .material_path = std::move(material_path),
                                      .collider_path = std::move(collider_path),
                                      .hitbox_path = std::move(hitbox_path),
                                      .is_spawn_point = is_spawn_point,
                                      .properties = std::move(properties)};
           }),
           py::arg("name"), py::arg("parent_index") = assets::kSceneNodeNoParent,
           py::arg("translation") = math::Vec3(0.0F, 0.0F, 0.0F),
           py::arg("rotation") = math::Quat(1.0F, 0.0F, 0.0F, 0.0F), py::arg("scale") = math::Vec3(1.0F, 1.0F, 1.0F),
           py::arg("mesh_path") = py::none(), py::arg("material_path") = py::none(),
           py::arg("collider_path") = py::none(), py::arg("hitbox_path") = py::none(),
           py::arg("is_spawn_point") = false,
           py::arg("properties") = std::vector<std::pair<std::string, std::string>>{})
      .def_readwrite("name", &assets::SceneNode::name)
      .def_readwrite("parent_index", &assets::SceneNode::parent_index)
      .def_readwrite("translation", &assets::SceneNode::translation)
      .def_readwrite("rotation", &assets::SceneNode::rotation)
      .def_readwrite("scale", &assets::SceneNode::scale)
      .def_readwrite("mesh_path", &assets::SceneNode::mesh_path)
      .def_readwrite("material_path", &assets::SceneNode::material_path)
      .def_readwrite("collider_path", &assets::SceneNode::collider_path)
      .def_readwrite("hitbox_path", &assets::SceneNode::hitbox_path)
      .def_readwrite("is_spawn_point", &assets::SceneNode::is_spawn_point)
      .def_readwrite("properties", &assets::SceneNode::properties);
}

void BindBlobTypes(py::module_& module) {
  py::class_<assets::CueSoundData>(module, "CueSoundData",
                                   "A cue's sound (ADR-0041): its gain in decibels and its variants' paths.")
      .def(py::init([](float gain_db, std::vector<std::string> variants) {
             return assets::CueSoundData{.gain_db = gain_db, .variants = std::move(variants)};
           }),
           py::arg("gain_db"), py::arg("variants"))
      .def_readwrite("gain_db", &assets::CueSoundData::gain_db)
      .def_readwrite("variants", &assets::CueSoundData::variants);
  py::class_<assets::AudioData>(module, "AudioData", "A mono PCM sound (ADR-0020).")
      .def(py::init([](std::uint32_t sample_rate, std::uint8_t bits_per_sample, const py::bytes& samples) {
             return assets::AudioData{
                 .sample_rate = sample_rate, .bits_per_sample = bits_per_sample, .samples = BytesOf(samples)};
           }),
           py::arg("sample_rate"), py::arg("bits_per_sample"), py::arg("samples"))
      .def_readwrite("sample_rate", &assets::AudioData::sample_rate)
      .def_readwrite("bits_per_sample", &assets::AudioData::bits_per_sample)
      .def_property(
          "samples", [](const assets::AudioData& sound) { return ToBytes(sound.samples); },
          [](assets::AudioData& sound, const py::bytes& samples) { sound.samples = BytesOf(samples); });
  py::class_<assets::AssetEntry>(module, "AssetEntry", "One index entry of a pack: an asset's type, path and blob.")
      .def(py::init([](assets::AssetType type, std::string path, const py::bytes& data) {
             return assets::AssetEntry{.type = type, .path = std::move(path), .data = BytesOf(data)};
           }),
           py::arg("type"), py::arg("path"), py::arg("data"))
      .def_readwrite("type", &assets::AssetEntry::type)
      .def_readwrite("path", &assets::AssetEntry::path)
      .def_property(
          "data", [](const assets::AssetEntry& entry) { return ToBytes(entry.data); },
          [](assets::AssetEntry& entry, const py::bytes& data) { entry.data = BytesOf(data); });
}

void BindEncodeFunctions(py::module_& module) {
  module.def(
      "encode_mesh_blob",
      [](const assets::MeshData& mesh) { return BlobOrThrow(assets::EncodeMeshBlob(mesh), "mesh"); }, py::arg("mesh"),
      "A mesh or collision blob (ADR-0031).");
  module.def(
      "encode_scene_blob",
      [](std::vector<assets::SceneNode> nodes) {
        return BlobOrThrow(assets::EncodeSceneBlob({.nodes = std::move(nodes)}), "scene");
      },
      py::arg("nodes"), "A scene blob of nodes, parents before children (ADR-0032).");
  module.def(
      "encode_texture_blob",
      [](const py::bytes& ktx2) { return BlobOrThrow(assets::EncodeTextureBlob(BytesOf(ktx2)), "texture"); },
      py::arg("ktx2"), "A texture blob of a KTX2 file (ADR-0017).");
  module.def(
      "encode_spawn_point_blob",
      [](const assets::SpawnPointData& spawn_point) {
        return BlobOrThrow(assets::EncodeSpawnPointBlob(spawn_point), "spawn point");
      },
      py::arg("spawn_point"));
  module.def(
      "encode_hitbox_blob",
      [](const assets::HitboxData& hitbox) { return BlobOrThrow(assets::EncodeHitboxBlob(hitbox), "hitbox"); },
      py::arg("hitbox"), "A hitbox blob (ADR-0040).");
  module.def(
      "encode_skeleton_blob",
      [](std::vector<assets::JointData> joints) {
        const auto what = std::format("a skeleton of {} joints", joints.size());
        return BlobOrThrow(assets::EncodeSkeletonBlob({.joints = std::move(joints)}), what);
      },
      py::arg("joints"), "A skeleton blob of joints, parents before children (ADR-0061).");
  module.def(
      "encode_eye_blob", [](const assets::EyeData& eye) { return BlobOrThrow(assets::EncodeEyeBlob(eye), "eye"); },
      py::arg("eye"), "An eye blob (ADR-0040).");
  module.def(
      "encode_rifle_blob",
      [](const assets::RifleData& rifle) { return BlobOrThrow(assets::EncodeRifleBlob(rifle), "rifle"); },
      py::arg("rifle"), "A rifle blob: its Grip, Muzzle, Sight and Fore-end (ADR-0064).");
  module.def(
      "encode_weapon_socket_blob",
      [](const assets::WeaponSocketData& socket) {
        return BlobOrThrow(assets::EncodeWeaponSocketBlob(socket), "weapon socket");
      },
      py::arg("socket"), "A weapon socket blob (ADR-0066).");
  module.def(
      "encode_script_blob",
      [](const py::bytes& script) { return BlobOrThrow(assets::EncodeScriptBlob(std::string_view(script)), "script"); },
      py::arg("script"), "A script blob of a Lua script's text (ADR-0031).");
  module.def(
      "encode_characters_blob",
      [](const std::vector<std::string>& characters) {
        return BlobOrThrow(assets::EncodeCharactersBlob(characters),
                           std::format("a character list of {}", characters.size()));
      },
      py::arg("characters"), "The scenario's character list, in manifest order (ADR-0042).");
  module.def(
      "encode_audio_blob",
      [](const assets::AudioData& sound) { return BlobOrThrow(assets::EncodeAudioBlob(sound), "audio"); },
      py::arg("sound"), "An audio blob (ADR-0020).");
  module.def(
      "encode_cue_sound_blob",
      [](const assets::CueSoundData& sound) { return BlobOrThrow(assets::EncodeCueSoundBlob(sound), "cue sound"); },
      py::arg("sound"), "A cue's sound: its gain and its variants' paths (ADR-0041).");
  module.def(
      "encode_animation_skeleton_blob",
      [](const py::bytes& archive) {
        return BlobOrThrow(assets::EncodeAnimationSkeletonBlob(BytesOf(archive)), "animation skeleton");
      },
      py::arg("archive"), "An animation-skeleton blob of an ozz-animation Skeleton archive (ADR-0066).");
  module.def(
      "encode_clip_blob",
      [](float stride_length, const std::vector<std::pair<assets::Foot, float>>& foot_contacts,
         const py::bytes& archive) {
        assets::ClipData clip{.stride_length = stride_length, .archive = BytesOf(archive)};
        for (const auto& [foot, ratio] : foot_contacts) {
          clip.foot_contacts.push_back({.foot = foot, .ratio = ratio});
        }
        return BlobOrThrow(assets::EncodeClipBlob(clip), "clip");
      },
      py::arg("stride_length"), py::arg("foot_contacts"), py::arg("archive"),
      "A clip blob (ADR-0066): its stride length in meters, its Foot contacts as (foot, ratio of its duration) in "
      "time order, and its ozz-animation Animation archive.");
  module.def(
      "encode_stance_transitions_blob",
      [](float standing_crouching, float standing_prone, float crouching_prone) {
        return BlobOrThrow(assets::EncodeStanceTransitionsBlob({.standing_crouching = standing_crouching,
                                                                .standing_prone = standing_prone,
                                                                .crouching_prone = crouching_prone}),
                           "stance transitions");
      },
      py::arg("standing_crouching"), py::arg("standing_prone"), py::arg("crouching_prone"),
      "A stance-transitions blob (ADR-0066): each pair of stances' transition duration, in seconds, either way.");
  module.def("character_animation_skeleton_path", &assets::CharacterAnimationSkeletonPath, py::arg("character"),
             "The pack-relative path of the character's ozz-animation Skeleton, e.g. soldier/Character/Skeleton.ozz.");
  module.def("character_clip_path", &assets::CharacterClipPath, py::arg("character"), py::arg("role"),
             "The pack-relative path of the character's Clip of role, e.g. soldier/Character/Clips/standing/forward.");
  module.def("character_stance_transitions_path", &assets::CharacterStanceTransitionsPath, py::arg("character"),
             "The pack-relative path of the character's stance transitions, e.g. soldier/Character/Clips.");
  module.def("cue_sound_path", &assets::CueSoundPath, py::arg("cue"),
             "The pack-relative path of the cue named cue's sound, e.g. sounds/gunshot (ADR-0031).");
  module.def("cue_variant_path", &assets::CueVariantPath, py::arg("cue"), py::arg("variant"),
             "The pack-relative path of the cue named cue's variant-th variant, e.g. sounds/gunshot/0 (ADR-0031).");
}

}  // namespace

void BindPackEncoder(py::module_& module) {
  BindErrors(module);
  BindEnums(module);
  BindConstants(module);
  BindGeometryTypes(module);
  BindRifleTypes(module);
  BindSceneNode(module);
  BindBlobTypes(module);
  BindEncodeFunctions(module);
  module.def("write_pack", &WritePack, py::arg("output_path"), py::arg("entries"), py::arg("signing_key"),
             py::arg("client_pack_hash") = py::none(),
             "Writes entries into a new pack at output_path, signed with signing_key (64 raw bytes, ADR-0018), its "
             "header naming client_pack_hash when given (a server pack's). Atomic: no pack is left behind on a "
             "refusal. Returns the pack's BLAKE3 hash, the one its trailer signs.");
}

}  // namespace augusta::pack
