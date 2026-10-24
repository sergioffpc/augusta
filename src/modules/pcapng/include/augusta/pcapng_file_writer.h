#ifndef AUGUSTA_PCAPNG_FILE_WRITER_H_
#define AUGUSTA_PCAPNG_FILE_WRITER_H_

#include <chrono>
#include <cstddef>
#include <filesystem>
#include <memory>
#include <string>
#include <string_view>

#include "augusta/faults.h"
#include "augusta/networking.h"

/// \file
/// The tap that writes a Development build's pcapng of the messages it sends
/// and receives (ADR-0076): one file per run, in the directory each
/// executable's runtime takes from its config's `debug.network.pcapng`, of
/// pcapng.h's blocks. The transport reports to it on whichever thread sends or
/// receives; it only encodes and enqueues there, and a thread of its own writes,
/// so no tick and no Network I/O thread waits for the disk. A full queue, or a
/// failed create or write, stops the file there with one WARN line naming the
/// cause: the file keeps every block written before it, and the runtime goes on
/// as if nothing were tapped. A debugging aid, it has no strict mode. A debug
/// tool: only a Development build has it (ADR-0074).
namespace augusta::pcapng {

/// How many messages wait for the writer's thread before the queue is full.
inline constexpr std::size_t kDefaultQueueCapacity = 4096;

/// The file a run started at started writes: `<executable>-<start>.pcapng`, its
/// start in UTC to the millisecond, e.g. `augustac-20231114T221320123Z.pcapng`.
std::string FileName(std::string_view executable, std::chrono::system_clock::time_point started);

struct FileWriterOptions {
  /// 0 makes the queue full at the first message.
  std::size_t queue_capacity = kDefaultQueueCapacity;
  /// When given, asked before each message's block is written
  /// (failure::Site::kPcapngWrite), so a test makes the write fail.
  failure::Faults* faults = nullptr;
};

class FileWriter final : public networking::Tap {
 public:
  /// Starts the writer's thread, which creates directory / FileName(executable,
  /// started) and writes its Section Header Block, naming executable and version,
  /// and its Interface Description Block. directory is not created: the config
  /// that names it checks it can be written.
  FileWriter(const std::filesystem::path& directory, std::string_view executable, std::string_view version,
             std::chrono::system_clock::time_point started, FileWriterOptions options = {});
  /// Writes every message still queued, then closes the file.
  ~FileWriter() override;
  FileWriter(const FileWriter&) = delete;
  FileWriter& operator=(const FileWriter&) = delete;
  FileWriter(FileWriter&&) = delete;
  FileWriter& operator=(FileWriter&&) = delete;

  /// The file it writes, or would have.
  [[nodiscard]] const std::filesystem::path& Path() const;

  /// Encodes message and queues it for the writer's thread, never waiting for
  /// the disk; nothing once the file has stopped. Any thread.
  void Report(const networking::TappedMessage& message) override;

 private:
  struct Impl;
  std::unique_ptr<Impl> impl_;
};

/// The writer of this run's pcapng in directory, started now, its path logged
/// at INFO; null when directory is empty, which `debug.network.pcapng` leaves
/// it when it is not set. What each executable's runtime taps its transport
/// with, naming itself as executable and the engine's version.
std::unique_ptr<FileWriter> RunWriter(const std::filesystem::path& directory, std::string_view executable,
                                      std::string_view version);

}  // namespace augusta::pcapng

#endif  // AUGUSTA_PCAPNG_FILE_WRITER_H_
