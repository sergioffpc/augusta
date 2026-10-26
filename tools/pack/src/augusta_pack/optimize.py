"""usd-optimize stage cleanup (ADR-0015).

usd-optimize ships no CLI - per its own PyPI description it is "a standalone
C++ library with Python bindings... without installing Omniverse Kit",
exposing only a Python API (usd_optimize.core). This drives that API
directly instead.
"""

from pathlib import Path

from pxr import Usd
from pxr import UsdUtils
import usd_optimize.core as usd_optimize_core

# triangulateMeshes first: cook.py's own topology validation
# (_validate_triangle_topology, ADR-0030) rejects non-triangular faces
# outright rather than triangulating them itself - that's usd-optimize's
# job - so topology must be triangulated before dedup/flatten/
# removeSmallGeometry touch it.
OPERATIONS = [
    "triangulateMeshes",
    "deduplicateHierarchies",
    "flattenHierarchy",
    "removeSmallGeometry",
]


class OptimizeError(RuntimeError):
    """Raised when a usd-optimize operation reports failure."""


def optimize_stage(input_path: Path, output_path: Path) -> None:
    """Cleans up the stage at input_path and exports it to output_path."""
    stage = Usd.Stage.Open(str(input_path))
    if not stage:
        raise OptimizeError(f"could not open stage: {input_path}")

    context = usd_optimize_core.ExecutionContext()
    context.set_stage(stage)
    core = usd_optimize_core.UsdOptimizeCore.getInstance()
    results = core.executeConfig(
        context, [{"operation": op} for op in OPERATIONS]
    )

    for op, (success, error, _output) in zip(OPERATIONS, results, strict=True):
        if not success:
            raise OptimizeError(f"{op} failed: {error}")

    # Exported elsewhere, a path relative to the authored stage would name
    # another file, such as a character's Clips under authoring/animations/
    # (ADR-0066): each is anchored to the authored stage first.
    root_layer = stage.GetRootLayer()
    UsdUtils.ModifyAssetPaths(
        root_layer,
        lambda path: root_layer.ComputeAbsolutePath(path) if path else path,
    )
    root_layer.Export(str(output_path))
