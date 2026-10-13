#ifndef AUGUSTA_RENDERER_GPU_CRASH_DUMPS_H_
#define AUGUSTA_RENDERER_GPU_CRASH_DUMPS_H_

#include <d3d12.h>

/// \file
/// GPU crash dumps for the NVRHI Renderer's D3D12 device, with Nsight
/// Aftermath when the build found its SDK (cmake/Aftermath.cmake); without it,
/// each of these does nothing. A dump is a .nv-gpudmp, which Nsight Graphics
/// opens, written beside the executable. Aftermath that fails to start (no
/// NVIDIA driver, WARP) is logged and left off: rendering never depends on it.
namespace augusta::renderer {

/// Starts watching the process's D3D12 devices for GPU faults. Called before
/// the device is created, which Aftermath must see; only the first call does
/// anything.
void EnableGpuCrashDumps();

/// Adds device's resource tracking and call stacks to its dumps.
void AttachGpuCrashDumps(ID3D12Device& device);

/// Once a device is lost: waits a few seconds at most for its dump, which
/// Aftermath writes on its own thread, so the process does not end first.
void AwaitGpuCrashDump();

}  // namespace augusta::renderer

#endif  // AUGUSTA_RENDERER_GPU_CRASH_DUMPS_H_
