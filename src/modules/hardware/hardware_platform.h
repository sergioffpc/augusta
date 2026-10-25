#ifndef AUGUSTA_HARDWARE_HARDWARE_PLATFORM_H_
#define AUGUSTA_HARDWARE_HARDWARE_PLATFORM_H_

#include <string>
#include <string_view>

#include "augusta/hardware.h"

/// ile
/// What QueryMachine asks the OS, private to the module: hardware_windows.cpp
/// or hardware_linux.cpp, the one the platform builds, asks it; hardware.cpp
/// puts the answers together and describes them on every platform. Each never
/// fails: what it can't find out is left empty or 0. QueryGpus, the platform's
/// too, is hardware.h's.
namespace augusta::hardware {

/// The OS's name and version: Windows' from its registry, Linux's
/// distribution and kernel.
std::string QueryOs();

/// The CPU's model name.
std::string QueryCpuModel();

/// The machine's physical memory.
Memory QueryMemory();

/// What the process's cgroup caps it at; none on Windows, which has no cgroup.
Limits QueryLimits();

/// text without its leading and trailing blanks and line ends.
std::string_view Trim(std::string_view text);

}  // namespace augusta::hardware

#endif  // AUGUSTA_HARDWARE_HARDWARE_PLATFORM_H_
