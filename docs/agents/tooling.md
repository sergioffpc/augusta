# Windows debugging tools

Windows-only tools for profiling, GPU debugging and content authoring, reached
from `CLAUDE.md`.

## Build kinds

Every preset (`windows`, `windows-debug`, ...) makes a Development build, with
the debug tools compiled in, whatever its optimisation; `windows-shipping` and
`linux-shipping` make a Shipping build, what players get, with none of them
(ADR-0074). The tools below work on either, but a Shipping client carries no
Debug HUD, and a Shipping build joins only a Shipping server.

## Profiling

To use nsys refer to 'C:\Program Files\NVIDIA Corporation\Nsight Systems
2026.5.1\skills\nsight-systems\SKILL.md'

A whole session is too long to read, so a Development client's Profiling Debug
panel marks a window of frames for nsys to record alone. A client the panel runs
in alone records nothing: start it under a windowed capture, which
`make profile` (`PRESET=...`, a Development one) does after building it:

```sh
nsys profile --capture-range=nvtx --nvtx-capture=augusta.capture \
  --env-var=NSYS_NVTX_PROFILER_REGISTER_ONLY=0 augustac.exe
```

nsys stops the client once the window closes and writes the report,
`augustac-<pid>.nsys-rep` in `build/<preset dir>/profiles` (`PROFILE_DIR=...`).

`NSYS_NVTX_PROFILER_REGISTER_ONLY=0` lets the capture start on a range whose
message is a plain string, as `augusta.capture` is, rather than a registered
one. Then turn the panel on (`debug.profiling.panel: true`, or from the Debug
index F1 opens), set N on its slider (1 to 3600 frames, 300 by default) and
press "Capture N frames": the client opens the NVTX range `augusta.capture`, in
the default domain, over the next N frames, and nsys records only those. The
slider and the button are greyed out until the window closes, and the panel
counts the frames through it. The same panel says whether Aftermath is enabled,
whether it is attached to the device, and the directory a GPU crash dump would
land in (below).

## Network impairment

To play on a bad link by hand against a nearby server, turn on a Development
client's Network Debug panel (`debug.network.panel: true`, or from the Debug
index F1 opens) and use its Impairment group: sliders for latency (each way),
jitter, loss and reorder, or one click on a named profile (Broadband, NFR-02,
Worst case, the impaired-link tests' values), and Off. It applies to the
client's one connection both ways, through `link::SimulateNetworkConditions`,
and lasts until Off or the client exits, across a reconnect; nothing about it is
written to a config. The server is never told and measures the link as a real
one, so the other players are untouched. The panel shows the measured RTT beside
the controls, and the Debug HUD's network line ends in `IMPAIRED` while any of
it is on. For a repeatable session, script it instead with the Agents'
`impair_links` (ADR-0052).

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

The game's own PhysX scenes go to OmniPVD through `debug.physics_recording`, in
`augustad.yaml` or `augustac.yaml` (a Development build's key, read at startup):

1. Build Debug (`make build PRESET=windows-debug`, or `linux-debug`): only a
   Debug PhysX has OmniPVD, and only a Debug build stages OmniPVD's runtime,
   `PVDRuntime_64`, beside `augustad` and `augustac` (`cmake/PvdRuntime.cmake`,
   from the PhysX overlay port in `ports/physx`).
2. Set `debug.physics_recording: true` and start the executable. It records
   every PhysX scene in the process, from startup to exit, into one
   `<executable>-<UTC time>.ovd` in the system's temporary directory (`%TEMP%`,
   or `$TMPDIR`/`/tmp`), and logs
   `subsystem=physics event=recording_started path="..."` at `INFO`. Where it
   cannot record (a non-Debug build, a runtime that does not load), it logs one
   `event=recording_unavailable` `WARN` naming the key and why, and runs on.
3. Stop the executable, so the file is complete, and open it in OmniPVD (the
   `omni.physx.pvd` extension in USD Composer).
