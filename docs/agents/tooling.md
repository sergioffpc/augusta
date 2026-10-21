# Windows debugging tools

Windows-only tools for profiling, GPU debugging and content authoring, reached
from `CLAUDE.md`.

## Profiling

To use nsys refer to 'C:\Program Files\NVIDIA Corporation\Nsight Systems
2026.5.1\skills\nsight-systems\SKILL.md'

## Graphics/GPU debugging

Nsight Graphics is the replacement for augusta_renderer's former full in-app
debug HUD (only a small FPS/RTT readout, `DebugHud`, remains): frame capture,
draw-call/pixel inspection, and shader debugging for the Vulkan backend. Launch
'C:\Program Files\NVIDIA Corporation\Nsight Graphics
2026.3.1\host\windows-desktop-nomad-x64\ngfx-ui.exe' and attach to (or launch)
`augustac.exe`.

A Debug `augustac.exe` runs under Vulkan's validation layer (ADR-0059) when the
loader finds it through `VK_ADD_LAYER_PATH`, and without it, after a warning,
when not: the "Debug Client - Debug (MSVC)" launch configuration points it at
`build\x64-windows-debug\vcpkg_installed\x64-windows\debug\bin`. An error the
layer reports is logged as `event=vulkan_validation_error` and stops the client.

GPU crash dumps are Nsight Aftermath's on the Vulkan device (ADR-0059), from the
Aftermath SDK inside Nsight Graphics, which `cmake/Aftermath.cmake` finds on
Windows and Linux; a build with it stages the SDK's shared libraries beside
`augustac`. The renderer (`vulkan_gpu_crash_dumps.h`) logs
`event=gpu_crash_dumps_enabled` once Aftermath watches, and
`event=gpu_crash_dumps_attached` once its device has
`VK_NV_device_diagnostics_config` (an NVIDIA GPU, not lavapipe). On a GPU
crash/TDR, seen as `VK_ERROR_DEVICE_LOST`, it writes the Run's
`dumps/augustac-<run-id>-<n>.nv-gpudmp` beside the executable, logged as
`event=gpu_crash_dump`, which Nsight Graphics opens and 'C:\Program Files\NVIDIA
Corporation\Nsight Graphics
2026.3.1\host\windows-desktop-nomad-x64\nv-aftermath-format.exe' decodes.

## Content authoring/physics debugging

NVIDIA Omniverse USD Composer is where the ADR-0015 map-authoring toolchain's
scene assembly and PhysX authoring/debugging happens (colliders, joints, live
simulation) - see ADR-0015 for the full pipeline. OmniPVD (the `omni.physx.pvd`
extension, inside Composer) records/replays a PhysX simulation as USD for
offline inspection - a debugging aid only, produces no artifact that reaches the
runtime pack.
