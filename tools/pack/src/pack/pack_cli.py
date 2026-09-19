"""Pack inspection commands: `augustap-inspect` (header, index and trailer) and
`augustap-verify` (hash + signature check), both ADR-0031 container-level
and independent of the USD stack. A pack argument is an absolute path, or
relative to <assets-root>/packs; the `.pack` extension is optional.
"""

import argparse
import sys
from pathlib import Path

from pack import pack
from pack.assets_root import default_assets_root
from pack.keys import read_public_key
from pack.reader import TRAILER_SIZE, PackError, read_pack, verify_pack

_PACK_EXTENSION = ".pack"


def _resolve_pack(packs_dir: Path, pack_arg: Path) -> Path:
    """Returns pack_arg (absolute) or packs_dir/pack_arg (relative), with the
    .pack extension appended if that's what exists. Raises FileNotFoundError.
    """
    path = pack_arg if pack_arg.is_absolute() else packs_dir / pack_arg
    for candidate in (path, path.with_name(path.name + _PACK_EXTENSION)):
        if candidate.is_file():
            return candidate
    raise FileNotFoundError(f"Pack not found: {path} (also tried with {_PACK_EXTENSION})")


def _add_common_arguments(parser: argparse.ArgumentParser) -> None:
    parser.add_argument(
        "pack",
        type=Path,
        help="Pack file: an absolute path, or relative to <assets-root>/packs. The .pack extension is optional.",
    )
    parser.add_argument(
        "--assets-root",
        type=Path,
        default=default_assets_root(),
        help="Hermetic environment root (default: inferred from this interpreter's own venv).",
    )


def _format_size(size: int) -> str:
    return f"{size:,}"


def _format_entry_count(count: int) -> str:
    return f"{count} {'entry' if count == 1 else 'entries'}"


def inspect_main(argv: list[str] | None = None) -> int:
    parser = argparse.ArgumentParser(
        description="Shows a pack's container: the header fields, the index (one line per entry: type, offset, "
        "size in bytes, path) and the trailer (BLAKE3 hash and Ed25519 signature). Reads only those sections; "
        "it does not verify the hash or signature (see augustap-verify)."
    )
    _add_common_arguments(parser)
    args = parser.parse_args(argv)

    try:
        pack_path = _resolve_pack(args.assets_root / "packs", args.pack)
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

    print()
    print(f"Data (offset {_format_size(info.data_offset)}, {_format_size(info.index_offset - info.data_offset)} bytes)")

    print()
    print(f"Index (offset {_format_size(info.index_offset)}, {_format_size(index_size)} bytes, {_format_entry_count(len(info.entries))})")
    type_width = max((len(entry.type_name) for entry in info.entries), default=0)
    offset_width = max((len(_format_size(entry.offset)) for entry in info.entries), default=0)
    size_width = max((len(_format_size(entry.size)) for entry in info.entries), default=0)
    for entry in info.entries:
        print(
            f"  {entry.type_name:<{type_width}}  {_format_size(entry.offset):>{offset_width}}  "
            f"{_format_size(entry.size):>{size_width}}  {entry.path}"
        )

    print()
    print(f"Trailer (offset {_format_size(trailer_offset)}, {TRAILER_SIZE} bytes) - not verified, see augustap-verify")
    print(f"  BLAKE3 hash   {info.hash.hex()}")
    print(f"  signature     {info.signature.hex()}")
    return 0


def verify_main(argv: list[str] | None = None) -> int:
    parser = argparse.ArgumentParser(
        description="Verifies a pack: recomputes its BLAKE3 hash and checks the trailer's Ed25519 signature "
        "against a public key, then checks the header and index are well-formed. "
        "Exit status is 0 if the pack verifies, 1 otherwise."
    )
    _add_common_arguments(parser)
    parser.add_argument("--public-key", type=Path, default=None, help="Default: <assets-root>/keys/augusta.pub")
    args = parser.parse_args(argv)

    public_key_path = args.public_key or args.assets_root / "keys" / "augusta.pub"
    try:
        pack_path = _resolve_pack(args.assets_root / "packs", args.pack)
        public_key = read_public_key(public_key_path)
        info = verify_pack(pack_path, public_key)
    except (FileNotFoundError, ValueError) as error:
        # PackError and read_public_key's size error are both ValueErrors.
        print(error, file=sys.stderr)
        return 1

    print(f"OK: {pack_path}")
    print(f"  {_format_entry_count(len(info.entries))}, {_format_size(info.file_size)} bytes")
    print(f"  BLAKE3 {info.hash.hex()}")
    print(f"  signed by {public_key_path}")
    return 0
