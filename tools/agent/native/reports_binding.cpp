#include <optional>
#include <string>

#include <pybind11/pybind11.h>
#include <pybind11/stl.h>

#include "agent_report.h"
#include "augusta/body_part.h"
#include "augusta/harness.h"
#include "augusta/intent.h"
#include "bindings.h"
#include "conversions.h"

namespace augusta::agent::native {

namespace py = pybind11;

using augusta::agent::AgentFailure;
using augusta::agent::IntentEnded;
using augusta::body_part::BodyPart;
using augusta::harness::Death;
using augusta::harness::HitConfirmation;
using augusta::harness::IntentEnding;
using augusta::harness::MatchEnd;
using augusta::harness::MatchStart;
using augusta::harness::Shot;

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

}  // namespace augusta::agent::native
