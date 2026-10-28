#include "shutdown_signal.h"

#include <atomic>
#include <csignal>

namespace augusta::server {

namespace {

// Static storage, constant-initialized, so the handler never runs a guarded
// initialization, which is not async-signal-safe.
constinit std::atomic<bool> g_shutdown_request{false};
static_assert(std::atomic<bool>::is_always_lock_free, "the signal handler must set it lock-free");

extern "C" void HandleShutdownSignal(int signal) {
  g_shutdown_request.store(true);
  // Windows' C runtime resets the handler to SIG_DFL before calling it; the C
  // standard lets a handler re-install itself for the signal it handles.
  std::signal(signal, HandleShutdownSignal);
}

}  // namespace

const std::atomic<bool>& GlobalShutdownRequest() { return g_shutdown_request; }

void InstallShutdownSignalHandlers() {
  std::signal(SIGINT, HandleShutdownSignal);
  std::signal(SIGTERM, HandleShutdownSignal);
}

}  // namespace augusta::server
