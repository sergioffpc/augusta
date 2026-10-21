#ifndef AUGUSTA_HARDWARE_PROC_FILES_H_
#define AUGUSTA_HARDWARE_PROC_FILES_H_

#include <cstdint>
#include <optional>
#include <string>
#include <string_view>

#include "augusta/hardware.h"

/// \file
/// What hardware.cpp reads from Linux's text files, in /proc, /etc and its
/// cgroup, private to the module and kept apart from reading them so it is
/// tested on every platform.
namespace augusta::hardware {

/// The first "model name" in /proc/cpuinfo's text, trimmed; empty if none.
std::string CpuModelFromCpuinfo(std::string_view cpuinfo);

/// MemTotal and MemAvailable from /proc/meminfo's text, which gives them in
/// KiB; 0 for either it lacks.
Memory MemoryFromMeminfo(std::string_view meminfo);

/// The CPUs a cgroup's cpu.max text ("<quota> <period>", in microseconds)
/// allows; nullopt for "max", which caps nothing, or text that is not one.
std::optional<double> CpusFromCpuMax(std::string_view cpu_max);

/// The bytes a cgroup's memory.max text allows; nullopt for "max", which caps
/// nothing, or text that is not a number.
std::optional<std::uint64_t> BytesFromMemoryMax(std::string_view memory_max);

/// PRETTY_NAME from /etc/os-release's text, unquoted; empty if none.
std::string PrettyNameFromOsRelease(std::string_view os_release);

}  // namespace augusta::hardware

#endif  // AUGUSTA_HARDWARE_PROC_FILES_H_
