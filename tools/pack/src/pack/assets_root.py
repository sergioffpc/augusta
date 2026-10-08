"""Assets-root layout shared by the command-line entry points.

Kept apart from cli.py so the lightweight pack commands don't import the USD
stack just to locate <assets-root>.
"""

from pathlib import Path
import sys


def default_assets_root() -> Path:
    """The assets root of the venv this interpreter runs from.

    sys.executable is <assets-root>/python/pack/Scripts/python.exe inside the
    uv tool venv tools/pack/scripts/bootstrap.sh installs this project into.
    """
    return Path(sys.executable).resolve().parents[3]
