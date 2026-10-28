#include <algorithm>
#include <cstddef>
#include <cstdint>
#include <expected>
#include <filesystem>
#include <fstream>
#include <ios>
#include <optional>
#include <string_view>
#include <system_error>
#include <vector>

#include <blake3.h>
#include <sodium.h>

#include "augusta/assets.h"
#include "augusta/pack_encoder.h"
#include "ed25519.h"
#include "pack_format.h"
#include "wire_format.h"

namespace augusta::assets {

namespace {

// Rejects entries.size()/each path exceeding this module's pragmatic
// limits, and any two entries sharing a path (ResolveMesh/ResolveScene
// would be ambiguous about which one they name).
std::expected<void, WriteError> ValidateEntries(const std::vector<AssetEntry>& entries) {
  if (entries.size() > kMaxEntries) {
    return std::unexpected(WriteError::kTooLarge);
  }
  for (const auto& entry : entries) {
    if (entry.path.size() > kMaxPathLength) {
      return std::unexpected(WriteError::kTooLarge);
    }
  }

  std::vector<std::string_view> paths;
  paths.reserve(entries.size());
  for (const auto& entry : entries) {
    paths.push_back(entry.path);
  }
  std::ranges::sort(paths);
  if (std::ranges::adjacent_find(paths) != paths.end()) {
    return std::unexpected(WriteError::kDuplicatePath);
  }
  return {};
}

// The header/data/index sections of a pack (ADR-0031), assembled but not
// yet hashed/signed/written.
struct PackSections {
  std::vector<std::byte> header;
  std::vector<std::byte> data_section;
  std::vector<std::byte> index_section;
};

std::expected<PackSections, WriteError> BuildPackSections(const std::vector<AssetEntry>& entries,
                                                          const std::optional<PackHash>& client_pack) {
  PackSections sections;
  ByteWriter data_writer(sections.data_section);
  ByteWriter index_writer(sections.index_section);
  ByteWriter header_writer(sections.header);

  std::vector<std::uint64_t> offsets(entries.size());
  std::vector<std::uint64_t> sizes(entries.size());
  std::uint64_t cursor = kHeaderSize;
  for (std::size_t i = 0; i < entries.size(); ++i) {
    offsets[i] = cursor;
    sizes[i] = entries[i].data.size();
    data_writer.WriteBytes(entries[i].data);
    cursor += entries[i].data.size();
  }

  for (std::size_t i = 0; i < entries.size(); ++i) {
    index_writer.WriteU8(static_cast<std::uint8_t>(entries[i].type));
    if (!index_writer.WriteString(entries[i].path)) {
      return std::unexpected(WriteError::kTooLarge);
    }
    index_writer.WriteU64(offsets[i]);
    index_writer.WriteU64(sizes[i]);
  }
  const std::uint64_t index_offset = cursor;

  header_writer.WriteChars(kPackMagic);
  header_writer.WriteU32(kFormatVersion);
  header_writer.WriteU64(kHeaderSize);
  header_writer.WriteU64(index_offset);
  header_writer.WriteU32(static_cast<std::uint32_t>(entries.size()));
  header_writer.WriteU8(client_pack.has_value() ? kHeaderHasClientPack : 0);
  header_writer.WriteBytes(client_pack.value_or(PackHash{}));

  const std::uint64_t total_size =
      sections.header.size() + sections.data_section.size() + sections.index_section.size() + kTrailerSize;
  if (total_size > kMaxPackSize) {
    return std::unexpected(WriteError::kTooLarge);
  }
  return sections;
}

// ADR-0030's pack -> hash -> sign order: BLAKE3 over everything through
// the end of the index, then Ed25519-sign that hash.
PackTrailer SignPack(const PackSections& sections, const Ed25519PrivateKey& signing_key) {
  blake3_hasher hasher;
  blake3_hasher_init(&hasher);
  blake3_hasher_update(&hasher, sections.header.data(), sections.header.size());
  blake3_hasher_update(&hasher, sections.data_section.data(), sections.data_section.size());
  blake3_hasher_update(&hasher, sections.index_section.data(), sections.index_section.size());

  PackTrailer trailer;
  blake3_hasher_finalize(&hasher, reinterpret_cast<std::uint8_t*>(trailer.hash.data()), trailer.hash.size());
  crypto_sign_detached(reinterpret_cast<unsigned char*>(trailer.signature.data()), nullptr,
                       reinterpret_cast<const unsigned char*>(trailer.hash.data()), trailer.hash.size(),
                       reinterpret_cast<const unsigned char*>(signing_key.data()));
  return trailer;
}

// Writes sections+trailer to a temporary file next to output_path, then
// renames it into place - a failed write only ever leaves a stray .tmp
// file behind, never a truncated file at output_path (review: "Fazer a
// escrita de packs atomicamente"), since the rename is the only step
// that touches output_path itself, and it's a single filesystem
// operation.
std::expected<void, WriteError> WritePackFile(const std::filesystem::path& output_path, const PackSections& sections,
                                              const PackTrailer& trailer) {
  std::filesystem::path tmp_path = output_path;
  tmp_path += ".tmp";
  {
    std::ofstream out(tmp_path, std::ios::binary | std::ios::trunc);
    if (!out) {
      return std::unexpected(WriteError::kIoError);
    }
    out.write(reinterpret_cast<const char*>(sections.header.data()),
              static_cast<std::streamsize>(sections.header.size()));
    out.write(reinterpret_cast<const char*>(sections.data_section.data()),
              static_cast<std::streamsize>(sections.data_section.size()));
    out.write(reinterpret_cast<const char*>(sections.index_section.data()),
              static_cast<std::streamsize>(sections.index_section.size()));
    out.write(reinterpret_cast<const char*>(trailer.hash.data()), static_cast<std::streamsize>(trailer.hash.size()));
    out.write(reinterpret_cast<const char*>(trailer.signature.data()),
              static_cast<std::streamsize>(trailer.signature.size()));
    if (!out) {
      std::error_code ignored;
      std::filesystem::remove(tmp_path, ignored);
      return std::unexpected(WriteError::kIoError);
    }
  }

  std::error_code rename_error;
  std::filesystem::rename(tmp_path, output_path, rename_error);
  if (rename_error) {
    std::error_code ignored;
    std::filesystem::remove(tmp_path, ignored);
    return std::unexpected(WriteError::kIoError);
  }
  return {};
}

}  // namespace

std::expected<PackHash, WriteError> WritePack(const std::filesystem::path& output_path,
                                              const std::vector<AssetEntry>& entries,
                                              const Ed25519PrivateKey& signing_key,
                                              const std::optional<PackHash>& client_pack) {
  EnsureSodiumInitialized();

  if (auto validated = ValidateEntries(entries); !validated) {
    return std::unexpected(validated.error());
  }

  auto sections = BuildPackSections(entries, client_pack);
  if (!sections) {
    return std::unexpected(sections.error());
  }

  const PackTrailer trailer = SignPack(*sections, signing_key);
  if (auto written = WritePackFile(output_path, *sections, trailer); !written) {
    return std::unexpected(written.error());
  }
  return trailer.hash;
}

std::string_view DescribeWriteError(WriteError error) {
  switch (error) {
    case WriteError::kIoError:
      return "could not be written";
    case WriteError::kDuplicatePath:
      return "has two entries that share a path";
    case WriteError::kTooLarge:
      return "has entries that exceed the pack's size limits";
  }
  return "could not be written for an unknown reason";
}

}  // namespace augusta::assets
