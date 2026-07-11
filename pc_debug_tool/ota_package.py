# -*- coding: utf-8 -*-
from __future__ import annotations

import hashlib
import json
import struct
import time
from dataclasses import dataclass
from pathlib import Path
from typing import Any


PACKAGE_MAGIC = b"GLIMMER-OTA-PKG1\n"
PACKAGE_EXTENSIONS = {".glimmerota", ".ota"}
PRODUCT_ID = "glimmer-b85"
SIGNATURE_ALGORITHM = "RSA-2048-SHA256"
SHA256_DIGESTINFO_PREFIX = bytes.fromhex("3031300d060960864801650304020105000420")

# Public key for OTA package verification. Keep the matching private key outside
# the repository; production builds should replace this key before release.
PUBLIC_KEY_N_HEX = (
    "ae7c169d271150fda14521c5205e3c518f5a38e27ce8d27ee978173cbd3fedecf0d4968754cb9b08f4d7e5249721f412"
    "4150d9a5dde7cbfd6d8a8a269daf37cdef983fcde2b268de82534172b71bf573f609f91778b800907f1641aca12bc263"
    "01a8109a2f642391e27ff1c450ca2487511a47a5945bb1c1bbc8574b3a527c72f7244c97c00a727cf67402007a9e327f"
    "ba3d44d37875fb1aea64b36df2a4739b26ebe707f91654fb911ea2f980e1e92a12b3787cb3d91dbf5f0e9d3e202d49be"
    "8cc93facd7c2d99ec01e5797facfcb5719b115cb820cf9a3918a1d174e4fcebf5938a3900977828d45d4ba659e8f34b0"
    "a5b92df6f394a7afe8b0a71f8ce35b65"
)
PUBLIC_KEY_E = 65537


@dataclass(frozen=True)
class OtaImage:
    firmware: bytes
    signed: bool
    version_code: int
    manifest: dict[str, Any]
    source_path: Path


def canonical_manifest_bytes(manifest: dict[str, Any]) -> bytes:
    return json.dumps(manifest, sort_keys=True, separators=(",", ":"), ensure_ascii=True).encode("utf-8")


def build_manifest(firmware: bytes, version_code: int, product: str = PRODUCT_ID) -> dict[str, Any]:
    if version_code <= 0:
        raise ValueError("version_code must be positive")
    return {
        "format": 1,
        "product": product,
        "version_code": int(version_code),
        "firmware_size": len(firmware),
        "firmware_sha256": hashlib.sha256(firmware).hexdigest(),
        "signature": SIGNATURE_ALGORITHM,
        "created_at": int(time.time()),
    }


def build_package(firmware: bytes, manifest: dict[str, Any], signature: bytes) -> bytes:
    manifest_bytes = canonical_manifest_bytes(manifest)
    if len(manifest_bytes) > 0xFFFF_FFFF:
        raise ValueError("manifest too large")
    if len(signature) > 0xFFFF:
        raise ValueError("signature too large")
    return (
        PACKAGE_MAGIC
        + struct.pack("<I", len(manifest_bytes))
        + manifest_bytes
        + struct.pack("<H", len(signature))
        + signature
        + firmware
    )


def _verify_rsa_pkcs1_v1_5_sha256(message: bytes, signature: bytes) -> bool:
    modulus = int(PUBLIC_KEY_N_HEX, 16)
    exponent = PUBLIC_KEY_E
    key_len = (modulus.bit_length() + 7) // 8
    if len(signature) != key_len:
        return False

    encoded = pow(int.from_bytes(signature, "big"), exponent, modulus).to_bytes(key_len, "big")
    digest = hashlib.sha256(message).digest()
    expected_tail = SHA256_DIGESTINFO_PREFIX + digest
    padding_len = key_len - len(expected_tail) - 3
    if padding_len < 8:
        return False
    expected = b"\x00\x01" + (b"\xff" * padding_len) + b"\x00" + expected_tail
    return encoded == expected


