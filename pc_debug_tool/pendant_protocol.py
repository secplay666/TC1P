# -*- coding: utf-8 -*-
from __future__ import annotations

from dataclasses import dataclass
import struct
from typing import Dict, Optional, Tuple


MAGIC = 0xA5
VERSION = 0x01
MAX_PACKET_LEN = 20
HEADER_LEN = 9
CRC_LEN = 2
CHUNK_MAX_LEN = MAX_PACKET_LEN - HEADER_LEN - CRC_LEN
MESSAGE_MAX_LEN = 192

TYPE_CMD = 1
TYPE_RSP = 2
TYPE_LOG = 3
TYPE_EVENT = 4

CMD_GET_DEVICE_INFO = 0x01
CMD_GET_SYSTEM_STATE = 0x02
CMD_GET_ADV_FRAME = 0x03
CMD_GET_PEER_TABLE = 0x04
CMD_SET_RSSI_CONFIG = 0x05
CMD_MOTOR_TEST = 0x06
CMD_LOG_ENABLE = 0x07
CMD_DEBUG_RESET_STATS = 0x08
CMD_ENTER_SLEEP = 0x09
CMD_GET_FLASH_MAP = 0x0A
CMD_GET_IDENTITY = 0x0B
CMD_WRITE_IDENTITY = 0x0C
CMD_LOCK_IDENTITY = 0x0D
CMD_GET_FACTORY_INFO = 0x0E
CMD_RUN_FACTORY_TEST = 0x0F
CMD_SHELL_EXEC = 0x10

EVENT_PEER_LEVEL = 0x81
EVENT_SYSTEM = 0x82
EVENT_ERROR = 0x83

CMD_NAMES = {
    CMD_GET_DEVICE_INFO: "GET_DEVICE_INFO",
    CMD_GET_SYSTEM_STATE: "GET_SYSTEM_STATE",
    CMD_GET_ADV_FRAME: "GET_ADV_FRAME",
    CMD_GET_PEER_TABLE: "GET_PEER_TABLE",
    CMD_SET_RSSI_CONFIG: "SET_RSSI_CONFIG",
    CMD_MOTOR_TEST: "MOTOR_TEST",
    CMD_LOG_ENABLE: "LOG_ENABLE",
    CMD_DEBUG_RESET_STATS: "DEBUG_RESET_STATS",
    CMD_ENTER_SLEEP: "ENTER_SLEEP",
    CMD_GET_FLASH_MAP: "GET_FLASH_MAP",
    CMD_GET_IDENTITY: "GET_IDENTITY",
    CMD_WRITE_IDENTITY: "WRITE_IDENTITY",
    CMD_LOCK_IDENTITY: "LOCK_IDENTITY",
    CMD_GET_FACTORY_INFO: "GET_FACTORY_INFO",
    CMD_RUN_FACTORY_TEST: "RUN_FACTORY_TEST",
    CMD_SHELL_EXEC: "SHELL_EXEC",
}

STATUS_NAMES = {
    0x00: "OK",
    0x01: "ERR_PARAM",
    0x02: "ERR_STATE",
    0x03: "ERR_BUSY",
    0x04: "ERR_UNSUPPORTED",
    0x05: "ERR_CRC",
    0x06: "ERR_NO_MEM",
    0x07: "ERR_PERMISSION",
    0x08: "ERR_NOT_FOUND",
    0x09: "ERR_FLASH",
}

STATE_NAMES = {
    0: "BOOT",
    1: "SELF_CHECK",
    2: "ADV_SCAN",
    3: "APP_CONNECTED",
    4: "SLEEP_PREPARE",
    5: "SLEEP",
    6: "ERROR",
}

PEER_LEVEL_NAMES = {
    0: "NONE",
    1: "S1",
    2: "S2",
    3: "S3",
    4: "LOST",
}

FLASH_PART_NAMES = {
    0: "Identity",
    1: "Config",
    2: "Bond",
    3: "Event Log",
    4: "Factory",
}

IDENTITY_FLAG_PRESENT = 0x01
IDENTITY_FLAG_LOCKED = 0x02
IDENTITY_FLAG_DEV_FALLBACK = 0x04
IDENTITY_FLAG_VALID = 0x08

