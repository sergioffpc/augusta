#ifndef AUGUSTA_UTF8_H_
#define AUGUSTA_UTF8_H_

#include <filesystem>
#include <string>

/// \file
/// How a path becomes text for a log line, a message or a comparison with a
/// name from either: in UTF-8 on every platform. path::string() converts to the
/// ANSI code page on Windows and throws for a character it lacks, so a
/// directory or file name in another script would stop whatever wrote it. On
/// Linux a path is already its bytes, and nothing changes.
namespace augusta::utf8 {

/// path in UTF-8. Never throws for a character the code page lacks.
[[nodiscard]] inline std::string FromPath(const std::filesystem::path& path) {
  const std::u8string utf8 = path.u8string();
  return {utf8.begin(), utf8.end()};
}

}  // namespace augusta::utf8

#endif  // AUGUSTA_UTF8_H_
