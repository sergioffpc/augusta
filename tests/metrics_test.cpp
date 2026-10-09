#include "metrics.h"

#include <atomic>
#include <chrono>
#include <cstddef>
#include <cstdint>
#include <optional>
#include <stdexcept>
#include <string>
#include <string_view>
#include <thread>
#include <vector>

#include <boost/asio.hpp>
#include <boost/beast.hpp>
#include <gtest/gtest.h>
#include <prometheus/collectable.h>
#include <prometheus/counter.h>
#include <prometheus/gauge.h>
#include <prometheus/metric_family.h>
#include <prometheus/registry.h>

#include "augusta/faults.h"
#include "augusta/logging.h"
#include "augusta/tick.h"
#include "liveness.h"

// The endpoint over real HTTP on the loopback, so its paths and their status
// codes are checked as Prometheus and the liveness probe see them, and its
// acceptor made to fail (faults.h), so its degradation is checked as the
// platform sees it: a closed port and one log line.
namespace {

namespace asio = boost::asio;
namespace beast = boost::beast;
namespace http = beast::http;
using Tcp = asio::ip::tcp;

using augusta::failure::Faults;
using augusta::failure::Site;
using augusta::server::AcceptRetryDelay;
using augusta::server::kLivenessWindow;
using augusta::server::kMetricsAcceptAttempts;
using augusta::server::kMetricsFirstAcceptRetry;
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

// Whether a connection to port on the loopback is refused: nothing listens.
bool Refused(std::uint16_t port) {
  asio::io_context io;
  Tcp::socket socket(io);
  beast::error_code error;
  socket.connect(Tcp::endpoint(asio::ip::address_v4::loopback(), port), error);
  return error == asio::error::connection_refused;
}

// Whether endpoint stops being available within a generous bound on its
// retries' waits.
bool BecomesUnavailable(const MetricsEndpoint& endpoint) {
  const auto deadline = std::chrono::steady_clock::now() + std::chrono::seconds{10};
  while (endpoint.Available() && std::chrono::steady_clock::now() < deadline) {
    std::this_thread::sleep_for(std::chrono::milliseconds{10});
  }
  return !endpoint.Available();
}

std::size_t Occurrences(std::string_view text, std::string_view needle) {
  std::size_t count = 0;
  for (std::size_t at = text.find(needle); at != std::string_view::npos; at = text.find(needle, at + 1)) {
    ++count;
  }
  return count;
}

// The longest the endpoint waits in all between its first accept failure in a
// row and its last, permanent one.
std::chrono::milliseconds RetryWaits() {
  std::chrono::milliseconds total{0};
  for (int failures = 1; failures < kMetricsAcceptAttempts; ++failures) {
    total += *AcceptRetryDelay(failures);
  }
  return total;
}

// What the server counts, failing to be collected.
class FailingCollectable : public prometheus::Collectable {
 public:
  std::vector<prometheus::MetricFamily> Collect() const override { throw std::runtime_error("collection failed"); }
};

class MetricsEndpointTest : public ::testing::Test {
 protected:
  std::uint16_t port_ = FreePort();
  std::atomic<Clock::time_point> last_tick_end_{Clock::now()};
  // What the server counts, as the endpoint is handed it: here, one counter.
  prometheus::Registry server_metrics_;
  Faults faults_;
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

TEST_F(MetricsEndpointTest, APeerThatDisconnectsMidRequestDoesNotStopTheEndpoint) {
  const MetricsEndpoint endpoint(port_, last_tick_end_, {server_metrics_});
  {
    asio::io_context io;
    beast::tcp_stream stream(io);
    stream.connect(Tcp::endpoint(asio::ip::address_v4::loopback(), port_));
    asio::write(stream, asio::buffer(std::string_view("GET /livez HTTP/1.1\r\n")));
  }

  EXPECT_EQ(Get(port_, "/livez").result(), http::status::ok);
  EXPECT_TRUE(endpoint.Available());
}

TEST_F(MetricsEndpointTest, AnAcceptFailureThatClearsWithinTheAttemptsIsRecoveredFrom) {
  faults_.Arm(Site::kMetricsAccept, "Too many open files", static_cast<std::uint32_t>(kMetricsAcceptAttempts - 1));
  const MetricsEndpoint endpoint(port_, last_tick_end_, {server_metrics_}, &faults_);

  // Connected while the acceptor fails: answered once it accepts again.
  EXPECT_EQ(Get(port_, "/livez").result(), http::status::ok);
  EXPECT_TRUE(endpoint.Available());
}

TEST_F(MetricsEndpointTest, AnAcceptFailureOnEveryAttemptMakesTheEndpointUnavailableAndClosesItsPort) {
  faults_.Arm(Site::kMetricsAccept, "Too many open files", static_cast<std::uint32_t>(kMetricsAcceptAttempts));
  const MetricsEndpoint endpoint(port_, last_tick_end_, {server_metrics_}, &faults_);

  ASSERT_TRUE(BecomesUnavailable(endpoint));
  // The liveness probe is refused, so the platform restarts the pod.
  EXPECT_TRUE(Refused(port_));
}

TEST_F(MetricsEndpointTest, APersistentAcceptFailureIsRetriedWithWaitsThenLoggedOnce) {
  augusta::logging::Init();
  augusta::logging::SetLogLevel(augusta::logging::Severity::kInfo);
  faults_.Arm(Site::kMetricsAccept, "Too many open files", Faults::kEveryTime);
  const auto start = std::chrono::steady_clock::now();
  std::chrono::steady_clock::duration took{};
  testing::internal::CaptureStdout();
  {
    const MetricsEndpoint endpoint(port_, last_tick_end_, {server_metrics_}, &faults_);
    ASSERT_TRUE(BecomesUnavailable(endpoint));
    took = std::chrono::steady_clock::now() - start;
    // As long as its longest wait, in which a further attempt would show.
    std::this_thread::sleep_for(*AcceptRetryDelay(kMetricsAcceptAttempts - 1));
  }
  const std::string written = testing::internal::GetCapturedStdout();

  // Not a tight loop: it waited between its attempts.
  EXPECT_GE(took, RetryWaits());
  EXPECT_EQ(Occurrences(written, "WARN"), static_cast<std::size_t>(kMetricsAcceptAttempts - 1)) << written;
  EXPECT_EQ(Occurrences(written, "event=accept_retrying"), static_cast<std::size_t>(kMetricsAcceptAttempts - 1))
      << written;
  EXPECT_EQ(Occurrences(written, "ERROR"), 1U) << written;
  EXPECT_EQ(Occurrences(written, "event=unavailable code=metrics_endpoint_failed disposition=subsystem"), 1U)
      << written;
}

TEST_F(MetricsEndpointTest, ARequestThatFailsIsAnswered500AndWarnedOfWhileTheEndpointKeepsServing) {
  augusta::logging::Init();
  augusta::logging::SetLogLevel(augusta::logging::Severity::kInfo);
  const FailingCollectable failing;
  const MetricsEndpoint endpoint(port_, last_tick_end_, {failing});

  testing::internal::CaptureStdout();
  const auto failed = Get(port_, "/metrics");
  const auto after = Get(port_, "/livez");
  const std::string written = testing::internal::GetCapturedStdout();

  EXPECT_EQ(failed.result(), http::status::internal_server_error);
  EXPECT_EQ(after.result(), http::status::ok);
  EXPECT_TRUE(endpoint.Available());
  EXPECT_EQ(Occurrences(written, "WARN"), 1U) << written;
  EXPECT_EQ(Occurrences(written, "event=request_failed"), 1U) << written;
  EXPECT_EQ(Occurrences(written, "ERROR"), 0U) << written;
}

TEST(AcceptRetryDelayTest, EachFailureInARowWaitsTwiceAsLongAsTheOneBefore) {
  EXPECT_EQ(AcceptRetryDelay(1), std::optional{kMetricsFirstAcceptRetry});
  for (int failures = 2; failures < kMetricsAcceptAttempts; ++failures) {
    ASSERT_TRUE(AcceptRetryDelay(failures).has_value());
    EXPECT_EQ(*AcceptRetryDelay(failures), *AcceptRetryDelay(failures - 1) * 2);
  }
}

TEST(AcceptRetryDelayTest, TheLastAttemptIsNotRetried) {
  EXPECT_EQ(AcceptRetryDelay(kMetricsAcceptAttempts), std::nullopt);
  EXPECT_EQ(AcceptRetryDelay(kMetricsAcceptAttempts + 1), std::nullopt);
}

}  // namespace
