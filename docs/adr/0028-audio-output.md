# Audio Output: miniaudio

Steam Audio (ADR-0010) only processes audio buffers already in memory
(HRTF/binaural spatialization) - it opens no output device, decodes no files,
and mixes no voices. miniaudio (MIT, single-header) fills that gap: device
output, mono PCM decoding (ADR-0020) to the device's format and rate, and the
output stream simultaneous voices are mixed into. augusta::audio spatializes
each voice through Steam Audio inside miniaudio's device callback and sums the
voices into the period it fills.

A client that cannot open an output device logs why at WARN and runs with a
silent engine: a missing headset never stops play, and CI runners, which have no
audio device, run unchanged. Only the Windows client links miniaudio and Steam
Audio; every other build's engine is always silent, so the Linux build has no
audio dependency (ADR-0010).

## Consequences

Chosen over XAudio2 (Windows SDK, no extra dependency, but no built-in file
decoding or mixing - both would have to be hand-rolled) and OpenAL Soft (LGPL,
and its own 3D positional audio would go unused in favor of Steam Audio, same as
miniaudio's). miniaudio trades one more vendored dependency for materially less
plumbing code, consistent with this project's focus being elsewhere (ballistics,
networking, ECS - see ENGINEERING.md).
