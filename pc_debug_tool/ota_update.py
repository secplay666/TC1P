# -*- coding: utf-8 -*-
from __future__ import annotations

import argparse
import asyncio
import math
import struct
import time
from dataclasses import dataclass
from pathlib import Path
from typing import Any, Optional
from uuid import UUID

from bleak import BleakClient, BleakScanner

from headless_chat_test import is_glimmer_name, normalize_uuid


OTA_SERVICE_UUIDS = {
    str(UUID(bytes=bytes([0x12, 0x19, 0x0D, 0x0C, 0x0B, 0x0A, 0x09, 0x08, 0x07, 0x06, 0x05, 0x04, 0x03, 0x02, 0x01, 0x00]))),
    str(UUID(bytes_le=bytes([0x12, 0x19, 0x0D, 0x0C, 0x0B, 0x0A, 0x09, 0x08, 0x07, 0x06, 0x05, 0x04, 0x03, 0x02, 0x01, 0x00]))),
    str(UUID(bytes=bytes([0x12, 0x19, 0x0D, 0x0C, 0x0B, 0x0A, 0x09, 0x08, 0x07, 0x06, 0x05, 0x04, 0x03, 0x02, 0x01, 0x00])[::-1])),
}
OTA_DATA_UUIDS = {
    str(UUID(bytes=bytes([0x12, 0x2B, 0x0D, 0x0C, 0x0B, 0x0A, 0x09, 0x08, 0x07, 0x06, 0x05, 0x04, 0x03, 0x02, 0x01, 0x00]))),
    str(UUID(bytes_le=bytes([0x12, 0x2B, 0x0D, 0x0C, 0x0B, 0x0A, 0x09, 0x08, 0x07, 0x06, 0x05, 0x04, 0x03, 0x02, 0x01, 0x00]))),
    str(UUID(bytes=bytes([0x12, 0x2B, 0x0D, 0x0C, 0x0B, 0x0A, 0x09, 0x08, 0x07, 0x06, 0x05, 0x04, 0x03, 0x02, 0x01, 0x00])[::-1])),
}

CMD_OTA_START_EXT = 0xFF03
CMD_OTA_END = 0xFF02
CMD_OTA_RESULT = 0xFF06
CMD_OTA_SCHEDULE_PDU_NUM = 0xFF08
CMD_OTA_SCHEDULE_FW_SIZE = 0xFF09

OTA_RESULT_NAMES = {
    0x00: "SUCCESS",
    0x01: "DATA_PACKET_SEQ_ERR",
    0x02: "PACKET_INVALID",
    0x03: "DATA_CRC_ERR",
    0x04: "WRITE_FLASH_ERR",
    0x05: "DATA_INCOMPLETE",
    0x06: "FLOW_ERR",
    0x07: "FW_CHECK_ERR",
    0x08: "VERSION_COMPARE_ERR",
    0x09: "PDU_LEN_ERR",
    0x0A: "FIRMWARE_MARK_ERR",
    0x0B: "FW_SIZE_ERR",
    0x0C: "DATA_PACKET_TIMEOUT",
    0x0D: "TIMEOUT",
    0x0E: "CONNECTION_TERMINATE",
    0x0F: "MCU_NOT_SUPPORTED",
    0x10: "LOGIC_ERR",
}


def log(message: str) -> None:
    print(message, flush=True)


@dataclass
class OtaTarget:
    address: str
    name: str = ""
    rssi: Optional[int] = None


class OtaResult(Exception):
    def __init__(self, code: int) -> None:
        self.code = code
        super().__init__(f"OTA result {code}: {OTA_RESULT_NAMES.get(code, 'UNKNOWN')}")


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


def iter_services(services: Any) -> list[Any]:
    try:
        return list(services)
    except TypeError:
        return list(getattr(services, "services", {}).values())


def find_ota_char(services: Any) -> Any:
    fallback = None
    for service in iter_services(services):
        service_uuid = normalize_uuid(getattr(service, "uuid", ""))
        for char in getattr(service, "characteristics", []):
            char_uuid = normalize_uuid(char.uuid)
            if char_uuid in OTA_DATA_UUIDS:
                return char
            if service_uuid in OTA_SERVICE_UUIDS:
                fallback = fallback or char
    if fallback is not None:
        return fallback

    lines = []
    for service in iter_services(services):
        lines.append(f"service {getattr(service, 'uuid', '')}")
        for char in getattr(service, "characteristics", []):
            lines.append(f"  char {char.uuid} props={','.join(getattr(char, 'properties', []))}")
    raise RuntimeError("OTA characteristic not found:\n" + "\n".join(lines))


