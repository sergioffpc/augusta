"""resolve_scenario (ADR-0041): a scenario name resolves to its manifest's
map, characters and scripts, or is refused before anything is cooked.
"""

import pytest
from conftest import EXAMPLES_ROOT

from pack.pack import MAX_CHARACTERS
from pack.scenario import ScenarioError, resolve_scenario


def make_scenario(root, manifest, scripts=("parameters.lua",), maps=("map.usda",)):
    """Builds an assets root under root with one map, one character and a scenario named "test"."""
    authoring = root / "authoring"
    (authoring / "maps" / "test").mkdir(parents=True)
    for map_name in maps:
        (authoring / "maps" / "test" / map_name).write_text("#usda 1.0\n")
    (authoring / "characters" / "player").mkdir(parents=True)
    (authoring / "characters" / "player" / "character.usda").write_text("#usda 1.0\n")
    folder = authoring / "scenarios" / "test"
    folder.mkdir(parents=True)
    if manifest is not None:
        (folder / "manifest.yaml").write_text(manifest)
    for script in scripts:
        (folder / script).parent.mkdir(parents=True, exist_ok=True)
        (folder / script).write_text("return {}")
    return root


def test_the_example_scenario_resolves():
    scenario = resolve_scenario(EXAMPLES_ROOT, "augusta")

    assert scenario.map_stage_path == EXAMPLES_ROOT / "authoring" / "maps" / "augusta" / "map.usda"
    assert [character.manifest_path for character in scenario.characters] == ["characters/player"]
    assert "parameters.lua" in [path for path, _ in scenario.scripts]


def test_scripts_are_addressed_relative_to_the_scenario_and_sorted(tmp_path):
    root = make_scenario(tmp_path, "map: maps/test\n", scripts=("rules/round.lua", "parameters.lua"))

    scenario = resolve_scenario(root, "test")

    assert [path for path, _ in scenario.scripts] == ["parameters.lua", "rules/round.lua"]


def test_a_scenario_that_does_not_exist_is_refused(tmp_path):
    with pytest.raises(ScenarioError, match="Scenario not found"):
        resolve_scenario(tmp_path, "missing")


def test_a_scenario_without_a_manifest_is_refused(tmp_path):
    root = make_scenario(tmp_path, None)

    with pytest.raises(ScenarioError, match="manifest.yaml"):
        resolve_scenario(root, "test")


def test_a_scenario_without_parameters_is_refused(tmp_path):
    root = make_scenario(tmp_path, "map: maps/test\n", scripts=())

    with pytest.raises(ScenarioError, match="parameters.lua"):
        resolve_scenario(root, "test")


def test_a_map_that_does_not_resolve_to_a_stage_is_refused(tmp_path):
    root = make_scenario(tmp_path, "map: maps/elsewhere\n")

    with pytest.raises(ScenarioError, match="Stage not found"):
        resolve_scenario(root, "test")


def test_a_map_with_two_stages_is_ambiguous(tmp_path):
    root = make_scenario(tmp_path, "map: maps/test\n", maps=("map.usda", "map.usdc"))

    with pytest.raises(ScenarioError, match="ambiguous"):
        resolve_scenario(root, "test")


def test_more_characters_than_an_index_can_name_are_refused(tmp_path):
    characters = "".join("  - characters/player\n" for _ in range(MAX_CHARACTERS + 1))
    root = make_scenario(tmp_path, f"map: maps/test\ncharacters:\n{characters}")

    with pytest.raises(ScenarioError, match="at most"):
        resolve_scenario(root, "test")
