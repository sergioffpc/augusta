// augusta_agent._native: the pybind11 bindings of an Agent (agent.h)
// and of the test-only in-process server (testing_server.h), wrapped by the
// augusta_agent package, and the dispatch thread that hands Agents' Reports
// (agent_report.h) to it. Every value a script reads is a copy:
// nothing it holds changes under it.

#include <stdexcept>

#include <pybind11/pybind11.h>
#include <pybind11/stl.h>

#include "augusta/failure.h"
#include "augusta/link.h"
#include "augusta/logging.h"
#include "bindings.h"
#include "dispatch.h"

namespace py = pybind11;

using augusta::agent::native::Dispatch;

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
  augusta::agent::native::BindCommand(module);
  augusta::agent::native::BindIntents(module);
  augusta::agent::native::BindStats(module);
  augusta::agent::native::BindLinkFaults(module);
  augusta::agent::native::BindView(module);
  augusta::agent::native::BindReports(module);
  augusta::agent::native::BindAgent(module);
  augusta::agent::native::BindTesting(module);
}
