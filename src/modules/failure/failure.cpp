#include "augusta/failure.h"

#include <atomic>
#include <cstddef>
#include <cstdint>
#include <format>
#include <iterator>
#include <mutex>
#include <optional>
#include <string>
#include <string_view>

#include "augusta/faults.h"

namespace augusta::failure {

namespace {

std::string_view CodeName(Code code) {
  switch (code) {
    case Code::kInvalidPeerInput:
      return "invalid_peer_input";
    case Code::kPeerMisbehaving:
      return "peer_misbehaving";
    case Code::kPeerConnectionLost:
      return "peer_connection_lost";
    case Code::kJoinRefused:
      return "join_refused";
    case Code::kServerUnreachable:
      return "server_unreachable";
    case Code::kRecordingWriteFailed:
      return "recording_write_failed";
    case Code::kRecordingFlushFailed:
      return "recording_flush_failed";
    case Code::kMetricsEndpointFailed:
      return "metrics_endpoint_failed";
    case Code::kCaptureWriteFailed:
      return "capture_write_failed";
    case Code::kCaptureFlushFailed:
      return "capture_flush_failed";
    case Code::kCaptureQueueFull:
      return "capture_queue_full";
    case Code::kCaptureRecordTooLong:
      return "capture_record_too_long";
    case Code::kTransportInitFailed:
      return "transport_init_failed";
    case Code::kListenerSetupFailed:
      return "listener_setup_failed";
    case Code::kTransportSendFailed:
      return "transport_send_failed";
    case Code::kTransportReceiveFailed:
      return "transport_receive_failed";
    case Code::kWorkerCreationFailed:
      return "worker_creation_failed";
    case Code::kWorkerFailed:
      return "worker_failed";
    case Code::kInvariantViolated:
      return "invariant_violated";
    case Code::kStrictRecordingFailed:
      return "strict_recording_failed";
    case Code::kStrictCaptureFailed:
      return "strict_capture_failed";
    case Code::kInvalidConfiguration:
      return "invalid_configuration";
    case Code::kInvalidContent:
      return "invalid_content";
    case Code::kDependencyInitFailed:
      return "dependency_init_failed";
  }
  return "unknown";
}

std::string_view DispositionName(Disposition disposition) {
  switch (disposition) {
    case Disposition::kPeer:
      return "peer";
    case Disposition::kSession:
      return "session";
    case Disposition::kSubsystem:
      return "subsystem";
    case Disposition::kRuntime:
      return "runtime";
    case Disposition::kProcess:
      return "process";
  }
  return "unknown";
}

// Text in double quotes, with what would end the quotes or the line escaped:
// a dependency's message must not forge a second key or a second log line.
std::string Quoted(std::string_view text) {
  std::string quoted = "\"";
  for (const char c : text) {
    switch (c) {
      case '"':
        quoted += "\\\"";
        break;
      case '\\':
        quoted += "\\\\";
        break;
      case '\n':
        quoted += "\\n";
        break;
      case '\r':
        quoted += "\\r";
        break;
      default:
        quoted += c;
    }
  }
  quoted += '"';
  return quoted;
}

// A context value as is when it is one plain logfmt token, else Quoted.
std::string Value(std::string_view value) {
  const bool plain = !value.empty() && value.find_first_of(" \"=\\\r\n") == std::string_view::npos;
  return plain ? std::string(value) : Quoted(value);
}

}  // namespace

Disposition DispositionOf(Code code) {
  switch (code) {
    case Code::kInvalidPeerInput:
      return Disposition::kPeer;
    case Code::kPeerMisbehaving:
    case Code::kPeerConnectionLost:
    case Code::kJoinRefused:
    case Code::kServerUnreachable:
      return Disposition::kSession;
    case Code::kRecordingWriteFailed:
    case Code::kRecordingFlushFailed:
    case Code::kMetricsEndpointFailed:
    case Code::kCaptureWriteFailed:
    case Code::kCaptureFlushFailed:
    case Code::kCaptureQueueFull:
    case Code::kCaptureRecordTooLong:
      return Disposition::kSubsystem;
    case Code::kTransportInitFailed:
    case Code::kListenerSetupFailed:
    case Code::kTransportSendFailed:
    case Code::kTransportReceiveFailed:
    case Code::kWorkerCreationFailed:
    case Code::kWorkerFailed:
    case Code::kInvariantViolated:
    case Code::kStrictRecordingFailed:
    case Code::kStrictCaptureFailed:
      return Disposition::kRuntime;
    case Code::kInvalidConfiguration:
    case Code::kInvalidContent:
    case Code::kDependencyInitFailed:
      return Disposition::kProcess;
  }
  // A value no enumerator names is a corrupted one: trust nothing narrower.
  return Disposition::kProcess;
}

std::string DescribeFailure(const Failure& failure) {
  std::string line =
      std::format("code={} disposition={}", CodeName(failure.code), DispositionName(DispositionOf(failure.code)));
  for (const ContextField& field : failure.context) {
    std::format_to(std::back_inserter(line), " {}={}", field.key, Value(field.value));
  }
  if (!failure.detail.empty()) {
    std::format_to(std::back_inserter(line), " detail={}", Quoted(failure.detail));
  }
  return line;
}

void Faults::Arm(Site site, std::string_view detail, std::uint32_t times) {
  Armed& armed = sites_[static_cast<std::size_t>(site)];
  const std::lock_guard<std::mutex> lock(mutex_);
  armed.detail = detail;
  armed.times.store(times, std::memory_order_relaxed);
}

void Faults::Disarm(Site site) { Arm(site, {}, 0); }

std::optional<std::string> Faults::Trip(Site site) {
  Armed& armed = sites_[static_cast<std::size_t>(site)];
  if (armed.times.load(std::memory_order_relaxed) == 0) {
    return std::nullopt;
  }
  const std::lock_guard<std::mutex> lock(mutex_);
  const std::uint32_t times = armed.times.load(std::memory_order_relaxed);
  if (times == 0) {
    return std::nullopt;
  }
  if (times != kEveryTime) {
    armed.times.store(times - 1, std::memory_order_relaxed);
  }
  return armed.detail;
}

void Faults::ThrowIfTripped(Site site) {
  if (std::optional<std::string> detail = Trip(site)) {
    throw InjectedFault(*detail);
  }
}

}  // namespace augusta::failure
