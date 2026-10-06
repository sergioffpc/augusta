"""A scenario is named, not pathed (ADR-0041).

`augusta-pack <name>` resolves to <assets-root>/authoring/scenarios/<name>.yaml,
a manifest naming, by file, the one map, every character, the sound of every cue
and the scripts that scenario composes:

    authoring/scenarios/test.yaml                  # the manifest below
    authoring/maps/test.usda
    authoring/characters/player.usda
    authoring/sounds/test/gunshot.wav              # one mono PCM WAV per cue
    authoring/scripts/parameters/default.lua
    authoring/scripts/rules/last_standing.lua

    map: maps/test.usda
    characters:
      - characters/player.usda
    sounds:
      gunshot: sounds/test/gunshot.wav
      ...                                          # every cue in sounds.CUES
    scripts:
      parameters: scripts/parameters/default.lua
      rules: scripts/rules/last_standing.lua

Every path is relative to authoring/. The cooker packs everything the manifest
names: the map's stage and every character's stage into the client and server
packs (a character's own prim paths addressed under its path, e.g.
characters/player/Character/Visual - ADR-0040), each script into the
server pack under its role's fixed name (parameters.lua, rules.lua - ADR-0022,
ADR-0039), signed with the map/characters so it cannot change during a run, and
each cue's sound into the client pack only (ADR-0020, ADR-0031). A character's
path is its stage's path without the extension, so converting a stage between
.usda and .usdc never renames the character a client asks for.
"""

from dataclasses import dataclass
from pathlib import Path
from pathlib import PurePosixPath

import yaml

from pack.pack import MAX_CHARACTERS
from pack.sounds import CUES
from pack.sounds import CueSounds
from pack.sounds import read_sound
from pack.sounds import SoundError

USD_EXTENSIONS = (".usd", ".usda", ".usdc", ".usdz")

# The keys a manifest may hold; any other is a typo, refused rather than
# ignored.
MANIFEST_KEYS = ("map", "characters", "sounds", "scripts")

# Each script role a manifest's scripts may name, and the path it is packed at,
# which is where the server reads it (assets.h kParametersScriptPath,
# scripting.h kRulesScriptPath).
PARAMETERS_SCRIPT = "parameters"
RULES_SCRIPT = "rules"
SCRIPT_PACK_PATHS = {
    PARAMETERS_SCRIPT: "parameters.lua",
    RULES_SCRIPT: "rules.lua",
}


class ScenarioError(Exception):
    """Raised when a scenario name does not resolve to a usable scenario."""


@dataclass(frozen=True)
class Character:
    """One character a scenario's manifest names."""

    # The character's stage path relative to authoring/ without its extension
    # (e.g. "characters/player"): the prefix its own blobs are addressed
    # under in the pack (ADR-0040), and the name a client asks for it by.
    path: str
    # <authoring>/<manifest's entry>, the stage the cooker walks.
    stage_path: Path


@dataclass(frozen=True)
class Scenario:
    """A scenario and everything its manifest composes (ADR-0041)."""

    name: str
    # <authoring>/scenarios/<name>.yaml
    manifest_path: Path
    # <authoring>/<manifest's map>, the stage the cooker walks.
    map_stage_path: Path
    # Every character the manifest named, in manifest order.
    characters: list[Character]
    # Each script's path in the server pack with its bytes, in path order so a
    # cook is reproducible.
    scripts: list[tuple[str, bytes]]
    # The sound of every cue the client plays.
    sounds: CueSounds


def resolve_scenario(assets_root: Path, name: str) -> Scenario:
    """Reads the map, characters, sounds and scripts a scenario names.

    The scenario's manifest is <assets_root>/authoring/scenarios/<name>.yaml.

    Raises:
        ScenarioError: the manifest is missing or malformed, a file it names
            is missing or not what its key needs (a USD stage, a mono PCM
            WAV), it lacks a cue's sound or the parameters script, or it holds
            a key, cue or script role it does not know.
    """
    authoring_dir = assets_root / "authoring"
    manifest_path = authoring_dir / "scenarios" / f"{name}.yaml"
    if not manifest_path.is_file():
        raise ScenarioError(f"Scenario not found: {manifest_path}")

    manifest = _read_manifest(manifest_path)

    map_rel = manifest.get("map")
    if not isinstance(map_rel, str) or not map_rel:
        raise ScenarioError(
            f"{manifest_path}: 'map' must name a map's stage, e.g. map: "
            f"maps/test.usda"
        )
    map_stage_path = _find_stage(authoring_dir, map_rel, manifest_path)

    characters_rel = manifest.get("characters", [])
    if not isinstance(characters_rel, list) or not all(
        isinstance(entry, str) and entry for entry in characters_rel
    ):
        raise ScenarioError(
            f"{manifest_path}: 'characters' must be a list of character stages"
        )
    # Checked here rather than left to the cook, so usd-optimize and validation
    # never run on every stage of a scenario that can't be packed.
    if len(characters_rel) > MAX_CHARACTERS:
        raise ScenarioError(
            f"{manifest_path}: a scenario composes at most {MAX_CHARACTERS} "
            f"characters (ADR-0042), this one names {len(characters_rel)}"
        )
    characters = [
        Character(
            path=PurePosixPath(char_rel).with_suffix("").as_posix(),
            stage_path=_find_stage(authoring_dir, char_rel, manifest_path),
        )
        for char_rel in characters_rel
    ]

    return Scenario(
        name=name,
        manifest_path=manifest_path,
        map_stage_path=map_stage_path,
        characters=characters,
        scripts=_read_scripts(
            authoring_dir, manifest.get("scripts"), manifest_path
        ),
        sounds=_read_sounds(
            authoring_dir, manifest.get("sounds"), manifest_path
        ),
    )


