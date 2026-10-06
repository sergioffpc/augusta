#include "metrics.h"

#include <atomic>
#include <chrono>
#include <cstddef>
#include <cstdint>
#include <cstdlib>
#include <exception>
#include <iterator>
#include <memory>
#include <string>
#include <thread>
#include <utility>
#include <vector>

#include <boost/asio.hpp>
#include <boost/beast.hpp>
#include <prometheus/collectable.h>
#include <prometheus/gauge.h>
#include <prometheus/metric_family.h>
#include <prometheus/registry.h>
#include <prometheus/text_serializer.h>

#include "augusta/logging.h"
#include "augusta/tick.h"
#include "augusta/version.h"
#include "liveness.h"

namespace augusta::server {
namespace {

namespace asio = boost::asio;
namespace beast = boost::beast;
namespace http = beast::http;
using Tcp = asio::ip::tcp;

using Request = http::request<http::string_body>;
using Response = http::response<http::string_body>;

// The exposition format's own content type (Prometheus' text format 0.0.4).
constexpr const char* kExpositionContentType = "text/plain; version=0.0.4; charset=utf-8";
constexpr const char* kTextContentType = "text/plain; charset=utf-8";

// How long a connection has to send its request and take the response: a
// scraper or probe that stalls is dropped rather than held.
constexpr std::chrono::seconds kRequestTimeout{5};

// What the endpoint answers from, read on its thread.
struct Sources {
  const prometheus::Registry& registry;
  ServerMetrics server_metrics;
  const std::atomic<tick::Clock::time_point>& last_tick_end;
};

// Every family /metrics serves: the Process family, then the server's.
std::vector<prometheus::MetricFamily> CollectAll(const Sources& sources) {
  std::vector<prometheus::MetricFamily> families = sources.registry.Collect();
  for (const prometheus::Collectable& each : sources.server_metrics) {
    std::vector<prometheus::MetricFamily> server = each.Collect();
    families.insert(families.end(), std::make_move_iterator(server.begin()), std::make_move_iterator(server.end()));
  }
  return families;
}

// What builds the server image stamps into it; local builds have none.
std::string BuildCommit() {
  const char* const commit = std::getenv("AUGUSTA_COMMIT");
  return commit != nullptr && *commit != '\0' ? std::string(commit) : std::string("unknown");
}

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

Response TextResponse(const Request& request, http::status status, const char* content_type, std::string body) {
  Response response(status, request.version());
  response.set(http::field::content_type, content_type);
  response.keep_alive(false);
  response.body() = std::move(body);
  response.prepare_payload();
  return response;
}

// Decision: the response to request. A request that fails is logged and
// answered 500, so the endpoint keeps serving.
Response Respond(const Sources& sources, const Request& request) {
  try {
    if (request.method() != http::verb::get) {
      return TextResponse(request, http::status::method_not_allowed, kTextContentType, "only GET\n");
    }
    if (request.target() == "/metrics") {
      return TextResponse(request, http::status::ok, kExpositionContentType,
                          prometheus::TextSerializer().Serialize(CollectAll(sources)));
    }
    if (request.target() == "/livez") {
      // now first: a tick that ends between the two reads is then still live.
      const tick::Clock::time_point now = tick::Clock::now();
      return IsLive(sources.last_tick_end.load(std::memory_order_relaxed), now)
                 ? TextResponse(request, http::status::ok, kTextContentType, "ok\n")
                 : TextResponse(request, http::status::service_unavailable, kTextContentType,
                                "no tick has finished within the liveness window\n");
    }
    return TextResponse(request, http::status::not_found, kTextContentType, "not found\n");
  } catch (const std::exception& error) {
    LE("subsystem=metrics event=request_failed target={} error={}", std::string(request.target()), error.what());
    return TextResponse(request, http::status::internal_server_error, kTextContentType, "internal error\n");
  }
}

// Mechanism: one connection, one request and its response, then closed. Kept
// alive by the handlers it has pending on the endpoint's io_context.
class Session : public std::enable_shared_from_this<Session> {
 public:
  Session(Tcp::socket socket, const Sources& sources) : stream_(std::move(socket)), sources_(sources) {}

  void Start() {
    stream_.expires_after(kRequestTimeout);
    http::async_read(stream_, buffer_, request_,
                     [self = shared_from_this()](beast::error_code error, std::size_t /*bytes*/) {
                       if (!error) {
                         self->Write();
                       }
                     });
  }

 private:
  void Write() {
    response_ = Respond(sources_, request_);
    http::async_write(stream_, response_, [self = shared_from_this()](beast::error_code /*error*/, std::size_t) {
      beast::error_code ignored;
      self->stream_.socket().shutdown(Tcp::socket::shutdown_send, ignored);
    });
  }

  beast::tcp_stream stream_;
  beast::flat_buffer buffer_;
  Request request_;
  Response response_;
  const Sources& sources_;
};

// The listening socket. Windows' SO_REUSEADDR would let a second server take
// a port already in use; elsewhere it lets a restarted server bind while its
// predecessor's closed connections linger in TIME_WAIT.
Tcp::acceptor Listen(asio::io_context& io, std::uint16_t port) {
#ifdef _WIN32
  constexpr bool kReuseAddress = false;
#else
  constexpr bool kReuseAddress = true;
#endif
  return {io, Tcp::endpoint(Tcp::v4(), port), kReuseAddress};
}

}  // namespace

struct MetricsEndpoint::Impl {
  prometheus::Registry registry;
  Sources sources;
  asio::io_context io;
  Tcp::acceptor acceptor;
  // Declared last, so it is joined before what it serves from goes.
  std::thread thread;

  Impl(std::uint16_t port, const std::atomic<tick::Clock::time_point>& last_tick_end, ServerMetrics server_metrics)
      : sources{.registry = registry, .server_metrics = std::move(server_metrics), .last_tick_end = last_tick_end},
        acceptor(Listen(io, port)) {
    AddProcessMetrics(registry);
    Accept();
    thread = std::thread([this] { Serve(); });
  }

  ~Impl() {
    io.stop();
    thread.join();
  }

  Impl(const Impl&) = delete;
  Impl& operator=(const Impl&) = delete;
  Impl(Impl&&) = delete;
  Impl& operator=(Impl&&) = delete;

  void Accept() {
    acceptor.async_accept([this](beast::error_code error, Tcp::socket socket) {
      if (error == asio::error::operation_aborted) {
        return;
      }
      if (error) {
        LW("subsystem=metrics event=accept_failed error={}", error.message());
      } else {
        std::make_shared<Session>(std::move(socket), sources)->Start();
      }
      Accept();
    });
  }

  // The endpoint's thread (ADR-0049). Not supervised: if it fails, the failure
  // is logged and the tick loop keeps running; /livez then stops answering.
  void Serve() {
    try {
      io.run();
    } catch (const std::exception& error) {
      LE("subsystem=metrics event=serving_failed error={}", error.what());
    }
  }
};

MetricsEndpoint::MetricsEndpoint(std::uint16_t port, const std::atomic<tick::Clock::time_point>& last_tick_end,
                                 ServerMetrics server_metrics)
    : impl_(std::make_unique<Impl>(port, last_tick_end, std::move(server_metrics))) {}

MetricsEndpoint::~MetricsEndpoint() = default;

}  // namespace augusta::server
