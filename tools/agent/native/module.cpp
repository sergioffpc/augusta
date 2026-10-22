// augusta_agent._native: the pybind11 bindings of an Agent (augusta/agent.h)
// and of the test-only in-process server (testing_server.h), wrapped by the
// augusta_agent package, and the dispatch thread that hands Agents' Reports
// (augusta/agent_report.h) to it. Every value a script reads is a copy:
// nothing it holds changes under it.

#include <cstdint>
#include <exception>
#include <filesystem>
#include <memory>
#include <optional>
#include <stdexcept>
#include <string>
#include <thread>
#include <tuple>
#include <unordered_map>
#include <utility>
#include <variant>
#include <vector>

#include <boost/uuid/uuid_io.hpp>
#include <pybind11/pybind11.h>
#include <pybind11/stl.h>
#include <pybind11/stl/filesystem.h>

#include "augusta/agent.h"
#include "augusta/agent_report.h"
#include "augusta/body_part.h"
#include "augusta/command.h"
#include "augusta/failure.h"
#include "augusta/harness.h"
#include "augusta/intent.h"
#include "augusta/link.h"
#include "augusta/logging.h"
#include "augusta/math.h"
#include "augusta/netcode_stats.h"
#include "augusta/physics.h"
#include "testing_server.h"

namespace py = pybind11;

namespace {

using augusta::agent::Agent;
using augusta::agent::AgentFailure;
using augusta::agent::AgentStats;
using augusta::agent::AgentView;
using augusta::agent::IntentEnded;
using augusta::agent::LoadedPack;
using augusta::agent::Readiness;
using augusta::agent::Report;
using augusta::agent::ReportQueue;
using augusta::agent::ReportSink;
using augusta::body_part::BodyPart;
using augusta::command::Command;
using augusta::harness::AimAt;
using augusta::harness::AimIntent;
using augusta::harness::AuthoritativeState;
using augusta::harness::Death;
using augusta::harness::EntityBody;
using augusta::harness::EntityId;
using augusta::harness::FireBursts;
using augusta::harness::HitConfirmation;
using augusta::harness::HoldFire;
using augusta::harness::IntentEnding;
using augusta::harness::Lobby;
using augusta::harness::Look;
using augusta::harness::MatchEnd;
using augusta::harness::MatchPlayer;
using augusta::harness::MatchStart;
using augusta::harness::Move;
using augusta::harness::MovementIntent;
using augusta::harness::MoveTo;
using augusta::harness::NetcodeStats;
using augusta::harness::Phase;
using augusta::harness::Reload;
using augusta::harness::RosterEntry;
using augusta::harness::SessionId;
using augusta::harness::Shot;
using augusta::harness::TriggerIntent;
using augusta::link::ConnectionStats;
using augusta::link::SimulatedConditions;
using augusta::math::Vec3;
using augusta::physics::BodyState;
using augusta::physics::Stance;

using Vec3Tuple = std::tuple<float, float, float>;

// What load_pack raises: the pack or its key did not verify, or its Map did
// not load.
class PackError : public std::runtime_error {
 public:
  using std::runtime_error::runtime_error;
};

Vec3Tuple ToTuple(const Vec3& vector) { return {vector.x, vector.y, vector.z}; }

Vec3 ToVec3(const Vec3Tuple& tuple) { return {std::get<0>(tuple), std::get<1>(tuple), std::get<2>(tuple)}; }

// An Entity ID as the number the script sees.
template <typename Id>
std::uint32_t Number(Id id) {
  return static_cast<std::uint32_t>(id);
}

// A Session ID as the string the script sees: its canonical form, which a
// script only compares.
std::string Text(const SessionId& session) { return boost::uuids::to_string(session); }

template <typename T>
py::tuple ToPyTuple(const std::vector<T>& items) {
  return py::tuple(py::cast(items));
}

// An object the script may close before Python lets it go, as a with block
// does: closing destroys it, joining its threads, after which its calls
// raise. Its threads never take the GIL, so it lets go of it meanwhile.
template <typename T>
class Closable {
 public:
  explicit Closable(std::unique_ptr<T> held) : held_(std::move(held)) {}

  T& Get() {
    if (!held_) {
      throw std::runtime_error("closed");
    }
    return *held_;
  }

  void Close() {
    std::unique_ptr<T> closing = std::move(held_);
    const py::gil_scoped_release release;
    closing.reset();
  }

