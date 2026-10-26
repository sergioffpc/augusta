#ifndef AUGUSTA_AGENT_DISPATCH_H_
#define AUGUSTA_AGENT_DISPATCH_H_

#include <cstdint>
#include <thread>
#include <unordered_map>

#include <pybind11/pybind11.h>

#include "agent_report.h"

/// \file
/// How the Agents' Reports (agent_report.h) reach the script of the Agents'
/// Python module (module.cpp): the Runner's threads put them on one queue, and
/// one thread of the module's own hands them to Python. The Agent binding
/// (bindings.h) registers each Agent's receiver; PYBIND11_MODULE starts and
/// stops the thread.
namespace augusta::agent::native {

/// The process's one ReportQueue, and the one dispatch thread that waits on it
/// with the GIL released and calls, with it held, the receiver each Agent
/// registered with every Report the Agent put: the Runner's threads put them
/// and never touch Python. Started at import, stopped at exit before the
/// interpreter finalizes (PYBIND11_MODULE), and never destroyed, as an Agent
/// a script never closed may still put Reports after.
class Dispatch {
 public:
  static Dispatch& Get();

  /// Calls receiver with every Report the Agent numbered as returned puts on
  /// the sink For gives it, until Unregister. GIL held.
  std::uint64_t Register(pybind11::object receiver);

  /// Drops the Agent's Reports from now on, those waiting among them. GIL held.
  void Unregister(std::uint64_t agent);

  /// Where the Agent agent puts its Reports.
  ReportSink For(std::uint64_t agent);

  /// Drops every receiver and Report, and joins the thread. GIL held, which it
  /// lets go of while it joins.
  void Stop();

 private:
  Dispatch();

  void Run();

  // GIL held. A receiver that raises is the script's bug, told as Python
  // tells an exception no one can catch; the next Report is still delivered.
  void Deliver(AddressedReport& addressed);

  ReportQueue queue_;
  // GIL-guarded, as next_agent_ is.
  std::unordered_map<std::uint64_t, pybind11::object> receivers_;
  std::uint64_t next_agent_ = 0;
  // Last, so it starts once the rest is there.
  std::thread thread_;
};

}  // namespace augusta::agent::native

#endif  // AUGUSTA_AGENT_DISPATCH_H_
