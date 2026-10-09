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
#include <system_error>
#include <unordered_map>
#include <unordered_set>
#include <utility>
#include <variant>
#include <vector>

#include "admission.h"
#include "augusta/assets.h"
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
  // One Replay and the capture it is of, by its viewer's connection.
  struct Viewer {
    std::string capture;
    std::unique_ptr<Replay> replay;
  };

  HostMetrics metrics;
  const std::uint8_t tick_rate_hz;
  const parameters::Parameters parameters;
  const std::size_t max_viewers;
  const assets::PackHash client_pack;
  // What every Replay's World is built from, unchanged for the server's life.
  const Scenario scenario;
  const std::unordered_map<std::string, simulation::Character> characters;
  const PolicyMaker policy;
  const ReplayCatalog catalog;

  networking::Server network;
  failure::FirstFailure transport_failure;
  failure::FirstFailure invariant_failure;

  // Guards everything below: the Network I/O thread adds viewers, and both
  // threads take them out.
  mutable std::mutex mutex;
  std::unordered_map<networking::PeerId, Viewer> viewers;
  std::unordered_map<networking::PeerId, MisbehaviourTracker> misbehaviour;
  std::unordered_set<networking::PeerId> expelled;
  AdmissionDeadlines admission_deadlines;
  logging::Throttle drop_warnings{std::chrono::seconds{1}};
  // Simulation thread only.
  Heartbeat heartbeat{std::chrono::steady_clock::now()};

  Impl(const ReplayServerConfig& config, Scenario scenario_in, PolicyMaker policy_in)
      : metrics(config.tick_rate_hz),
        tick_rate_hz(config.tick_rate_hz),
        parameters(config.parameters),
        max_viewers(config.max_viewers),
        client_pack(scenario_in.client_pack),
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
  // as Host counts what it sends.
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

  // Closes peer's connection from this side, once what was sent to it
  // reliably has arrived. The transport reports no departure for it, so its
  // state goes here. With mutex held.
  void Close(networking::PeerId peer) {
    network.Disconnect(peer);
    expelled.insert(peer);
    Forget(peer);
  }

  void Forget(networking::PeerId peer) {
    misbehaviour.erase(peer);
    admission_deadlines.Left(peer);
    viewers.erase(peer);
    metrics.sessions.Set(static_cast<double>(viewers.size()));
  }

  // Counts rejection toward peer's misbehaviour, and disconnects it once it
  // has misbehaved too often. With mutex held.
  void Judge(networking::PeerId peer, PeerRejection rejection, std::chrono::steady_clock::time_point now) {
    if (IsMisbehaviour(rejection)) {
      metrics.misbehaviour[rejection].Increment();
    }
    if (misbehaviour[peer].Record(rejection, now) == Verdict::kDisconnect) {
      LW("subsystem=replay event=misbehaving_disconnected peer={} reason=\"{}\"", PeerNumber(peer),
         DescribePeerRejection(rejection));
      metrics.disconnects_before_admission[Leaving::kMisbehaving].Increment();
      Close(peer);
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
  // closes the connection. With mutex held.
  void HandleReplayListRequest(networking::PeerId peer) {
    const std::vector<ReplayListing> listed = ListedOf(catalog.List());
    LI("subsystem=replay event=replay_list peer={} captures={}", PeerNumber(peer), listed.size());
    Send(peer, ToWire(listed), networking::Reliability::kReliable);
    Close(peer);
  }

  // Why request is refused before its capture is looked for: its version,
  // then its pack, as a Join's are (ADR-0043).
  [[nodiscard]] std::optional<JoinRefusal> RefusalOf(const ReplayRequest& request) const {
    if (request.engine_version != EngineVersion()) {
      return JoinRefusal::kVersionMismatch;
    }
    if (request.client_pack != client_pack) {
      return JoinRefusal::kPackMismatch;
    }
    return std::nullopt;
  }

  // Starts a Replay of the capture request names for peer, or refuses it:
  // after its version and pack, its capture, then whether max_viewers already
  // run. The capture is read and the World built here, on the Network I/O
  // thread and with lock, which holds mutex, let go, so no tick waits on
  // either. Only this thread adds viewers, so none joins in the meantime.
  void HandleReplayRequest(networking::PeerId peer, const ReplayRequest& request,
                           std::chrono::steady_clock::time_point now, std::unique_lock<std::mutex>& lock) {
    if (viewers.contains(peer)) {
      LD("subsystem=replay event=dropped peer={} reason=\"a second replay request\"", PeerNumber(peer));
      return;
    }
    if (const std::optional<JoinRefusal> refusal = RefusalOf(request)) {
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
    auto replay = std::make_unique<Replay>(*std::move(capture), NewWorld(), characters);
    lock.lock();
    admission_deadlines.Admitted(peer);
    Send(peer, ToWire(Admission{.session = kViewerSession, .character = {}}, tick_rate_hz, parameters),
         networking::Reliability::kReliable);
    Send(peer, ToWire(replay->Start(), replay->Spawns(), replay->FirstTick()), networking::Reliability::kReliable);
    viewers.emplace(peer, Viewer{.capture = request.capture, .replay = std::move(replay)});
    metrics.joins_admitted.Increment();
    metrics.sessions.Set(static_cast<double>(viewers.size()));
    LI("subsystem=replay event=replay_started peer={} capture={} viewers={}", PeerNumber(peer), request.capture,
       viewers.size());
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
    if (std::holds_alternative<protocol::ReplayListRequestWire>(*decoded)) {
      HandleReplayListRequest(message.from);
    } else if (const auto* request = std::get_if<protocol::ReplayRequestWire>(&*decoded)) {
      HandleReplayRequest(message.from, FromWire(*request), now, lock);
    } else if (std::holds_alternative<protocol::JoinRequestWire>(*decoded)) {
      RefuseToPlay(message.from, now);
    } else {
      // Commands and Readies among them: a viewer sends nothing once watching.
      LW_LIMITED(drop_warnings,
                 "subsystem=replay event=dropped_malformed peer={} bytes={} reason=\"not a replay request\"",
                 PeerNumber(message.from), message.payload.size());
      Judge(message.from, PeerRejection::kNotAClientMessage, now);
    }
  }

  // Runs viewer's Replay one tick and sends it what the tick resolved;
  // returns whether the Replay has ended. With mutex held.
  bool TickViewer(networking::PeerId peer, Viewer& viewer) {
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
  for (const networking::PeerEvent& event : impl.network.PumpEvents()) {
    switch (event.type) {
      case networking::PeerEventType::kConnectRequested: {
        impl.network.Accept(event.peer);
        const std::lock_guard<std::mutex> lock(impl.mutex);
        impl.admission_deadlines.Connected(event.peer, now);
        break;
      }
      case networking::PeerEventType::kConnected:
        break;
      case networking::PeerEventType::kDisconnected: {
        const std::lock_guard<std::mutex> lock(impl.mutex);
        if (impl.viewers.contains(event.peer)) {
          LI("subsystem=replay event=viewer_left peer={}", PeerNumber(event.peer));
        }
        impl.Forget(event.peer);
        break;
      }
    }
  }
  auto received = impl.network.ReceiveMessages();
  if (!received.has_value()) {
    impl.transport_failure.Record(std::move(received.error()));
    return;
  }
  for (const networking::PeerMessage& message : *received) {
    std::unique_lock<std::mutex> lock(impl.mutex);
    if (impl.expelled.contains(message.from)) {
      continue;
    }
    impl.metrics.received_bytes.Increment(message.payload.size());
    impl.HandleMessage(message, now, lock);
  }
  const std::lock_guard<std::mutex> lock(impl.mutex);
  for (const networking::PeerId peer : impl.admission_deadlines.TakeOverdue(now)) {
    LW("subsystem=replay event=misbehaving_disconnected peer={} reason=\"not admitted in time\"", PeerNumber(peer));
    impl.metrics.disconnects_before_admission[Leaving::kMisbehaving].Increment();
    impl.Close(peer);
  }
  impl.expelled.clear();
}

void ReplayServer::Tick() {
  Impl& impl = *impl_;
  const std::lock_guard<std::mutex> lock(impl.mutex);
  std::vector<networking::PeerId> ended;
  for (auto& [peer, viewer] : impl.viewers) {
    if (impl.TickViewer(peer, viewer)) {
      ended.push_back(peer);
    }
  }
  for (const networking::PeerId peer : ended) {
    impl.Close(peer);
  }
}

void ReplayServer::RecordTiming(const tick::Timing& timing) {
  HostMetrics& metrics = impl_->metrics;
  metrics.tick_duration.Observe(std::chrono::duration<double>(timing.duration).count());
  metrics.ticks.Increment();
  metrics.ticks_late.Increment(timing.late ? 1 : 0);
  metrics.tick_overruns.Increment(timing.overrun ? 1 : 0);
  metrics.tick_resyncs.Increment(timing.resynchronised ? 1 : 0);
  const std::optional<Activity> second = impl_->heartbeat.Record(Totals(metrics), std::chrono::steady_clock::now());
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
