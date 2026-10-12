# Windows debugging tools

Windows-only tools for profiling, GPU debugging and content authoring, reached
from `CLAUDE.md`.

## Profiling

To use nsys refer to 'C:\Program Files\NVIDIA Corporation\Nsight Systems
2026.5.1\skills\nsight-systems\SKILL.md'

## Graphics/GPU debugging

Nsight Graphics is the replacement for augusta_renderer's former full in-app
debug HUD (only a small FPS/RTT readout, `DebugHud`, remains): frame capture,
draw-call/pixel inspection, and shader debugging for the D3D12 backend. Launch
'C:\Program Files\NVIDIA Corporation\Nsight Graphics
2026.3.1\host\windows-desktop-nomad-x64\ngfx-ui.exe' and attach to (or launch)
`augustac.exe`.

renderer.cpp also requests `enableAftermath` unconditionally on the Falcor
`Device::Desc` - on a GPU crash/TDR this writes a `.nv-gpudmp` crash dump next
to `augustac.exe`, decodable with 'C:\Program Files\NVIDIA Corporation\Nsight
Graphics 2026.3.1\host\windows-desktop-nomad-x64\nv-aftermath-format.exe'.

## Content authoring/physics debugging

NVIDIA Omniverse USD Composer is where the ADR-0015 map-authoring toolchain's
scene assembly and PhysX authoring/debugging happens (colliders, joints, live
simulation) - see ADR-0015 for the full pipeline. OmniPVD (the `omni.physx.pvd`
extension, inside Composer) records/replays a PhysX simulation as USD for
offline inspection - a debugging aid only, produces no artifact that reaches the
runtime pack.
