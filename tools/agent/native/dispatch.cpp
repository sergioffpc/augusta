#include "dispatch.h"

#include <cstdint>
#include <exception>
#include <utility>
#include <variant>
#include <vector>

#include <pybind11/pybind11.h>
#include <pybind11/stl.h>

#include "agent_report.h"
#include "augusta/logging.h"

namespace augusta::agent::native {

namespace py = pybind11;

Dispatch& Dispatch::Get() {
  static auto* const kDispatch = new Dispatch();
  return *kDispatch;
}

std::uint64_t Dispatch::Register(py::object receiver) {
  const std::uint64_t agent = next_agent_++;
  receivers_.emplace(agent, std::move(receiver));
  return agent;
}

void Dispatch::Unregister(std::uint64_t agent) { receivers_.erase(agent); }

ReportSink Dispatch::For(std::uint64_t agent) {
  return [this, agent](Report report) { queue_.Put(agent, std::move(report)); };
}

void Dispatch::Stop() {
  receivers_.clear();
  queue_.Close();
  const py::gil_scoped_release release;
  if (thread_.joinable()) {
    thread_.join();
  }
}

Dispatch::Dispatch() : thread_([this] { Run(); }) {}

void Dispatch::Run() {
  for (std::vector<augusta::agent::AddressedReport> reports = queue_.TakeAll(); !reports.empty();
       reports = queue_.TakeAll()) {
    const py::gil_scoped_acquire gil;
    for (augusta::agent::AddressedReport& addressed : reports) {
      Deliver(addressed);
    }
  }
}

void Dispatch::Deliver(augusta::agent::AddressedReport& addressed) {
  const auto found = receivers_.find(addressed.agent);
  if (found == receivers_.end()) {
    return;
  }
  // Its own reference: the receiver may unregister itself.
  const py::object receiver = found->second;
  try {
    receiver(std::visit([](auto& report) { return py::cast(std::move(report)); }, addressed.report));
  } catch (py::error_already_set& error) {
    error.discard_as_unraisable("augusta_agent: delivering a Report");
  } catch (const std::exception& error) {
    LOG_ERROR("subsystem=agent event=report_dropped reason=\"{}\"", error.what());
  }
}

}  // namespace augusta::agent::native
