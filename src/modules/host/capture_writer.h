#ifndef AUGUSTA_HOST_CAPTURE_WRITER_H_
#define AUGUSTA_HOST_CAPTURE_WRITER_H_

#include <atomic>
#include <condition_variable>
#include <cstddef>
#include <cstdint>
#include <deque>
#include <filesystem>
#include <fstream>
#include <mutex>
#include <optional>
#include <string>
#include <thread>
#include <unordered_set>

#include <boost/uuid/nil_generator.hpp>
#include <boost/uuid/uuid.hpp>

#include "augusta/capture.h"
#include "augusta/capture_retention.h"
#include "augusta/capturer.h"
#include "augusta/failure.h"
#include "augusta/faults.h"

/// \file
/// The writer thread behind a Capturer (augusta/capturer.h, ADR-0050), private
/// to the module: the queue the Simulation thread hands each Match's file
/// opened, its records and its file closed to, and the thread that creates,
/// writes, flushes and closes the files, keeps the directory within its
/// retention, and keeps the run's first loss and the health of its captures.
/// Capturer owns one and does the encoding on the Simulation thread.
namespace augusta::host {

// The thread a Capturer writes its files on and what is queued for it, oldest
// first: each Match's file opened, its records, and its file closed, each
// item naming its Match by its Match ID. The Simulation thread only
// ever waits on the lock, which the writer holds to take an item and never
// across a write. It keeps the run's first loss and the health it leaves the
// captures in, and tells its observer what it does, the state of captures
// under the lock so that a stop the writer finds is never told after the
// Simulation thread has moved to another Match.
class Capturer::Writer {
 public:
  Writer(std::filesystem::path directory, CaptureOptions options);

  // Writes what is still queued before it goes, then stops the captures.
  ~Writer();

  Writer(const Writer&) = delete;
  Writer& operator=(const Writer&) = delete;
  Writer(Writer&&) = delete;
  Writer& operator=(Writer&&) = delete;

  // match is the Match open, captured until its Close or a stop.
  void Begin(const boost::uuids::uuid& match);

  void Open(const boost::uuids::uuid& match, std::filesystem::path path, capture::Encoded header);

  void Close(const boost::uuids::uuid& match);

  // Queues match's record, or stops its capture there when capacity records
  // are still unwritten. Once its capture has stopped, drops every one.
  void Push(const boost::uuids::uuid& match, capture::Encoded record);

  void Stop(const boost::uuids::uuid& match, CaptureStep step);

  void WaitUntilWritten();

  // Waits until what is queued is written, then stops the captures: nothing
  // queued after is written.
  void Finish();

  [[nodiscard]] CaptureHealth Health() const;

  [[nodiscard]] std::optional<failure::Failure> Loss() const;

 private:
  enum class Kind : std::uint8_t { kOpen, kRecord, kClose };

  // Where the writer lost a Match's file, and what the disk said of it.
  struct Failed {
    CaptureStep step;
    std::string detail;
  };

  struct Item {
    Kind kind;
    boost::uuids::uuid match;
    // kOpen: the magic with the header; kRecord: the record.
    std::optional<capture::Encoded> encoded;
    std::filesystem::path path;
  };

  // Queues an item that opens a file: never refused, since it is one per Match.
  void Enqueue(Item item);

  void Run();

  // Handle under the writer thread's boundary: an exception escaping it, from
  // the disk or anything else the writer calls, loses the capture as a failed
  // write does, where it would otherwise end the process.
  std::optional<Failed> HandleGuarded(const Item& item);

  // Mechanism: carries out item on the writer's own file, or says why the
  // file failed. Writer thread only.
  std::optional<Failed> Handle(const Item& item);

  // Makes room in the directory for the magic and the header, then creates
  // the file with them.
  std::optional<Failed> OpenFile(const Item& item);

  std::optional<std::string> Trip(failure::Site site) const;

  // Makes room in the directory for encoded, then writes it.
  std::optional<Failed> Persist(const capture::Encoded& encoded);

  // Writes and flushes encoded, or says which step the file failed at. An
  // injected fault fails the file, as the disk would.
  std::optional<Failed> Write(const capture::Encoded& encoded);

  void CloseFile(const boost::uuids::uuid& match);

  // match's capture stops at step: reported once, with what the disk said of
  // it in detail, nothing more of it queued, and its file closed once the
  // writer gets to it. Told as the state of captures only while match is
  // still the one open. With mutex_ held.
  void StopLocked(const boost::uuids::uuid& match, CaptureStep step, std::string detail);

  // The run's first loss, of match's record at step: kept, and reported once
  // as the health it leaves the captures in. A later one changes nothing.
  // With mutex_ held.
  void LoseLocked(const boost::uuids::uuid& match, CaptureStep step, std::string detail);

  // The run's captures are over, logged once with whether any lost a record.
  // With mutex_ held.
  void StopCapturesLocked();

  void Enter(CaptureHealth health);

  const CaptureMode mode_;
  failure::Faults* const faults_;
  const std::size_t capacity_;
  // Told nothing, for a Capturer given no observer.
  CaptureObserver unobserved_;
  CaptureObserver* const observer_;
  // Kept within its retention by the writer thread alone.
  CaptureDirectory directory_;
  mutable std::mutex mutex_;
  std::condition_variable ready_;
  // Notified each time the queue is found empty, its last item handled.
  std::condition_variable written_;
  std::deque<Item> queue_;
  std::size_t records_queued_ = 0;
  // The Matches whose capture stopped and whose file is not yet closed.
  std::unordered_set<boost::uuids::uuid> stopped_;
  // The Match the Simulation thread has open (nil for none), as Begin and Close set it.
  boost::uuids::uuid open_match_ = boost::uuids::nil_uuid();
  bool closing_ = false;
  // An item off the queue but not yet handled keeps WaitUntilWritten waiting.
  bool writing_ = false;
  // Written under mutex_, read without it by any thread.
  std::atomic<CaptureHealth> health_ = CaptureHealth::kEnabled;
  std::optional<failure::Failure> loss_;
  // The file open, the Match it captures (nil for none), and how many records
  // it holds. Writer thread only.
  std::ofstream file_;
  boost::uuids::uuid file_match_ = boost::uuids::nil_uuid();
  std::filesystem::path path_;
  std::size_t records_ = 0;
  // The directory's captures, as scanned at the file's creation and grown by
  // what was written since. Writer thread only.
  // Declared last, so it is joined before what it writes from goes.
  std::thread thread_;
};

}  // namespace augusta::host

#endif  // AUGUSTA_HOST_CAPTURE_WRITER_H_
