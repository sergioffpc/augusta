#include <atomic>
#include <chrono>
#include <condition_variable>
#include <cstddef>
#include <cstdint>
#include <deque>
#include <filesystem>
#include <format>
#include <fstream>
#include <ios>
#include <memory>
#include <mutex>
#include <optional>
#include <string>
#include <string_view>
#include <thread>
#include <utility>
#include <vector>

#include "augusta/faults.h"
#include "augusta/logging.h"
#include "augusta/networking.h"
#include "augusta/pcapng.h"
#include "augusta/pcapng_file_writer.h"

namespace augusta::pcapng {
namespace {

constexpr std::string_view kExtension = ".pcapng";

// path in UTF-8: path::string() converts to the ANSI code page on Windows, and
// throws for a character it lacks.
std::string Utf8(const std::filesystem::path& path) {
  const std::u8string text = path.u8string();
  return {text.begin(), text.end()};
}

// Why the file stopped, as its WARN line's reason=.
enum class StopReason : std::uint8_t {
  kCreateFailed,
  kWriteFailed,
  kQueueFull,
};

std::string_view StopReasonName(StopReason reason) {
  switch (reason) {
    case StopReason::kCreateFailed:
      return "create_failed";
    case StopReason::kWriteFailed:
      return "write_failed";
    case StopReason::kQueueFull:
      return "queue_full";
  }
  return "unknown";
}

}  // namespace

std::string FileName(std::string_view executable, std::chrono::system_clock::time_point started) {
  const auto milliseconds = std::chrono::floor<std::chrono::milliseconds>(started);
  const auto seconds = std::chrono::floor<std::chrono::seconds>(milliseconds);
  return std::format("{}-{:%Y%m%dT%H%M%S}{:03}Z{}", executable, seconds, (milliseconds - seconds).count(), kExtension);
}

struct FileWriter::Impl {
  Impl(std::filesystem::path file, std::string_view executable, std::string_view version, FileWriterOptions options)
      : path(std::move(file)),
        headers(EncodeSectionHeader(executable, version)),
        capacity(options.queue_capacity),
        faults(options.faults) {
    const std::vector<std::byte> interface = EncodeInterfaceDescription();
    headers.insert(headers.end(), interface.begin(), interface.end());
    thread = std::thread([this] { Run(); });
  }

  ~Impl() {
    {
      const std::scoped_lock lock(mutex);
      closing = true;
    }
    wake.notify_one();
    thread.join();
  }

  Impl(const Impl&) = delete;
  Impl& operator=(const Impl&) = delete;
  Impl(Impl&&) = delete;
  Impl& operator=(Impl&&) = delete;

  void Enqueue(const networking::TappedMessage& message) {
    if (stopped.load(std::memory_order_relaxed)) {
      return;
    }
    std::vector<std::byte> block = EncodeEnhancedPacket(message);
    bool full = false;
    bool first_stop = false;
    {
      const std::scoped_lock lock(mutex);
      full = queue.size() >= capacity;
      if (full) {
        // Stopped under the lock, so no later message is queued after one
        // that was dropped.
        first_stop = !stopped.exchange(true);
      } else {
        queue.push_back(std::move(block));
      }
    }
    // Logged after the lock is released: the reporting thread is the
    // transport's, which must not wait on the writer's lock while it logs.
    if (first_stop) {
      LogStopped(StopReason::kQueueFull, {});
    }
    if (!full) {
      wake.notify_one();
    }
  }

  // The first stop alone is logged: the file stops once.
  void Stop(StopReason reason, std::string_view detail) {
    if (!stopped.exchange(true)) {
      LogStopped(reason, detail);
    }
  }

  void LogStopped(StopReason reason, std::string_view detail) const {
    LOG_WARN("subsystem=pcapng event=pcapng_stopped reason={} path=\"{}\" detail=\"{}\"", StopReasonName(reason),
             Utf8(path), detail);
  }

  // The writer's thread: the headers, then each queued block until closing, or
  // until the file fails. A queue that filled up stops taking blocks, but those
  // it holds came before, so they are still written.
  void Run() {
    std::ofstream file(path, std::ios::binary);
    if (!file.write(reinterpret_cast<const char*>(headers.data()), static_cast<std::streamsize>(headers.size())) ||
        !file.flush()) {
      Stop(file.is_open() ? StopReason::kWriteFailed : StopReason::kCreateFailed, "");
      return;
    }
    std::deque<std::vector<std::byte>> batch;
    while (true) {
      {
        std::unique_lock lock(mutex);
        wake.wait(lock, [this] { return closing || !queue.empty(); });
        if (queue.empty()) {
          return;
        }
        batch.swap(queue);
      }
      if (std::optional<std::string> failed = WriteAll(file, batch)) {
        Stop(StopReason::kWriteFailed, *failed);
        return;
      }
    }
  }

  // Writes and flushes batch's blocks, emptying it, or says why the file failed:
  // an injected fault fails it as the disk would.
  std::optional<std::string> WriteAll(std::ofstream& file, std::deque<std::vector<std::byte>>& batch) const {
    for (const std::vector<std::byte>& block : batch) {
      if (std::optional<std::string> fault =
              faults == nullptr ? std::nullopt : faults->Trip(failure::Site::kPcapngWrite)) {
        return fault;
      }
      if (!file.write(reinterpret_cast<const char*>(block.data()), static_cast<std::streamsize>(block.size()))) {
        return "the write failed";
      }
    }
    batch.clear();
    if (!file.flush()) {
      return "the flush failed";
    }
    return std::nullopt;
  }

  const std::filesystem::path path;
  // The Section Header and Interface Description Blocks, the file's first bytes.
  std::vector<std::byte> headers;
  const std::size_t capacity;
  failure::Faults* const faults;
  std::atomic<bool> stopped{false};
  std::mutex mutex;
  std::condition_variable wake;
  // Guarded by mutex.
  std::deque<std::vector<std::byte>> queue;
  bool closing = false;
  std::thread thread;
};

FileWriter::FileWriter(const std::filesystem::path& directory, std::string_view executable, std::string_view version,
                       std::chrono::system_clock::time_point started, FileWriterOptions options)
    : impl_(std::make_unique<Impl>(directory / FileName(executable, started), executable, version, options)) {}

FileWriter::~FileWriter() = default;

const std::filesystem::path& FileWriter::Path() const { return impl_->path; }

void FileWriter::Report(const networking::TappedMessage& message) { impl_->Enqueue(message); }

std::unique_ptr<FileWriter> RunWriter(const std::filesystem::path& directory, std::string_view executable,
                                      std::string_view version) {
  if (directory.empty()) {
    return nullptr;
  }
  auto writer = std::make_unique<FileWriter>(directory, executable, version, std::chrono::system_clock::now());
  LOG_INFO("subsystem=pcapng event=pcapng_writing path=\"{}\"", Utf8(writer->Path()));
  return writer;
}

}  // namespace augusta::pcapng
