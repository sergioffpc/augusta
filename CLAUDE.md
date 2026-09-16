## Agent skills

### Issue tracker

Issues are tracked in this repo's GitHub Issues, using the `gh` CLI. See `docs/agents/issue-tracker.md`.

### Domain docs

Single-context layout: `CONTEXT.md` + `docs/adr/` at the repo root (neither exists yet; created lazily by `/domain-modeling`). See `docs/agents/domain.md`.

### Profiling

To use nsys refer to 'C:\Program Files\NVIDIA Corporation\Nsight Systems 2026.5.1\skills\nsight-systems\SKILL.md'

### Graphics/GPU debugging

Nsight Graphics is the replacement for augusta_renderer's now-removed in-app debug HUD: frame capture, draw-call/pixel inspection, and shader debugging for the D3D12 backend. Launch 'C:\Program Files\NVIDIA Corporation\Nsight Graphics 2026.3.1\host\windows-desktop-nomad-x64\ngfx-ui.exe' and attach to (or launch) `augustac.exe`.

renderer.cpp also requests `enableAftermath` unconditionally on the Falcor `Device::Desc` - on a GPU crash/TDR this writes a `.nv-gpudmp` crash dump next to `augustac.exe`, decodable with 'C:\Program Files\NVIDIA Corporation\Nsight Graphics 2026.3.1\host\windows-desktop-nomad-x64\nv-aftermath-format.exe'.
