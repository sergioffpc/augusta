"""resolve_scenario (ADR-0041): a scenario name resolves to its manifest's
map, characters, scripts and cue sounds, or is refused before anything is
cooked.
"""

import pytest
from conftest import EXAMPLES_ROOT, write_float_wav, write_wav

from pack.pack import MAX_CHARACTERS
from pack.scenario import ScenarioError, resolve_scenario

# The client's cue catalogue, as a scenario's sounds folder must hold it.
CUES = ["gunshot", "hit_marker", "hit_taken", "death", "match_won", "match_lost"]

# Two frames of mono 16-bit PCM.
MONO_FRAMES = b"\x01\x00\xff\x7f"

MANIFEST = "map: maps/test\nsounds: sounds/test\n"


def make_scenario(root, manifest, scripts=("parameters.lua",), maps=("map.usda",), cues=CUES):
    """Builds an assets root under root with one map, one character, a sounds
    folder (sounds/test) holding a sound for each of cues, and a scenario named
    "test".
    """
    authoring = root / "authoring"
    (authoring / "maps" / "test").mkdir(parents=True)
    for map_name in maps:
        (authoring / "maps" / "test" / map_name).write_text("#usda 1.0\n")
    (authoring / "characters" / "player").mkdir(parents=True)
    (authoring / "characters" / "player" / "character.usda").write_text("#usda 1.0\n")
    for cue in cues:
        write_wav(authoring / "sounds" / "test" / f"{cue}.wav", MONO_FRAMES)
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
    assert scenario.sounds.path == "sounds/augusta"
    assert [cue for cue, _ in scenario.sounds.cues] == CUES


def test_scripts_are_addressed_relative_to_the_scenario_and_sorted(tmp_path):
    root = make_scenario(tmp_path, MANIFEST, scripts=("rules/round.lua", "parameters.lua"))

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
    root = make_scenario(tmp_path, MANIFEST, scripts=())

    with pytest.raises(ScenarioError, match="parameters.lua"):
        resolve_scenario(root, "test")


def test_a_map_that_does_not_resolve_to_a_stage_is_refused(tmp_path):
    root = make_scenario(tmp_path, "map: maps/elsewhere\nsounds: sounds/test\n")

    with pytest.raises(ScenarioError, match="Stage not found"):
        resolve_scenario(root, "test")


def test_a_map_with_two_stages_is_ambiguous(tmp_path):
    root = make_scenario(tmp_path, MANIFEST, maps=("map.usda", "map.usdc"))

    with pytest.raises(ScenarioError, match="ambiguous"):
        resolve_scenario(root, "test")


def test_more_characters_than_an_index_can_name_are_refused(tmp_path):
    characters = "".join("  - characters/player\n" for _ in range(MAX_CHARACTERS + 1))
    root = make_scenario(tmp_path, f"{MANIFEST}characters:\n{characters}")

    with pytest.raises(ScenarioError, match="at most"):
        resolve_scenario(root, "test")


def test_each_cue_resolves_to_its_sound_in_catalogue_order(tmp_path):
    root = make_scenario(tmp_path, MANIFEST)

    scenario = resolve_scenario(root, "test")

    assert scenario.sounds.path == "sounds/test"
    assert [cue for cue, _ in scenario.sounds.cues] == CUES
    sound = dict(scenario.sounds.cues)["gunshot"]
    assert (sound.sample_rate, sound.bits_per_sample, sound.samples) == (22050, 16, MONO_FRAMES)


def test_a_scenario_without_sounds_is_refused(tmp_path):
    root = make_scenario(tmp_path, "map: maps/test\n")

    with pytest.raises(ScenarioError, match="'sounds'"):
        resolve_scenario(root, "test")


def test_a_scenario_missing_a_cue_is_refused_naming_the_cue(tmp_path):
    root = make_scenario(tmp_path, MANIFEST, cues=[cue for cue in CUES if cue != "hit_taken"])

    with pytest.raises(ScenarioError, match="hit_taken"):
        resolve_scenario(root, "test")


def test_a_stereo_sound_is_refused_naming_the_file(tmp_path):
    root = make_scenario(tmp_path, MANIFEST)
    write_wav(root / "authoring" / "sounds" / "test" / "death.wav", MONO_FRAMES, channels=2)

    with pytest.raises(ScenarioError, match=r"death\.wav.*mono"):
        resolve_scenario(root, "test")


def test_a_sound_that_is_not_pcm_is_refused_naming_the_file(tmp_path):
    root = make_scenario(tmp_path, MANIFEST)
    write_float_wav(root / "authoring" / "sounds" / "test" / "match_won.wav")

    with pytest.raises(ScenarioError, match=r"match_won\.wav.*PCM"):
        resolve_scenario(root, "test")


def test_a_file_that_is_not_a_wav_is_refused_naming_it(tmp_path):
    root = make_scenario(tmp_path, MANIFEST)
    (root / "authoring" / "sounds" / "test" / "gunshot.wav").write_bytes(b"not a wav file")

    with pytest.raises(ScenarioError, match=r"gunshot\.wav"):
        resolve_scenario(root, "test")
