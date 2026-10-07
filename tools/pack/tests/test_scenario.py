"""resolve_scenario (ADR-0041).

A scenario name resolves to the map, characters, cue sounds and scripts its
manifest names by file, or is refused before anything is cooked.
"""

from conftest import EXAMPLES_ROOT
from conftest import write_float_wav
from conftest import write_wav
from pack.pack import MAX_CHARACTERS
from pack.scenario import resolve_scenario
from pack.scenario import ScenarioError
import pytest
import yaml

# The client's cue catalogue, as a scenario's sounds must name it.
CUES = [
    "gunshot",
    "hit_marker",
    "hit_taken",
    "death",
    "match_won",
    "match_lost",
]

# Two frames of mono 16-bit PCM.
MONO_FRAMES = b"\x01\x00\xff\x7f"


def manifest(**overrides):
    """A manifest naming everything make_scenario writes.

    Overrides replace its keys (None drops one).
    """
    entries = {
        "map": "maps/test.usda",
        "characters": ["characters/player.usda"],
        "sounds": {cue: f"sounds/test/{cue}.wav" for cue in CUES},
        "scripts": {"parameters": "scripts/parameters/default.lua"},
    }
    entries.update(overrides)
    return {key: value for key, value in entries.items() if value is not None}


def make_scenario(root, entries):
    """Builds an assets root under root with a scenario named "test".

    The root holds a map, a character, a sound for every cue, a parameters and
    a rules script, and the scenario's manifest is entries (None writes none).
    """
    authoring = root / "authoring"
    for stage in ("maps/test.usda", "characters/player.usda"):
        (authoring / stage).parent.mkdir(parents=True, exist_ok=True)
        (authoring / stage).write_text("#usda 1.0\n")
    for cue in CUES:
        write_wav(authoring / "sounds" / "test" / f"{cue}.wav", MONO_FRAMES)
    for script, text in (
        ("parameters/default.lua", "return {}"),
        ("rules/round.lua", "function on_tick() end"),
    ):
        (authoring / "scripts" / script).parent.mkdir(
            parents=True, exist_ok=True
        )
        (authoring / "scripts" / script).write_text(text)
    (authoring / "scenarios").mkdir(parents=True)
    if entries is not None:
        (authoring / "scenarios" / "test.yaml").write_text(
            yaml.safe_dump(entries)
        )
    return root


def test_the_example_scenario_resolves():
    scenario = resolve_scenario(EXAMPLES_ROOT, "augusta")

    assert (
        scenario.map_stage_path
        == EXAMPLES_ROOT / "authoring" / "maps" / "augusta.usda"
    )
    assert [character.path for character in scenario.characters] == [
        "characters/player"
    ]
    assert [path for path, _ in scenario.scripts] == [
        "parameters.lua",
        "rules.lua",
    ]
    assert [cue for cue, _ in scenario.sounds.cues] == CUES


def test_a_character_is_named_by_its_stage_path_without_the_extension(tmp_path):
    root = make_scenario(tmp_path, manifest())

    scenario = resolve_scenario(root, "test")

    assert [character.path for character in scenario.characters] == [
        "characters/player"
    ]
    assert (
        scenario.characters[0].stage_path
        == root / "authoring" / "characters" / "player.usda"
    )


def test_each_script_is_packed_under_its_role_in_path_order(tmp_path):
    scripts = {
        "rules": "scripts/rules/round.lua",
        "parameters": "scripts/parameters/default.lua",
    }
    root = make_scenario(tmp_path, manifest(scripts=scripts))

    scenario = resolve_scenario(root, "test")

    assert scenario.scripts == [
        ("parameters.lua", b"return {}"),
        ("rules.lua", b"function on_tick() end"),
    ]


def test_a_scenario_that_does_not_exist_is_refused(tmp_path):
    with pytest.raises(ScenarioError, match="Scenario not found"):
        resolve_scenario(tmp_path, "missing")


def test_a_manifest_with_an_unknown_key_is_refused(tmp_path):
    root = make_scenario(tmp_path, manifest(sound={}))

    with pytest.raises(ScenarioError, match="unknown key.*sound"):
        resolve_scenario(root, "test")


def test_a_scenario_without_parameters_is_refused(tmp_path):
    root = make_scenario(
        tmp_path, manifest(scripts={"rules": "scripts/rules/round.lua"})
    )

    with pytest.raises(ScenarioError, match="parameters"):
        resolve_scenario(root, "test")


def test_a_scenario_without_scripts_is_refused(tmp_path):
    root = make_scenario(tmp_path, manifest(scripts=None))

    with pytest.raises(ScenarioError, match="'scripts'"):
        resolve_scenario(root, "test")


