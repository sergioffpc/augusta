#include "replay_server.h"

#include <algorithm>
#include <chrono>
#include <cstddef>
#include <cstdint>
#include <expected>
#include <filesystem>
#include <format>
#include <memory>
#include <mutex>
#include <optional>
#include <ranges>
#include <stdexcept>
#include <string>
#include <string_view>
#include <system_error>
#include <unordered_map>
#include <utility>
#include <variant>
#include <vector>

#include "augusta/failure.h"
#include "augusta/first_failure.h"
#include "augusta/logging.h"
#include "augusta/networking.h"
#include "augusta/parameters.h"
#include "augusta/protocol.h"
#include "augusta/replication.h"
#include "augusta/simulation.h"
#include "augusta/tick.h"
#include "augusta/version.h"
#include "capture.h"
#include "connection_sample.h"
#include "content.h"
#include "heartbeat.h"
#include "host_log.h"
#include "host_metrics.h"
#include "match.h"
#include "misbehaviour.h"
#include "peer_gate.h"
#include "policy_loader.h"
#include "replay.h"
#include "replay_catalog.h"
#include "simulation_mapping.h"
#include "tick_messages.h"
#include "wire.h"

namespace augusta::server {

namespace {

// A Replay viewer plays no one, so it is no player's session: Session IDs
// start at 1 (ADR-0038).
constexpr SessionId kViewerSession{0};

// The characters' paths, in the scenario's order: what a capture's Joins must name.
std::vector<std::string> PathsOf(const std::vector<Character>& characters) {
  std::vector<std::string> paths;
  paths.reserve(characters.size());
  for (const Character& character : characters) {
    paths.push_back(character.path);
  }
  return paths;
}

// The directory a replay server replays from, which must be one.
std::filesystem::path RequireDirectory(const std::filesystem::path& directory) {
  std::error_code error;
  if (!std::filesystem::is_directory(directory, error)) {
    throw std::runtime_error(
        std::format("server::ReplayServer: the replay captures directory {} is not a directory", directory.string()));
  }
  return directory;
}

}  // namespace

std::expected<std::vector<ViewerMessage>, failure::Failure> ViewerMessages(const ReplayTick& tick) {
  const simulation::State& state = tick.result.state;
  std::vector<protocol::MessageWire> messages;
  std::vector<networking::Reliability> reliabilities;
  const auto add = [&](protocol::MessageWire message, networking::Reliability reliability) {
    messages.push_back(std::move(message));
    reliabilities.push_back(reliability);
  };
  // Every body, and no recipient: a viewer has no rifle, health or Commands of its own.
  add(ToWire(replication::PlanUpdates(state, state.tick, {})), networking::Reliability::kUnreliable);
  for (const replication::Shot& shot : replication::PlanShots(state, state.tick)) {
    add(ToWire(shot), networking::Reliability::kReliable);
  }
  for (const replication::Death& death : replication::PlanDeaths(state)) {
    add(ToWire(death), networking::Reliability::kReliable);
  }
  add(ToWire(tick.views, state.tick), networking::Reliability::kUnreliable);
  if (tick.end.has_value()) {
    add(ToWire(MatchEnd{.players = {}, .winner = tick.end->winner}), networking::Reliability::kReliable);
  }
  std::vector<ViewerMessage> encoded;
  encoded.reserve(messages.size());
  for (std::size_t i = 0; i < messages.size(); ++i) {
    auto payload = EncodeToSend(messages[i]);
    if (!payload.has_value()) {
      return std::unexpected(std::move(payload.error()));
    }
    encoded.push_back({.payload = *std::move(payload), .reliability = reliabilities[i]});
  }
  return encoded;
}

std::vector<ReplayListing> ListedOf(const std::vector<ReplayListing>& listings) {
  std::vector<ReplayListing> listed;
  // Newest last, as the listing orders them, so the oldest are the ones left out.
  for (const ReplayListing& listing : std::views::reverse(listings)) {
    if (listed.size() == protocol::kMaxReplayListings) {
      break;
    }
    // A name no Replay request could carry is no capture a viewer can watch.
    if (listing.name.size() > protocol::kMaxCaptureNameLength) {
      continue;
    }
    listed.push_back(listing);
  }
  std::ranges::reverse(listed);
  return listed;
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
  const Scenario scenario;
  const std::unordered_map<std::string, simulation::Character> characters;
  const PolicyMaker policy;
  // Network I/O thread only, without the lock: listing reads the disk.
  ReplayCatalog catalog;

  networking::Server network;
  failure::FirstFailure transport_failure;
  failure::FirstFailure invariant_failure;

  // Guards everything below: the Network I/O thread adds viewers, and both
  // threads take them out.
  mutable std::mutex mutex;
  std::unordered_map<networking::PeerId, std::shared_ptr<Viewer>> viewers;
  PeerGate gate{metrics};
  logging::Throttle drop_warnings{std::chrono::seconds{1}};
  // Simulation thread only.
  Heartbeat heartbeat{std::chrono::steady_clock::now()};

  Impl(const ReplayServerConfig& config, Scenario scenario_in, PolicyMaker policy_in)
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
    LI("subsystem=replay event=replay_server_started directory={} max_viewers={}", config.captures.string(),
       max_viewers);
  }

