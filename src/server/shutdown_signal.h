#ifndef AUGUSTA_SERVER_SHUTDOWN_SIGNAL_H_
#define AUGUSTA_SERVER_SHUTDOWN_SIGNAL_H_

#include <atomic>

/// \file
/// augustad's shutdown signal (ADR-0005): its SIGINT/SIGTERM handler sets one
/// shutdown request that lives as long as the process, and touches nothing
/// else - no runtime, no supervisor - so a signal at any point, before a run,
/// during it or after its runtime is released, either stops the run or does
/// nothing. A ServerProgram's runtime watches the request (RuntimeOptions,
/// server.h) and turns it into its supervisor's stop. main.cpp installs the
/// handler. Windows' console control events (a closed console window) have
/// no handler: there, only Ctrl+C's SIGINT stops augustad.
namespace augusta::server {

/// The process's shutdown request: false until the handler
/// InstallShutdownSignalHandlers installs runs, then true for the rest of the
/// process. A lock-free atomic with static storage, so it outlives every
/// runtime that watches it.
[[nodiscard]] const std::atomic<bool>& GlobalShutdownRequest();

/// Installs the handler for SIGINT and SIGTERM that sets
/// GlobalShutdownRequest. The handler re-installs itself, so a second signal
/// is handled too where the platform resets it (Windows' C runtime).
void InstallShutdownSignalHandlers();

}  // namespace augusta::server

#endif  // AUGUSTA_SERVER_SHUTDOWN_SIGNAL_H_