def _parse_package(data: bytes) -> tuple[dict[str, Any], bytes, bytes, bytes]:
    if not data.startswith(PACKAGE_MAGIC):
        raise ValueError("not a Glimmer OTA package")
    offset = len(PACKAGE_MAGIC)
    if len(data) < offset + 4:
        raise ValueError("OTA package truncated before manifest length")
    manifest_len = struct.unpack_from("<I", data, offset)[0]
    offset += 4
    if manifest_len == 0 or len(data) < offset + manifest_len + 2:
        raise ValueError("OTA package manifest truncated")
    manifest_bytes = data[offset:offset + manifest_len]
    offset += manifest_len
    signature_len = struct.unpack_from("<H", data, offset)[0]
    offset += 2
    if signature_len == 0 or len(data) < offset + signature_len:
        raise ValueError("OTA package signature truncated")
    signature = data[offset:offset + signature_len]
    offset += signature_len
    firmware = data[offset:]
    try:
        manifest = json.loads(manifest_bytes.decode("utf-8"))
    except (UnicodeDecodeError, json.JSONDecodeError) as exc:
        raise ValueError(f"invalid OTA package manifest: {exc}") from exc
    return manifest, manifest_bytes, firmware, signature


def is_package_bytes(data: bytes) -> bool:
    return data.startswith(PACKAGE_MAGIC)


def verify_package(data: bytes, max_size: int, current_version_code: int = 0, allow_downgrade: bool = False) -> OtaImage:
    manifest, manifest_bytes, firmware, signature = _parse_package(data)
    product = manifest.get("product")
    version_code = int(manifest.get("version_code", 0))
    firmware_size = int(manifest.get("firmware_size", -1))
    firmware_sha256 = str(manifest.get("firmware_sha256", ""))
    signature_alg = manifest.get("signature")

    if manifest.get("format") != 1:
        raise ValueError("unsupported OTA package format")
    if product != PRODUCT_ID:
        raise ValueError(f"unsupported OTA product: {product}")
    if signature_alg != SIGNATURE_ALGORITHM:
        raise ValueError(f"unsupported OTA signature algorithm: {signature_alg}")
    if not _verify_rsa_pkcs1_v1_5_sha256(manifest_bytes, signature):
        raise ValueError("OTA package signature verification failed")
    if firmware_size != len(firmware):
        raise ValueError(f"OTA firmware size mismatch: manifest={firmware_size}, actual={len(firmware)}")
    if hashlib.sha256(firmware).hexdigest().lower() != firmware_sha256.lower():
        raise ValueError("OTA firmware sha256 mismatch")
    if len(firmware) > max_size:
        raise ValueError(f"OTA firmware {len(firmware)} bytes exceeds max {max_size} bytes")
    if version_code <= 0:
        raise ValueError("OTA package version_code must be positive")
    if current_version_code > 0 and version_code <= current_version_code and not allow_downgrade:
        raise ValueError(f"OTA downgrade blocked: package={version_code}, device={current_version_code}")
    return OtaImage(
        firmware=firmware,
        signed=True,
        version_code=version_code,
        manifest=manifest,
        source_path=Path(),
    )


def load_ota_image(
    path: str | Path,
    max_size: int,
    current_version_code: int = 0,
    allow_unsigned_bin: bool = False,
    allow_downgrade: bool = False,
) -> OtaImage:
    source = Path(path)
    data = source.read_bytes()
    if is_package_bytes(data):
        image = verify_package(data, max_size, current_version_code, allow_downgrade)
        return OtaImage(image.firmware, image.signed, image.version_code, image.manifest, source)
    if not allow_unsigned_bin:
        raise ValueError("raw .bin OTA is disabled; use a signed .glimmerota package or enable unsigned dev mode")
    if len(data) > max_size:
        raise ValueError(f"firmware {len(data)} bytes exceeds max {max_size} bytes")
    return OtaImage(
        firmware=data,
        signed=False,
        version_code=0,
        manifest={},
        source_path=source,
    )
