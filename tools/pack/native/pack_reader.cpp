#include "pack_reader.h"

#include <exception>
#include <filesystem>
#include <format>
#include <memory>
#include <optional>
#include <stdexcept>
#include <string>
#include <utility>
#include <vector>

#include <pybind11/pybind11.h>
#include <pybind11/stl.h>
#include <pybind11/stl/filesystem.h>

#include "augusta/assets.h"
#include "bytes.h"

namespace augusta::pack {

namespace {

namespace py = pybind11;

// Thrown for a refused ReadPackLayout or Pack::Load, and translated into the
// module's LoadError with reason set.
class LoadFailure : public std::runtime_error {
 public:
  LoadFailure(assets::LoadError reason, const std::filesystem::path& path)
      : std::runtime_error(std::format("{} {}", path.string(), assets::DescribeLoadError(reason))), reason_(reason) {}

  [[nodiscard]] assets::LoadError Reason() const { return reason_; }

 private:
  assets::LoadError reason_;
};

void TranslateLoadFailure(std::exception_ptr exception) {
  try {
    if (exception) {
      std::rethrow_exception(exception);
    }
  } catch (const LoadFailure& failure) {
    const py::object type = py::module_::import("augusta_pack._native").attr("LoadError");
    py::object error = type(failure.what());
    error.attr("reason") = failure.Reason();
    // Python.h's, which pybind11.h includes for its users.
    PyErr_SetObject(type.ptr(), error.ptr());  // NOLINT(misc-include-cleaner)
  }
}

std::optional<py::bytes> ClientPackOf(const std::optional<assets::PackHash>& client_pack) {
  if (!client_pack) {
    return std::nullopt;
  }
  return ToBytes(*client_pack);
}

assets::PackLayout ReadPackLayout(const std::filesystem::path& path) {
  auto layout = assets::ReadPackLayout(path);
  if (!layout) {
    throw LoadFailure(layout.error(), path);
  }
  return *std::move(layout);
}

std::unique_ptr<assets::Pack> LoadPack(const std::filesystem::path& path, const py::bytes& public_key) {
  auto pack = assets::Pack::Load(path, FixedBytesOf<assets::kEd25519PublicKeySize>(public_key, "public_key"));
  if (!pack) {
    throw LoadFailure(pack.error(), path);
  }
  return std::make_unique<assets::Pack>(*std::move(pack));
}

void BindLoadError(py::module_& module) {
  py::enum_<assets::LoadError>(module, "LoadErrorReason", "Why a pack was refused: a LoadError's reason.")
      .value("IO_ERROR", assets::LoadError::kIoError)
      .value("BAD_MAGIC", assets::LoadError::kBadMagic)
      .value("UNSUPPORTED_VERSION", assets::LoadError::kUnsupportedVersion)
      .value("TRUNCATED", assets::LoadError::kTruncated)
      .value("INVALID_INDEX", assets::LoadError::kInvalidIndex)
      .value("HASH_MISMATCH", assets::LoadError::kHashMismatch)
      .value("SIGNATURE_INVALID", assets::LoadError::kSignatureInvalid);
  // PyExc_ValueError: Python.h's, as above.
  py::exception<LoadFailure>(module, "LoadError", PyExc_ValueError);  // NOLINT(misc-include-cleaner)
  py::register_exception_translator(&TranslateLoadFailure);
}

void BindLayout(py::module_& module) {
  py::class_<assets::PackEntry>(module, "PackEntry", "One entry of a pack's index: where its blob lies in the file.")
      .def_readonly("type", &assets::PackEntry::type)
      .def_readonly("offset", &assets::PackEntry::offset, "From the start of the file, in bytes.")
      .def_readonly("size", &assets::PackEntry::size, "The blob's, in bytes.")
      .def_readonly("path", &assets::PackEntry::path);
  py::class_<assets::PackLayout>(module, "PackLayout", "A pack's header, index and trailer, none of them verified.")
      .def_readonly("version", &assets::PackLayout::version)
      .def_readonly("file_size", &assets::PackLayout::file_size)
      .def_readonly("data_offset", &assets::PackLayout::data_offset, "Where the header ends and the data starts.")
      .def_readonly("index_offset", &assets::PackLayout::index_offset, "Where the data ends and the index starts.")
      .def_property_readonly(
          "client_pack_hash", [](const assets::PackLayout& layout) { return ClientPackOf(layout.client_pack); },
          "The hash of the client pack the header names; None if it names none.")
      .def_readonly("entries", &assets::PackLayout::entries)
      .def_property_readonly(
          "hash", [](const assets::PackLayout& layout) { return ToBytes(layout.hash); },
          "The BLAKE3 hash the trailer holds, not checked against the file's.")
      .def_property_readonly(
          "signature", [](const assets::PackLayout& layout) { return ToBytes(layout.signature); },
          "The trailer's Ed25519 signature of hash, not checked against any key.");
  module.def("read_pack_layout", &ReadPackLayout, py::arg("path"),
             "The header, index and trailer of the pack at path, read without a key and not verified.");
}

void BindPack(py::module_& module) {
  py::class_<assets::Pack>(module, "Pack", "A pack Pack::Load verified: its hash, signature, header and index.")
      .def_property_readonly(
          "entries",
          [](const assets::Pack& pack) {
            const auto entries = pack.Entries();
            return std::vector<assets::PackEntry>(entries.begin(), entries.end());
          },
          "Every entry of its index, in index order.")
      .def_property_readonly("hash", [](const assets::Pack& pack) { return ToBytes(pack.Hash()); })
      .def_property_readonly(
          "client_pack_hash", [](const assets::Pack& pack) { return ClientPackOf(pack.ClientPackHash()); },
          "The hash of the client pack its header names; None if it names none.")
      .def_property_readonly("path", &assets::Pack::Path);
  module.def("load_pack", &LoadPack, py::arg("path"), py::arg("public_key"),
             "Loads the pack at path as the client and server do, verifying its hash and its signature against "
             "public_key (32 raw bytes, ADR-0018).");
}

}  // namespace

void BindPackReader(py::module_& module) {
  module.attr("PACK_MAGIC") = std::string(assets::kPackMagic);
  BindLoadError(module);
  BindLayout(module);
  BindPack(module);
}

}  // namespace augusta::pack
