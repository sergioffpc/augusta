#include "augusta/version.h"

#include <cstddef>
#include <format>
#include <iterator>
#include <span>
#include <string>
#include <string_view>

#ifdef _WIN32
#include <windows.h>
#else
#include <elf.h>
#include <link.h>
#endif

namespace augusta {

namespace {

#ifdef _WIN32

// The CodeView record the linker writes into the debug directory, naming the
// PDB: "RSDS", its GUID, its age, then its path.
struct CodeViewRecord {
  DWORD signature;
  GUID guid;
  DWORD age;
};

constexpr DWORD kRsdsSignature = 0x53445352;  // "RSDS", little-endian.

std::string DescribePdb() {
  // The executable's own image, as loaded: every module is linked into it.
  const auto* base = reinterpret_cast<const std::byte*>(GetModuleHandleW(nullptr));
  if (base == nullptr) {
    return "pdb_guid=unknown pdb_age=unknown";
  }
  const auto* dos = reinterpret_cast<const IMAGE_DOS_HEADER*>(base);
  const auto* nt = reinterpret_cast<const IMAGE_NT_HEADERS*>(base + dos->e_lfanew);
  const IMAGE_DATA_DIRECTORY& directory = nt->OptionalHeader.DataDirectory[IMAGE_DIRECTORY_ENTRY_DEBUG];
  const std::span entries(reinterpret_cast<const IMAGE_DEBUG_DIRECTORY*>(base + directory.VirtualAddress),
                          directory.Size / sizeof(IMAGE_DEBUG_DIRECTORY));
  for (const IMAGE_DEBUG_DIRECTORY& entry : entries) {
    if (entry.Type != IMAGE_DEBUG_TYPE_CODEVIEW || entry.AddressOfRawData == 0 ||
        entry.SizeOfData < sizeof(CodeViewRecord)) {
      continue;
    }
    const auto* record = reinterpret_cast<const CodeViewRecord*>(base + entry.AddressOfRawData);
    if (record->signature != kRsdsSignature) {
      continue;
    }
    const GUID& guid = record->guid;
    // Data4's first two bytes, a hyphen, then its last six.
    std::string guid_text = std::format("{:08X}-{:04X}-{:04X}-", guid.Data1, guid.Data2, guid.Data3);
    for (std::size_t i = 0; i < std::size(guid.Data4); ++i) {
      if (i == 2) {
        guid_text += '-';
      }
      guid_text += std::format("{:02X}", guid.Data4[i]);
    }
    return std::format("pdb_guid={} pdb_age={}", guid_text, record->age);
  }
  return "pdb_guid=unknown pdb_age=unknown";
}

#else

// The note a GNU build ID is: "GNU" and NT_GNU_BUILD_ID, then the ID's bytes.
constexpr std::string_view kGnuNoteName{"GNU\0", 4};

constexpr std::size_t AlignedToFour(std::size_t size) { return (size + 3U) & ~std::size_t{3U}; }

// The build ID in the notes of segment, as lowercase hex; empty if none.
std::string BuildIdIn(const std::byte* segment, std::size_t size) {
  std::size_t at = 0;
  while (at + sizeof(ElfW(Nhdr)) <= size) {
    const auto* note = reinterpret_cast<const ElfW(Nhdr)*>(segment + at);
    const std::size_t name_at = at + sizeof(ElfW(Nhdr));
    const std::size_t description_at = name_at + AlignedToFour(note->n_namesz);
    const std::size_t next = description_at + AlignedToFour(note->n_descsz);
    if (next > size) {
      return {};
    }
    const std::string_view name(reinterpret_cast<const char*>(segment + name_at), note->n_namesz);
    if (note->n_type == NT_GNU_BUILD_ID && name == kGnuNoteName) {
      std::string hex;
      for (const std::byte byte : std::span(segment + description_at, note->n_descsz)) {
        hex += std::format("{:02x}", static_cast<unsigned>(byte));
      }
      return hex;
    }
    at = next;
  }
  return {};
}

std::string DescribeBuildId() {
  std::string build_id;
  // The first object dl_iterate_phdr reports is the executable itself.
  dl_iterate_phdr(
      [](dl_phdr_info* info, std::size_t /*size*/, void* found) {
        auto& id = *static_cast<std::string*>(found);
        for (const ElfW(Phdr) & header : std::span(info->dlpi_phdr, info->dlpi_phnum)) {
          if (header.p_type == PT_NOTE && id.empty()) {
            id = BuildIdIn(reinterpret_cast<const std::byte*>(info->dlpi_addr + header.p_vaddr), header.p_memsz);
          }
        }
        return 1;  // Stop after the executable.
      },
      &build_id);
  return std::format("build_id={}", build_id.empty() ? "unknown" : build_id);
}

#endif

}  // namespace

std::string_view EngineVersion() { return "5.0.0"; }

std::string DescribeBuild() {
#ifdef _WIN32
  return DescribePdb();
#else
  return DescribeBuildId();
#endif
}

}  // namespace augusta
