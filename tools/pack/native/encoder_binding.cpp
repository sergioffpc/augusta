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
      .value("PARTIAL_SAMPLE", assets::EncodeError::kPartialSample);
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
      .value("SOUNDS", assets::AssetType::kSounds);
  py::enum_<assets::BodyPart>(module, "BodyPart", "Where on a player a hitbox is (US-11).")
      .value("HEAD", assets::BodyPart::kHead)
      .value("TORSO", assets::BodyPart::kTorso)
      .value("LIMB", assets::BodyPart::kLimb);
}

void BindConstants(py::module_& module) {
  module.attr("SCENE_PATH") = std::string(assets::kScenePath);
  module.attr("CHARACTERS_PATH") = std::string(assets::kCharactersPath);
  module.attr("SOUNDS_PATH") = std::string(assets::kSoundsPath);
  module.attr("SCENE_NODE_NO_PARENT") = assets::kSceneNodeNoParent;
  module.attr("MAX_CHARACTERS") = assets::kMaxCharacters;
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
  py::class_<assets::HitboxData>(module, "HitboxData", "A hitbox: the body part it stands for, and its geometry.")
      .def(py::init([](assets::BodyPart part, assets::MeshData mesh) {
             return assets::HitboxData{.part = part, .mesh = std::move(mesh)};
           }),
           py::arg("part"), py::arg("mesh"))
      .def_readwrite("part", &assets::HitboxData::part)
      .def_readwrite("mesh", &assets::HitboxData::mesh);
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
      "encode_eye_blob", [](const assets::EyeData& eye) { return BlobOrThrow(assets::EncodeEyeBlob(eye), "eye"); },
      py::arg("eye"), "An eye blob (ADR-0040).");
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
      "encode_sounds_blob",
      [](std::string_view sounds_path) { return BlobOrThrow(assets::EncodeSoundsBlob(sounds_path), "sounds path"); },
      py::arg("sounds_path"), "The prefix the cue sounds are addressed under (ADR-0031).");
}

}  // namespace

void BindPackEncoder(py::module_& module) {
  BindErrors(module);
  BindEnums(module);
  BindConstants(module);
  BindGeometryTypes(module);
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
