#include <cstdint>
#include <optional>
#include <string>
#include <vector>

#include <pybind11/pybind11.h>
#include <pybind11/stl.h>

#include "agent.h"
#include "augusta/harness.h"
#include "augusta/physics.h"
#include "bindings.h"
#include "conversions.h"

namespace augusta::agent::native {

namespace py = pybind11;

using augusta::agent::AgentView;
using augusta::harness::AuthoritativeState;
using augusta::harness::EntityBody;
using augusta::harness::EntityId;
using augusta::harness::Lobby;
using augusta::harness::MatchPlayer;
using augusta::harness::Phase;
using augusta::harness::RosterEntry;
using augusta::physics::BodyState;

namespace {

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

}  // namespace

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

}  // namespace augusta::agent::native
