#include <cstddef>
#include <format>
#include <span>
#include <string>
#include <string_view>

#include <elf.h>
#include <link.h>

#include "augusta/version.h"

namespace augusta {

namespace {

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

}  // namespace

// The GNU build ID in the executable's own notes.
std::string DescribeBuild() {
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

}  // namespace augusta
