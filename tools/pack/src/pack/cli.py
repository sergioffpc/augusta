"""Augusta asset-cooking pipeline CLI (ADR-0030), run on a scenario: a folder
holding a stage and its Lua scripts (see scenario.py). usd-optimize (ADR-0015,
Python API - see optimize.py) -> usd-validation-nvidia (ADR-0015, also
called via its own Python API - see validate.py) -> cook_stage (bake to
signed client/server packs, ADR-0031/ADR-0032 - see cook.py). cook_stage is
pure Python too: it walks the USD stage via pxr directly and calls the
small native _meshoptimizer/_textconv bindings only for the two pieces with
no Python equivalent - no subprocess/CLI binary anywhere in this pipeline.
A validation failure aborts before cooking, so no pack is written for a
stage that didn't pass cleanup/validation, unless --skip-validation is given.
Every *.lua file under the scenario folder goes into the server pack (ADR-0031,
ADR-0039).

The scenario argument is an ordinary path - relative to the current directory
or absolute - naming the scenario's own folder directly; it is never resolved
against an assets root. This project is installed into the hermetic
environment tools/pack/scripts/bootstrap-windows.ps1 builds
(--assets-root/python), so --assets-root defaults to the root of the venv this
interpreter is already running from and is used only for the defaults below,
never to locate the scenario itself: packs default to
--assets-root/packs/<scenario folder name>.*.pack, and the signing key to
--assets-root/keys/augusta.key.
"""

import argparse
import sys
import tempfile
import time
import uuid
from pathlib import Path

from pack.assets_root import default_assets_root
from pack.cook import CookError, cook_stage
from pack.keys import read_private_key
from pack.optimize import OptimizeError, optimize_stage
from pack.progress import Progress
from pack.scenario import ScenarioError, resolve_scenario
from pack.validate import ValidationError, validate_stage


def main(argv: list[str] | None = None) -> int:
    parser = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    parser.add_argument(
        "scenario",
        type=Path,
        help="Scenario folder - relative to the current directory or absolute: <scenario>/map.usd* is the raw "
        "authored stage (e.g. exported from USD Composer) and every *.lua under the folder is packed into the "
        "server pack. It needs a parameters.lua. tools\\pack\\examples\\augusta is a worked example.",
    )
    parser.add_argument(
        "--assets-root",
        type=Path,
        default=default_assets_root(),
        help="Hermetic environment root, for the --client-output-pack/--server-output-pack/--signing-key defaults "
        "below only (default: inferred from this interpreter's own venv).",
    )
    parser.add_argument(
        "--client-output-pack", type=Path, default=None, help="Default: <assets-root>/packs/<scenario folder name>.client.pack"
    )
    parser.add_argument(
        "--server-output-pack", type=Path, default=None, help="Default: <assets-root>/packs/<scenario folder name>.server.pack"
    )
    parser.add_argument("--signing-key", type=Path, default=None, help="Default: <assets-root>/keys/augusta.key")
    parser.add_argument(
        "--skip-validation",
        action="store_true",
        help="Skip usd-validation-nvidia (e.g. third-party stages that fail its checks); usd-optimize and the cook still run.",
    )
    args = parser.parse_args(argv)

    assets_root: Path = args.assets_root
    packs_dir = assets_root / "packs"

    try:
        scenario = resolve_scenario(args.scenario)
    except ScenarioError as error:
        print(error, file=sys.stderr)
        return 1
    stage_path = scenario.stage_path
    stage_name = scenario.name

    signing_key_path = args.signing_key or assets_root / "keys" / "augusta.key"
    client_output_pack = args.client_output_pack or packs_dir / f"{stage_name}.client.pack"
    server_output_pack = args.server_output_pack or packs_dir / f"{stage_name}.server.pack"

    # The stage itself was already confirmed by resolve_scenario; only the
    # assets-root-derived signing key can still be missing here.
    if not signing_key_path.exists():
        print(
            f"Signing key not found: {signing_key_path} - pass --signing-key, or run "
            f"tools\\pack\\scripts\\bootstrap-windows.ps1 {assets_root} first.",
            file=sys.stderr,
        )
        return 1

    signing_key = read_private_key(signing_key_path)
    for pack in (client_output_pack, server_output_pack):
        pack.parent.mkdir(parents=True, exist_ok=True)

    pipeline_start = time.monotonic()
    with tempfile.TemporaryDirectory() as tmp_dir:
        cleaned_stage = Path(tmp_dir) / f"{stage_name}-cleaned-{uuid.uuid4()}.usda"

        print(f"[1/3] usd-optimize: {stage_path}")
        step_start = time.monotonic()
        try:
            optimize_stage(stage_path, cleaned_stage)
        except OptimizeError as error:
            print(f"usd-optimize failed: {error} - stage not cleaned, cook aborted.", file=sys.stderr)
            return 1

        print(f"[1/3] done in {time.monotonic() - step_start:.1f}s")

        if args.skip_validation:
            print("[2/3] usd-validation-nvidia: skipped (--skip-validation)")
        else:
            print("[2/3] usd-validation-nvidia")
            step_start = time.monotonic()
            try:
                validate_stage(cleaned_stage)
            except ValidationError as error:
                print(f"{error} - cook aborted, no pack written.", file=sys.stderr)
                return 1

            print(f"[2/3] done in {time.monotonic() - step_start:.1f}s")

        print(f"[3/3] cooking into {client_output_pack} (client) / {server_output_pack} (server)")
        step_start = time.monotonic()
        # The prim total is only known once cook_stage has traversed the
        # stage, so the Progress is created on the first callback.
        progress: Progress | None = None

        def report_prim(done: int, total: int, prim_path: str) -> None:
            nonlocal progress
            if progress is None:
                progress = Progress("      prims", total)
            progress.update(done, prim_path)

        try:
            report = cook_stage(
                cleaned_stage,
                client_output_pack,
                server_output_pack,
                signing_key,
                scripts=scenario.scripts,
                on_prim=report_prim,
            )
        except CookError as error:
            if progress is not None:
                progress.finish()
            print(str(error), file=sys.stderr)
            return 1
        if progress is not None:
            progress.finish()
        print(
            f"      cooked {report.mesh_count} mesh(es), {report.texture_count} texture(s), "
            f"{report.node_count} node(s), {report.script_count} script(s) (server pack)"
        )
        print(f"[3/3] done in {time.monotonic() - step_start:.1f}s")

    print(f"Pipeline complete in {time.monotonic() - pipeline_start:.1f}s: client pack {client_output_pack}, server pack {server_output_pack}")
    return 0


if __name__ == "__main__":
    sys.exit(main())