def validate_firmware(data: bytes, max_size: int, force: bool) -> None:
    if len(data) < 0x1C:
        raise ValueError("firmware too small")
    boot_mark = data[0x08]
    fw_size = struct.unpack_from("<I", data, 0x18)[0]
    if boot_mark != 0x4B and not force:
        raise ValueError(f"bad boot mark 0x{boot_mark:02X}; use --force to override")
    if fw_size != len(data) and not force:
        raise ValueError(f"firmware size field {fw_size} != file size {len(data)}; use --force to override")
    if len(data) > max_size and not force:
        raise ValueError(f"firmware {len(data)} bytes exceeds max {max_size} bytes; use --force to override")


def ota_start_ext_packet(pdu_len: int) -> bytes:
    if pdu_len < 16 or pdu_len > 240 or pdu_len % 16:
        raise ValueError("PDU length must be 16,32,...,240")
    return struct.pack("<HBB16s", CMD_OTA_START_EXT, pdu_len, 0, bytes(16))


def ota_data_packet(index: int, chunk: bytes, pdu_len: int) -> bytes:
    if len(chunk) > pdu_len:
        raise ValueError("chunk too long")
    if len(chunk) < pdu_len:
        chunk = chunk + bytes([0xFF]) * (pdu_len - len(chunk))
    body = struct.pack("<H", index) + chunk
    return body + struct.pack("<H", crc16(body))


def ota_end_packet(max_index: int) -> bytes:
    return struct.pack("<HHH", CMD_OTA_END, max_index & 0xFFFF, (~max_index) & 0xFFFF)


async def scan_targets(timeout_s: float, limit: int) -> list[OtaTarget]:
    result = await BleakScanner.discover(timeout=timeout_s, return_adv=True)
    targets: list[OtaTarget] = []
    for dev, adv in result.values():
        name = dev.name or adv.local_name or ""
        service_uuids = {normalize_uuid(x) for x in (getattr(adv, "service_uuids", []) or [])}
        is_ota = bool(service_uuids & OTA_SERVICE_UUIDS)
        if is_glimmer_name(name) or is_ota:
            targets.append(OtaTarget(address=dev.address, name=name, rssi=getattr(adv, "rssi", None)))
    targets.sort(key=lambda item: (-(item.rssi or -999), item.address))
    return targets[:limit] if limit > 0 else targets


async def run_one(target: OtaTarget, firmware: bytes, pdu_len: int, pace_s: float, write_response: bool) -> bool:
    result_code: Optional[int] = None
    schedule_count = 0
    schedule_size = 0

    def on_notify(_sender: Any, data: bytearray) -> None:
        nonlocal result_code, schedule_count, schedule_size
        payload = bytes(data)
        if len(payload) < 2:
            return
        cmd = struct.unpack_from("<H", payload, 0)[0]
        if cmd == CMD_OTA_RESULT and len(payload) >= 3:
            result_code = payload[2]
            log(f"{target.address}: result={OTA_RESULT_NAMES.get(result_code, result_code)}")
        elif cmd == CMD_OTA_SCHEDULE_PDU_NUM and len(payload) >= 4:
            schedule_count = struct.unpack_from("<H", payload, 2)[0]
        elif cmd == CMD_OTA_SCHEDULE_FW_SIZE and len(payload) >= 6:
            schedule_size = struct.unpack_from("<I", payload, 2)[0]

    log(f"{target.address}: connecting name={target.name or '-'} rssi={target.rssi}")
    async with BleakClient(target.address) as client:
        services = client.services
        if services is None and hasattr(client, "get_services"):
            services = await client.get_services()
        ota_char = find_ota_char(services)
        mtu_size = getattr(client, "mtu_size", None)
        max_wwr = getattr(ota_char, "max_write_without_response_size", None)
        log(f"{target.address}: ota_char={ota_char.uuid} props={','.join(getattr(ota_char, 'properties', []))} mtu={mtu_size} max_wwr={max_wwr}")
        write_len = pdu_len + 4
        if write_response and mtu_size and write_len > mtu_size - 3:
            raise RuntimeError(f"PDU {pdu_len} needs ATT MTU >= {write_len + 3}, current MTU is {mtu_size}")
        if not write_response and max_wwr and write_len > max_wwr:
            raise RuntimeError(f"PDU {pdu_len} needs write-without-response >= {write_len}, current max is {max_wwr}")
        try:
            await client.start_notify(ota_char, on_notify)
        except Exception as exc:
            log(f"{target.address}: notify unavailable: {exc}")

        chunks = math.ceil(len(firmware) / pdu_len)
        max_index = chunks - 1
        log(f"{target.address}: start OTA size={len(firmware)} pdu={pdu_len} chunks={chunks}")
        await client.write_gatt_char(ota_char, ota_start_ext_packet(pdu_len), response=write_response)
        await asyncio.sleep(0.15)

        last_print = time.monotonic()
        for index in range(chunks):
            start = index * pdu_len
            packet = ota_data_packet(index, firmware[start:start + pdu_len], pdu_len)
            await client.write_gatt_char(ota_char, packet, response=write_response)
            if pace_s > 0:
                await asyncio.sleep(pace_s)
            now = time.monotonic()
            if now - last_print >= 1.0 or index + 1 == chunks:
                percent = (index + 1) * 100.0 / chunks
                extra = f" schedule_pdu={schedule_count}" if schedule_count else ""
                if schedule_size:
                    extra += f" schedule_size={schedule_size}"
                log(f"{target.address}: {index + 1}/{chunks} {percent:.1f}%{extra}")
                last_print = now

        await asyncio.sleep(0.15)
        await client.write_gatt_char(ota_char, ota_end_packet(max_index), response=write_response)
        log(f"{target.address}: OTA_END sent max_index={max_index}")

        deadline = time.monotonic() + 20.0
        while time.monotonic() < deadline:
            if result_code is not None:
                if result_code == 0:
                    return True
                raise OtaResult(result_code)
            if not client.is_connected:
                log(f"{target.address}: disconnected before result; device may be rebooting")
                return True
            await asyncio.sleep(0.2)
        raise TimeoutError("OTA result timeout")


