#include "metrics.h"

#include <atomic>
#include <chrono>
#include <cstdint>
#include <cstdlib>
#include <exception>
#include <memory>
#include <string>
#include <string_view>
#include <vector>

#include <CivetServer.h>
#include <civetweb.h>
#include <prometheus/gauge.h>
#include <prometheus/registry.h>
#include <prometheus/text_serializer.h>

#include "augusta/logging.h"
#include "augusta/tick.h"
#include "augusta/version.h"
#include "liveness.h"

namespace augusta::server {
namespace {

// The exposition format's own content type (Prometheus' text format 0.0.4).
constexpr const char* kExpositionContentType = "text/plain; version=0.0.4; charset=utf-8";
constexpr int kInternalServerError = 500;
constexpr int kServiceUnavailable = 503;

// What builds the server image stamps into it; local builds have none.
std::string BuildCommit() {
  const char* const commit = std::getenv("AUGUSTA_COMMIT");
  return commit != nullptr && *commit != '\0' ? std::string(commit) : std::string("unknown");
}

void SendText(mg_connection* conn, std::string_view body, const char* content_type) {
  mg_send_http_ok(conn, content_type, static_cast<std::int64_t>(body.size()));
  mg_write(conn, body.data(), body.size());
}

// civetweb calls a handler from C, so an exception must not leave it: a failed
// request is logged and answered 500, and the endpoint keeps serving.
template <typename Respond>
bool Answer(std::string_view path, mg_connection* conn, Respond respond) {
  try {
    respond();
  } catch (const std::exception& error) {
    LE("subsystem=metrics event=request_failed path={} error={}", path, error.what());
    mg_send_http_error(conn, kInternalServerError, "%s", "internal error");
  }
  return true;
}

class MetricsHandler : public CivetHandler {
 public:
  explicit MetricsHandler(const prometheus::Registry& registry) : registry_(registry) {}

  bool handleGet(CivetServer* /*server*/, mg_connection* conn) override {
    return Answer("/metrics", conn, [this, conn] {
      SendText(conn, prometheus::TextSerializer().Serialize(registry_.Collect()), kExpositionContentType);
    });
  }

 private:
  const prometheus::Registry& registry_;
};

class LivezHandler : public CivetHandler {
 public:
  explicit LivezHandler(const std::atomic<tick::Clock::time_point>& last_tick_end) : last_tick_end_(last_tick_end) {}

  bool handleGet(CivetServer* /*server*/, mg_connection* conn) override {
    return Answer("/livez", conn, [this, conn] {
      // now first: a tick that ends between the two reads is then still live.
      const tick::Clock::time_point now = tick::Clock::now();
      if (IsLive(last_tick_end_.load(std::memory_order_relaxed), now)) {
        SendText(conn, "ok\n", "text/plain; charset=utf-8");
      } else {
        mg_send_http_error(conn, kServiceUnavailable, "%s", "no tick has finished within the liveness window");
      }
    });
  }

 private:
  const std::atomic<tick::Clock::time_point>& last_tick_end_;
};

// The metrics fixed for the life of the process.
void AddProcessMetrics(prometheus::Registry& registry) {
  prometheus::BuildGauge()
      .Name("augustad_build_info")
      .Help("The version and commit augustad was built from, always 1.")
      .Register(registry)
      .Add({{"version", std::string(EngineVersion())}, {"commit", BuildCommit()}})
      .Set(1);
  const auto start = std::chrono::duration<double>(std::chrono::system_clock::now().time_since_epoch());
  prometheus::BuildGauge()
      .Name("augustad_start_time_seconds")
      .Help("When augustad started serving, in seconds since the Unix epoch.")
      .Register(registry)
      .Add({})
      .Set(start.count());
}

}  // namespace

struct MetricsEndpoint::Impl {
  prometheus::Registry registry;
  MetricsHandler metrics{registry};
  LivezHandler livez;
  // Declared last, so it stops serving before the handlers it calls go.
  CivetServer server;

  Impl(std::uint16_t port, const std::atomic<tick::Clock::time_point>& last_tick_end)
      : livez(last_tick_end),
        // One worker: requests come from one scraper and one probe.
        server(std::vector<std::string>{"listening_ports", std::to_string(port), "num_threads", "1"}) {
    AddProcessMetrics(registry);
    server.addHandler("/metrics", metrics);
    server.addHandler("/livez", livez);
  }
};

MetricsEndpoint::MetricsEndpoint(std::uint16_t port, const std::atomic<tick::Clock::time_point>& last_tick_end)
    : impl_(std::make_unique<Impl>(port, last_tick_end)) {}

MetricsEndpoint::~MetricsEndpoint() = default;

}  // namespace augusta::server
