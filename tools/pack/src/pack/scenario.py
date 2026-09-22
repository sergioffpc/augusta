"""A scenario is a folder holding one USD stage and the Lua scripts that go
with it:

    test_map/map.usda
    test_map/parameters.lua

The cooker is told the folder directly - relative to the current directory or
absolute, like any other path, not resolved against an assets root - and packs
everything under it: the stage into the client and server packs, and every
`*.lua` file into the server pack, addressed by its path relative to the folder
(ADR-0031, ADR-0039). The scripts are signed with the map and cannot change
during a run. The stage and the Parameters script have fixed names (map.usd*,
parameters.lua) rather than names derived from the folder, so renaming a
scenario never means renaming the files inside it.
"""

from dataclasses import dataclass
from pathlib import Path

USD_EXTENSIONS = (".usd", ".usda", ".usdc", ".usdz")

# The stage every scenario needs, named the same regardless of the folder's own
# name (ADR-0015).
STAGE_NAME = "map"

# The Parameters script (ADR-0039) every scenario needs: the server reads it out
# of its pack at startup, so a scenario without one is refused here, when it is
# cooked, rather than when a server tries to start on it.
PARAMETERS_SCRIPT = "parameters.lua"


class ScenarioError(Exception):
    """Raised when a scenario argument does not name a usable scenario folder."""


@dataclass(frozen=True)
class Scenario:
    folder: Path
    # <folder>/map.usd*, the stage the cooker walks.
    stage_path: Path
    # Each script's path relative to the folder ('/' separated, as it is addressed
    # in the pack), with its bytes, in path order so a cook is reproducible.
    scripts: list[tuple[str, bytes]]

    @property
    def name(self) -> str:
        return self.folder.name


def resolve_scenario(folder: Path) -> Scenario:
    """Finds folder's stage and scripts.

    folder is an ordinary path - relative to the current directory or absolute,
    like any file argument - naming the scenario's own directory directly, not
    a name looked up under some other root. Raises ScenarioError if it isn't a
    directory, if its stage is missing or ambiguous, or if it has no
    parameters.lua.
    """
    if not folder.is_dir():
        raise ScenarioError(f"Scenario folder not found: {folder}")

    stage_path = _find_stage(folder)
    scripts = _read_scripts(folder)
    if PARAMETERS_SCRIPT not in (path for path, _ in scripts):
        raise ScenarioError(
            f"Scenario {folder} has no {PARAMETERS_SCRIPT}: the server reads its Parameters from its pack "
            f"(see tools/pack/examples/augusta/parameters.lua)."
        )
    return Scenario(folder=folder, stage_path=stage_path, scripts=scripts)


def _find_stage(folder: Path) -> Path:
    """The folder's stage: <folder>/map.usd, .usda, .usdc or .usdz."""
    candidates = [folder / f"{STAGE_NAME}{extension}" for extension in USD_EXTENSIONS]
    found = [candidate for candidate in candidates if candidate.is_file()]
    if not found:
        tried = ", ".join(USD_EXTENSIONS)
        raise ScenarioError(f"Stage not found: {folder / STAGE_NAME} (tried extensions {tried})")
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