  [[nodiscard]] simulation::World NewWorld() const {
    return BuildSimulation(parameters, tick_rate_hz, scenario, policy());
  }

  // listen, once a World of the scenario has been built: a Map that is
  // rejected throws before the socket exists, so it leaves no bound port behind.
  [[nodiscard]] const networking::Endpoint& ListenOnceTheMapBuilds(const networking::Endpoint& listen) const {
    static_cast<void>(NewWorld());
    return listen;
  }

  // Sends payload, an encoded message, to peer as reliability says, counted
  // as Host counts what it sends. From either thread, with or without the lock.
  void Deliver(networking::PeerId peer, const networking::Payload& payload, networking::Reliability reliability) {
    networking::SendResult sent = SendCounted(network, metrics, peer, payload, reliability);
    if (!sent.has_value()) {
      transport_failure.Record(std::move(sent.error()));
    } else if (*sent == networking::SendOutcome::kAccepted && TypeOf(payload) == MessageType::kAuthoritativeState) {
      metrics.authoritative_state_update_bytes.Observe(static_cast<double>(payload.size()));
    }
  }

  // Sends message to peer as Deliver does, once encoded: one the protocol
  // cannot carry is sent to no one and kept in invariant_failure.
  void Send(networking::PeerId peer, const protocol::MessageWire& message, networking::Reliability reliability) {
    auto payload = EncodeToSend(message);
    if (!payload.has_value()) {
      invariant_failure.Record(std::move(payload.error()));
      return;
    }
    Deliver(peer, *payload, reliability);
  }

  void SetGauges() {
    metrics.sessions.Set(static_cast<double>(viewers.size()));
    metrics.replays.Set(static_cast<double>(viewers.size()));
  }

  // Everything of peer, whose connection is gone, is forgotten, its Replay
  // with it. With mutex held.
  void Forget(networking::PeerId peer) {
    gate.Left(peer);
    viewers.erase(peer);
    SetGauges();
  }

  // Closes peer's connection from this side, once what was sent to it
  // reliably has arrived. The transport reports no departure for it, so its
  // state goes here. With mutex held.
  void Close(networking::PeerId peer) {
    network.Disconnect(peer);
    gate.Expelled(peer);
    Forget(peer);
  }

  // peer's connection ended as how says. With mutex held.
  void HandleDisconnect(networking::PeerId peer, Leaving how) {
    if (viewers.contains(peer)) {
      LI("subsystem=replay event=viewer_left peer={} how={}", PeerNumber(peer), LeavingName(how));
      metrics.disconnects_from_match[how].Increment();
    } else {
      metrics.disconnects_before_admission[how].Increment();
    }
    Forget(peer);
  }

  // Disconnects peer for misbehaving or for not asking in time, for
  // reason, which only decides what is logged: a viewer's Replay ends with
  // it. With mutex held.
  void Expel(networking::PeerId peer, std::string_view reason) {
    LW("subsystem=replay event=misbehaving_disconnected peer={} reason=\"{}\"", PeerNumber(peer), reason);
    (viewers.contains(peer) ? metrics.disconnects_from_match
                            : metrics.disconnects_before_admission)[Leaving::kMisbehaving]
        .Increment();
    Close(peer);
  }

