"""Assets-root layout shared by the command-line entry points. Kept apart from
cli.py so the lightweight pack commands don't import the USD stack just to
locate <assets-root>.
"""

import sys
from pathlib import Path


def default_assets_root() -> Path:
    # sys.executable is <assets-root>/python/pack/Scripts/python.exe
    # inside the uv tool venv bootstrap-windows.ps1 installs this project into.
    return Path(sys.executable).resolve().parents[3]
