"""Ed25519 signing keypair generation/file I/O (ADR-0018) - raw fixed-size
binary files, matching augusta::assets::Ed25519PublicKey/Ed25519PrivateKey's
on-disk layout (32 and 64 bytes respectively, no framing). Keys are
generated via pynacl's libsodium bindings (nacl.bindings.crypto_sign_keypair)
rather than augusta_assets' own GenerateEd25519KeyPair - see pack.py's own
comment on why this project doesn't link that module. Cross-checked to
produce byte-identical, cross-verifiable keys/signatures against the real
libsodium C library.
"""

import argparse
import sys
from pathlib import Path

import nacl.bindings

PUBLIC_KEY_SIZE = 32
PRIVATE_KEY_SIZE = 64


def generate_keypair() -> tuple[bytes, bytes]:
    """Returns (public_key, private_key): 32 and 64 raw bytes."""
    public_key, private_key = nacl.bindings.crypto_sign_keypair()
    return public_key, private_key


def read_private_key(path: Path) -> bytes:
    data = path.read_bytes()
    if len(data) != PRIVATE_KEY_SIZE:
        raise ValueError(f"{path}: expected a {PRIVATE_KEY_SIZE}-byte Ed25519 private key, got {len(data)} bytes")
    return data


def write_keypair(prefix: Path) -> tuple[Path, Path]:
    """Generates a new keypair, writing it to <prefix>.pub / <prefix>.key. Returns (pub_path, key_path)."""
    public_key, private_key = generate_keypair()
    pub_path = prefix.with_name(prefix.name + ".pub")
    key_path = prefix.with_name(prefix.name + ".key")
    pub_path.write_bytes(public_key)
    key_path.write_bytes(private_key)
    return pub_path, key_path


def main(argv: list[str] | None = None) -> int:
    parser = argparse.ArgumentParser(description="Generates a new Ed25519 signing keypair (ADR-0018).")
    parser.add_argument("prefix", type=Path, help="Writes <prefix>.pub and <prefix>.key.")
    args = parser.parse_args(argv)

    pub_path, key_path = write_keypair(args.prefix)
    print(f"wrote {pub_path} and {key_path}")
    return 0


if __name__ == "__main__":
    sys.exit(main())
