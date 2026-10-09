#include "augusta/supervisor.h"

#include <atomic>
#include <functional>
#include <mutex>
#include <optional>
#include <string>
#include <string_view>
#include <thread>
#include <utility>

#include "augusta/failure.h"
#include "augusta/faults.h"
#include "augusta/logging.h"

namespace augusta::supervisor {

Supervisor::Supervisor(failure::Faults& faults) : faults_(&faults) {}

Supervisor::~Supervisor() { StopAndJoin(); }

void Supervisor::Spawn(std::string_view thread, std::function<WorkerResult()> body) {
  if (StopRequested()) {
    return;
  }
  auto started = failure::Guard(failure::Code::kWorkerCreationFailed, [&] {
    if (faults_ != nullptr) {
      faults_->ThrowIfTripped(failure::Site::kWorkerCreation);
    }
    workers_.emplace_back([this, name = std::string(thread), work = std::move(body)] { Supervise(name, work); });
  });
  if (!started) {
    RecordFailure(thread, std::move(started.error()));
  }
}

void Supervisor::Run(std::string_view thread, const std::function<WorkerResult()>& body) {
  if (StopRequested()) {
    return;
  }
  Supervise(thread, body);
}

void Supervisor::RequestStop() {
  State running = State::kRunning;
  state_.compare_exchange_strong(running, State::kStopping, std::memory_order_relaxed);
}

bool Supervisor::StopRequested() const { return state_.load(std::memory_order_relaxed) != State::kRunning; }

State Supervisor::GetState() const { return state_.load(std::memory_order_relaxed); }

std::optional<failure::Failure> Supervisor::Failure() const {
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
  state_.store(State::kStopped, std::memory_order_relaxed);
}

void Supervisor::Supervise(std::string_view thread, const std::function<WorkerResult()>& body) {
  auto ran = failure::Guard(failure::Code::kWorkerFailed, [&] {
    if (faults_ != nullptr) {
      faults_->ThrowIfTripped(failure::Site::kWorkerExecution);
    }
    return body();
  });
  if (!ran) {
    RecordFailure(thread, std::move(ran.error()));
  } else if (!ran->has_value()) {
    RecordFailure(thread, std::move(ran->error()));
  }
}

void Supervisor::RecordFailure(std::string_view thread, failure::Failure failure) {
  failure.context.push_back({.key = std::string(kThreadContextKey), .value = std::string(thread)});
  // The stop is requested before the failure is published, so whoever sees
  // the first cause also sees the stop.
  RequestStop();
  bool first = false;
  {
    const std::lock_guard<std::mutex> lock(failure_mutex_);
    if (!failure_.has_value()) {
      failure_ = failure;
      first = true;
    }
  }
  // The first cause is the runtime's one terminal line, even when a stop from
  // outside came before it. Only a failure after an earlier recorded one is a
  // consequence of that one.
  if (first) {
    LE("subsystem=supervisor event=worker_failed {}", failure::DescribeFailure(failure));
  } else {
    LW("subsystem=supervisor event=worker_failed_after_first_cause {}", failure::DescribeFailure(failure));
  }
}

}  // namespace augusta::supervisor
