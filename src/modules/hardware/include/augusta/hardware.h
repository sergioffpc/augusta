#ifndef AUGUSTA_HARDWARE_H_
#define AUGUSTA_HARDWARE_H_

#include <cstdint>
#include <string>

/// \file
/// The machine a process runs on, as the OS reports it at startup: its
/// operating system, CPU and memory. Each Runtime logs it once as it starts, so
/// a log sent with a bug report says what the bug ran on. The GPU, display and
/// audio device are each logged by the module that opens them (renderer, window,
/// audio), which alone knows which one it chose.
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

struct Machine {
  /// The OS's name and version, e.g. "Windows 10.0.26100 (24H2)" or "Ubuntu
  /// 24.04.1 LTS (Linux 6.8.0-45-generic)"; empty if unknown.
  std::string os;
  Cpu cpu;
  Memory memory;
};

/// Asks the OS. Never fails: what it can't find out is left empty or 0.
Machine QueryMachine();

/// machine as logfmt fields (ADR-0029), for a line's tail: os, cpu,
/// logical_cores, memory_mib and memory_available_mib, "unknown" for what is
/// empty or 0.
std::string DescribeMachine(const Machine& machine);

}  // namespace augusta::hardware

#endif  // AUGUSTA_HARDWARE_H_
