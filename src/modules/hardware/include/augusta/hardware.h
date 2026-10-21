#ifndef AUGUSTA_HARDWARE_H_
#define AUGUSTA_HARDWARE_H_

#include <cstdint>
#include <optional>
#include <string>

/// \file
/// The machine a process runs on, as the OS reports it at startup: its
/// operating system, CPU and memory. Each Runtime logs it once as it starts, so
/// a log sent with a bug report says what the bug ran on. The GPU, display and
/// audio device are each logged by the module that opens them (renderer, window,
/// audio), which alone knows which one it chose. The line's format is
/// ADR-0029's.
namespace augusta::hardware {

struct Cpu {
  /// As the OS names it, e.g. "AMD Ryzen 9 7950X 16-Core Processor"; empty if
  /// it names none.
  std::string model;
  /// Hardware threads; 0 if unknown.
  std::uint32_t logical_cores = 0;
};

/// Physical memory, in bytes; 0 where unknown.
struct Memory {
  std::uint64_t total_bytes = 0;
  /// What the OS could hand out now without swapping.
  std::uint64_t available_bytes = 0;
};

/// What the process may use of the machine, where its cgroup (v2) caps it, as
/// a Kubernetes pod's limits do; nullopt where nothing caps it, which is always
/// so on Windows.
struct Limits {
  /// CPUs' worth of time per period, e.g. 2.5.
  std::optional<double> cpus;
  std::optional<std::uint64_t> memory_bytes;
};

struct Machine {
  /// The OS's name and version, e.g. "Windows 10.0.26100 (24H2)" or "Ubuntu
  /// 24.04.1 LTS (Linux 6.8.0-45-generic)"; empty if unknown.
  std::string os;
  Cpu cpu;
  Memory memory;
  Limits limits;
};

/// Asks the OS. Never fails: what it can't find out is left empty or 0.
Machine QueryMachine();

/// machine as logfmt fields (ADR-0029), for a line's tail: os, cpu,
/// logical_cores, memory_mib and memory_available_mib, "unknown" for what is
/// empty or 0, then cpu_limit and memory_limit_mib, "none" for what is not
/// capped.
std::string DescribeMachine(const Machine& machine);

}  // namespace augusta::hardware

#endif  // AUGUSTA_HARDWARE_H_
