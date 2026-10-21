#ifndef AUGUSTA_HARDWARE_PROC_FILES_H_
#define AUGUSTA_HARDWARE_PROC_FILES_H_

#include <string>
#include <string_view>

#include "augusta/hardware.h"

/// \file
/// What hardware.cpp reads from Linux's text files, private to the module and
/// kept apart from reading them so it is tested on every platform.
namespace augusta::hardware {

/// The first "model name" in /proc/cpuinfo's text, trimmed; empty if none.
std::string CpuModelFromCpuinfo(std::string_view cpuinfo);

/// MemTotal and MemAvailable from /proc/meminfo's text, which gives them in
/// KiB; 0 for either it lacks.
Memory MemoryFromMeminfo(std::string_view meminfo);

/// PRETTY_NAME from /etc/os-release's text, unquoted; empty if none.
std::string PrettyNameFromOsRelease(std::string_view os_release);

}  // namespace augusta::hardware

#endif  // AUGUSTA_HARDWARE_PROC_FILES_H_
