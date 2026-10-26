#!/usr/bin/env python3
"""Checks every environment's packs are of the format the engine reads.

The engine refuses a pack written in any pack format version but its own
(kFormatVersion in src/modules/assets/pack_format.h, ADR-0031), so a server
whose pack is of another format exits at startup. Each server entry in clusters/
that names a packVersion records next to it the packFormat that pack was cooked
with, as augusta-publish prints it. This fails on an entry with no packFormat,
or one other than the engine's: a change to the format moves every
environment to packs re-cooked in it (docs/runbooks/cut-release.md).

  uv run --no-project --with pyyaml==6.0.3 scripts/check-pack-formats.py
"""

import pathlib
import re
import sys

import yaml

_ROOT = pathlib.Path(__file__).resolve().parent.parent
_HEADER = pathlib.PurePosixPath("src/modules/assets/pack_format.h")
_FORMAT_VERSION = re.compile(
    r"constexpr\s+\S+\s+kFormatVersion\s*=\s*(\d+)\s*;"
)


def engine_format(root):
    """The pack format version the engine at root reads and writes.

    Raises:
        ValueError: pack_format.h does not define kFormatVersion exactly once.
    """
    source = (root / _HEADER).read_text(encoding="utf-8")
    versions = _FORMAT_VERSION.findall(source)
    if len(versions) != 1:
        raise ValueError(
            f"{_HEADER} defines kFormatVersion {len(versions)} times, not once"
        )
    return int(versions[0])


def _servers(node, path=""):
    """Yields (path, mapping) for every mapping under node naming a pack."""
    if isinstance(node, dict):
        if "packVersion" in node:
            yield path, node
        for key, value in node.items():
            yield from _servers(value, f"{path}.{key}" if path else str(key))
    elif isinstance(node, list):
        for i, value in enumerate(node):
            yield from _servers(value, f"{path}[{i}]")


def mismatches(documents, expected):
    """What is wrong with the packFormat of each pack documents name.

    Args:
        documents: the YAML documents of one file, parsed.
        expected: the engine's pack format version.

    Returns:
        One message per server entry whose packFormat is missing, not an
        integer, or not expected.
    """
    errors = []
    for document in documents:
        for path, server in _servers(document):
            pack_format = server.get("packFormat")
            if pack_format is None:
                errors.append(
                    f"{path} names a packVersion but no packFormat: add the "
                    f"packFormat augusta-publish printed for it"
                )
            # Not isinstance: YAML's true and false are bools, which are ints
            # in Python, and a pack format is never one.
            elif type(pack_format) is not int:
                errors.append(
                    f"{path}.packFormat must be an integer, not {pack_format!r}"
                )
            elif pack_format != expected:
                errors.append(
                    f"{path}.packFormat is {pack_format}, but the engine reads "
                    f"pack format {expected} (kFormatVersion in {_HEADER}): "
                    f"cook and publish the scenario's packs in format "
                    f"{expected}, and move packVersion and packFormat to them"
                )
    return errors


def check(root):
    """Every mismatch in root's clusters/, each prefixed with its file."""
    expected = engine_format(root)
    errors = []
    for path in sorted((root / "clusters").rglob("*.y*ml")):
        with path.open(encoding="utf-8") as f:
            documents = list(yaml.safe_load_all(f))
        relative = path.relative_to(root).as_posix()
        errors.extend(
            f"{relative}: {error}" for error in mismatches(documents, expected)
        )
    return errors


def main():
    """Prints each mismatch; returns 1 if there is any."""
    errors = check(_ROOT)
    for error in errors:
        print(error, file=sys.stderr)
    return 1 if errors else 0


if __name__ == "__main__":
    sys.exit(main())