async def run(args: argparse.Namespace) -> int:
    firmware = Path(args.bin).read_bytes()
    validate_firmware(firmware, args.max_size_k * 1024, args.force)
    log(f"firmware={args.bin} size={len(firmware)} max={args.max_size_k * 1024}")

    targets = [OtaTarget(address=x) for x in args.address]
    if args.scan or not targets:
        scanned = await scan_targets(args.scan_timeout, args.limit)
        log("scan result:")
        for index, item in enumerate(scanned):
            log(f"  {index}: {item.address} rssi={item.rssi} name={item.name}")
        if args.scan:
            targets.extend(item for item in scanned if item.address not in {t.address for t in targets})
        elif scanned:
            targets.append(scanned[0])

    if not targets:
        log("FAIL: no OTA target")
        return 2
    if args.dry_run:
        return 0

    ok_count = 0
    for index, target in enumerate(targets, start=1):
        log(f"=== OTA {index}/{len(targets)} {target.address} ===")
        try:
            if await run_one(target, firmware, args.pdu_len, args.pace_ms / 1000.0, args.write_response):
                ok_count += 1
        except Exception as exc:
            log(f"{target.address}: FAIL {exc}")
            if args.stop_on_error:
                break
        await asyncio.sleep(args.gap)

    log(f"done: {ok_count}/{len(targets)} succeeded")
    return 0 if ok_count == len(targets) else 1


def main() -> int:
    parser = argparse.ArgumentParser(description="Glimmer Telink BLE OTA updater.")
    parser.add_argument("--bin", default="../tc_ble_multi_sdk/build/B85/pendant/pendant.bin", help="firmware bin path")
    parser.add_argument("--address", action="append", default=[], help="BLE address; can be repeated")
    parser.add_argument("--scan", action="store_true", help="scan and update all discovered Glimmer devices")
    parser.add_argument("--limit", type=int, default=0, help="max scanned devices, 0 means all")
    parser.add_argument("--scan-timeout", type=float, default=8.0)
    parser.add_argument("--pdu-len", type=int, default=16, help="OTA firmware payload per packet; PC Windows BLE usually requires 16")
    parser.add_argument("--pace-ms", type=float, default=0.0, help="delay between OTA data writes")
    parser.add_argument("--write-response", dest="write_response", action="store_true", default=True, help="use GATT write-with-response")
    parser.add_argument("--no-write-response", dest="write_response", action="store_false", help="use GATT write-without-response")
    parser.add_argument("--gap", type=float, default=3.0, help="delay between multiple devices")
    parser.add_argument("--max-size-k", type=int, default=192)
    parser.add_argument("--force", action="store_true")
    parser.add_argument("--dry-run", action="store_true")
    parser.add_argument("--stop-on-error", action="store_true")
    args = parser.parse_args()
    return asyncio.run(run(args))


if __name__ == "__main__":
    raise SystemExit(main())
