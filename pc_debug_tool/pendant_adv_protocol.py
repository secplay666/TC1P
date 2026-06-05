# -*- coding: utf-8 -*-
from __future__ import annotations

from dataclasses import dataclass
import struct
import zlib

import pendant_protocol as host


APP_ADV_COMPANY_ID_DEV = 0xFFFF
APP_ADV_MAGIC_LO = 0x44
APP_ADV_MAGIC_HI = 0x50
APP_ADV_PROTOCOL_VERSION = 0x01
APP_ADV_HEADER_LEN = 50
APP_ADV_PAYLOAD_MAX_LEN = 160
APP_ADV_FRAME_MAX_LEN = 240

ADV_FRAME_BEACON = 0x01
ADV_FRAME_DATA = 0x10
ADV_FRAME_ACK = 0x11
ADV_FRAME_CTRL = 0x12
ADV_FRAME_ERROR = 0x13

HOST_ADV_MAGIC_LO = 0x48
HOST_ADV_MAGIC_HI = 0x41
HOST_ADV_VERSION = 0x01
HOST_ADV_HEADER_LEN = 14
HOST_ADV_CHUNK_MAX_LEN = APP_ADV_PAYLOAD_MAX_LEN - HOST_ADV_HEADER_LEN

ZERO_EID = bytes(16)
DEFAULT_HOST_EID = b"PENDANT-HOST-001"


@dataclass
class AdvFrame:
    frame_type: int
    flags: int
    key_id: int
    device_state: int
    frame_seq: int
    src_eid: bytes
    dst_eid: bytes
    message_id: int
    fragment_index: int
    fragment_count: int
    payload: bytes


@dataclass
class HostAdvPacket:
    frame_type: int
    seq: int
    cmd: int
    status: int
    frag_index: int
    frag_count: int
    total_len: int
    message_crc: int
    chunk: bytes


def crc8(data: bytes) -> int:
    crc = 0xFF
    for value in data:
        crc ^= value
        for _ in range(8):
            crc = ((crc << 1) ^ 0x31) & 0xFF if crc & 0x80 else (crc << 1) & 0xFF
    return crc


def crc32(data: bytes) -> int:
    return zlib.crc32(data) & 0xFFFFFFFF


def _check_eid(eid: bytes, name: str) -> None:
    if len(eid) != 16:
        raise ValueError(f"{name} must be 16 bytes")


def encode_adv_frame(frame: AdvFrame) -> bytes:
    _check_eid(frame.src_eid, "src_eid")
    _check_eid(frame.dst_eid, "dst_eid")
    if len(frame.payload) > APP_ADV_PAYLOAD_MAX_LEN:
        raise ValueError("adv payload too long")

    header = bytearray(APP_ADV_HEADER_LEN)
    header[0] = APP_ADV_MAGIC_LO
    header[1] = APP_ADV_MAGIC_HI
    header[2] = APP_ADV_PROTOCOL_VERSION
    header[3] = APP_ADV_HEADER_LEN
    header[4] = frame.frame_type & 0xFF
    header[5] = frame.flags & 0xFF
    header[6] = frame.key_id & 0xFF
    header[7] = frame.device_state & 0xFF
    struct.pack_into("<H", header, 8, frame.frame_seq & 0xFFFF)
    header[10:26] = frame.src_eid
    header[26:42] = frame.dst_eid
    struct.pack_into("<I", header, 42, frame.message_id & 0xFFFFFFFF)
    header[46] = frame.fragment_index & 0xFF
    header[47] = frame.fragment_count & 0xFF
    header[48] = len(frame.payload)
    header[49] = crc8(bytes(header[:49]))

    vendor_payload = bytes(header) + frame.payload
    vendor_payload += struct.pack("<I", crc32(vendor_payload))
    ad_value = struct.pack("<H", APP_ADV_COMPANY_ID_DEV) + vendor_payload
    if len(ad_value) + 1 > 255:
        raise ValueError("manufacturer AD structure too long")
    return bytes([len(ad_value) + 1, 0xFF]) + ad_value