IDENTITY_FLAGS = {
    IDENTITY_FLAG_PRESENT: "PRESENT",
    IDENTITY_FLAG_LOCKED: "LOCKED",
    IDENTITY_FLAG_DEV_FALLBACK: "DEV_FALLBACK",
    IDENTITY_FLAG_VALID: "VALID",
}

FACTORY_FLAGS = {
    0x01: "IDENTITY_WRITTEN",
    0x02: "IDENTITY_LOCKED",
    0x04: "SELF_TEST_PASS",
}


@dataclass
class HostFrame:
    frame_type: int
    seq: int
    cmd: int
    status: int
    frag_index: int
    frag_count: int
    payload: bytes


@dataclass
class HostMessage:
    frame_type: int
    seq: int
    cmd: int
    status: int
    payload: bytes


def crc16(data: bytes) -> int:
    crc = 0xFFFF
    for value in data:
        crc ^= value
        for _ in range(8):
            if crc & 1:
                crc = (crc >> 1) ^ 0xA001
            else:
                crc >>= 1
            crc &= 0xFFFF
    return crc


def i8(value: int) -> int:
    return struct.unpack("b", bytes([value & 0xFF]))[0]


def u16le(data: bytes, offset: int = 0) -> int:
    return struct.unpack_from("<H", data, offset)[0]


def u32le(data: bytes, offset: int = 0) -> int:
    return struct.unpack_from("<I", data, offset)[0]


def hex_bytes(data: bytes, sep: str = " ") -> str:
    return sep.join(f"{b:02X}" for b in data)


def flag_names(value: int, names: dict[int, str]) -> str:
    flags = [name for bit, name in names.items() if value & bit]
    return "|".join(flags) if flags else "0"


def encode_message(frame_type: int, seq: int, cmd: int, status: int = 0, payload: bytes = b"") -> list[bytes]:
    if len(payload) > MESSAGE_MAX_LEN:
        raise ValueError(f"payload too long: {len(payload)}")

    chunks = [payload[i : i + CHUNK_MAX_LEN] for i in range(0, len(payload), CHUNK_MAX_LEN)]
    if not chunks:
        chunks = [b""]

    packets: list[bytes] = []
    for frag_index, chunk in enumerate(chunks):
        header = bytes(
            [
                MAGIC,
                VERSION,
                frame_type & 0xFF,
                seq & 0xFF,
                cmd & 0xFF,
                status & 0xFF,
                frag_index & 0xFF,
                len(chunks) & 0xFF,
                len(chunk) & 0xFF,
            ]
        )
        body = header + chunk
        packets.append(body + struct.pack("<H", crc16(body)))
    return packets


def decode_packet(data: bytes) -> HostFrame:
    if len(data) < HEADER_LEN + CRC_LEN:
        raise ValueError("packet too short")
    if data[0] != MAGIC:
        raise ValueError(f"bad magic: 0x{data[0]:02X}")
    if data[1] != VERSION:
        raise ValueError(f"bad version: 0x{data[1]:02X}")

    payload_len = data[8]
    expected_len = HEADER_LEN + payload_len + CRC_LEN
    if payload_len > CHUNK_MAX_LEN or len(data) != expected_len:
        raise ValueError(f"bad length: payload={payload_len}, packet={len(data)}")

    crc_expected = u16le(data, HEADER_LEN + payload_len)
    crc_actual = crc16(data[: HEADER_LEN + payload_len])
    if crc_expected != crc_actual:
        raise ValueError(f"crc mismatch: got 0x{crc_expected:04X}, calc 0x{crc_actual:04X}")

    frag_count = data[7]
    frag_index = data[6]
    if frag_count == 0 or frag_index >= frag_count:
        raise ValueError(f"bad fragment: {frag_index}/{frag_count}")

    return HostFrame(
        frame_type=data[2],
        seq=data[3],
        cmd=data[4],
        status=data[5],
        frag_index=frag_index,
        frag_count=frag_count,
        payload=data[HEADER_LEN : HEADER_LEN + payload_len],
    )


