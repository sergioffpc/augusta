#include <cstddef>
#include <cstdint>
#include <format>
#include <string>

#include <mach-o/dyld.h>
#include <mach-o/loader.h>

#include "augusta/version.h"

namespace augusta {

// The executable's Mach-O UUID (LC_UUID), which its dSYM and lldb match it by,
// in a GNU build ID's form: lowercase hex.
std::string DescribeBuild() {
  // The first image dyld loaded is the executable itself.
  const auto* header = reinterpret_cast<const mach_header_64*>(_dyld_get_image_header(0));
  if (header == nullptr || header->magic != MH_MAGIC_64) {
    return "build_id=unknown";
  }
  const auto* at = reinterpret_cast<const std::byte*>(header + 1);
  for (std::uint32_t i = 0; i < header->ncmds; ++i) {
    const auto* command = reinterpret_cast<const load_command*>(at);
    if (command->cmd == LC_UUID) {
      const auto* uuid = reinterpret_cast<const uuid_command*>(command);
      std::string hex;
      for (const std::uint8_t byte : uuid->uuid) {
        hex += std::format("{:02x}", byte);
      }
      return std::format("build_id={}", hex);
    }
    at += command->cmdsize;
  }
  return "build_id=unknown";
}

}  // namespace augusta