def decode_adv_frame(adv_data: bytes) -> AdvFrame:
    idx = 0
    payload = None
    while idx < len(adv_data):
        ad_len = adv_data[idx]
        idx += 1
        if ad_len == 0 or idx + ad_len > len(adv_data):
            break
        ad_type = adv_data[idx]
        idx += 1
        ad_value = adv_data[idx : idx + ad_len - 1]
        idx += ad_len - 1
        if ad_type == 0xFF and len(ad_value) >= 2 and struct.unpack_from("<H", ad_value, 0)[0] == APP_ADV_COMPANY_ID_DEV:
            payload = ad_value[2:]
            break

    if payload is None:
        raise ValueError("manufacturer payload not found")
    if len(payload) < APP_ADV_HEADER_LEN + 4:
        raise ValueError("adv frame too short")
    if payload[0] != APP_ADV_MAGIC_LO or payload[1] != APP_ADV_MAGIC_HI:
        raise ValueError("bad adv magic")
    if payload[2] != APP_ADV_PROTOCOL_VERSION or payload[3] != APP_ADV_HEADER_LEN:
        raise ValueError("unsupported adv protocol")
    if crc8(payload[:49]) != payload[49]:
        raise ValueError("adv header crc mismatch")

    payload_len = payload[48]
    total_len = APP_ADV_HEADER_LEN + payload_len + 4
    if len(payload) < total_len:
        raise ValueError("adv payload truncated")
    expected_crc = struct.unpack_from("<I", payload, APP_ADV_HEADER_LEN + payload_len)[0]
    actual_crc = crc32(payload[: APP_ADV_HEADER_LEN + payload_len])
    if expected_crc != actual_crc:
        raise ValueError("adv frame crc mismatch")

    return AdvFrame(
        frame_type=payload[4],
        flags=payload[5],
        key_id=payload[6],
        device_state=payload[7],
        frame_seq=struct.unpack_from("<H", payload, 8)[0],
        src_eid=payload[10:26],
        dst_eid=payload[26:42],
        message_id=struct.unpack_from("<I", payload, 42)[0],
        fragment_index=payload[46],
        fragment_count=payload[47],
        payload=payload[APP_ADV_HEADER_LEN : APP_ADV_HEADER_LEN + payload_len],
    )


def encode_host_adv_payload(
    frame_type: int,
    seq: int,
    cmd: int,
    status: int,
    frag_index: int,
    frag_count: int,
    total_len: int,
    message_crc: int,
    chunk: bytes,
) -> bytes:
    if len(chunk) > HOST_ADV_CHUNK_MAX_LEN:
        raise ValueError("host adv chunk too long")
    return (
        bytes(
            [
                HOST_ADV_MAGIC_LO,
                HOST_ADV_MAGIC_HI,
                HOST_ADV_VERSION,
                frame_type & 0xFF,
                seq & 0xFF,
                cmd & 0xFF,
                status & 0xFF,
                frag_index & 0xFF,
                frag_count & 0xFF,
            ]
        )
        + struct.pack("<HHB", total_len & 0xFFFF, message_crc & 0xFFFF, len(chunk))
        + chunk
    )


def decode_host_adv_payload(payload: bytes) -> HostAdvPacket:
    if len(payload) < HOST_ADV_HEADER_LEN:
        raise ValueError("host adv payload too short")
    if payload[0] != HOST_ADV_MAGIC_LO or payload[1] != HOST_ADV_MAGIC_HI or payload[2] != HOST_ADV_VERSION:
        raise ValueError("bad host adv header")
    total_len, message_crc, chunk_len = struct.unpack_from("<HHB", payload, 9)
    if len(payload) != HOST_ADV_HEADER_LEN + chunk_len:
        raise ValueError("bad host adv chunk length")
    return HostAdvPacket(
        frame_type=payload[3],
        seq=payload[4],
        cmd=payload[5],
        status=payload[6],
        frag_index=payload[7],
        frag_count=payload[8],
        total_len=total_len,
        message_crc=message_crc,
        chunk=payload[HOST_ADV_HEADER_LEN:],
    )


def build_host_command_adv_frames(
    dst_eid: bytes,
    cmd: int,
    payload: bytes = b"",
    seq: int = 1,
    src_eid: bytes = DEFAULT_HOST_EID,
    repeat: int = 3,
) -> list[bytes]:
    _check_eid(src_eid, "src_eid")
    _check_eid(dst_eid, "dst_eid")
    if len(payload) > host.MESSAGE_MAX_LEN:
        raise ValueError("host message too long")

    message_crc = host.crc16(payload)
    chunks = [payload[i : i + HOST_ADV_CHUNK_MAX_LEN] for i in range(0, len(payload), HOST_ADV_CHUNK_MAX_LEN)]
    if not chunks:
        chunks = [b""]

    frames: list[bytes] = []
    message_id = seq
    frame_seq = 0
    for _ in range(repeat):
        for frag_index, chunk in enumerate(chunks):
            host_payload = encode_host_adv_payload(
                host.TYPE_CMD,
                seq,
                cmd,
                0,
                frag_index,
                len(chunks),
                len(payload),
                message_crc,
                chunk,
            )
            frames.append(
                encode_adv_frame(
                    AdvFrame(
                        frame_type=ADV_FRAME_DATA,
                        flags=0,
                        key_id=1,
                        device_state=0,
                        frame_seq=frame_seq,
                        src_eid=src_eid,
                        dst_eid=dst_eid,
                        message_id=message_id,
                        fragment_index=frag_index,
                        fragment_count=len(chunks),
                        payload=host_payload,
                    )
                )
            )
            frame_seq = (frame_seq + 1) & 0xFFFF
            message_id = (message_id + 1) & 0xFFFFFFFF
    return frames
