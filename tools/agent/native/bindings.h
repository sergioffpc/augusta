#ifndef AUGUSTA_AGENT_BINDINGS_H_
#define AUGUSTA_AGENT_BINDINGS_H_

#include <pybind11/pybind11.h>

/// \file
/// The bindings of the Agents' Python module, augusta_agent._native, one per
/// part of what a script sees, which PYBIND11_MODULE (module.cpp) adds to the
/// module, each once: BindCommand before BindIntents, whose defaults name the
/// Stance it binds. What they hand a script is a copy (conversions.h).
namespace augusta::agent::native {

void BindCommand(pybind11::module_& module);

/// The Intents a script sets on an Agent's channels (augusta/intent.h).
void BindIntents(pybind11::module_& module);

/// What an Agent's prediction, fire and link have come to.
void BindStats(pybind11::module_& module);

/// The faults every link of the process is given: the transport's simulated
/// conditions (link.h), but for its timeout, which stays the transport's.
void BindLinkFaults(pybind11::module_& module);

void BindView(pybind11::module_& module);

/// What an Agent's script awaits (Agent.next_event), and what else it is told.
void BindReports(pybind11::module_& module);

void BindAgent(pybind11::module_& module);

void BindTesting(pybind11::module_& module);

}  // namespace augusta::agent::native

#endif  // AUGUSTA_AGENT_BINDINGS_H_
