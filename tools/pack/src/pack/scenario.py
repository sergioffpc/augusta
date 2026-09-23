"""A scenario is named, not pathed (ADR-0041): `augustap <name>` resolves to
<assets-root>/authoring/scenarios/<name>/, which holds a manifest.yaml naming
the one map and every character that scenario composes, plus the scripts that
go with it:

    authoring/scenarios/test_map/manifest.yaml   # map: maps/test_map
    authoring/scenarios/test_map/parameters.lua
    authoring/maps/test_map/map.usda
    authoring/characters/player/character.usda

The cooker packs everything the manifest names: the map's stage and every
named character's stage into the client and server packs (a character's own
prim paths addressed under its manifest path, e.g. characters/player/Visual -
ADR-0040), and every `*.lua` file under the scenario folder into the server
pack, addressed by its path relative to that folder (ADR-0031, ADR-0039). The
scripts are signed with the map/characters and cannot change during a run.
The map's stage, a character's stage, and the Parameters script all have
fixed names (map.usd*, character.usd*, parameters.lua) rather than names
derived from their folder, so renaming any of them never means renaming the
files inside.
"""

from dataclasses import dataclass
from pathlib import Path

import yaml

USD_EXTENSIONS = (".usd", ".usda", ".usdc", ".usdz")

# The stage every map needs, named the same regardless of its folder's own
# name (ADR-0015).
MAP_STAGE_NAME = "map"

# The stage every character needs, same fixed-name convention (ADR-0040).
CHARACTER_STAGE_NAME = "character"

# A scenario's composition manifest (ADR-0041), fixed name for the same
# reason.
MANIFEST_NAME = "manifest.yaml"

# The Parameters script (ADR-0039) every scenario needs: the server reads it out
# of its pack at startup, so a scenario without one is refused here, when it is
# cooked, rather than when a server tries to start on it.
PARAMETERS_SCRIPT = "parameters.lua"


class ScenarioError(Exception):
    """Raised when a scenario name does not resolve to a usable scenario."""


@dataclass(frozen=True)
class Character:
    # This character's path as the manifest named it, relative to authoring/
    # (e.g. "characters/player") - also the prefix its own blobs are
    # addressed under in the pack (ADR-0040).
    manifest_path: str
    # <authoring>/<manifest_path>/character.usd*, the stage the cooker walks.
    stage_path: Path


@dataclass(frozen=True)
class Scenario:
    name: str
    # <authoring>/scenarios/<name>/
    folder: Path
    # <authoring>/<manifest's map>/map.usd*, the stage the cooker walks.
    map_stage_path: Path
    # Every character the manifest named, in manifest order.
    characters: list[Character]
    # Each script's path relative to the folder ('/' separated, as it is addressed
    # in the pack), with its bytes, in path order so a cook is reproducible.
    scripts: list[tuple[str, bytes]]


def resolve_scenario(assets_root: Path, name: str) -> Scenario:
    """Resolves name to <assets_root>/authoring/scenarios/<name>/, reads its
    manifest.yaml, and finds the map/characters/scripts it names.

    Raises ScenarioError if the scenario folder or its manifest is missing,
    if the manifest is malformed or names a map/character that doesn't
    resolve to a stage, or if the scenario has no parameters.lua.
    """
    authoring_dir = assets_root / "authoring"
    folder = authoring_dir / "scenarios" / name
    if not folder.is_dir():
        raise ScenarioError(f"Scenario not found: {folder}")

    manifest = _read_manifest(folder)

    map_rel = manifest.get("map")
    if not isinstance(map_rel, str) or not map_rel:
        raise ScenarioError(f"{folder / MANIFEST_NAME}: 'map' must name a map, e.g. map: maps/test_map")
    map_stage_path = _find_stage(authoring_dir / map_rel, MAP_STAGE_NAME)

    characters_rel = manifest.get("characters", [])
    if not isinstance(characters_rel, list) or not all(isinstance(entry, str) and entry for entry in characters_rel):
        raise ScenarioError(f"{folder / MANIFEST_NAME}: 'characters' must be a list of character paths")
    characters = [
        Character(manifest_path=char_rel, stage_path=_find_stage(authoring_dir / char_rel, CHARACTER_STAGE_NAME))
        for char_rel in characters_rel
    ]

    scripts = _read_scripts(folder)
    if PARAMETERS_SCRIPT not in (path for path, _ in scripts):
        raise ScenarioError(
            f"Scenario {folder} has no {PARAMETERS_SCRIPT}: the server reads its Parameters from its pack "
            f"(see tools/pack/examples/authoring/scenarios/augusta/parameters.lua)."
        )
    return Scenario(name=name, folder=folder, map_stage_path=map_stage_path, characters=characters, scripts=scripts)


def _read_manifest(folder: Path) -> dict:
    manifest_path = folder / MANIFEST_NAME
    if not manifest_path.is_file():
        raise ScenarioError(f"Scenario {folder} has no {MANIFEST_NAME} (ADR-0041)")
    try:
        manifest = yaml.safe_load(manifest_path.read_text(encoding="utf-8"))
    except yaml.YAMLError as error:
        raise ScenarioError(f"{manifest_path} is not valid YAML: {error}") from error
    if not isinstance(manifest, dict):
        raise ScenarioError(f"{manifest_path} must be a mapping with 'map' and 'characters' keys")
    return manifest


def _find_stage(folder: Path, stage_name: str) -> Path:
    """folder's stage: <folder>/<stage_name>.usd, .usda, .usdc or .usdz."""
    candidates = [folder / f"{stage_name}{extension}" for extension in USD_EXTENSIONS]
    found = [candidate for candidate in candidates if candidate.is_file()]
    if not found:
        tried = ", ".join(USD_EXTENSIONS)
        raise ScenarioError(f"Stage not found: {folder / stage_name} (tried extensions {tried})")
    if len(found) > 1:
        names = ", ".join(candidate.name for candidate in found)
        raise ScenarioError(f"Stage name is ambiguous, several files match in {folder}: {names}")
    return found[0]


def _read_scripts(folder: Path) -> list[tuple[str, bytes]]:
    return [
        (path.relative_to(folder).as_posix(), path.read_bytes())
        for path in sorted(folder.rglob("*.lua"), key=lambda path: path.relative_to(folder).as_posix())
        if path.is_file()
    ]
