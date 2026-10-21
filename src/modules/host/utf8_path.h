#ifndef AUGUSTA_HOST_UTF8_PATH_H_
#define AUGUSTA_HOST_UTF8_PATH_H_

#include <filesystem>
#include <string>

/// \file
/// How the capture writer, its directory's retention, the replay catalog and
/// the Host's and replay server's lines naming a capture directory turn a path
/// into text: in UTF-8 on every platform. path::string() converts
/// to the ANSI code page on Windows and throws for a character it lacks, so a
/// file name in another script would stop a capture or the replay server. On
/// Linux a path is already its bytes, and nothing changes.
namespace augusta::host {

/// path in UTF-8, for a log line, a Replay list or a comparison with a name
/// from either.
[[nodiscard]] inline std::string Utf8(const std::filesystem::path& path) {
  const std::u8string utf8 = path.u8string();
  return {utf8.begin(), utf8.end()};
}

}  // namespace augusta::host

#endif  // AUGUSTA_HOST_UTF8_PATH_H_
