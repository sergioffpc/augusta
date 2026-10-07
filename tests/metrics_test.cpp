#include "metrics.h"

#include <atomic>
#include <chrono>
#include <cstdint>
#include <string>
#include <string_view>

#include <boost/asio.hpp>
#include <boost/beast.hpp>
#include <gtest/gtest.h>
#include <prometheus/counter.h>
#include <prometheus/gauge.h>
#include <prometheus/registry.h>

#include "augusta/tick.h"
#include "liveness.h"

// The endpoint over real HTTP on the loopback, so its paths and their status
// codes are checked as Prometheus and the liveness probe see them.
namespace {

namespace asio = boost::asio;
namespace beast = boost::beast;
namespace http = beast::http;
using Tcp = asio::ip::tcp;

using augusta::server::kLivenessWindow;
using augusta::server::MetricsEndpoint;
using augusta::tick::Clock;

// A port no socket holds now, for the endpoint to listen on.
std::uint16_t FreePort() {
  asio::io_context io;
  Tcp::acceptor acceptor(io, Tcp::endpoint(asio::ip::address_v4::loopback(), 0));
  return acceptor.local_endpoint().port();
}

http::response<http::string_body> Get(std::uint16_t port, std::string_view target,
                                      http::verb method = http::verb::get) {
  asio::io_context io;
  beast::tcp_stream stream(io);
  stream.connect(Tcp::endpoint(asio::ip::address_v4::loopback(), port));
  http::request<http::empty_body> request(method, target, 11);
  http::write(stream, request);
  beast::flat_buffer buffer;
  http::response<http::string_body> response;
  http::read(stream, buffer, response);
  return response;
}

class MetricsEndpointTest : public ::testing::Test {
 protected:
  std::uint16_t port_ = FreePort();
  std::atomic<Clock::time_point> last_tick_end_{Clock::now()};
  // What the server counts, as the endpoint is handed it: here, one counter.
  prometheus::Registry server_metrics_;
};

// Requirements: NFR-07
TEST_F(MetricsEndpointTest, LivezIsOkRightAfterATick) {
  const MetricsEndpoint endpoint(port_, last_tick_end_, {server_metrics_});

  const auto response = Get(port_, "/livez");

  EXPECT_EQ(response.result(), http::status::ok);
}

// Requirements: NFR-07
TEST_F(MetricsEndpointTest, LivezIsUnavailableOnceNoTickHasFinishedWithinTheWindow) {
  last_tick_end_ = Clock::now() - kLivenessWindow - std::chrono::seconds{1};
  const MetricsEndpoint endpoint(port_, last_tick_end_, {server_metrics_});

  const auto response = Get(port_, "/livez");

  EXPECT_EQ(response.result(), http::status::service_unavailable);
}

// Requirements: NFR-07
TEST_F(MetricsEndpointTest, MetricsServesTheProcessMetricsInTheTextExposition) {
  const MetricsEndpoint endpoint(port_, last_tick_end_, {server_metrics_});

  const auto response = Get(port_, "/metrics");

  EXPECT_EQ(response.result(), http::status::ok);
  EXPECT_EQ(response[http::field::content_type], "text/plain; version=0.0.4; charset=utf-8");
  EXPECT_NE(response.body().find("augustad_build_info{"), std::string::npos) << response.body();
  EXPECT_NE(response.body().find("augustad_start_time_seconds "), std::string::npos) << response.body();
}

// Requirements: NFR-07
TEST_F(MetricsEndpointTest, MetricsServesWhatTheServerCountsBesideTheProcessMetrics) {
  prometheus::BuildCounter()
      .Name("augustad_ticks_total")
      .Help("Ticks run.")
      .Register(server_metrics_)
      .Add({})
      .Increment(3);
  const MetricsEndpoint endpoint(port_, last_tick_end_, {server_metrics_});

  const auto response = Get(port_, "/metrics");

  EXPECT_NE(response.body().find("augustad_ticks_total 3"), std::string::npos) << response.body();
  EXPECT_NE(response.body().find("augustad_build_info{"), std::string::npos) << response.body();
}

// Requirements: NFR-07
TEST_F(MetricsEndpointTest, MetricsServesEachOfTheServersSources) {
  prometheus::Registry connection_health;
  prometheus::BuildGauge()
      .Name("augustad_connection_pending_bytes")
      .Help("Queued.")
      .Register(connection_health)
      .Add({{"session_id", "1"}})
      .Set(5);
  prometheus::BuildCounter().Name("augustad_ticks_total").Help("Ticks run.").Register(server_metrics_).Add({});
  const MetricsEndpoint endpoint(port_, last_tick_end_, {server_metrics_, connection_health});

  const auto response = Get(port_, "/metrics");

  EXPECT_NE(response.body().find("augustad_ticks_total 0"), std::string::npos) << response.body();
  EXPECT_NE(response.body().find("augustad_connection_pending_bytes{session_id=\"1\"} 5"), std::string::npos)
      << response.body();
}

// Requirements: NFR-07
TEST_F(MetricsEndpointTest, AnUnknownPathIsNotFound) {
  const MetricsEndpoint endpoint(port_, last_tick_end_, {server_metrics_});

  EXPECT_EQ(Get(port_, "/healthz").result(), http::status::not_found);
}

// Requirements: NFR-07
TEST_F(MetricsEndpointTest, OnlyGetIsAnswered) {
  const MetricsEndpoint endpoint(port_, last_tick_end_, {server_metrics_});

  EXPECT_EQ(Get(port_, "/metrics", http::verb::post).result(), http::status::method_not_allowed);
}

// Requirements: NFR-07
TEST_F(MetricsEndpointTest, APortInUseIsRefused) {
  asio::io_context io;
  const Tcp::acceptor taken(io, Tcp::endpoint(Tcp::v4(), port_), false);

  EXPECT_ANY_THROW(MetricsEndpoint(port_, last_tick_end_, {server_metrics_}));
}

}  // namespace