def test_an_unknown_script_role_is_refused(tmp_path):
    scripts = {
        "parameters": "scripts/parameters/default.lua",
        "behaviours": "scripts/rules/round.lua",
    }
    root = make_scenario(tmp_path, manifest(scripts=scripts))

    with pytest.raises(ScenarioError, match="unknown behaviours"):
        resolve_scenario(root, "test")


def test_a_script_that_does_not_exist_is_refused(tmp_path):
    root = make_scenario(
        tmp_path,
        manifest(scripts={"parameters": "scripts/parameters/missing.lua"}),
    )

    with pytest.raises(ScenarioError, match=r"file not found.*missing\.lua"):
        resolve_scenario(root, "test")


def test_a_map_that_does_not_exist_is_refused(tmp_path):
    root = make_scenario(
        tmp_path, manifest(map="maps/elsewhere/elsewhere.usda")
    )

    with pytest.raises(ScenarioError, match=r"file not found.*elsewhere\.usda"):
        resolve_scenario(root, "test")


def test_a_map_that_names_a_folder_is_refused(tmp_path):
    root = make_scenario(tmp_path, manifest(map="maps/test"))

    with pytest.raises(ScenarioError, match="not a USD stage"):
        resolve_scenario(root, "test")


def test_a_character_that_is_not_a_usd_stage_is_refused(tmp_path):
    root = make_scenario(
        tmp_path, manifest(characters=["sounds/test/death.wav"])
    )

    with pytest.raises(ScenarioError, match="not a USD stage"):
        resolve_scenario(root, "test")


def test_more_characters_than_an_index_can_name_are_refused(tmp_path):
    root = make_scenario(
        tmp_path,
        manifest(characters=["characters/player.usda"] * (MAX_CHARACTERS + 1)),
    )

    with pytest.raises(ScenarioError, match="at most"):
        resolve_scenario(root, "test")


def test_each_cue_resolves_to_its_sound_in_catalogue_order(tmp_path):
    root = make_scenario(tmp_path, manifest())

    scenario = resolve_scenario(root, "test")

    assert [cue for cue, _ in scenario.sounds.cues] == CUES
    sound = dict(scenario.sounds.cues)["gunshot"]
    assert (sound.sample_rate, sound.bits_per_sample, sound.samples) == (
        22050,
        16,
        MONO_FRAMES,
    )


def test_one_file_may_be_the_sound_of_several_cues(tmp_path):
    sounds = {cue: "sounds/test/death.wav" for cue in CUES}
    root = make_scenario(tmp_path, manifest(sounds=sounds))

    scenario = resolve_scenario(root, "test")

    assert [cue for cue, _ in scenario.sounds.cues] == CUES


def test_a_scenario_without_sounds_is_refused(tmp_path):
    root = make_scenario(tmp_path, manifest(sounds=None))

    with pytest.raises(ScenarioError, match="'sounds'"):
        resolve_scenario(root, "test")


def test_a_scenario_missing_a_cue_is_refused_naming_the_cue(tmp_path):
    sounds = {
        cue: f"sounds/test/{cue}.wav" for cue in CUES if cue != "hit_taken"
    }
    root = make_scenario(tmp_path, manifest(sounds=sounds))

    with pytest.raises(ScenarioError, match="hit_taken"):
        resolve_scenario(root, "test")


def test_an_unknown_cue_is_refused_naming_it(tmp_path):
    sounds = {cue: f"sounds/test/{cue}.wav" for cue in CUES} | {
        "footstep": "sounds/test/death.wav"
    }
    root = make_scenario(tmp_path, manifest(sounds=sounds))

    with pytest.raises(ScenarioError, match="unknown footstep"):
        resolve_scenario(root, "test")


def test_a_stereo_sound_is_refused_naming_the_file(tmp_path):
    root = make_scenario(tmp_path, manifest())
    write_wav(
        root / "authoring" / "sounds" / "test" / "death.wav",
        MONO_FRAMES,
        channels=2,
    )

    with pytest.raises(ScenarioError, match=r"death\.wav.*mono"):
        resolve_scenario(root, "test")


def test_a_sound_that_is_not_pcm_is_refused_naming_the_file(tmp_path):
    root = make_scenario(tmp_path, manifest())
    write_float_wav(root / "authoring" / "sounds" / "test" / "match_won.wav")

    with pytest.raises(ScenarioError, match=r"match_won\.wav.*PCM"):
        resolve_scenario(root, "test")


def test_a_file_that_is_not_a_wav_is_refused_naming_it(tmp_path):
    root = make_scenario(tmp_path, manifest())
    (root / "authoring" / "sounds" / "test" / "gunshot.wav").write_bytes(
        b"not a wav file"
    )

    with pytest.raises(ScenarioError, match=r"gunshot\.wav"):
        resolve_scenario(root, "test")
