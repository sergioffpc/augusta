#include "augusta/replay_server.h"

#include <chrono>
#include <cstddef>
#include <cstdint>
#include <expected>
#include <filesystem>
#include <memory>
#include <mutex>
#include <optional>
#include <string>
#include <string_view>
#include <system_error>
#include <unordered_map>
#include <utility>
#include <variant>
#include <vector>

#include "augusta/capture.h"
#include "augusta/connection_sample.h"
#include "augusta/content.h"
#include "augusta/failure.h"
#include "augusta/failure_latch.h"
#include "augusta/heartbeat.h"
#include "augusta/host_log.h"
#include "augusta/host_metrics.h"
#include "augusta/link.h"
#include "augusta/link_mapping.h"
#include "augusta/link_types.h"
#include "augusta/logging.h"
#include "augusta/match.h"
#include "augusta/misbehaviour.h"
#include "augusta/parameters.h"
#include "augusta/peer_gate.h"
#include "augusta/policy_loader.h"
#include "augusta/replay.h"
#include "augusta/replay_catalog.h"
#include "augusta/replication.h"
#include "augusta/simulation.h"
#include "augusta/simulation_mapping.h"
#include "augusta/tick.h"
#include "augusta/tick_messages.h"
#include "augusta/uuid.h"
#include "augusta/version.h"
#include "utf8_path.h"

namespace augusta::host {

namespace {

// A Replay viewer plays no one, so it is no player's session: Session IDs
// start at 1 (ADR-0038).
constexpr SessionId kViewerSession{0};

// The characters' paths, in the scenario's order: what a capture's Joins must name.
std::vector<std::string> PathsOf(const std::vector<scenario::Character>& characters) {
  std::vector<std::string> paths;
  paths.reserve(characters.size());
  for (const scenario::Character& character : characters) {
    paths.push_back(character.path);
  }
  return paths;
}

// The directory a replay server replays from, which must be one.
std::filesystem::path RequireDirectory(const std::filesystem::path& directory) {
  std::error_code error;
  if (!std::filesystem::is_directory(directory, error)) {
    throw failure::ClassifiedFailure(failure::Failure{.code = failure::Code::kInvalidConfiguration,
                                                      .context = {{.key = "directory", .value = Utf8(directory)}},
                                                      .detail = "the replay captures directory is not a directory"});
  }
  return directory;
}

}  // namespace

std::expected<std::vector<link::Outgoing>, failure::Failure> ViewerMessages(const ReplayTick& tick) {
  const simulation::State& state = tick.result.state;
  std::vector<std::expected<link::Outgoing, failure::Failure>> messages;
  // Every body, and no recipient: a viewer has no rifle, health or Commands of its own.
  messages.push_back(link::Encode(ToLink(replication::PlanUpdates(state, state.tick, {}))));
  for (const replication::Shot& shot : replication::PlanShots(state, state.tick)) {
    messages.push_back(link::Encode(ToLink(shot)));
  }
  for (const replication::Death& death : replication::PlanDeaths(state)) {
    messages.push_back(link::Encode(ToLink(death)));
  }
  messages.push_back(link::Encode(ToLink(tick.views, state.tick)));
  if (tick.end.has_value()) {
    messages.push_back(link::Encode(ToLink(MatchEnd{.players = {}, .winner = tick.end->winner})));
  }
  std::vector<link::Outgoing> encoded;
  encoded.reserve(messages.size());
  for (std::expected<link::Outgoing, failure::Failure>& message : messages) {
    if (!message.has_value()) {
      return std::unexpected(std::move(message.error()));
    }
    encoded.push_back(*std::move(message));
  }
  return encoded;
}

struct ReplayServer::Impl {
  // One Replay and the capture it is of. Shared with a tick under way, so the
  // Network I/O thread can forget a viewer that leaves while the Simulation
  // thread still steps its Replay outside the lock.
  struct Viewer {
    std::string capture;
    std::unique_ptr<Replay> replay;
  };