def _read_manifest(manifest_path: Path) -> dict:
    try:
        manifest = yaml.safe_load(manifest_path.read_text(encoding="utf-8"))
    except yaml.YAMLError as error:
        raise ScenarioError(
            f"{manifest_path} is not valid YAML: {error}"
        ) from error
    if not isinstance(manifest, dict):
        raise ScenarioError(
            f"{manifest_path} must be a mapping with 'map', 'sounds' and "
            f"'scripts' keys"
        )
    unknown = [str(key) for key in manifest if key not in MANIFEST_KEYS]
    if unknown:
        raise ScenarioError(
            f"{manifest_path}: unknown key(s) {', '.join(unknown)} (known: "
            f"{', '.join(MANIFEST_KEYS)})"
        )
    return manifest


def _named_file(
    authoring_dir: Path, file_rel: str, manifest_path: Path
) -> Path:
    """authoring_dir/file_rel, which must be a file."""
    path = authoring_dir / file_rel
    if not path.is_file():
        raise ScenarioError(f"{manifest_path}: file not found: {path}")
    return path


def _find_stage(
    authoring_dir: Path, stage_rel: str, manifest_path: Path
) -> Path:
    """The USD stage at authoring_dir/stage_rel."""
    if PurePosixPath(stage_rel).suffix not in USD_EXTENSIONS:
        raise ScenarioError(
            f"{manifest_path}: {stage_rel} is not a USD stage "
            f"({', '.join(USD_EXTENSIONS)})"
        )
    return _named_file(authoring_dir, stage_rel, manifest_path)


def _role_files(
    entries: object,
    key: str,
    example: str,
    known: tuple[str, ...],
    manifest_path: Path,
) -> dict:
    """The manifest's key, which must map some of known to a file each."""
    if not isinstance(entries, dict) or not all(
        isinstance(path, str) and path for path in entries.values()
    ):
        raise ScenarioError(
            f"{manifest_path}: '{key}' must map each one to its file, e.g. "
            f"{example}"
        )
    unknown = [str(name) for name in entries if name not in known]
    if unknown:
        raise ScenarioError(
            f"{manifest_path}: '{key}' names unknown {', '.join(unknown)} "
            f"(known: {', '.join(known)})"
        )
    return entries


def _read_scripts(
    authoring_dir: Path, scripts_rel: object, manifest_path: Path
) -> list[tuple[str, bytes]]:
    """Each script the manifest names, as (its path in the server pack, bytes).

    The Parameters script is required: the server reads it out of its pack at
    startup, so a scenario without one is refused here rather than when a server
    starts on it. The rules script is optional: without it, the scenario has no
    Game policy (ADR-0022).
    """
    scripts_rel = _role_files(
        scripts_rel,
        "scripts",
        "parameters: scripts/parameters/default.lua",
        tuple(SCRIPT_PACK_PATHS),
        manifest_path,
    )
    if PARAMETERS_SCRIPT not in scripts_rel:
        raise ScenarioError(
            f"{manifest_path}: 'scripts' names no {PARAMETERS_SCRIPT} script: "
            "the server reads its Parameters from its pack (see "
            "tools/composer/examples/authoring/scripts/parameters/"
            "default.lua)."
        )
    return sorted(
        (
            SCRIPT_PACK_PATHS[role],
            _named_file(authoring_dir, script_rel, manifest_path).read_bytes(),
        )
        for role, script_rel in scripts_rel.items()
    )


def _read_sounds(
    authoring_dir: Path, sounds_rel: object, manifest_path: Path
) -> CueSounds:
    """Every cue's sound from the manifest's sounds.

    A client needs them all, so one missing or unplayable is found here rather
    than in a Match.
    """
    sounds_rel = _role_files(
        sounds_rel,
        "sounds",
        "gunshot: sounds/test/gunshot.wav",
        CUES,
        manifest_path,
    )
    cues = []
    for cue in CUES:
        if cue not in sounds_rel:
            raise ScenarioError(
                f"{manifest_path}: 'sounds' has no sound for cue {cue!r}"
            )
        try:
            cues.append(
                (
                    cue,
                    read_sound(
                        _named_file(
                            authoring_dir, sounds_rel[cue], manifest_path
                        )
                    ),
                )
            )
        except SoundError as error:
            raise ScenarioError(str(error)) from error
    return CueSounds(cues=cues)