  // Counts rejection toward peer's misbehaviour, and expels it once it has
  // misbehaved too often. With mutex held.
  void Judge(networking::PeerId peer, PeerRejection rejection, std::chrono::steady_clock::time_point now) {
    if (gate.Judge(peer, rejection, now) == Verdict::kDisconnect) {
      Expel(peer, DescribePeerRejection(rejection));
    }
  }

  // Refuses peer for reason. With mutex held.
  void Refuse(networking::PeerId peer, JoinRefusal reason, std::chrono::steady_clock::time_point now) {
    LI("subsystem=replay event=join_refused peer={} reason=\"{}\"", PeerNumber(peer), DescribeJoinRefusal(reason));
    Send(peer, protocol::JoinRefusedWire{.reason = ToWire(reason)}, networking::Reliability::kReliable);
    metrics.joins_refused[reason].Increment();
    Judge(peer, PeerRejection::kJoinRefused, now);
  }

  // A replay server plays no Match: whatever asks to play in one - a Join
  // request, and a Reenact request (ADR-0050) - is refused here, the same way.
  // With mutex held.
  void RefuseToPlay(networking::PeerId peer, std::chrono::steady_clock::time_point now) {
    Refuse(peer, JoinRefusal::kReplayServer, now);
  }

  // Answers a Replay list request with the captures replayed here, then
  // closes the connection: a peer asks once a connection, and what else it
  // sent in the round is ignored. The directory is looked at with lock, which
  // holds mutex, let go, so no tick waits on the disk.
  void HandleReplayListRequest(networking::PeerId peer, std::unique_lock<std::mutex>& lock) {
    lock.unlock();
    const std::vector<ReplayListing> listed = ListedOf(catalog.List());
    lock.lock();
    LI("subsystem=replay event=replay_list peer={} captures={}", PeerNumber(peer), listed.size());
    Send(peer, ToWire(listed), networking::Reliability::kReliable);
    Close(peer);
  }

  // Starts a Replay of the capture request names for peer, or refuses it:
  // after its version and pack (RefusalOfClient), its capture, then whether
  // max_viewers already run. The capture is read and the World built here, on
  // the Network I/O thread and with lock, which holds mutex, let go, so no
  // tick waits on either. Only this thread adds viewers, so none joins in the
  // meantime.
  void HandleReplayRequest(networking::PeerId peer, const ReplayRequest& request,
                           std::chrono::steady_clock::time_point now, std::unique_lock<std::mutex>& lock) {
    if (const std::optional<JoinRefusal> refusal =
            RefusalOfClient(request.engine_version, request.client_pack, terms)) {
      Refuse(peer, *refusal, now);
      return;
    }
    lock.unlock();
    std::optional<Capture> capture = catalog.Find(request.capture);
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
    Send(peer, ToWire(Admission{.session = kViewerSession, .character = {}}, tick_rate_hz, parameters),
         networking::Reliability::kReliable);
    Send(peer, ToWire(viewer->replay->Start(), viewer->replay->Spawns(), viewer->replay->FirstTick()),
         networking::Reliability::kReliable);
    viewers.emplace(peer, std::move(viewer));
    metrics.joins_admitted.Increment();
    SetGauges();
    LI("subsystem=replay event=replay_started peer={} capture={} viewers={}", PeerNumber(peer), request.capture,
       viewers.size());
  }

  // Drops what message brought and judges its peer, for reason. With mutex held.
  void Drop(const networking::PeerMessage& message, std::string_view reason,
            std::chrono::steady_clock::time_point now) {
    LW_LIMITED(drop_warnings, "subsystem=replay event=dropped_malformed peer={} bytes={} reason=\"{}\"",
               PeerNumber(message.from), message.payload.size(), reason);
    Judge(message.from, PeerRejection::kNotAClientMessage, now);
  }