  HostMetrics metrics;
  const std::uint8_t tick_rate_hz;
  const parameters::Parameters parameters;
  const std::size_t max_viewers;
  const ClientTerms terms;
  // What every Replay's World is built from, unchanged for the server's life.
  const scenario::Scenario scenario;
  const std::unordered_map<std::string, simulation::Character> characters;
  const policy::PolicyMaker policy;
  // Network I/O thread only, without the lock: listing reads the disk.
  ReplayCatalog catalog;

  link::Server network;
  failure::FailureLatch transport_failure;
  failure::FailureLatch invariant_failure;

  // Guards everything below: the Network I/O thread adds viewers, and both
  // threads take them out.
  mutable std::mutex mutex;
  std::unordered_map<link::PeerId, std::shared_ptr<Viewer>> viewers;
  PeerGate gate{metrics};
  logging::Throttle drop_warnings{std::chrono::seconds{1}};
  // Simulation thread only.
  Heartbeat heartbeat{std::chrono::steady_clock::now()};

  Impl(const ReplayServerConfig& config, scenario::Scenario scenario_in, policy::PolicyMaker policy_in)
      : metrics(config.tick_rate_hz),
        tick_rate_hz(config.tick_rate_hz),
        parameters(config.parameters),
        max_viewers(config.max_viewers),
        terms{.engine_version = std::string(EngineVersion()), .client_pack = scenario_in.client_pack},
        scenario(std::move(scenario_in)),
        characters(ToSimulation(scenario.characters)),
        policy(std::move(policy_in)),
        catalog(RequireDirectory(config.captures), ReplayTerms{.server_pack = config.server_pack,
                                                               .tick_rate_hz = config.tick_rate_hz,
                                                               .characters = PathsOf(scenario.characters)}),
        network(ListenOnceTheMapBuilds(config.listen), config.faults) {
    LOG_INFO("subsystem=replay event=replay_server_started directory={} max_viewers={}", Utf8(config.captures),
             max_viewers);
  }

  [[nodiscard]] simulation::World NewWorld() const {
    return BuildSimulation(parameters, tick_rate_hz, scenario, policy());
  }

  // listen, once a World of the scenario has been built: a Map that is
  // rejected throws before the socket exists, so it leaves no bound port behind.
  [[nodiscard]] const link::Endpoint& ListenOnceTheMapBuilds(const link::Endpoint& listen) const {
    static_cast<void>(NewWorld());
    return listen;
  }

  // Sends message, encoded, to peer, counted as Host counts what it sends.
  // From either thread, with or without the lock.
  void Deliver(link::PeerId peer, const link::Outgoing& message) {
    link::SendResult sent = SendCounted(network, metrics, peer, message);
    if (!sent.has_value()) {
      transport_failure.Record(std::move(sent.error()));
    } else if (*sent == link::SendOutcome::kAccepted && message.Type() == MessageType::kAuthoritativeState) {
      metrics.authoritative_state_update_bytes.Observe(static_cast<double>(message.Payload().size()));
    }
  }

  // Sends message to peer as Deliver does: one the protocol cannot carry is
  // sent to no one and kept in invariant_failure.
  void Send(link::PeerId peer, const std::expected<link::Outgoing, failure::Failure>& message) {
    if (!message.has_value()) {
      invariant_failure.Record(message.error());
      return;
    }
    Deliver(peer, *message);
  }

  void SetGauges() {
    metrics.sessions.Set(static_cast<double>(viewers.size()));
    metrics.replays.Set(static_cast<double>(viewers.size()));
  }

  // Everything of peer, whose connection is gone, is forgotten, its Replay
  // with it. With mutex held.
  void Forget(link::PeerId peer) {
    gate.Left(peer);
    viewers.erase(peer);
    SetGauges();
  }

  // Closes peer's connection from this side, once what was sent to it
  // reliably has arrived. The transport reports no departure for it, so its
  // state goes here. With mutex held.
  void Close(link::PeerId peer) {
    network.Disconnect(peer);
    gate.Expelled(peer);
    Forget(peer);
  }

