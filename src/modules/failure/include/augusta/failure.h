#ifndef AUGUSTA_FAILURE_H_
#define AUGUSTA_FAILURE_H_

#include <cstdint>
#include <exception>
#include <expected>
#include <string>
#include <type_traits>
#include <vector>

/// \file
/// The error model the Client, the Server and the shared modules describe an
/// operational failure with (ADR-0033): a stable Code, which classifies it into
/// the Disposition it is recovered at; context naming what it happened to; and
/// the dependency's own words as detail, kept for diagnosis but never what
/// recovery branches on. It decides nothing about recovery itself: the boundary
/// that owns the scope a Disposition names stops the peer, Session, subsystem,
/// runtime or process, and writes the one ERR or CRIT line (ADR-0029). Other
/// modules' own error types stay as they are until they are moved onto it.
/// faults.h is the controlled fault injection tests make dependencies fail with.
namespace augusta::failure {

/// The scope a failure is recovered at, narrowest first.
enum class Disposition : std::uint8_t {
  /// One message from one peer is dropped; the peer stays connected.
  kPeer,
  /// One Session ends; the others and the Match go on.
  kSession,
  /// An optional, non-authoritative subsystem (metrics) stops or
  /// degrades, observably; authority is untouched.
  kSubsystem,
  /// The runtime stops: no new work is admitted, its workers are stopped and
  /// joined, and only then are its resources released.
  kRuntime,
  /// The process exits with a failure status.
  kProcess,
};

/// A failure's stable identity. Its values are fixed once released, so logs,
/// metrics and exit classifications keep meaning the same thing; a new failure
/// gets a new value, never a reused one. 100, 101 and 207 are retired.
enum class Code : std::uint16_t {
  /// A peer sent a message that does not decode or is not allowed now.
  kInvalidPeerInput = 1,
  /// A peer misbehaved often or badly enough to be disconnected.
  kPeerMisbehaving = 2,
  /// A peer's connection closed or timed out.
  kPeerConnectionLost = 3,
  /// The server refused this client's join (a client's one Session).
  kJoinRefused = 4,
  /// The connection ended before the server admitted this client: nothing
  /// answered, or what did was not a compatible server.
  kServerUnreachable = 5,
  /// The metrics endpoint stopped accepting connections.
  kMetricsEndpointFailed = 102,
  /// The local transport could not be initialized.
  kTransportInitFailed = 200,
  /// The listen socket or poll group could not be set up.
  kListenerSetupFailed = 201,
  /// The local transport refused a send.
  kTransportSendFailed = 202,
  /// The local transport failed to receive.
  kTransportReceiveFailed = 203,
  /// A worker thread could not be started.
  kWorkerCreationFailed = 204,
  /// A worker stopped on an unclassified failure.
  kWorkerFailed = 205,
  /// An invariant protocol correctness, authority or a resource's lifetime
  /// depends on does not hold.
  kInvariantViolated = 206,
  /// The command line or config file is not usable.
  kInvalidConfiguration = 300,
  /// A content pack, map, scenario or script is not usable.
  kInvalidContent = 301,
  /// A dependency the process needs before any runtime starts failed to start.
  kDependencyInitFailed = 302,
};

/// The scope code is recovered at: one fixed answer per Code, so no caller
/// decides it from a message.
[[nodiscard]] Disposition DispositionOf(Code code);

/// One `key=value` of a failure's context, e.g. `session=3`.
struct ContextField {
  std::string key;
  std::string value;
};

/// An operational failure.
struct Failure {
  Code code{};
  /// What it happened to, in order: a Session, a thread, a file.
  std::vector<ContextField> context;
  /// What the dependency said, for whoever diagnoses it; empty when it said
  /// nothing. Never sent to a peer.
  std::string detail;
};

/// The failure as logfmt for its boundary's log line (ADR-0029), e.g.
/// `code=transport_send_failed disposition=runtime session=3 detail="..."`.
[[nodiscard]] std::string DescribeFailure(const Failure& failure);

/// Runs body, the call into a dependency that may throw, and returns what it
/// returns; an exception escaping it becomes a Failure with code and the
/// exception's message as detail. The one place a dependency's exception is
/// allowed to reach: it goes no further than this call.
template <typename Body>
[[nodiscard]] std::expected<std::invoke_result_t<Body&>, Failure> Guard(Code code, Body&& body) {
  using Result = std::invoke_result_t<Body&>;
  try {
    if constexpr (std::is_void_v<Result>) {
      body();
      return {};
    } else {
      return body();
    }
  } catch (const std::exception& error) {
    return std::unexpected(Failure{.code = code, .context = {}, .detail = error.what()});
  } catch (...) {
    return std::unexpected(Failure{.code = code, .context = {}, .detail = "unknown exception"});
  }
}

}  // namespace augusta::failure

#endif  // AUGUSTA_FAILURE_H_
