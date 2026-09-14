# Audio Output: miniaudio

Steam Audio (ADR-0010) only processes audio buffers already in memory (HRTF/binaural spatialization) - it opens no output device, decodes no files, and mixes no voices. miniaudio (MIT, single-header) fills that gap: device output, mono PCM decoding (ADR-0020), and mixing multiple simultaneous voices into the output stream. augusta::audio calls Steam Audio to spatialize each voice, then hands the result to miniaudio for mixing/playback.

## Consequences

Chosen over XAudio2 (Windows SDK, no extra dependency, but no built-in file decoding or mixing - both would have to be hand-rolled) and OpenAL Soft (LGPL, and its own 3D positional audio would go unused in favor of Steam Audio, same as miniaudio's). miniaudio trades one more vendored dependency for materially less plumbing code, consistent with this being a hobby project whose learning focus is elsewhere (ballistics, networking, ECS - see ENGINEERING.md).