  // peer's connection ended as how says. With mutex held.
  void HandleDisconnect(link::PeerId peer, Leaving how) {
    if (viewers.contains(peer)) {
      LOG_INFO("subsystem=replay event=viewer_left peer={} how={}", PeerNumber(peer), LeavingName(how));
      metrics.disconnects_from_match[how].Increment();
    } else {
      metrics.disconnects_before_admission[how].Increment();
    }
    Forget(peer);
  }

  // Disconnects peer for misbehaving or for not asking in time, for
  // reason, which only decides what is logged: a viewer's Replay ends with
  // it. With mutex held.
  void Expel(link::PeerId peer, std::string_view reason) {
    LOG_WARN("subsystem=replay event=misbehaving_disconnected peer={} reason=\"{}\"", PeerNumber(peer), reason);
    (viewers.contains(peer) ? metrics.disconnects_from_match
                            : metrics.disconnects_before_admission)[Leaving::kMisbehaving]
        .Increment();
    Close(peer);
  }

  // Counts rejection toward peer's misbehaviour, and expels it once it has
  // misbehaved too often. With mutex held.
  void Judge(link::PeerId peer, PeerRejection rejection, std::chrono::steady_clock::time_point now) {
    if (gate.Judge(peer, rejection, now) == Verdict::kDisconnect) {
      Expel(peer, DescribePeerRejection(rejection));
    }
  }

  // Refuses peer for reason. With mutex held.
  void Refuse(link::PeerId peer, JoinRefusal reason, std::chrono::steady_clock::time_point now) {
    LOG_INFO("subsystem=replay event=join_refused peer={} reason=\"{}\"", PeerNumber(peer),
             DescribeJoinRefusal(reason));
    Send(peer, link::Encode(ToLink(reason)));
    metrics.joins_refused[reason].Increment();
    Judge(peer, PeerRejection::kJoinRefused, now);
  }

  // A replay server plays no Match: whatever asks to play in one - a Join
  // request, and a Reenact request (ADR-0050) - is refused here, the same way.
  // With mutex held.
  void RefuseToPlay(link::PeerId peer, std::chrono::steady_clock::time_point now) {
    Refuse(peer, JoinRefusal::kReplayServer, now);
  }

  // Answers a Replay list request with the captures replayed here, then
  // closes the connection: a peer asks once a connection, and what else it
  // sent in the round is ignored. The directory is looked at with lock, which
  // holds mutex, let go, so no tick waits on the disk.
  void HandleReplayListRequest(link::PeerId peer, std::unique_lock<std::mutex>& lock) {
    lock.unlock();
    const std::vector<ReplayListing> listed = link::ListedOf(catalog.List());
    lock.lock();
    LOG_INFO("subsystem=replay event=replay_list peer={} captures={}", PeerNumber(peer), listed.size());
    Send(peer, link::Encode(listed));
    Close(peer);
  }

