#include "augusta/supervisor.h"

#include <atomic>
#include <exception>
#include <format>
#include <functional>
#include <mutex>
#include <optional>
#include <string>
#include <string_view>
#include <thread>
#include <utility>

#include "augusta/logging.h"

namespace augusta::supervisor {

std::string DescribeWorkerFailure(const WorkerFailure& failure) {
  return std::format("the {} thread failed: {}", failure.thread, failure.reason);
}

Supervisor::~Supervisor() { StopAndJoin(); }

void Supervisor::Spawn(std::string_view thread, std::function<void()> body) {
  workers_.emplace_back([this, name = std::string(thread), work = std::move(body)] { Supervise(name, work); });
}

void Supervisor::Run(std::string_view thread, const std::function<void()>& body) { Supervise(thread, body); }

void Supervisor::RequestStop() { stop_requested_.store(true, std::memory_order_relaxed); }

bool Supervisor::StopRequested() const { return stop_requested_.load(std::memory_order_relaxed); }

std::optional<WorkerFailure> Supervisor::Failure() const {
  const std::lock_guard<std::mutex> lock(failure_mutex_);
  return failure_;
}

void Supervisor::StopAndJoin() {
  RequestStop();
  for (std::thread& worker : workers_) {
    if (worker.joinable()) {
      worker.join();
    }
  }
  workers_.clear();
}

void Supervisor::Supervise(std::string_view thread, const std::function<void()>& body) {
  try {
    body();
  } catch (const std::exception& error) {
    RecordFailure(thread, error.what());
  } catch (...) {
    RecordFailure(thread, "unknown exception");
  }
}

void Supervisor::RecordFailure(std::string_view thread, std::string_view reason) {
  LE("subsystem=supervisor event=worker_failed thread={} reason=\"{}\"", thread, reason);
  {
    const std::lock_guard<std::mutex> lock(failure_mutex_);
    if (!failure_.has_value()) {
      failure_ = WorkerFailure{.thread = std::string(thread), .reason = std::string(reason)};
    }
  }
  RequestStop();
}

}  // namespace augusta::supervisor
