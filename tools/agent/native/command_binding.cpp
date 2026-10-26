#include <cstdint>

#include <pybind11/pybind11.h>
#include <pybind11/stl.h>

#include "augusta/command.h"
#include "augusta/harness.h"
#include "augusta/intent.h"
#include "augusta/physics.h"
#include "bindings.h"
#include "conversions.h"

namespace augusta::agent::native {

namespace py = pybind11;

using augusta::command::Command;
using augusta::harness::AimAt;
using augusta::harness::EntityId;
using augusta::harness::FireBursts;
using augusta::harness::HoldFire;
using augusta::harness::Look;
using augusta::harness::Move;
using augusta::harness::MoveTo;
using augusta::harness::Reload;
using augusta::physics::Stance;

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

}  // namespace augusta::agent::native
