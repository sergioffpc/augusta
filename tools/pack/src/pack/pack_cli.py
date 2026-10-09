"""Pack inspection commands.

`augusta-inspect` (header, index and trailer) and `augusta-verify` (hash +
signature check), both ADR-0031 container-level and independent of the USD
stack. `augusta-inspect` reads a Match capture (ADR-0050) too, told from a
pack by its magic. A pack argument is an ordinary path - relative to the
current directory or absolute, like any file argument, never resolved against
an assets root; the `.pack` extension is optional.
"""

import argparse
from pathlib import Path
import sys

from pack import capture
from pack import pack
from pack.assets_root import default_assets_root
from pack.keys import read_public_key
from pack.reader import PackError
from pack.reader import read_pack
from pack.reader import TRAILER_SIZE
from pack.reader import verify_pack

_PACK_EXTENSION = ".pack"


def _resolve_pack(pack_arg: Path) -> Path:
    """Returns pack_arg as given, or with .pack appended if that exists.

    Raises:
        FileNotFoundError: neither exists.
    """
    for candidate in (
        pack_arg,
        pack_arg.with_name(pack_arg.name + _PACK_EXTENSION),
    ):
        if candidate.is_file():
            return candidate
    raise FileNotFoundError(
        f"Pack not found: {pack_arg} (also tried with {_PACK_EXTENSION})"
    )


def _add_pack_argument(parser: argparse.ArgumentParser) -> None:
    parser.add_argument(
        "pack",
        type=Path,
        help="Pack file - relative to the current directory or absolute. The "
        ".pack extension is optional.",
    )


def _format_size(size: int) -> str:
    return f"{size:,}"


def _format_entry_count(count: int) -> str:
    return f"{count} {'entry' if count == 1 else 'entries'}"


def _inspect_capture(path: Path) -> int:
    """Prints the Match capture at path; returns the exit code."""
    try:
        read = capture.read_capture(path.read_bytes())
    except capture.CaptureError as error:
        print(f"{path}: {error}", file=sys.stderr)
        return 1
    print(f"{path}: {_format_size(path.stat().st_size)} bytes")
    print()
    for line in capture.format_capture(read):
        print(line)
    return 0


def inspect_main(argv: list[str] | None = None) -> int:
    """`augusta-inspect`; returns the process's exit code."""
    parser = argparse.ArgumentParser(
        description="Shows a pack's container: the header fields, the index "
        "(one line per entry: type, offset, size in bytes, path) and the "
        "trailer (BLAKE3 hash and Ed25519 signature). Reads only those "
        "sections; it does not verify the hash or signature (see "
        "augusta-verify). Given a Match capture instead, shows its header, "
        "its length and one line per player."
    )
    _add_pack_argument(parser)
    args = parser.parse_args(argv)

    try:
        pack_path = _resolve_pack(args.pack)
        with pack_path.open("rb") as file:
            is_capture = capture.is_capture(file.read(len(capture.MAGIC)))
        if is_capture:
            return _inspect_capture(pack_path)
        info = read_pack(pack_path)
    except (FileNotFoundError, PackError) as error:
        print(error, file=sys.stderr)
        return 1

    trailer_offset = info.file_size - TRAILER_SIZE
    index_size = trailer_offset - info.index_offset
    print(f"{pack_path}: {_format_size(info.file_size)} bytes")

    print()
    print(f"Header (offset 0, {_format_size(pack.HEADER_SIZE)} bytes)")
    print(f"  magic         {pack.MAGIC.decode('ascii')}")
    print(f"  version       {info.version}")
    print(f"  data offset   {_format_size(info.data_offset)}")
    print(f"  index offset  {_format_size(info.index_offset)}")
    print(f"  index count   {_format_size(len(info.entries))}")
    client_pack = (
        info.client_pack_hash.hex()
        if info.client_pack_hash is not None
        else "none"
    )
    print(f"  client pack   {client_pack}")

    print()
    print(
        f"Data (offset {_format_size(info.data_offset)}, "
        f"{_format_size(info.index_offset - info.data_offset)} bytes)"
    )

    print()
    print(
        f"Index (offset {_format_size(info.index_offset)}, "
        f"{_format_size(index_size)} bytes, "
        f"{_format_entry_count(len(info.entries))})"
    )
    type_width = max(
        (len(entry.type_name) for entry in info.entries), default=0
    )
    offset_width = max(
        (len(_format_size(entry.offset)) for entry in info.entries), default=0
    )
    size_width = max(
        (len(_format_size(entry.size)) for entry in info.entries), default=0
    )
    for entry in info.entries:
        print(
            f"  {entry.type_name:<{type_width}}  "
            f"{_format_size(entry.offset):>{offset_width}}  "
            f"{_format_size(entry.size):>{size_width}}  {entry.path}"
        )

    print()
    print(
        f"Trailer (offset {_format_size(trailer_offset)}, {TRAILER_SIZE} "
        f"bytes) - not verified, see augusta-verify"
    )
    print(f"  BLAKE3 hash   {info.hash.hex()}")
    print(f"  signature     {info.signature.hex()}")
    return 0


def verify_main(argv: list[str] | None = None) -> int:
    """`augusta-verify`; returns the process's exit code."""
    parser = argparse.ArgumentParser(
        description="Verifies a pack: recomputes its BLAKE3 hash and checks "
        "the trailer's Ed25519 signature against a public key, then checks the "
        "header and index are well-formed. Exit status is 0 if the pack "
        "verifies, 1 otherwise."
    )
    _add_pack_argument(parser)
    parser.add_argument(
        "--assets-root",
        type=Path,
        default=default_assets_root(),
        help="Hermetic environment root, for the --public-key default only "
        "(default: inferred from this interpreter's own venv).",
    )
    parser.add_argument(
        "--public-key",
        type=Path,
        default=None,
        help="Default: <assets-root>/keys/signing.pub",
    )
    args = parser.parse_args(argv)

    public_key_path = (
        args.public_key or args.assets_root / "keys" / "signing.pub"
    )
    try:
        pack_path = _resolve_pack(args.pack)
        public_key = read_public_key(public_key_path)
        info = verify_pack(pack_path, public_key)
    except (FileNotFoundError, ValueError) as error:
        # PackError and read_public_key's size error are both ValueErrors.
        print(error, file=sys.stderr)
        return 1

    print(f"OK: {pack_path}")
    print(
        f"  {_format_entry_count(len(info.entries))}, "
        f"{_format_size(info.file_size)} bytes"
    )
    print(f"  BLAKE3 {info.hash.hex()}")
    print(f"  signed by {public_key_path}")
    return 0
