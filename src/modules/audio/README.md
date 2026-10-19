# audio

`augusta_audio`, `augusta_cues`. Client-only: the headless server never links
it.

## Responsibility

Plays the client's one-shot sound cues: the catalogue of every cue the client
can play, whose sound a scenario's client pack must ship (`augusta/cues.h`),
each time one of its variants, never the one it played last, at its gain,
spatialized through Steam Audio and mixed to the output device by miniaudio,
with that split hidden. No output device is not an error: the client runs the
same, silent.

## Not its responsibility

- Deciding which cue plays, when and from where: presentation's AudioCues phase.
- Cooking sounds into a pack: `tools/pack`.

## Decisions

[ADR-0010](../../../docs/adr/0010-audio.md),
[ADR-0020](../../../docs/adr/0020-audio-asset-format.md),
[ADR-0028](../../../docs/adr/0028-audio-output.md),
[ADR-0041](../../../docs/adr/0041-scenario-composition-manifest.md).