  // With lock, which holds mutex.
  void HandleMessage(const networking::PeerMessage& message, std::chrono::steady_clock::time_point now,
                     std::unique_lock<std::mutex>& lock) {
    const auto decoded = protocol::Decode(message.payload);
    if (!decoded.has_value()) {
      LW_LIMITED(drop_warnings, "subsystem=replay event=dropped_malformed peer={} bytes={} reason=\"{}\"",
                 PeerNumber(message.from), message.payload.size(), protocol::DescribeDecodeError(decoded.error()));
      Judge(message.from, PeerRejection::kUndecodable, now);
      return;
    }
    metrics.messages_received[TypeOf(message.payload)].Increment();
    // A viewer has asked for its Replay and sends nothing more: whatever it
    // sends, a request among them, is dropped and judged, and its Replay goes on.
    if (viewers.contains(message.from)) {
      Drop(message, "a viewer sends nothing", now);
    } else if (std::holds_alternative<protocol::ReplayListRequestWire>(*decoded)) {
      HandleReplayListRequest(message.from, lock);
    } else if (const auto* request = std::get_if<protocol::ReplayRequestWire>(&*decoded)) {
      HandleReplayRequest(message.from, FromWire(*request), now, lock);
    } else if (std::holds_alternative<protocol::JoinRequestWire>(*decoded)) {
      RefuseToPlay(message.from, now);
    } else {
      Drop(message, "not a replay request", now);
    }
  }

  // Runs viewer's Replay one tick and sends it what the tick resolved, timed
  // into the metrics; returns whether the Replay has ended. Simulation thread,
  // without the lock: only this thread steps a Replay, and the transport and
  // the metrics are safe from any thread.
  bool TickViewer(networking::PeerId peer, Viewer& viewer) {
    const auto start = std::chrono::steady_clock::now();
    const ReplayTick tick = viewer.replay->Step();
    if (tick.divergence.has_value()) {
      LW("subsystem=replay event=replay_diverged peer={} capture={} {}", PeerNumber(peer), viewer.capture,
         DescribeDivergence(*tick.divergence));
    }
    // None of the tick is sent if the protocol cannot carry all of it.
    auto messages = ViewerMessages(tick);
    if (!messages.has_value()) {
      invariant_failure.Record(std::move(messages.error()));
    } else {
      for (const ViewerMessage& message : *messages) {
        Deliver(peer, message.payload, message.reliability);
      }
    }
    metrics.replay_tick_duration.Observe(
        std::chrono::duration<double>(std::chrono::steady_clock::now() - start).count());
    if (!tick.end.has_value()) {
      return false;
    }
    LI("subsystem=replay event=replay_ended peer={} capture={} ticks={}", PeerNumber(peer), viewer.capture,
       tick.result.state.tick);
    return true;
  }
};

ReplayServer::ReplayServer(const ReplayServerConfig& config, Scenario scenario, PolicyMaker policy)
    : impl_(std::make_unique<Impl>(config, std::move(scenario), std::move(policy))) {}

ReplayServer::~ReplayServer() = default;

networking::Endpoint ReplayServer::ListenEndpoint() const { return impl_->network.LocalEndpoint(); }

void ReplayServer::PumpNetwork(std::chrono::steady_clock::time_point now) {
  Impl& impl = *impl_;
  PumpPeers(impl.network, impl.metrics, impl.mutex, impl.gate, now,
            PeerHandlers{
                .disconnected = [&impl](networking::PeerId peer, Leaving how) { impl.HandleDisconnect(peer, how); },
                .message = [&impl, now](const networking::PeerMessage& message,
                                        std::unique_lock<std::mutex>& lock) { impl.HandleMessage(message, now, lock); },
                .overdue = [&impl](networking::PeerId peer) { impl.Expel(peer, "not admitted in time"); },
            },
            impl.transport_failure);
}

void ReplayServer::Tick() {
  Impl& impl = *impl_;
  std::vector<std::pair<networking::PeerId, std::shared_ptr<Impl::Viewer>>> watching;
  {
    const std::lock_guard<std::mutex> lock(impl.mutex);
    watching.assign(impl.viewers.begin(), impl.viewers.end());
  }
  // Each Replay is stepped without the lock, so the Network I/O thread waits
  // on none of them: only a viewer's own lookup is under it.
  std::vector<std::pair<networking::PeerId, std::shared_ptr<Impl::Viewer>>> ended;
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
  LD("subsystem=replay event=heartbeat viewers={} ticks={} late={} overrun={} messages={} dropped={} misbehaving={}",
     Viewers(), second->ticks, second->late, second->overrun, second->messages, second->dropped, second->misbehaving);
}

std::vector<ConnectionSample> ReplayServer::SampleConnections() {
  std::vector<ConnectionSample> samples;
  for (const networking::PeerStats& peer : impl_->network.GetStats()) {
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

}  // namespace augusta::server
