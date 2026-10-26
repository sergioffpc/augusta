#include <optional>

#include <pybind11/pybind11.h>
#include <pybind11/stl.h>

#include "augusta/link.h"
#include "bindings.h"

namespace augusta::agent::native {

namespace py = pybind11;

using augusta::link::SimulatedConditions;

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

}  // namespace augusta::agent::native