class MessageAssembler:
    def __init__(self) -> None:
        self._rx: Dict[Tuple[int, int, int], Tuple[int, list[Optional[bytes]]]] = {}

    def reset(self) -> None:
        self._rx.clear()

    def push(self, frame: HostFrame) -> Optional[HostMessage]:
        key = (frame.frame_type, frame.seq, frame.cmd)
        frag_count, chunks = self._rx.get(key, (frame.frag_count, [None] * frame.frag_count))
        if frag_count != frame.frag_count:
            self._rx.pop(key, None)
            raise ValueError("fragment count changed")

        chunks[frame.frag_index] = frame.payload
        self._rx[key] = (frag_count, chunks)
        if any(chunk is None for chunk in chunks):
            return None

        payload = b"".join(chunk or b"" for chunk in chunks)
        self._rx.pop(key, None)
        return HostMessage(frame.frame_type, frame.seq, frame.cmd, frame.status, payload)


def make_empty_command(seq: int, cmd: int) -> list[bytes]:
    return encode_message(TYPE_CMD, seq, cmd, 0, b"")


def make_log_enable(seq: int, enabled: bool) -> list[bytes]:
    return encode_message(TYPE_CMD, seq, CMD_LOG_ENABLE, 0, bytes([1 if enabled else 0]))


def make_motor_test(seq: int, pattern: int) -> list[bytes]:
    return encode_message(TYPE_CMD, seq, CMD_MOTOR_TEST, 0, bytes([pattern & 0xFF]))


def make_rssi_config(seq: int, t1: int, t2: int, t3: int, tin_ms: int, tout_ms: int) -> list[bytes]:
    payload = struct.pack("<bbbHH", t1, t2, t3, tin_ms, tout_ms)
    return encode_message(TYPE_CMD, seq, CMD_SET_RSSI_CONFIG, 0, payload)


def make_shell_exec(seq: int, line: str) -> list[bytes]:
    return encode_message(TYPE_CMD, seq, CMD_SHELL_EXEC, 0, line.encode("utf-8")[:MESSAGE_MAX_LEN])


def build_unique_id(product_sn: int, terminal_sn: int, random_value: int, reserved: int = 0) -> bytes:
    return struct.pack("<IIII", product_sn & 0xFFFFFFFF, terminal_sn & 0xFFFFFFFF, random_value & 0xFFFFFFFF, reserved & 0xFFFFFFFF)


def parse_unique_id(unique_id: bytes) -> dict:
    if len(unique_id) != 16:
        raise ValueError("unique id must be 16 bytes")
    return {
        "raw": unique_id.hex().upper(),
        "product_sn": u32le(unique_id, 0),
        "terminal_sn": u32le(unique_id, 4),
        "random": u32le(unique_id, 8),
        "reserved": u32le(unique_id, 12),
    }


def make_write_identity_payload(unique_id: bytes, lock_after_write: bool = False) -> bytes:
    if len(unique_id) != 16:
        raise ValueError("unique id must be 16 bytes")
    return bytes([1 if lock_after_write else 0]) + unique_id


def parse_device_info(payload: bytes) -> dict:
    if len(payload) < 36:
        raise ValueError("device info payload too short")
    return {
        "info_version": payload[0],
        "hw_rev": payload[1],
        "fw_version": f"{payload[3]}.{payload[2]}.{payload[4]}",
        "key_id": payload[5],
        "privacy_mode": payload[6],
        "short_id": u32le(payload, 8),
        "eid": payload[12:28].hex().upper(),
        "cmd_count": u16le(payload, 28),
        "log_count": u16le(payload, 30),
        "crc_error_count": u16le(payload, 32),
        "host_frame_version": payload[34],
        "adv_protocol_version": payload[35],
    }


def parse_system_state(payload: bytes) -> dict:
    if len(payload) < 20:
        raise ValueError("system state payload too short")
    state = payload[0]
    prev_state = payload[1]
    return {
        "state": state,
        "state_name": STATE_NAMES.get(state, f"UNKNOWN_{state}"),
        "previous_state": prev_state,
        "previous_state_name": STATE_NAMES.get(prev_state, f"UNKNOWN_{prev_state}"),
        "error_code": u16le(payload, 2),
        "wakeup_reason": payload[4],
        "host_ready": payload[5],
        "uptime_s": u32le(payload, 6),
        "battery_mv": u16le(payload, 10),
        "battery_percent": payload[12],
        "battery_low": payload[13],
        "battery_critical": payload[14],
        "charge_state": payload[15],
        "peer_count": payload[16],
        "motor_busy": payload[17],
        "log_enable": payload[18],
    }


