# -*- coding: utf-8 -*-
from __future__ import annotations

import argparse
import base64
import hashlib
import os
import re
import sys
from pathlib import Path


ROOT = Path(__file__).resolve().parents[1]
sys.path.insert(0, str(ROOT / "pc_debug_tool"))

from ota_package import build_manifest, build_package, canonical_manifest_bytes  # noqa: E402


DEFAULT_BIN = ROOT / "tc_ble_multi_sdk" / "build" / "B85" / "pendant" / "pendant.bin"
DEFAULT_BUILD_INFO = ROOT / "tc_ble_multi_sdk" / "vendor" / "pendant" / "build_info" / "app_build_info.h"
SHA256_DIGESTINFO_PREFIX = bytes.fromhex("3031300d060960864801650304020105000420")


def read_version_code(path: Path = DEFAULT_BUILD_INFO) -> int:
    text = path.read_text(encoding="utf-8", errors="ignore")
    match = re.search(r"^\s*#define\s+APP_BUILD_VERSION_CODE\s+(\d+)\s*$", text, re.MULTILINE)
    if not match:
        raise ValueError(f"APP_BUILD_VERSION_CODE not found in {path}")
    return int(match.group(1))


def der_read_len(data: bytes, offset: int) -> tuple[int, int]:
    first = data[offset]
    offset += 1
    if first < 0x80:
        return first, offset
    count = first & 0x7F
    if count == 0 or count > 4 or offset + count > len(data):
        raise ValueError("invalid DER length")
    value = 0
    for _ in range(count):
        value = (value << 8) | data[offset]
        offset += 1
    return value, offset


def der_read_integer(data: bytes, offset: int) -> tuple[int, int]:
    if offset >= len(data) or data[offset] != 0x02:
        raise ValueError("invalid DER integer")
    length, offset = der_read_len(data, offset + 1)
    raw = data[offset:offset + length]
    if len(raw) != length:
        raise ValueError("truncated DER integer")
    return int.from_bytes(raw, "big", signed=False), offset + length


def load_pkcs1_private_key(path: Path) -> tuple[int, int]:
    text = path.read_text(encoding="ascii")
    begin = "-----BEGIN RSA PRIVATE KEY-----"
    end = "-----END RSA PRIVATE KEY-----"
    if begin not in text or end not in text:
        raise ValueError("fallback signer only supports PKCS#1 'BEGIN RSA PRIVATE KEY' PEM")
    body = text.split(begin, 1)[1].split(end, 1)[0]
    der = base64.b64decode("".join(body.split()))
    if not der or der[0] != 0x30:
        raise ValueError("invalid PKCS#1 private key")
    seq_len, offset = der_read_len(der, 1)
    end_offset = offset + seq_len
    version, offset = der_read_integer(der, offset)
    if version != 0:
        raise ValueError("unsupported RSA private key version")
    modulus, offset = der_read_integer(der, offset)
    _public_exponent, offset = der_read_integer(der, offset)
    private_exponent, offset = der_read_integer(der, offset)
    if offset >= end_offset:
        return modulus, private_exponent
    return modulus, private_exponent


def sign_manifest_fallback(private_key_path: Path, manifest_bytes: bytes) -> bytes:
    modulus, private_exponent = load_pkcs1_private_key(private_key_path)
    key_len = (modulus.bit_length() + 7) // 8
    digest = hashlib.sha256(manifest_bytes).digest()
    tail = SHA256_DIGESTINFO_PREFIX + digest
    padding_len = key_len - len(tail) - 3
    if padding_len < 8:
        raise ValueError("RSA key too small for SHA-256 signature")
    encoded = b"\x00\x01" + (b"\xff" * padding_len) + b"\x00" + tail
    signature = pow(int.from_bytes(encoded, "big"), private_exponent, modulus).to_bytes(key_len, "big")
    return signature


def sign_manifest(private_key_path: Path, manifest_bytes: bytes) -> bytes:
    try:
        from cryptography.hazmat.primitives import hashes, serialization
        from cryptography.hazmat.primitives.asymmetric import padding
    except ImportError:
        return sign_manifest_fallback(private_key_path, manifest_bytes)

    password = os.environ.get("GLIMMER_OTA_PRIVATE_KEY_PASSWORD")
    password_bytes = password.encode("utf-8") if password else None
    private_key = serialization.load_pem_private_key(private_key_path.read_bytes(), password=password_bytes)
    return private_key.sign(manifest_bytes, padding.PKCS1v15(), hashes.SHA256())


def main() -> int:
    parser = argparse.ArgumentParser(description="Build a signed Glimmer OTA package.")
    parser.add_argument("--bin", default=str(DEFAULT_BIN), help="raw Telink firmware .bin")
    parser.add_argument("--out", help="output .glimmerota path")
    parser.add_argument("--version-code", type=int, default=0, help="numeric anti-rollback version")
    parser.add_argument(
        "--private-key",
        default=os.environ.get("GLIMMER_OTA_PRIVATE_KEY", ""),
        help="RSA private key PEM path, or GLIMMER_OTA_PRIVATE_KEY env var",
    )
    args = parser.parse_args()

    firmware_path = Path(args.bin)
    private_key_path = Path(args.private_key) if args.private_key else None
    if not private_key_path:
        raise SystemExit("missing --private-key or GLIMMER_OTA_PRIVATE_KEY")
    if not private_key_path.exists():
        raise SystemExit(f"private key not found: {private_key_path}")

    version_code = args.version_code or read_version_code()
    firmware = firmware_path.read_bytes()
    manifest = build_manifest(firmware, version_code)
    manifest_bytes = canonical_manifest_bytes(manifest)
    signature = sign_manifest(private_key_path, manifest_bytes)
    package = build_package(firmware, manifest, signature)

    out_path = Path(args.out) if args.out else firmware_path.with_suffix(".glimmerota")
    out_path.write_bytes(package)
    print(f"wrote {out_path}")
    print(f"version_code={version_code} firmware={len(firmware)} bytes package={len(package)} bytes")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
