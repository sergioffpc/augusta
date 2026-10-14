#ifndef AUGUSTA_RENDERER_GPU_CRASH_DUMPS_H_
#define AUGUSTA_RENDERER_GPU_CRASH_DUMPS_H_

/// \file
/// GPU crash dumps for the Renderer's Vulkan device (ADR-0059), with Nsight
/// Aftermath when the build found its SDK (cmake/Aftermath.cmake); without it,
/// each of these does nothing. vulkan_device.cpp enables them before it makes
/// the device, and then turns on the device extensions Aftermath reads
/// (device_choice.h); swapchain.cpp waits for a dump once the device is lost. A
/// dump is a .nv-gpudmp, which Nsight Graphics opens, written beside the
/// executable. Aftermath that fails to start (no NVIDIA driver) is logged and
/// left off: rendering never depends on it.
namespace augusta::renderer {

/// Starts watching the process's Vulkan devices for GPU faults, which only
/// devices made afterwards are. Only the first call does anything; each
/// returns whether Aftermath is watching.
bool EnableGpuCrashDumps();

/// Once the device is lost: waits a few seconds at most for its dump, which
/// Aftermath writes on its own thread, so the process does not end first.
void AwaitGpuCrashDump();

}  // namespace augusta::renderer

#endif  // AUGUSTA_RENDERER_GPU_CRASH_DUMPS_H_