 private:
  std::unique_ptr<T> held_;
};

// with closes it on the way out.
template <typename T, typename Class>
void DefContextManager(Class& bound) {
  bound.def("close", &Closable<T>::Close)
      .def(
          "__enter__", [](Closable<T>& self) -> Closable<T>& { return self; }, py::return_value_policy::reference)
      .def("__exit__", [](Closable<T>& self, const py::args&) { self.Close(); });
}

// The process's one ReportQueue, and the one dispatch thread that waits on it
// with the GIL released and calls, with it held, the receiver each Agent
// registered with every Report the Agent put: the Runner's threads put them
// and never touch Python. Started at import, stopped at exit before the
// interpreter finalizes (PYBIND11_MODULE), and never destroyed, as an Agent
// a script never closed may still put Reports after.
class Dispatch {
 public:
  static Dispatch& Get() {
    static auto* const kDispatch = new Dispatch();
    return *kDispatch;
  }

  // Calls receiver with every Report the Agent numbered as returned puts on
  // the sink For gives it, until Unregister. GIL held.
  std::uint64_t Register(py::object receiver) {
    const std::uint64_t agent = next_agent_++;
    receivers_.emplace(agent, std::move(receiver));
    return agent;
  }

  // Drops the Agent's Reports from now on, those waiting among them. GIL held.
  void Unregister(std::uint64_t agent) { receivers_.erase(agent); }

  // Where the Agent agent puts its Reports.
  ReportSink For(std::uint64_t agent) {
    return [this, agent](Report report) { queue_.Put(agent, std::move(report)); };
  }

  // Drops every receiver and Report, and joins the thread. GIL held, which it
  // lets go of while it joins.
  void Stop() {
    receivers_.clear();
    queue_.Close();
    const py::gil_scoped_release release;
    if (thread_.joinable()) {
      thread_.join();
    }
  }

 private:
  Dispatch() : thread_([this] { Run(); }) {}

  void Run() {
    for (std::vector<augusta::agent::AddressedReport> reports = queue_.TakeAll(); !reports.empty();
         reports = queue_.TakeAll()) {
      const py::gil_scoped_acquire gil;
      for (augusta::agent::AddressedReport& addressed : reports) {
        Deliver(addressed);
      }
    }
  }

  // GIL held. A receiver that raises is the script's bug, told as Python
  // tells an exception no one can catch; the next Report is still delivered.
  void Deliver(augusta::agent::AddressedReport& addressed) {
    const auto found = receivers_.find(addressed.agent);
    if (found == receivers_.end()) {
      return;
    }
    // Its own reference: the receiver may unregister itself.
    const py::object receiver = found->second;
    try {
      receiver(std::visit([](auto& report) { return py::cast(std::move(report)); }, addressed.report));
    } catch (py::error_already_set& error) {
      error.discard_as_unraisable("augusta_agent: delivering a Report");
    } catch (const std::exception& error) {
      LOG_ERROR("subsystem=agent event=report_dropped reason=\"{}\"", error.what());
    }
  }

  ReportQueue queue_;
  // GIL-guarded, as next_agent_ is.
  std::unordered_map<std::uint64_t, py::object> receivers_;
  std::uint64_t next_agent_ = 0;
  // Last, so it starts once the rest is there.
  std::thread thread_;
};

// An Agent and its receiver, which the script may close before Python lets
// it go: closing stops its Reports and destroys it, joining its threads,
// after which its calls raise.
class AgentHandle {
 public:
  AgentHandle(const std::string& server, const LoadedPack& pack, const std::string& character, Readiness readiness,
              py::object receiver)
      : number_(Dispatch::Get().Register(std::move(receiver))) {
    try {
      agent_ = std::make_unique<Agent>(server, pack, character, readiness, Dispatch::Get().For(number_));
    } catch (...) {
      Dispatch::Get().Unregister(number_);
      throw;
    }
  }

  AgentHandle(const AgentHandle&) = delete;
  AgentHandle& operator=(const AgentHandle&) = delete;
  AgentHandle(AgentHandle&&) = delete;
  AgentHandle& operator=(AgentHandle&&) = delete;
  ~AgentHandle() { Close(); }

  Agent& Get() {
    if (!agent_) {
      throw std::runtime_error("closed");
    }
    return *agent_;
  }

  // GIL held, which it lets go of while the Agent's threads are joined.
  void Close() {
    if (!agent_) {
      return;
    }
    Dispatch::Get().Unregister(number_);
    std::unique_ptr<Agent> closing = std::move(agent_);
    const py::gil_scoped_release release;
    closing.reset();
  }