  // Starts a Replay of the capture request names for peer, or refuses it:
  // after its version and pack (RefusalOfClient), its capture, then whether
  // max_viewers already run. The capture is read and the World built here, on
  // the Network I/O thread and with lock, which holds mutex, let go, so no
  // tick waits on either. Only this thread adds viewers, so none joins in the
  // meantime.
  void HandleReplayRequest(link::PeerId peer, const ReplayRequest& request, std::chrono::steady_clock::time_point now,
                           std::unique_lock<std::mutex>& lock) {
    if (const std::optional<JoinRefusal> refusal =
            RefusalOfClient(request.engine_version, request.client_pack, terms)) {
      Refuse(peer, *refusal, now);
      return;
    }
    lock.unlock();
    std::optional<capture::Capture> capture = catalog.Find(request.capture);
    lock.lock();
    if (!capture.has_value()) {
      Refuse(peer, JoinRefusal::kUnknownCapture, now);
      return;
    }
    if (viewers.size() >= max_viewers) {
      Refuse(peer, JoinRefusal::kLobbyFull, now);
      return;
    }
    lock.unlock();
    auto viewer = std::make_shared<Viewer>(Viewer{
        .capture = request.capture, .replay = std::make_unique<Replay>(*std::move(capture), NewWorld(), characters)});
    lock.lock();
    gate.Admitted(peer);
    Send(peer, link::Encode(ToLink(Admission{.session = kViewerSession, .character = {}}, tick_rate_hz, parameters)));
    // A capture carries no Match ID, so the Replay's Match start names none.
    Send(peer, link::Encode(ToLink(viewer->replay->Start(), viewer->replay->Spawns(), viewer->replay->FirstTick(),
                                   uuid::Uuid{})));
    viewers.emplace(peer, std::move(viewer));
    metrics.joins_admitted.Increment();
    SetGauges();
    LOG_INFO("subsystem=replay event=replay_started peer={} capture={} viewers={}", PeerNumber(peer), request.capture,
             viewers.size());
  }

  // Drops what message brought and judges its peer, for reason. With mutex held.
  void Drop(const link::PeerMessage& message, std::string_view reason, std::chrono::steady_clock::time_point now) {
    LOG_WARN_LIMITED(drop_warnings, "subsystem=replay event=dropped_malformed peer={} bytes={} reason=\"{}\"",
                     PeerNumber(message.from), message.bytes, reason);
    Judge(message.from, PeerRejection::kNotAClientMessage, now);
  }

  // With lock, which holds mutex.
  void HandleMessage(const link::PeerMessage& message, std::chrono::steady_clock::time_point now,
                     std::unique_lock<std::mutex>& lock) {
    if (!message.type.has_value()) {
      LOG_WARN_LIMITED(drop_warnings, "subsystem=replay event=dropped_malformed peer={} bytes={} reason=\"{}\"",
                       PeerNumber(message.from), message.bytes, message.type.error());
      Judge(message.from, PeerRejection::kUndecodable, now);
      return;
    }
    metrics.messages_received[*message.type].Increment();
    // A viewer has asked for its Replay and sends nothing more: whatever it
    // sends, a request among them, is dropped and judged, and its Replay goes on.
    if (viewers.contains(message.from)) {
      Drop(message, "a viewer sends nothing", now);
      return;
    }
    if (!message.message.has_value()) {
      Drop(message, "not a replay request", now);
      return;
    }
    const link::ClientMessage& said = *message.message;
    if (std::holds_alternative<link::ReplayListRequest>(said)) {
      HandleReplayListRequest(message.from, lock);
    } else if (const auto* request = std::get_if<ReplayRequest>(&said)) {
      HandleReplayRequest(message.from, *request, now, lock);
    } else if (std::holds_alternative<JoinRequest>(said)) {
      // A Join request, or a Reenact request (ADR-0050).
      RefuseToPlay(message.from, now);
    } else {
      Drop(message, "not a replay request", now);
    }
  }

