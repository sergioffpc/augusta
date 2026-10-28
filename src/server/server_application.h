#ifndef AUGUSTA_SERVER_APPLICATION_H_
#define AUGUSTA_SERVER_APPLICATION_H_

#include <expected>
#include <optional>

#include "augusta/application.h"
#include "augusta/config.h"
#include "augusta/failure.h"
#include "augusta/faults.h"
#include "augusta/server_config.h"
#include "server.h"

/// \file
/// augustad's phases behind the application boundary (application.h,
/// ADR-0033): reading its config file, initializing the transport, verifying
/// its pack and loading the content a ServerRuntime is constructed from, then
/// running it. Each phase classifies its own failure - an unusable config,
/// content that does not verify or load, a transport that does not start - so
/// main() only concludes the one Outcome. The runtime's own failures are its
/// supervisor's first cause (server.h), passed on as they are. The runtime it
/// constructs watches the process's shutdown request (shutdown_signal.h).
/// main.cpp adds only what a test cannot: installing the signal handlers, and
/// the exit.
namespace augusta::server {

/// The config file command_line names, read, or the
/// failure::Code::kInvalidConfiguration failure saying why command_line or the
/// file is unusable, or that an exception escaping the read became
/// (failure::Guard), so none reaches std::terminate. A command line asking for
/// help or the version is main's to answer before this.
[[nodiscard]] std::expected<config::ServerConfig, failure::Failure> ReadServerConfig(
    const std::expected<config::CommandLine, config::ConfigError>& command_line);

/// augustad's Program for file_config: initialize logs the machine and the
/// allocator, mimalloc as main() read it or the platform's when it read none (a
/// sanitizer build, ADR-0078), then starts the transport
/// (failure::Code::kTransportInitFailed), after the physics recording
/// `debug.physics_recording` asks for in a Development build, which the
/// Program holds until it goes (physics::StartRecording); construct verifies the pack and loads
/// its content (kInvalidContent), then constructs the ServerRuntime, which
/// throws its failure already classified (a rejected map mesh, a listen address
/// that can't be parsed or bound, a capture directory that can't be created;
/// see host.h); run runs it until Stop(), the process's shutdown request
/// (shutdown_signal.h) or its first cause. faults, for tests
/// only, is asked at the transport's initialization, by the server at each of
/// its sites (host.h) and by the runtime's supervisor, and must outlive the
/// Program's run.
[[nodiscard]] application::Program<ServerRuntime> ServerProgram(
    const config::ServerConfig& file_config, const std::optional<application::MimallocFacts>& mimalloc,
    failure::Faults* faults = nullptr);

}  // namespace augusta::server

#endif  // AUGUSTA_SERVER_APPLICATION_H_
