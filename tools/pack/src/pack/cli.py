"""Augusta asset-cooking pipeline CLI (ADR-0030): usd-optimize (ADR-0015,
Python API - see optimize.py) -> usd-validation-nvidia (ADR-0015, also
called via its own Python API - see validate.py) -> cook_stage (bake to
signed client/server packs, ADR-0031/ADR-0032 - see cook.py). cook_stage is
pure Python too: it walks the USD stage via pxr directly and calls the
small native _meshoptimizer/_textconv bindings only for the two pieces with
no Python equivalent - no subprocess/CLI binary anywhere in this pipeline.
A validation failure aborts before cooking, so no pack is written for a
stage that didn't pass cleanup/validation, unless --skip-validation is given.

This project is installed into the hermetic environment tools/asset-
pipeline/scripts/bootstrap-windows.ps1 builds (--assets-root/python),
so --assets-root defaults to the root of the venv this interpreter is
already running from. The stage argument is always a path relative to
--assets-root/authoring, and its packs are written to the same relative
location under --assets-root/packs; the signing key is expected at
--assets-root/keys.
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
from pack.validate import ValidationError, validate_stage


_USD_EXTENSIONS = (".usd", ".usda", ".usdc", ".usdz")


class StageNotFoundError(Exception):
    """Raised when stage doesn't resolve to exactly one authored USD file."""


def _resolve_stage(authoring_dir: Path, stage: Path) -> Path | None:
    """Returns authoring_dir/stage, or None if stage isn't a plain relative
    path staying inside authoring_dir (absolute, or escaping via '..').

    The USD extension is optional: a stage without one is looked up as
    <stage>.usd/.usda/.usdc/.usdz. Raises StageNotFoundError if none or more
    than one of those exists.
    """
    if stage.is_absolute() or ".." in stage.parts:
        return None

    path = authoring_dir / stage
    if path.suffix.lower() in _USD_EXTENSIONS:
        return path

    candidates = [path.with_name(path.name + extension) for extension in _USD_EXTENSIONS]
    found = [candidate for candidate in candidates if candidate.is_file()]
    if not found:
        tried = ", ".join(_USD_EXTENSIONS)
        raise StageNotFoundError(f"Stage not found: {path} (tried extensions {tried})")
    if len(found) > 1:
        names = ", ".join(candidate.name for candidate in found)
        raise StageNotFoundError(f"Stage name is ambiguous, several files match: {names} - pass the extension explicitly.")
    return found[0]


def main(argv: list[str] | None = None) -> int:
    parser = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    parser.add_argument(
        "stage",
        type=Path,
        help="Raw authored USD stage (e.g. exported from USD Composer), relative to <assets-root>/authoring. "
        "The extension is optional: 'Stage' finds Stage.usd, .usda, .usdc or .usdz.",
    )
    parser.add_argument(
        "--assets-root",
        type=Path,
        default=default_assets_root(),
        help="Hermetic environment root (default: inferred from this interpreter's own venv).",
    )
    parser.add_argument("--client-output-pack", type=Path, default=None, help="Default: <assets-root>/packs/<stage>.client.pack")
    parser.add_argument("--server-output-pack", type=Path, default=None, help="Default: <assets-root>/packs/<stage>.server.pack")
    parser.add_argument("--signing-key", type=Path, default=None, help="Default: <assets-root>/keys/augusta.key")
    parser.add_argument(
        "--skip-validation",
        action="store_true",
        help="Skip usd-validation-nvidia (e.g. third-party stages that fail its checks); usd-optimize and the cook still run.",
    )
    args = parser.parse_args(argv)

    assets_root: Path = args.assets_root
    authoring_dir = assets_root / "authoring"
    packs_dir = assets_root / "packs"

    try:
        stage_path = _resolve_stage(authoring_dir, args.stage)
    except StageNotFoundError as error:
        print(error, file=sys.stderr)
        return 1
    if stage_path is None:
        print(f"Stage must be a path relative to {authoring_dir} (no absolute paths or '..'): {args.stage}", file=sys.stderr)
        return 1
    stage_name = stage_path.stem
    pack_dir = packs_dir / args.stage.parent

    signing_key_path = args.signing_key or assets_root / "keys" / "augusta.key"
    client_output_pack = args.client_output_pack or pack_dir / f"{stage_name}.client.pack"
    server_output_pack = args.server_output_pack or pack_dir / f"{stage_name}.server.pack"

    for label, path in (
        ("Stage", stage_path),
        ("Signing key", signing_key_path),
    ):
        if not path.exists():
            print(f"{label} not found: {path} - run tools\\pack\\scripts\\bootstrap-windows.ps1 {assets_root} first.", file=sys.stderr)
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
                cleaned_stage, client_output_pack, server_output_pack, signing_key, on_prim=report_prim
            )
        except CookError as error:
            if progress is not None:
                progress.finish()
            print(str(error), file=sys.stderr)
            return 1
        if progress is not None:
            progress.finish()
        print(f"      cooked {report.mesh_count} mesh(es), {report.texture_count} texture(s), {report.node_count} node(s)")
        print(f"[3/3] done in {time.monotonic() - step_start:.1f}s")

    print(f"Pipeline complete in {time.monotonic() - pipeline_start:.1f}s: client pack {client_output_pack}, server pack {server_output_pack}")
    return 0


if __name__ == "__main__":
    sys.exit(main())