def parse_peer_table(payload: bytes) -> list[dict]:
    if not payload:
        return []
    count = payload[0]
    peers = []
    offset = 1
    for _ in range(count):
        if offset + 8 > len(payload):
            break
        level = payload[offset + 4]
        peers.append(
            {
                "short_id": u32le(payload, offset),
                "level": level,
                "level_name": PEER_LEVEL_NAMES.get(level, str(level)),
                "rssi": i8(payload[offset + 5]),
                "rssi_avg": i8(payload[offset + 6]),
                "flags": payload[offset + 7],
            }
        )
        offset += 8
    return peers


def parse_flash_map(payload: bytes) -> dict:
    if len(payload) < 37:
        raise ValueError("flash map payload too short")

    has_master_pairing = len(payload) >= 41 and len(payload) >= 41 + payload[40] * 9
    app_base_offset = 32 if has_master_pairing else 28
    app_total_offset = 36 if has_master_pairing else 32
    count_offset = 40 if has_master_pairing else 36

    info = {
        "flash_mid": u32le(payload, 0),
        "flash_vendor": u32le(payload, 4),
        "flash_size": u32le(payload, 8),
        "sdk_reserved_start": u32le(payload, 12),
        "sdk_mac_addr": u32le(payload, 16),
        "sdk_calibration_addr": u32le(payload, 20),
        "sdk_smp_pairing_addr": u32le(payload, 24),
        "sdk_master_pairing_addr": u32le(payload, 28) if has_master_pairing else 0,
        "app_base_addr": u32le(payload, app_base_offset),
        "app_total_size": u32le(payload, app_total_offset),
        "parts": [],
    }
    count = payload[count_offset]
    offset = count_offset + 1
    for _ in range(count):
        if offset + 9 > len(payload):
            break
        part = payload[offset]
        info["parts"].append(
            {
                "part": part,
                "name": FLASH_PART_NAMES.get(part, f"Part {part}"),
                "addr": u32le(payload, offset + 1),
                "size": u32le(payload, offset + 5),
            }
        )
        offset += 9
    return info


def parse_identity_info(payload: bytes) -> dict:
    if len(payload) < 56:
        raise ValueError("identity payload too short")
    unique_id = payload[4:20]
    parsed = parse_unique_id(unique_id)
    flags = payload[1]
    return {
        "version": payload[0],
        "flags": flags,
        "flags_text": flag_names(flags, IDENTITY_FLAGS),
        "crc16": u16le(payload, 2),
        "unique_id": parsed["raw"],
        "product_sn": u32le(payload, 20),
        "terminal_sn": u32le(payload, 24),
        "random": u32le(payload, 28),
        "reserved": u32le(payload, 32),
        "short_id": u32le(payload, 36),
        "eid": payload[40:56].hex().upper(),
    }


def parse_factory_info(payload: bytes) -> dict:
    if len(payload) < 40:
        raise ValueError("factory info payload too short")
    flags = payload[1]
    unique_id = payload[24:40]
    parsed = parse_unique_id(unique_id)
    return {
        "version": payload[0],
        "flags": flags,
        "flags_text": flag_names(flags, FACTORY_FLAGS),
        "crc16": u16le(payload, 2),
        "write_count": u32le(payload, 4),
        "lock_count": u32le(payload, 8),
        "test_mask": u32le(payload, 12),
        "result_mask": u32le(payload, 16),
        "last_error": u32le(payload, 20),
        "last_unique_id": parsed["raw"],
        "last_product_sn": parsed["product_sn"],
        "last_terminal_sn": parsed["terminal_sn"],
        "last_random": parsed["random"],
        "last_reserved": parsed["reserved"],
    }


