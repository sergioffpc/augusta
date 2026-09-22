"""A scenario is a folder under <assets-root>/authoring holding one USD stage and
the Lua scripts that go with it:

    authoring/test_map/test_map.usda
    authoring/test_map/parameters.lua

The cooker is told the folder (`augustap test_map`) and packs everything under
it: the stage into the client and server packs, and every `*.lua` file into the
server pack, addressed by its path relative to the folder (ADR-0031, ADR-0039).
The scripts are signed with the map and cannot change during a run.
"""

from dataclasses import dataclass
from pathlib import Path

USD_EXTENSIONS = (".usd", ".usda", ".usdc", ".usdz")

# The Parameters script (ADR-0039) every scenario needs: the server reads it out
# of its pack at startup, so a scenario without one is refused here, when it is
# cooked, rather than when a server tries to start on it.
PARAMETERS_SCRIPT = "parameters.lua"


class ScenarioError(Exception):
    """Raised when a scenario argument does not name a usable scenario folder."""


@dataclass(frozen=True)
class Scenario:
    folder: Path
    # <folder>/<folder name>.usd*, the stage the cooker walks.
    stage_path: Path
    # Each script's path relative to the folder ('/' separated, as it is addressed
    # in the pack), with its bytes, in path order so a cook is reproducible.
    scripts: list[tuple[str, bytes]]

    @property
    def name(self) -> str:
        return self.folder.name


def resolve_scenario(authoring_dir: Path, scenario: Path) -> Scenario:
    """Finds the scenario folder authoring_dir/scenario, its stage and its scripts.

    scenario must be a plain relative path staying inside authoring_dir. Raises
    ScenarioError if it is not, if the folder or its stage is missing (or
    ambiguous), or if the folder has no parameters.lua.
    """
    if scenario.is_absolute() or ".." in scenario.parts or not scenario.parts:
        raise ScenarioError(
            f"A scenario must be a folder relative to {authoring_dir} (no absolute paths or '..'): {scenario}"
        )

    folder = authoring_dir / scenario
    if not folder.is_dir():
        raise ScenarioError(f"Scenario folder not found: {folder}")

    stage_path = _find_stage(folder)
    scripts = _read_scripts(folder)
    if PARAMETERS_SCRIPT not in (path for path, _ in scripts):
        raise ScenarioError(
            f"Scenario {folder} has no {PARAMETERS_SCRIPT}: the server reads its Parameters from its pack "
            f"(see config/parameters.example.lua)."
        )
    return Scenario(folder=folder, stage_path=stage_path, scripts=scripts)


def _find_stage(folder: Path) -> Path:
    """The folder's stage: <folder>/<folder name>.usd, .usda, .usdc or .usdz."""
    candidates = [folder / f"{folder.name}{extension}" for extension in USD_EXTENSIONS]
    found = [candidate for candidate in candidates if candidate.is_file()]
    if not found:
        tried = ", ".join(USD_EXTENSIONS)
        raise ScenarioError(f"Stage not found: {folder / folder.name} (tried extensions {tried})")
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
