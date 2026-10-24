"""optimize_stage: a stage cleaned up and exported elsewhere (ADR-0015)."""

from augusta_pack.optimize import optimize_stage
from pxr import Usd


def test_a_cleaned_stage_still_composes_the_files_it_references(tmp_path):
    authoring = tmp_path / "authoring"
    (authoring / "animations").mkdir(parents=True)
    (authoring / "animations" / "walk.usda").write_text(
        '#usda 1.0\n(\n    defaultPrim = "Walk"\n)\n\n'
        'def Xform "Walk"\n{\n    custom double stride = 1.4\n}\n',
        encoding="utf-8",
    )
    (authoring / "characters").mkdir()
    stage_path = authoring / "characters" / "soldier.usda"
    stage_path.write_text(
        '#usda 1.0\n(\n    defaultPrim = "Character"\n)\n\n'
        'def Xform "Character"\n{\n'
        '    def "Walk" (references = @../animations/walk.usda@) {}\n}\n',
        encoding="utf-8",
    )
    cleaned_path = tmp_path / "elsewhere" / "soldier-cleaned.usda"
    cleaned_path.parent.mkdir()

    optimize_stage(stage_path, cleaned_path)

    cleaned = Usd.Stage.Open(str(cleaned_path))
    walk = cleaned.GetPrimAtPath("/Character/Walk")
    assert walk.GetAttribute("stride").Get() == 1.4
