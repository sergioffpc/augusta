#include <cstdint>
#include <filesystem>
#include <memory>
#include <stdexcept>
#include <string>
#include <utility>

#include <pybind11/pybind11.h>
#include <pybind11/stl.h>
#include <pybind11/stl/filesystem.h>

#include "agent.h"
#include "augusta/command.h"
#include "augusta/intent.h"
#include "bindings.h"
#include "dispatch.h"

namespace augusta::agent::native {

namespace py = pybind11;

using augusta::agent::Agent;
using augusta::agent::LoadedPack;
using augusta::agent::Readiness;
using augusta::command::Command;
using augusta::harness::AimIntent;
using augusta::harness::MovementIntent;
using augusta::harness::TriggerIntent;

namespace {

// What load_pack raises: the pack or its key did not verify, or its Map did
// not load.
class PackError : public std::runtime_error {
 public:
  using std::runtime_error::runtime_error;
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

}  // namespace

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

}  // namespace augusta::agent::native