def parse_log(payload: bytes) -> dict:
    if len(payload) < 4:
        raise ValueError("log payload too short")
    return {
        "level": payload[0],
        "count": u16le(payload, 1),
        "flags": payload[3],
        "text": payload[4:].decode("utf-8", errors="replace"),
    }


def parse_event(cmd: int, payload: bytes) -> dict:
    if cmd == EVENT_PEER_LEVEL:
        if len(payload) < 20:
            raise ValueError("peer level event too short")
        return {
            "event": "PEER_LEVEL",
            "eid": payload[:16].hex().upper(),
            "old_level": payload[16],
            "old_level_name": PEER_LEVEL_NAMES.get(payload[16], str(payload[16])),
            "new_level": payload[17],
            "new_level_name": PEER_LEVEL_NAMES.get(payload[17], str(payload[17])),
            "rssi_avg": i8(payload[18]),
            "reason": payload[19],
        }
    if cmd == EVENT_ERROR:
        if len(payload) < 4:
            raise ValueError("error event too short")
        return {"event": "ERROR", "error_code": u16le(payload, 0), "detail": u16le(payload, 2)}
    return {"event": f"0x{cmd:02X}", "payload_hex": hex_bytes(payload)}


def format_response(message: HostMessage) -> str:
    name = CMD_NAMES.get(message.cmd, f"0x{message.cmd:02X}")
    status = STATUS_NAMES.get(message.status, f"0x{message.status:02X}")

    if message.status != 0:
        return f"{name}: {status}"

    try:
        if message.cmd == CMD_GET_DEVICE_INFO:
            info = parse_device_info(message.payload)
            return (
                f"{name}: short_id=0x{info['short_id']:08X}, eid={info['eid']}, "
                f"hw={info['hw_rev']}, key={info['key_id']}, cmds={info['cmd_count']}, logs={info['log_count']}"
            )
        if message.cmd == CMD_GET_SYSTEM_STATE:
            info = parse_system_state(message.payload)
            return (
                f"{name}: state={info['state_name']}, uptime={info['uptime_s']}s, "
                f"battery={info['battery_mv']}mV/{info['battery_percent']}%, peers={info['peer_count']}, "
                f"motor_busy={info['motor_busy']}"
            )
        if message.cmd == CMD_GET_PEER_TABLE:
            peers = parse_peer_table(message.payload)
            return f"{name}: {len(peers)} peer(s)"
        if message.cmd == CMD_GET_ADV_FRAME:
            return f"{name}: {len(message.payload)} bytes, {hex_bytes(message.payload)}"
        if message.cmd == CMD_GET_FLASH_MAP:
            info = parse_flash_map(message.payload)
            return (
                f"{name}: mid=0x{info['flash_mid']:08X}, size={info['flash_size']} bytes, "
                f"app_base=0x{info['app_base_addr']:X}, reserved=0x{info['sdk_reserved_start']:X}"
            )
        if message.cmd in (CMD_GET_IDENTITY, CMD_WRITE_IDENTITY, CMD_LOCK_IDENTITY):
            info = parse_identity_info(message.payload)
            return (
                f"{name}: flags={info['flags_text']}, product=0x{info['product_sn']:08X}, "
                f"terminal=0x{info['terminal_sn']:08X}, short_id=0x{info['short_id']:08X}"
            )
        if message.cmd == CMD_GET_FACTORY_INFO:
            info = parse_factory_info(message.payload)
            return (
                f"{name}: flags={info['flags_text']}, writes={info['write_count']}, "
                f"locks={info['lock_count']}, last_error={info['last_error']}"
            )
        if message.cmd == CMD_RUN_FACTORY_TEST:
            if len(message.payload) < 9:
                raise ValueError("factory test payload too short")
            return (
                f"{name}: test_mask=0x{u32le(message.payload, 1):08X}, "
                f"result=0x{u32le(message.payload, 5):08X}"
            )
        if message.cmd == CMD_SHELL_EXEC:
            return message.payload.decode("utf-8", errors="replace").rstrip()
    except Exception as exc:
        return f"{name}: parse error: {exc}; raw={hex_bytes(message.payload)}"

    return f"{name}: {status}, payload={hex_bytes(message.payload)}"