 private:
  std::uint64_t number_;
  std::unique_ptr<Agent> agent_;
};

// Who is in the Lobby.
void BindLobby(py::module_& module) {
  py::class_<RosterEntry>(module, "RosterEntry")
      .def_property_readonly("session", [](const RosterEntry& entry) { return Text(entry.session); })
      .def_readonly("character", &RosterEntry::character);

  py::class_<Lobby>(module, "Lobby")
      .def_readonly("version", &Lobby::version)
      .def_property_readonly("roster", [](const Lobby& lobby) { return ToPyTuple(lobby.roster); });
}

// A Match's players and bodies, and an Agent's own.
void BindMatch(py::module_& module) {
  py::class_<MatchPlayer>(module, "MatchPlayer")
      .def_property_readonly("session", [](const MatchPlayer& player) { return Text(player.session); })
      .def_property_readonly("entity", [](const MatchPlayer& player) { return Number(player.entity); })
      .def_readonly("character", &MatchPlayer::character)
      .def_property_readonly("spawn", [](const MatchPlayer& player) { return ToTuple(player.spawn); });

  py::class_<BodyState>(module, "Body")
      .def_property_readonly("position", [](const BodyState& body) { return ToTuple(body.position); })
      .def_property_readonly("velocity", [](const BodyState& body) { return ToTuple(body.velocity); })
      .def_readonly("stance", &BodyState::stance);

  py::class_<EntityBody>(module, "EntityBody")
      .def_property_readonly("entity", [](const EntityBody& body) { return Number(body.entity); })
      .def_readonly("body", &EntityBody::body)
      .def_readonly("yaw", &EntityBody::yaw);

  py::class_<AuthoritativeState>(module, "AuthoritativeState")
      .def_readonly("tick", &AuthoritativeState::tick)
      .def_readonly("acknowledged_sequence", &AuthoritativeState::acknowledged_sequence)
      .def_property_readonly("bodies", [](const AuthoritativeState& state) { return ToPyTuple(state.bodies); })
      .def_readonly("health", &AuthoritativeState::health);
}

void BindView(py::module_& module) {
  py::enum_<Phase>(module, "Phase")
      .value("NOT_ADMITTED", Phase::kNotAdmitted)
      .value("LOBBY", Phase::kLobby)
      .value("MATCH", Phase::kMatch);

  BindLobby(module);
  BindMatch(module);

  // What an Agent has been told as of one moment, and its predicted body.
  py::class_<AgentView>(module, "View")
      .def_property_readonly("phase", [](const AgentView& view) { return view.server->GetPhase(); })
      .def_property_readonly("session",
                             [](const AgentView& view) -> std::optional<std::string> {
                               if (!view.server->accepted.has_value()) {
                                 return std::nullopt;
                               }
                               return Text(view.server->accepted->session);
                             })
      .def_property_readonly("player_count",
                             [](const AgentView& view) -> std::optional<std::uint8_t> {
                               if (!view.server->accepted.has_value()) {
                                 return std::nullopt;
                               }
                               return view.server->accepted->parameters.player_count;
                             })
      .def_property_readonly("lobby", [](const AgentView& view) { return view.server->lobby; })
      .def_property_readonly("matches_started", [](const AgentView& view) { return view.server->matches_started; })
      .def_property_readonly("in_match", [](const AgentView& view) { return view.server->in_match; })
      .def_property_readonly("match_players",
                             [](const AgentView& view) {
                               const auto& start = view.server->match_start;
                               return start.has_value() ? ToPyTuple(start->players) : py::tuple();
                             })
      .def_property_readonly("own_entity",
                             [](const AgentView& view) -> std::optional<std::uint32_t> {
                               const auto entity = view.server->OwnEntity();
                               if (!entity.has_value()) {
                                 return std::nullopt;
                               }
                               return Number(*entity);
                             })
      .def_property_readonly("authoritative", [](const AgentView& view) { return view.server->authoritative; })
      .def_property_readonly("dead",
                             [](const AgentView& view) {
                               std::vector<std::uint32_t> dead;
                               for (const EntityId entity : view.server->dead) {
                                 dead.push_back(Number(entity));
                               }
                               return ToPyTuple(dead);
                             })
      .def_readonly("own", &AgentView::own);
}

// What an Agent's script awaits (Agent.next_event), and what else it is told.
void BindReports(py::module_& module) {
  py::enum_<BodyPart>(module, "BodyPart")
      .value("HEAD", BodyPart::kHead)
      .value("TORSO", BodyPart::kTorso)
      .value("LIMB", BodyPart::kLimb);

  py::class_<Shot>(module, "Shot")
      .def_property_readonly("shooter", [](const Shot& shot) { return Number(shot.shooter); })
      .def_readonly("tick", &Shot::tick)
      .def_property_readonly("origin", [](const Shot& shot) { return ToTuple(shot.origin); })
      .def_readonly("yaw", &Shot::yaw)
      .def_readonly("pitch", &Shot::pitch);

  py::class_<HitConfirmation>(module, "HitConfirmation")
      .def_property_readonly("target", [](const HitConfirmation& hit) { return Number(hit.target); })
      .def_readonly("damage", &HitConfirmation::damage)
      .def_readonly("part", &HitConfirmation::part);

  py::class_<Death>(module, "Death")
      .def_property_readonly("victim", [](const Death& death) { return Number(death.victim); })
      .def_property_readonly("killer", [](const Death& death) { return Number(death.killer); })
      .def_readonly("yaw", &Death::yaw)
      .def_readonly("pitch", &Death::pitch)
      .def_readonly("part", &Death::part);

  py::class_<MatchStart>(module, "MatchStart")
      .def_property_readonly("players", [](const MatchStart& start) { return ToPyTuple(start.players); })
      .def_readonly("first_tick", &MatchStart::first_tick);

  py::class_<MatchEnd>(module, "MatchEnd")
      .def_property_readonly("winner", [](const MatchEnd& end) -> std::optional<std::string> {
        if (!end.winner.has_value()) {
          return std::nullopt;
        }
        return Text(*end.winner);
      });

  py::enum_<IntentEnding>(module, "IntentEnding")
      .value("REPLACED", IntentEnding::kReplaced)
      .value("DONE", IntentEnding::kDone)
      .value("ARRIVED", IntentEnding::kArrived)
      .value("BLOCKED", IntentEnding::kBlocked)
      .value("TARGET_GONE", IntentEnding::kTargetGone);

  py::class_<IntentEnded>(module, "IntentEnded")
      .def_readonly("intent", &IntentEnded::intent)
      .def_readonly("ending", &IntentEnded::ending);

  py::class_<AgentFailure>(module, "AgentFailure").def_readonly("description", &AgentFailure::description);
}

void BindCommand(py::module_& module) {
  py::enum_<Stance>(module, "Stance")
      .value("STANDING", Stance::kStanding)
      .value("CROUCHING", Stance::kCrouching)
      .value("PRONE", Stance::kProne);

  // One whole Command, as a Raw Intent holds it. Its Seen time is not the
  // script's: the Harness always fills it in.
  py::class_<Command>(module, "Command")
      .def(py::init([](const Vec3Tuple& move, bool sprint, Stance stance, float yaw, float pitch, bool ads, bool fire,
                       bool reload) {
             Command command;
             command.movement = {.direction = ToVec3(move), .sprint = sprint, .desired_stance = stance};
             command.yaw = yaw;
             command.pitch = pitch;
             command.ads = ads;
             command.fire = fire;
             command.reload = reload;
             return command;
           }),
           py::kw_only(), py::arg("move") = Vec3Tuple{}, py::arg("sprint") = false,
           py::arg("stance") = Stance::kStanding, py::arg("yaw") = 0.0F, py::arg("pitch") = 0.0F,
           py::arg("ads") = false, py::arg("fire") = false, py::arg("reload") = false)
      .def_property_readonly("move", [](const Command& command) { return ToTuple(command.movement.direction); })
      .def_property_readonly("sprint", [](const Command& command) { return command.movement.sprint; })
      .def_property_readonly("stance", [](const Command& command) { return command.movement.desired_stance; })
      .def_readonly("yaw", &Command::yaw)
      .def_readonly("pitch", &Command::pitch)
      .def_readonly("ads", &Command::ads)
      .def_readonly("fire", &Command::fire)
      .def_readonly("reload", &Command::reload);
}

// The Intents a script sets on an Agent's channels (augusta/intent.h).
void BindIntents(py::module_& module) {
  py::class_<Move>(module, "Move")
      .def(py::init([](const Vec3Tuple& direction, bool sprint, Stance stance) {
             return Move{.direction = ToVec3(direction), .sprint = sprint, .stance = stance};
           }),
           py::arg("direction"), py::kw_only(), py::arg("sprint") = false, py::arg("stance") = Stance::kStanding)
      .def_property_readonly("direction", [](const Move& move) { return ToTuple(move.direction); })
      .def_readonly("sprint", &Move::sprint)
      .def_readonly("stance", &Move::stance);

  py::class_<MoveTo>(module, "MoveTo")
      .def(py::init([](const Vec3Tuple& point, bool sprint, Stance stance) {
             return MoveTo{.point = ToVec3(point), .sprint = sprint, .stance = stance};
           }),
           py::arg("point"), py::kw_only(), py::arg("sprint") = false, py::arg("stance") = Stance::kStanding)
      .def_property_readonly("point", [](const MoveTo& move_to) { return ToTuple(move_to.point); })
      .def_readonly("sprint", &MoveTo::sprint)
      .def_readonly("stance", &MoveTo::stance);

  py::class_<Look>(module, "Look")
      .def(py::init([](float yaw, float pitch) { return Look{.yaw = yaw, .pitch = pitch}; }), py::arg("yaw"),
           py::arg("pitch"))
      .def_readonly("yaw", &Look::yaw)
      .def_readonly("pitch", &Look::pitch);

  py::class_<AimAt>(module, "AimAt")
      .def(py::init([](std::uint32_t target) { return AimAt{.target = static_cast<EntityId>(target)}; }),
           py::arg("target"))
      .def_property_readonly("target", [](const AimAt& aim_at) { return Number(aim_at.target); });

  py::class_<HoldFire>(module, "HoldFire").def(py::init<>());
  py::class_<Reload>(module, "Reload").def(py::init<>());
  py::class_<FireBursts>(module, "FireBursts").def(py::init<>());
}

// What an Agent's prediction, fire and link have come to.
void BindStats(py::module_& module) {
  py::class_<NetcodeStats>(module, "NetcodeStats")
      .def_readonly("match_ticks", &NetcodeStats::match_ticks)
      .def_readonly("corrections", &NetcodeStats::corrections)
      .def_readonly("largest_correction_m", &NetcodeStats::largest_correction_m)
      .def_readonly("rounds_fired", &NetcodeStats::rounds_fired)
      .def_readonly("hit_confirmations", &NetcodeStats::hit_confirmations)
      .def_property_readonly("correction_rate", &NetcodeStats::CorrectionRate)
      .def_property_readonly("hit_rate", &NetcodeStats::HitRate);

  py::class_<ConnectionStats>(module, "ConnectionStats")
      .def_readonly("ping_ms", &ConnectionStats::ping_ms)
      .def_readonly("quality_local", &ConnectionStats::quality_local)
      .def_readonly("quality_remote", &ConnectionStats::quality_remote)
      .def_readonly("in_bytes_per_sec", &ConnectionStats::in_bytes_per_sec)
      .def_readonly("out_bytes_per_sec", &ConnectionStats::out_bytes_per_sec)
      .def_readonly("max_jitter_us", &ConnectionStats::max_jitter_us)
      .def_readonly("pending_bytes", &ConnectionStats::pending_bytes);

  py::class_<AgentStats>(module, "Stats")
      .def_readonly("netcode", &AgentStats::netcode)
      .def_readonly("connection", &AgentStats::connection);
}

// The faults every link of the process is given: the transport's simulated
// conditions (link.h), but for its timeout, which stays the transport's.
void BindLinkFaults(py::module_& module) {
  py::class_<SimulatedConditions>(module, "LinkFaults")
      .def(py::init([](int latency_ms, int jitter_mean_ms, int jitter_max_ms, float loss_percent, float reorder_percent,
                       int reorder_delay_ms) {
             return SimulatedConditions{.latency_ms = latency_ms,
                                        .jitter_mean_ms = jitter_mean_ms,
                                        .jitter_max_ms = jitter_max_ms,
                                        .loss_percent = loss_percent,
                                        .reorder_percent = reorder_percent,
                                        .reorder_delay_ms = reorder_delay_ms,
                                        .timeout_ms = 0};
           }),
           py::kw_only(), py::arg("latency_ms") = 0, py::arg("jitter_mean_ms") = 0, py::arg("jitter_max_ms") = 0,
           py::arg("loss_percent") = 0.0F, py::arg("reorder_percent") = 0.0F, py::arg("reorder_delay_ms") = 0)
      .def_readonly("latency_ms", &SimulatedConditions::latency_ms)
      .def_readonly("jitter_mean_ms", &SimulatedConditions::jitter_mean_ms)
      .def_readonly("jitter_max_ms", &SimulatedConditions::jitter_max_ms)
      .def_readonly("loss_percent", &SimulatedConditions::loss_percent)
      .def_readonly("reorder_percent", &SimulatedConditions::reorder_percent)
      .def_readonly("reorder_delay_ms", &SimulatedConditions::reorder_delay_ms);

  module.def(
      "impair_links",
      [](const std::optional<SimulatedConditions>& faults) {
        augusta::link::SimulateNetworkConditions(faults.value_or(SimulatedConditions{}));
      },
      py::arg("faults"),
      "Gives every packet the process sends from now on faults, or none if None: every Agent's link, the "
      "handshake of one still connecting among them, and an in-process server's.");
}

void BindAgent(py::module_& module) {
  py::register_exception<PackError>(module, "PackError");

  py::class_<LoadedPack>(module, "Pack");

  module.def(
      "load_pack",
      [](const std::filesystem::path& path, const std::filesystem::path& public_key) {
        auto pack = augusta::agent::LoadPack(path, public_key);
        if (!pack) {
          throw PackError(augusta::agent::DescribeLoadPackError(pack.error(), path, public_key));
        }
        return *std::move(pack);
      },
      py::arg("path"), py::arg("public_key"));

  py::class_<AgentHandle>(module, "Agent")
      .def(py::init([](const std::string& server, const LoadedPack& pack, const std::string& character, bool auto_ready,
                       py::object receiver) {
             return std::make_unique<AgentHandle>(server, pack, character,
                                                  auto_ready ? Readiness::kForEveryRoster : Readiness::kWhenTold,
                                                  std::move(receiver));
           }),
           py::arg("server"), py::arg("pack"), py::arg("character"), py::arg("auto_ready"), py::arg("receiver"))
      .def(
          "set_raw", [](AgentHandle& self, const Command& command) { return self.Get().SetRaw(command); },
          py::arg("command"))
      .def(
          "set_movement",
          [](AgentHandle& self, const MovementIntent& intent) { return self.Get().SetMovement(intent); },
          py::arg("intent"))
      .def(
          "set_aim", [](AgentHandle& self, const AimIntent& intent) { return self.Get().SetAim(intent); },
          py::arg("intent"))
      .def(
          "set_trigger", [](AgentHandle& self, const TriggerIntent& intent) { return self.Get().SetTrigger(intent); },
          py::arg("intent"))
      .def("clear_movement", [](AgentHandle& self) { self.Get().ClearMovement(); })
      .def("clear_aim", [](AgentHandle& self) { self.Get().ClearAim(); })
      .def("clear_trigger", [](AgentHandle& self) { self.Get().ClearTrigger(); })
      .def("ready", [](AgentHandle& self) { self.Get().Ready(); })
      .def("view", [](AgentHandle& self) { return self.Get().View(); })
      .def("stats", [](AgentHandle& self) { return self.Get().Stats(); })
      .def("close", &AgentHandle::Close);
}

void BindTesting(py::module_& module) {
  py::module_ testing = module.def_submodule("testing", "The in-process server the package's tests play against.");
  using Server = augusta::agent::testing::Server;
  using ServerHandle = Closable<Server>;
  py::class_<ServerHandle> server(testing, "Server");
  server
      .def(py::init([](const std::filesystem::path& pack, const std::filesystem::path& public_key,
                       std::uint8_t player_count) {
             return ServerHandle(std::make_unique<Server>(pack, public_key, player_count));
           }),
           py::arg("pack"), py::arg("public_key"), py::arg("player_count"))
      .def_property_readonly("address", [](ServerHandle& self) { return self.Get().Address(); });
  DefContextManager<Server>(server);
}

}  // namespace

PYBIND11_MODULE(_native, module) {
  augusta::logging::Init();
  // Once, process-wide, before any Session is constructed - see link.h;
  // never shut down, as an Agent may outlive any point the module could.
  if (const auto initialized = augusta::link::Init(); !initialized) {
    throw std::runtime_error(augusta::failure::DescribeFailure(initialized.error()));
  }
  // Stopped before the interpreter finalizes, while the GIL it takes is
  // still there to take.
  Dispatch::Get();
  py::module_::import("atexit").attr("register")(py::cpp_function([] { Dispatch::Get().Stop(); }));
  BindCommand(module);
  BindIntents(module);
  BindStats(module);
  BindLinkFaults(module);
  BindView(module);
  BindReports(module);
  BindAgent(module);
  BindTesting(module);
}
