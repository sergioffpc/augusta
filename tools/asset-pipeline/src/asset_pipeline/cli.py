"""Augusta asset-cooking pipeline CLI (ADR-0030): usd-optimize (ADR-0015,
Python API - see optimize.py) -> usd-validation-nvidia (ADR-0015, also
called via its own Python API - see validate.py) -> cook_stage (bake to
signed client/server packs, ADR-0031/ADR-0032 - see cook.py). cook_stage is
pure Python too: it walks the USD stage via pxr directly and calls the
small native _meshoptimizer/_textconv bindings only for the two pieces with
no Python equivalent - no subprocess/CLI binary anywhere in this pipeline.
A validation failure aborts before cooking, so no pack is written for a
stage that didn't pass cleanup/validation.

This project is installed into the hermetic environment scripts/bootstrap-
asset-pipeline.ps1 builds (--assets-root/python), so --assets-root defaults
to the root of the venv this interpreter is already running from - the
signing key is expected at --assets-root/keys.
"""

import argparse
import sys
import tempfile
import uuid
from pathlib import Path

from asset_pipeline.cook import CookError, cook_stage
from asset_pipeline.keys import read_private_key
from asset_pipeline.optimize import OptimizeError, optimize_stage
from asset_pipeline.validate import ValidationError, validate_stage


def _default_assets_root() -> Path:
    # sys.executable is <assets-root>/python/Scripts/python.exe inside the
    # hermetic venv this project is installed into.
    return Path(sys.executable).resolve().parents[2]


def main(argv: list[str] | None = None) -> int:
    parser = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    parser.add_argument("stage", type=Path, help="Raw authored USD stage (e.g. exported from USD Composer).")
    parser.add_argument(
        "--assets-root",
        type=Path,
        default=_default_assets_root(),
        help="Hermetic environment root (default: inferred from this interpreter's own venv).",
    )
    parser.add_argument("--client-output-pack", type=Path, default=None, help="Default: <assets-root>/packs/<stage>.client.pack")
    parser.add_argument("--server-output-pack", type=Path, default=None, help="Default: <assets-root>/packs/<stage>.server.pack")
    parser.add_argument("--signing-key", type=Path, default=None, help="Default: <assets-root>/keys/augusta.key")
    args = parser.parse_args(argv)

    assets_root: Path = args.assets_root
    packs_dir = assets_root / "packs"
    stage_name = args.stage.stem

    signing_key_path = args.signing_key or assets_root / "keys" / "augusta.key"
    client_output_pack = args.client_output_pack or packs_dir / f"{stage_name}.client.pack"
    server_output_pack = args.server_output_pack or packs_dir / f"{stage_name}.server.pack"

    for label, path in (
        ("Stage", args.stage),
        ("Signing key", signing_key_path),
    ):
        if not path.exists():
            print(f"{label} not found: {path} - run tools\\asset-pipeline\\scripts\\bootstrap-asset-pipeline.ps1 {assets_root} first.", file=sys.stderr)
            return 1

    signing_key = read_private_key(signing_key_path)
    packs_dir.mkdir(parents=True, exist_ok=True)

    with tempfile.TemporaryDirectory() as tmp_dir:
        cleaned_stage = Path(tmp_dir) / f"{stage_name}-cleaned-{uuid.uuid4()}.usda"

        print(f"Running usd-optimize on {args.stage}...")
        try:
            optimize_stage(args.stage, cleaned_stage)
        except OptimizeError as error:
            print(f"usd-optimize failed: {error} - stage not cleaned, cook aborted.", file=sys.stderr)
            return 1

        print(f"Running usd-validation-nvidia on {cleaned_stage}...")
        try:
            validate_stage(cleaned_stage)
        except ValidationError as error:
            print(f"{error} - cook aborted, no pack written.", file=sys.stderr)
            return 1

        print(f"Cooking {cleaned_stage} into {client_output_pack} (client) / {server_output_pack} (server)...")
        try:
            report = cook_stage(cleaned_stage, client_output_pack, server_output_pack, signing_key)
        except CookError as error:
            print(str(error), file=sys.stderr)
            return 1
        print(f"Cooked {report.mesh_count} mesh(es), {report.texture_count} texture(s), {report.node_count} node(s).")

    print(f"Pipeline complete: client pack {client_output_pack}, server pack {server_output_pack}")
    return 0


if __name__ == "__main__":
    sys.exit(main())
