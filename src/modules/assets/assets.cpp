#include "augusta/assets.h"

#include <algorithm>
#include <array>
#include <cstring>
#include <fstream>
#include <optional>
#include <span>
#include <system_error>

namespace augusta::assets {

namespace {

constexpr std::array<char, 4> kMagic = {'A', 'U', 'G', 'P'};
constexpr std::uint32_t kFormatVersion = 1;
// magic + version(u32) + data_offset(u64) + index_offset(u64) + index_count(u32),
// per ADR-0031's header field list.
constexpr std::uint64_t kHeaderSize =
    kMagic.size() + sizeof(std::uint32_t) + sizeof(std::uint64_t) + sizeof(std::uint64_t) + sizeof(std::uint32_t);

constexpr std::size_t kBitsPerByte = 8;
constexpr std::uint32_t kByteMask = 0xFFU;

void AppendU8(std::vector<std::byte>& buf, std::uint8_t value) { buf.push_back(static_cast<std::byte>(value)); }

void AppendU32(std::vector<std::byte>& buf, std::uint32_t value) {
  for (std::size_t i = 0; i < sizeof(value); ++i) {
    buf.push_back(static_cast<std::byte>((value >> (kBitsPerByte * i)) & kByteMask));
  }
}

void AppendU64(std::vector<std::byte>& buf, std::uint64_t value) {
  for (std::size_t i = 0; i < sizeof(value); ++i) {
    buf.push_back(static_cast<std::byte>((value >> (kBitsPerByte * i)) & kByteMask));
  }
}

void AppendF32(std::vector<std::byte>& buf, float value) {
  std::uint32_t bits = 0;
  std::memcpy(&bits, &value, sizeof(bits));
  AppendU32(buf, bits);
}

void AppendChars(std::vector<std::byte>& buf, std::string_view chars) {
  for (char c : chars) {
    buf.push_back(static_cast<std::byte>(static_cast<unsigned char>(c)));
  }
}

void AppendBytes(std::vector<std::byte>& buf, std::span<const std::byte> data) {
  buf.insert(buf.end(), data.begin(), data.end());
}

// Reads primitive values out of a byte span left-to-right, failing (via
// std::nullopt) the moment a read would run past the end - the one place
// both header and index parsing route every bounds check through.
class ByteReader {
 public:
  explicit ByteReader(std::span<const std::byte> data) : data_(data) {}

  std::optional<std::uint8_t> ReadU8() {
    const auto bytes = ReadBytes(sizeof(std::uint8_t));
    if (!bytes) {
      return std::nullopt;
    }
    return static_cast<std::uint8_t>((*bytes)[0]);
  }

  std::optional<std::uint32_t> ReadU32() {
    const auto bytes = ReadBytes(sizeof(std::uint32_t));
    if (!bytes) {
      return std::nullopt;
    }
    std::uint32_t value = 0;
    for (std::size_t i = 0; i < bytes->size(); ++i) {
      value |= static_cast<std::uint32_t>((*bytes)[i]) << (kBitsPerByte * i);
    }
    return value;
  }

  std::optional<std::uint64_t> ReadU64() {
    const auto bytes = ReadBytes(sizeof(std::uint64_t));
    if (!bytes) {
      return std::nullopt;
    }
    std::uint64_t value = 0;
    for (std::size_t i = 0; i < bytes->size(); ++i) {
      value |= static_cast<std::uint64_t>((*bytes)[i]) << (kBitsPerByte * i);
    }
    return value;
  }

  std::optional<float> ReadF32() {
    const auto bits = ReadU32();
    if (!bits) {
      return std::nullopt;
    }
    float value = 0.0F;
    const std::uint32_t raw = *bits;
    std::memcpy(&value, &raw, sizeof(value));
    return value;
  }

  std::optional<std::span<const std::byte>> ReadBytes(std::size_t count) {
    if (count > data_.size() - pos_) {
      return std::nullopt;
    }
    const auto result = data_.subspan(pos_, count);
    pos_ += count;
    return result;
  }

