"""The client's cue sounds (ADR-0020, ADR-0031): the cue catalogue is fixed in
code, here and in the client (augusta/cues.h), and a scenario's manifest names
a mono PCM WAV file for each cue.
"""

import wave
from dataclasses import dataclass
from pathlib import Path

# The client's cue catalogue (augusta/cues.h), in its order.
CUES = ("gunshot", "hit_marker", "hit_taken", "death", "match_won", "match_lost")

# The prefix every cue's sound is addressed under in the client pack
# (<SOUNDS_PREFIX>/<cue>), recorded at SOUNDS_PATH so the client can find them.
SOUNDS_PREFIX = "sounds"


class SoundError(Exception):
    """Raised when a sound file is not a mono PCM WAV."""


@dataclass(frozen=True)
class Sound:
    """One mono PCM sound: its samples as the WAV file holds them (little-endian;
    8-bit unsigned, wider signed).
    """

    sample_rate: int
    bits_per_sample: int
    samples: bytes


@dataclass(frozen=True)
class CueSounds:
    # (cue, sound) for every cue, in CUES order.
    cues: list[tuple[str, Sound]]


def read_sound(path: Path) -> Sound:
    """Reads the mono PCM WAV file at path. Raises SoundError naming the file if it
    is not a WAV, not PCM, or not mono.
    """
    try:
        with wave.open(str(path), "rb") as wav:
            channels = wav.getnchannels()
            sample_width = wav.getsampwidth()
            sample_rate = wav.getframerate()
            samples = wav.readframes(wav.getnframes())
    except (wave.Error, EOFError) as error:
        # The wave module reads PCM only, so anything else is refused here too.
        raise SoundError(f"{path} is not a PCM WAV file: {error}") from error
    if channels != 1:
        raise SoundError(f"{path} has {channels} channels, a cue sound must be mono (ADR-0020)")
    return Sound(sample_rate=sample_rate, bits_per_sample=8 * sample_width, samples=samples)