  // Runs viewer's Replay one tick and sends it what the tick resolved, timed
  // into the metrics; returns whether the Replay has ended. Simulation thread,
  // without the lock: only this thread steps a Replay, and the transport and
  // the metrics are safe from any thread.
  bool TickViewer(link::PeerId peer, Viewer& viewer) {
    const auto start = std::chrono::steady_clock::now();
    const ReplayTick tick = viewer.replay->Step();
    if (tick.divergence.has_value()) {
      LOG_WARN("subsystem=replay event=replay_diverged peer={} capture={} {}", PeerNumber(peer), viewer.capture,
               DescribeDivergence(*tick.divergence));
    }
    // None of the tick is sent if the protocol cannot carry all of it.
    auto messages = ViewerMessages(tick);
    if (!messages.has_value()) {
      invariant_failure.Record(std::move(messages.error()));
    } else {
      for (const link::Outgoing& message : *messages) {
        Deliver(peer, message);
      }
    }
    metrics.replay_tick_duration.Observe(
        std::chrono::duration<double>(std::chrono::steady_clock::now() - start).count());
    if (!tick.end.has_value()) {
      return false;
    }
    LOG_INFO("subsystem=replay event=replay_ended peer={} capture={} ticks={}", PeerNumber(peer), viewer.capture,
             tick.result.state.tick);
    return true;
  }
};

ReplayServer::ReplayServer(const ReplayServerConfig& config, scenario::Scenario scenario, policy::PolicyMaker policy)
    : impl_(std::make_unique<Impl>(config, std::move(scenario), std::move(policy))) {}

ReplayServer::~ReplayServer() = default;

link::Endpoint ReplayServer::ListenEndpoint() const { return impl_->network.LocalEndpoint(); }

void ReplayServer::PumpNetwork(std::chrono::steady_clock::time_point now) {
  Impl& impl = *impl_;
  PumpPeers(impl.network, impl.metrics, impl.mutex, impl.gate, now,
            PeerHandlers{
                .disconnected = [&impl](link::PeerId peer, Leaving how) { impl.HandleDisconnect(peer, how); },
                .message = [&impl, now](const link::PeerMessage& message,
                                        std::unique_lock<std::mutex>& lock) { impl.HandleMessage(message, now, lock); },
                .overdue = [&impl](link::PeerId peer) { impl.Expel(peer, "not admitted in time"); },
            },
            impl.transport_failure);
}

void ReplayServer::Tick() {
  Impl& impl = *impl_;
  std::vector<std::pair<link::PeerId, std::shared_ptr<Impl::Viewer>>> watching;
  {
    const std::lock_guard<std::mutex> lock(impl.mutex);
    watching.assign(impl.viewers.begin(), impl.viewers.end());
  }
  // Each Replay is stepped without the lock, so the Network I/O thread waits
  // on none of them: only a viewer's own lookup is under it.
  std::vector<std::pair<link::PeerId, std::shared_ptr<Impl::Viewer>>> ended;
  for (const auto& [peer, viewer] : watching) {
    if (impl.TickViewer(peer, *viewer)) {
      ended.emplace_back(peer, viewer);
    }
  }
  const std::lock_guard<std::mutex> lock(impl.mutex);
  for (const auto& [peer, viewer] : ended) {
    // Unless it left meanwhile, and the connection is another's by now.
    if (const auto still = impl.viewers.find(peer); still != impl.viewers.end() && still->second == viewer) {
      impl.Close(peer);
    }
  }
}

void ReplayServer::RecordTiming(const tick::Timing& timing) {
  const std::optional<Activity> second =
      CountTick(impl_->metrics, impl_->heartbeat, timing, std::chrono::steady_clock::now());
  if (!second.has_value()) {
    return;
  }
  LOG_DEBUG(
      "subsystem=replay event=heartbeat viewers={} ticks={} late={} overrun={} messages={} dropped={} misbehaving={}",
      Viewers(), second->ticks, second->late, second->overrun, second->messages, second->dropped, second->misbehaving);
}

std::vector<ConnectionSample> ReplayServer::SampleConnections() {
  std::vector<ConnectionSample> samples;
  for (const link::PeerStats& peer : impl_->network.GetStats()) {
    samples.push_back(ConnectionSample{.session = std::nullopt, .stats = peer.stats});
  }
  return samples;
}

std::size_t ReplayServer::Viewers() const {
  const std::lock_guard<std::mutex> lock(impl_->mutex);
  return impl_->viewers.size();
}

const HostMetrics& ReplayServer::Metrics() const { return impl_->metrics; }

std::optional<failure::Failure> ReplayServer::TakeTransportFailure() { return impl_->transport_failure.Take(); }

std::optional<failure::Failure> ReplayServer::TakeInvariantFailure() { return impl_->invariant_failure.Take(); }

}  // namespace augusta::host
