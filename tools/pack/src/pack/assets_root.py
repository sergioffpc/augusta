"""Assets-root layout shared by the command-line entry points.

Kept apart from cli.py so the lightweight pack commands don't import the USD
stack just to locate <assets-root>.
"""

from pathlib import Path
import sys


def default_assets_root() -> Path:
    """The assets root of the venv this interpreter runs from.

    sys.prefix is <assets-root>/python/pack, the uv tool venv
    tools/pack/scripts/bootstrap.sh installs this project into. Not
    sys.executable: on Linux the venv's bin/python links to uv's own
    interpreter, outside the assets root.
    """
    return Path(sys.prefix).parents[1]
