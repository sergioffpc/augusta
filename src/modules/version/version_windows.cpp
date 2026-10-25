#include <cstddef>
#include <format>
#include <iterator>
#include <span>
#include <string>

#include <windows.h>

#include "augusta/version.h"

namespace augusta {

namespace {

// The CodeView record the linker writes into the debug directory, naming the
// PDB: "RSDS", its GUID, its age, then its path.
struct CodeViewRecord {
  DWORD signature;
  GUID guid;
  DWORD age;
};

constexpr DWORD kRsdsSignature = 0x53445352;  // "RSDS", little-endian.

}  // namespace

// The PDB's GUID and age, from the CodeView record in the executable's own
// image.
std::string DescribeBuild() {
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

}  // namespace augusta
