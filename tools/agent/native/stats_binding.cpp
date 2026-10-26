#include <pybind11/pybind11.h>
#include <pybind11/stl.h>

#include "agent.h"
#include "augusta/link.h"
#include "augusta/netcode_stats.h"
#include "bindings.h"

namespace augusta::agent::native {

namespace py = pybind11;

using augusta::agent::AgentStats;
using augusta::harness::NetcodeStats;
using augusta::link::ConnectionStats;

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

}  // namespace augusta::agent::native