 private:
  std::span<const std::byte> data_;
  std::size_t pos_ = 0;
};

std::optional<MeshData> DecodeMeshBlob(std::span<const std::byte> blob) {
  ByteReader reader(blob);

  const auto point_count = reader.ReadU32();
  if (!point_count) {
    return std::nullopt;
  }

  // No reserve() here: point_count/index_count come straight off
  // untrusted blob bytes, not yet validated against the blob's actual
  // remaining size - reserving on their word could attempt a multi-
  // gigabyte allocation before the per-element bounds checks below ever
  // get a chance to reject a corrupt or truncated blob.
  MeshData mesh;
  for (std::uint32_t i = 0; i < *point_count; ++i) {
    const auto x = reader.ReadF32();
    const auto y = reader.ReadF32();
    const auto z = reader.ReadF32();
    if (!x || !y || !z) {
      return std::nullopt;
    }
    mesh.points.emplace_back(*x, *y, *z);
  }

  const auto index_count = reader.ReadU32();
  if (!index_count) {
    return std::nullopt;
  }

  for (std::uint32_t i = 0; i < *index_count; ++i) {
    const auto index = reader.ReadU32();
    if (!index) {
      return std::nullopt;
    }
    mesh.indices.push_back(*index);
  }

  return mesh;
}

struct IndexEntry {
  AssetType type;
  std::string path;
  std::uint64_t offset;
  std::uint64_t size;
};

}  // namespace

std::string SanitizePrimPath(std::string_view usd_prim_path) {
  if (!usd_prim_path.empty() && usd_prim_path.front() == '/') {
    usd_prim_path.remove_prefix(1);
  }
  return std::string(usd_prim_path);
}

std::vector<std::byte> EncodeMeshBlob(const MeshData& mesh) {
  std::vector<std::byte> blob;
  AppendU32(blob, static_cast<std::uint32_t>(mesh.points.size()));
  for (const auto& point : mesh.points) {
    AppendF32(blob, point.x);
    AppendF32(blob, point.y);
    AppendF32(blob, point.z);
  }
  AppendU32(blob, static_cast<std::uint32_t>(mesh.indices.size()));
  for (auto index : mesh.indices) {
    AppendU32(blob, index);
  }
  return blob;
}

std::expected<void, WriteError> WritePack(const std::filesystem::path& output_path,
                                          const std::vector<AssetEntry>& entries) {
  std::vector<std::byte> data_section;
  std::vector<std::uint64_t> offsets(entries.size());
  std::vector<std::uint64_t> sizes(entries.size());

  std::uint64_t cursor = kHeaderSize;
  for (std::size_t i = 0; i < entries.size(); ++i) {
    offsets[i] = cursor;
    sizes[i] = entries[i].data.size();
    AppendBytes(data_section, entries[i].data);
    cursor += entries[i].data.size();
  }

  std::vector<std::byte> index_section;
  for (std::size_t i = 0; i < entries.size(); ++i) {
    AppendU8(index_section, static_cast<std::uint8_t>(entries[i].type));
    AppendU32(index_section, static_cast<std::uint32_t>(entries[i].path.size()));
    AppendChars(index_section, entries[i].path);
    AppendU64(index_section, offsets[i]);
    AppendU64(index_section, sizes[i]);
  }
  const std::uint64_t index_offset = cursor;

  std::vector<std::byte> header;
  AppendChars(header, std::string_view(kMagic.data(), kMagic.size()));
  AppendU32(header, kFormatVersion);
  AppendU64(header, kHeaderSize);
  AppendU64(header, index_offset);
  AppendU32(header, static_cast<std::uint32_t>(entries.size()));

  std::ofstream out(output_path, std::ios::binary | std::ios::trunc);
  if (!out) {
    return std::unexpected(WriteError::kIoError);
  }
  out.write(reinterpret_cast<const char*>(header.data()), static_cast<std::streamsize>(header.size()));
  out.write(reinterpret_cast<const char*>(data_section.data()), static_cast<std::streamsize>(data_section.size()));
  out.write(reinterpret_cast<const char*>(index_section.data()), static_cast<std::streamsize>(index_section.size()));
  if (!out) {
    return std::unexpected(WriteError::kIoError);
  }

  return {};
}

struct Pack::Impl {
  std::vector<std::byte> bytes;
  std::vector<IndexEntry> index;
};

Pack::Pack() = default;
Pack::Pack(Pack&&) noexcept = default;
Pack& Pack::operator=(Pack&&) noexcept = default;
Pack::~Pack() = default;

std::expected<Pack, LoadError> Pack::Load(const std::filesystem::path& path) {
  std::error_code ec;
  const auto file_size = std::filesystem::file_size(path, ec);
  if (ec) {
    return std::unexpected(LoadError::kIoError);
  }

  std::ifstream in(path, std::ios::binary);
  if (!in) {
    return std::unexpected(LoadError::kIoError);
  }
  std::vector<std::byte> bytes(file_size);
  in.read(reinterpret_cast<char*>(bytes.data()), static_cast<std::streamsize>(bytes.size()));
  if (!in) {
    return std::unexpected(LoadError::kIoError);
  }

  ByteReader header_reader(bytes);
  const auto magic = header_reader.ReadBytes(kMagic.size());
  if (!magic || std::memcmp(magic->data(), kMagic.data(), kMagic.size()) != 0) {
    return std::unexpected(LoadError::kBadMagic);
  }

  const auto version = header_reader.ReadU32();
  if (!version) {
    return std::unexpected(LoadError::kTruncated);
  }
  if (*version != kFormatVersion) {
    return std::unexpected(LoadError::kUnsupportedVersion);
  }

  const auto data_offset = header_reader.ReadU64();
  const auto index_offset = header_reader.ReadU64();
  const auto index_count = header_reader.ReadU32();
  if (!data_offset || !index_offset || !index_count) {
    return std::unexpected(LoadError::kTruncated);
  }
  // The data section always starts immediately after the header
  // (WritePack never writes it anywhere else) - a header claiming
  // otherwise is internally inconsistent, not just short.
  if (*data_offset != kHeaderSize) {
    return std::unexpected(LoadError::kTruncated);
  }
  if (*index_offset > bytes.size()) {
    return std::unexpected(LoadError::kTruncated);
  }

  // No reserve() here either, for the same reason as DecodeMeshBlob's -
  // *index_count is still just an untrusted header field at this point.
  ByteReader index_reader(std::span(bytes).subspan(*index_offset));
  std::vector<IndexEntry> index;
  for (std::uint32_t i = 0; i < *index_count; ++i) {
    const auto type = index_reader.ReadU8();
    const auto path_length = index_reader.ReadU32();
    if (!type || !path_length) {
      return std::unexpected(LoadError::kTruncated);
    }
    const auto path_bytes = index_reader.ReadBytes(*path_length);
    const auto offset = index_reader.ReadU64();
    const auto size = index_reader.ReadU64();
    if (!path_bytes || !offset || !size) {
      return std::unexpected(LoadError::kTruncated);
    }
    if (*offset > bytes.size() || *size > bytes.size() - *offset) {
      return std::unexpected(LoadError::kTruncated);
    }

    std::string entry_path(reinterpret_cast<const char*>(path_bytes->data()), path_bytes->size());
    index.push_back(IndexEntry{static_cast<AssetType>(*type), std::move(entry_path), *offset, *size});
  }

  Pack pack;
  pack.impl_ = std::make_unique<Impl>(std::move(bytes), std::move(index));
  return pack;
}

std::expected<MeshData, ResolveError> Pack::ResolveMesh(std::string_view path) const {
  const auto it = std::find_if(impl_->index.begin(), impl_->index.end(),
                               [&](const IndexEntry& entry) { return entry.path == path; });
  if (it == impl_->index.end()) {
    return std::unexpected(ResolveError::kNotFound);
  }
  if (it->type != AssetType::kMesh) {
    return std::unexpected(ResolveError::kTypeMismatch);
  }

  const std::span<const std::byte> blob(impl_->bytes.data() + it->offset, it->size);
  auto mesh = DecodeMeshBlob(blob);
  if (!mesh) {
    return std::unexpected(ResolveError::kCorruptBlob);
  }
  return std::move(*mesh);
}

}  // namespace augusta::assets
