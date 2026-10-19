#ifndef AUGUSTA_AGENT_REPORT_H_
#define AUGUSTA_AGENT_REPORT_H_

#include <condition_variable>
#include <cstdint>
#include <deque>
#include <functional>
#include <mutex>
#include <string>
#include <variant>
#include <vector>

#include "augusta/harness.h"
#include "augusta/intent.h"

/// \file
/// What an Agent tells its script, and the one queue a process's Agents put it
/// on (ADR-0052's thread boundary): the Runner's threads put Reports here and
/// never touch Python; one dispatch thread takes them and hands them to the
/// script. Nothing here knows of Python.
namespace augusta::agent {

/// An Intent an Agent was given ended: the number the Agent gave it when it
/// was set, and how.
struct IntentEnded {
  std::uint64_t intent = 0;
  harness::IntentEnding ending{};
};

/// The Agent stopped playing: its Session failed, or its Runner did. There is
/// no reconnecting.
struct AgentFailure {
  /// The sentence augustac would show for it.
  std::string description;
};

/// Something an Agent tells its script: one of its events (a Shot, a Hit
/// confirmation, a Death, a Match start, a Match end), an Intent's ending, or
/// its failure.
using Report = std::variant<harness::Shot, harness::HitConfirmation, harness::Death, harness::MatchStart,
                            harness::MatchEnd, IntentEnded, AgentFailure>;

/// Where an Agent puts its Reports: called on the Runner's threads, and on the
/// thread that sets an Intent for its Replaced ending, so it must return at
/// once and never wait on anything a tick waits for.
using ReportSink = std::function<void(Report)>;

/// A Report and the number of the Agent it is from.
struct AddressedReport {
  std::uint64_t agent = 0;
  Report report;
};

/// One process's Reports, from every Agent, in the order they were put.
class ReportQueue {
 public:
  /// Puts report from agent at the back, unless the queue is closed. Safe
  /// from any thread; waits only on the queue's own lock.
  void Put(std::uint64_t agent, Report report);

  /// Waits until a Report is waiting or the queue is closed, and takes every
  /// one waiting: empty only once it is closed. One taker at a time.
  [[nodiscard]] std::vector<AddressedReport> TakeAll();

  /// Wakes the taker and drops every Report put from now on.
  void Close();

 private:
  std::mutex mutex_;
  std::condition_variable put_;
  std::deque<AddressedReport> reports_;
  bool closed_ = false;
};

}  // namespace augusta::agent

#endif  // AUGUSTA_AGENT_REPORT_H_
