"""Where the command-line entry points find <assets-root> by default.

The uv tool venv tools/pack/scripts/bootstrap.sh builds is
<assets-root>/python/pack, on every platform.
"""

import sys

from pack.assets_root import default_assets_root


def test_the_assets_root_is_two_levels_above_the_venv(tmp_path, monkeypatch):
    # Not found through the interpreter, which on Linux the venv links to
    # uv's own, outside the assets root.
    assets_root = tmp_path / "assets"
    monkeypatch.setattr(sys, "prefix", str(assets_root / "python" / "pack"))
    monkeypatch.setattr(sys, "executable", str(tmp_path / "uv" / "python"))

    assert default_assets_root() == assets_root
