// augusta_agent._native: the pybind11 bindings of an Agent (agent.h) and of
// the test-only in-process server (testing_server.h), wrapped by the
// augusta_agent package. Every value a script reads is a copy: nothing it
// holds changes under it.

#include <cstdint>
#include <filesystem>
#include <memory>
#include <optional>
#include <stdexcept>
#include <string>
#include <tuple>
#include <utility>
#include <vector>

#include <pybind11/pybind11.h>
#include <pybind11/stl.h>
#include <pybind11/stl/filesystem.h>

#include "agent.h"
#include "augusta/command.h"
#include "augusta/failure.h"
#include "augusta/harness.h"
#include "augusta/logging.h"
#include "augusta/math.h"
#include "augusta/networking.h"
#include "augusta/physics.h"
#include "testing_server.h"

namespace py = pybind11;

namespace {

using augusta::agent::Agent;
using augusta::agent::AgentView;
using augusta::agent::LoadedPack;
using augusta::command::Command;
using augusta::harness::AuthoritativeState;
using augusta::harness::EntityBody;
using augusta::harness::Lobby;
using augusta::harness::MatchPlayer;
using augusta::harness::Phase;
using augusta::harness::RosterEntry;
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

// Who is in the Lobby.
void BindLobby(py::module_& module) {
  py::class_<RosterEntry>(module, "RosterEntry")
      .def_property_readonly("session",
                             [](const RosterEntry& entry) { return static_cast<std::uint32_t>(entry.session); })
      .def_readonly("character", &RosterEntry::character);

  py::class_<Lobby>(module, "Lobby")
      .def_readonly("version", &Lobby::version)
      .def_property_readonly("roster", [](const Lobby& lobby) { return ToPyTuple(lobby.roster); });
}

// A Match's players and bodies, and an Agent's own.
void BindMatch(py::module_& module) {
  py::class_<MatchPlayer>(module, "MatchPlayer")
      .def_property_readonly("session",
                             [](const MatchPlayer& player) { return static_cast<std::uint32_t>(player.session); })
      .def_property_readonly("entity",
                             [](const MatchPlayer& player) { return static_cast<std::uint32_t>(player.entity); })
      .def_readonly("character", &MatchPlayer::character)
      .def_property_readonly("spawn", [](const MatchPlayer& player) { return ToTuple(player.spawn); });

  py::class_<BodyState>(module, "Body")
      .def_property_readonly("position", [](const BodyState& body) { return ToTuple(body.position); })
      .def_property_readonly("velocity", [](const BodyState& body) { return ToTuple(body.velocity); })
      .def_readonly("stance", &BodyState::stance);

  py::class_<EntityBody>(module, "EntityBody")
      .def_property_readonly("entity", [](const EntityBody& body) { return static_cast<std::uint32_t>(body.entity); })
      .def_readonly("body", &EntityBody::body)
      .def_readonly("yaw", &EntityBody::yaw);

  py::class_<AuthoritativeState>(module, "AuthoritativeState")
      .def_readonly("tick", &AuthoritativeState::tick)
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
                             [](const AgentView& view) -> std::optional<std::uint32_t> {
                               if (!view.server->accepted.has_value()) {
                                 return std::nullopt;
                               }
                               return static_cast<std::uint32_t>(view.server->accepted->session);
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
                               return static_cast<std::uint32_t>(*entity);
                             })
      .def_property_readonly("authoritative", [](const AgentView& view) { return view.server->authoritative; })
      .def_readonly("own", &AgentView::own);
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

void BindAgent(py::module_& module) {
  py::register_exception<PackError>(module, "PackError");

  py::class_<LoadedPack>(module, "Pack");

  module.def(
      "load_pack",
      [](const std::filesystem::path& path, const std::filesystem::path& public_key) {
        auto pack = augusta::agent::LoadPack(path, public_key);
        if (!pack) {
          throw PackError(pack.error());
        }
        return *std::move(pack);
      },
      py::arg("path"), py::arg("public_key"));

  using AgentHandle = Closable<Agent>;
  py::class_<AgentHandle> agent(module, "Agent");
  agent
      .def(py::init([](const std::string& server, const LoadedPack& pack, const std::string& character) {
             return AgentHandle(std::make_unique<Agent>(server, pack, character));
           }),
           py::arg("server"), py::arg("pack"), py::arg("character"))
      .def(
          "set_raw", [](AgentHandle& self, const Command& command) { self.Get().SetRaw(command); }, py::arg("command"))
      .def("view", [](AgentHandle& self) { return self.Get().View(); })
      .def_property_readonly("failure", [](AgentHandle& self) { return self.Get().Failure(); });
  DefContextManager<Agent>(agent);
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
  // Once, process-wide, before any Session is constructed - see networking.h;
  // never shut down, as an Agent may outlive any point the module could.
  if (const auto initialized = augusta::networking::Init(); !initialized) {
    throw std::runtime_error(augusta::failure::DescribeFailure(initialized.error()));
  }
  BindCommand(module);
  BindView(module);
  BindAgent(module);
  BindTesting(module);
}
