#ifndef AUGUSTA_HARDWARE_H_
#define AUGUSTA_HARDWARE_H_

#include <cstdint>
#include <optional>
#include <string>
#include <vector>

/// \file
/// The machine a process runs on, as the OS reports it at startup: its
/// operating system, CPU and memory, and its GPUs with their drivers. Each
/// Runtime logs it once as it starts, so a log sent with a bug report says what
/// the bug ran on, even a Run that ends before its Renderer starts. Which GPU
/// the Renderer draws with, the display and the audio device are each logged
/// by the module that opens them (renderer, window, audio), which alone knows
/// which one it chose. The line's format is ADR-0029's.
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

/// What serves malloc, as a logfmt field (ADR-0029) for the same line:
/// `allocator=mimalloc/3.5.3` from mimalloc_version as mi_version() gives it
/// (major * 10000 + minor * 100 + patch, 30503 for 3.5.3), which the
/// executable read of mimalloc (ADR-0078); `allocator=platform` without one, in
/// a sanitizer build, whose sanitizer's allocator serves.
std::string DescribeAllocator(std::optional<int> mimalloc_version);

/// One GPU, as the OS lists it.
struct Gpu {
  /// As the OS names it, e.g. "NVIDIA GeForce RTX 4090"; empty if it names
  /// none, as Linux does.
  std::string name;
  /// PCI IDs.
  std::uint32_t vendor_id = 0;
  std::uint32_t device_id = 0;
  /// The kernel driver, e.g. "nvidia"; empty where the OS does not say, as on
  /// Windows.
  std::string driver;
  /// The driver's version, e.g. "32.0.15.6094" on Windows (its user-mode
  /// driver's) or "560.35.03" on Linux (its kernel module's); empty if
  /// unknown, as for a Linux driver built into the kernel.
  std::string driver_version;
};

/// Asks the OS for each GPU it lists: DXGI's hardware adapters on Windows, the
/// DRM cards on Linux; none on macOS, whose GPUs are in IOKit's registry. Never
/// fails: empty if it finds none.
std::vector<Gpu> QueryGpus();

/// gpu as logfmt fields (ADR-0029): name, vendor (its name for NVIDIA, AMD and
/// Intel, its ID otherwise), device_id, driver and driver_version, "unknown"
/// for what is empty.
std::string DescribeGpu(const Gpu& gpu);

}  // namespace augusta::hardware

#endif  // AUGUSTA_HARDWARE_H_
