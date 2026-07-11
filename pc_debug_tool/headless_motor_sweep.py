# -*- coding: utf-8 -*-
from __future__ import annotations

import argparse
import asyncio
from dataclasses import dataclass, field
from typing import Any, Optional

from bleak import BleakClient

import pendant_protocol as proto
from headless_chat_test import find_debug_chars, scan_pendants


DEFAULT_CASES = [
    ("base", 0x46, 0x5C),
    ("step1", 0x40, 0x55),
    ("step2", 0x38, 0x4A),
    ("step3", 0x30, 0x40),
    ("step4", 0x28, 0x35),
    ("step5", 0x20, 0x2A),
]


@dataclass
class MotorEndpoint:
    address: str
    client: Optional[BleakClient] = None
    chars: dict[str, Any] = field(default_factory=dict)
    assembler: proto.MessageAssembler = field(default_factory=proto.MessageAssembler)
    seq: int = 1
    responses: asyncio.Queue[proto.HostMessage] = field(default_factory=asyncio.Queue)

    async def connect(self) -> None:
        self.client = BleakClient(self.address, winrt={"use_cached_services": False})
        await self.client.connect(timeout=30.0)
        services = self.client.services
        if services is None and hasattr(self.client, "get_services"):
            services = await self.client.get_services()
        if services is None:
            raise RuntimeError("service discovery failed")
        self.chars = find_debug_chars(services)
        missing = [role for role in ("cmd", "rsp") if role not in self.chars]
        if missing:
            raise RuntimeError(f"missing debug chars: {', '.join(missing)}")
        await self.client.start_notify(self.chars["rsp"], self._on_notify)
        print(f"connected {self.address}")

    async def disconnect(self) -> None:
        if self.client and self.client.is_connected:
            await self.client.disconnect()
        print("disconnected")

    async def motor_test(
        self,
        pattern: int,
        rated: int | None = None,
        clamp: int | None = None,
        drive_time: int | None = None,
        timeout_s: float = 5.0,
    ) -> proto.HostMessage:
        seq = self._next_seq()
        packets = proto.make_motor_test(seq, pattern, rated, clamp, drive_time)
        await self._send_packets(packets)
        end = asyncio.get_running_loop().time() + timeout_s
        while True:
            remaining = end - asyncio.get_running_loop().time()
            if remaining <= 0:
                raise TimeoutError("MOTOR_TEST response timeout")
            message = await asyncio.wait_for(self.responses.get(), timeout=remaining)
            if message.cmd == proto.CMD_MOTOR_TEST and message.seq == seq:
                return message

    def _next_seq(self) -> int:
        value = self.seq
        self.seq = 1 if self.seq >= 255 else self.seq + 1
        return value

    async def _send_packets(self, packets: list[bytes]) -> None:
        if not self.client or not self.client.is_connected:
            raise RuntimeError("not connected")
        for packet in packets:
            try:
                await self.client.write_gatt_char(self.chars["cmd"], packet, response=True)
            except Exception:
                await self.client.write_gatt_char(self.chars["cmd"], packet, response=False)
            await asyncio.sleep(0.02)

    def _on_notify(self, _sender: Any, data: bytearray) -> None:
        try:
            frame = proto.decode_packet(bytes(data))
            message = self.assembler.push(frame)
        except Exception as exc:
            print(f"notify decode error: {exc}")
            return
        if message is None or message.frame_type != proto.TYPE_RSP:
            return
        self.responses.put_nowait(message)


def parse_case(value: str) -> tuple[str, int, int]:
    parts = [part.strip() for part in value.split(",")]
    if len(parts) == 2:
        rated_s, clamp_s = parts
        name = f"custom{rated_s}/{clamp_s}"
    elif len(parts) == 3:
        name, rated_s, clamp_s = parts
    else:
        raise argparse.ArgumentTypeError("case must be rated,clamp or name,rated,clamp")
    return name, int(rated_s, 0), int(clamp_s, 0)


def format_motor_result(name: str, requested_rated: int, requested_clamp: int, message: proto.HostMessage) -> str:
    status = proto.STATUS_NAMES.get(message.status, f"0x{message.status:02X}")
    try:
        info = proto.parse_motor_test(message.payload)
    except Exception:
        return (
            f"{name}: status={status} requested=0x{requested_rated:02X}/0x{requested_clamp:02X} "
            f"payload={proto.hex_bytes(message.payload)}"
        )
    return (
        f"{name}: status={status} requested=0x{requested_rated:02X}/0x{requested_clamp:02X} "
        f"applied=0x{info.get('rated', 0):02X}/0x{info.get('clamp', 0):02X} "
        f"ready={info.get('ready', 0)} busy={info.get('busy', 0)} timeout={info.get('timeout', 0)} "
        f"last_st=0x{info.get('last_status', 0):02X} live_st=0x{info.get('live_status', 0):02X} "
        f"go={info.get('live_go', 0)} diag_z=0x{info.get('diag_z', 0):02X} "
        f"lra={info.get('lra_period', 0)} fb=0x{info.get('fb_ctrl', 0):02X} "
        f"rated_clamp=0x{info.get('rated_clamp', 0):02X}"
    )


async def run(args: argparse.Namespace) -> int:
    address = args.address
    if not address:
        devices = await scan_pendants(args.scan_timeout)
        print("scan result:")
        for index, dev in enumerate(devices):
            print(f"  {index}: {dev.address} rssi={dev.rssi} name={dev.name}")
        if not devices:
            print("FAIL: no Glimmer devices")
            return 2
        address = devices[0].address

    cases = args.case or DEFAULT_CASES
    ep = MotorEndpoint(address)
    try:
        await ep.connect()
        await asyncio.sleep(0.4)
        for name, rated, clamp in cases:
            print(f"play {name}: rated=0x{rated:02X}, clamp=0x{clamp:02X}")
            message = await ep.motor_test(args.pattern, rated, clamp, args.drive_time)
            print(format_motor_result(name, rated, clamp, message))
            await asyncio.sleep(args.gap)
            stop_message = await ep.motor_test(0)
            print(format_motor_result(f"{name}-post", rated, clamp, stop_message))
            await asyncio.sleep(0.3)
        final_stop = await ep.motor_test(0)
        print(format_motor_result("final-stop", 0, 0, final_stop))
        return 0
    finally:
        await ep.disconnect()


def main() -> int:
    parser = argparse.ArgumentParser(description="Run a DRV2625 motor rated/clamp sweep over Glimmer debug GATT.")
    parser.add_argument("--address", default="", help="BLE address")
    parser.add_argument("--scan-timeout", type=float, default=6.0)
    parser.add_argument("--pattern", type=int, default=1)
    parser.add_argument("--drive-time", type=lambda value: int(value, 0), default=0x10)
    parser.add_argument("--gap", type=float, default=2.0)
    parser.add_argument("--case", action="append", type=parse_case, help="rated,clamp or name,rated,clamp; can repeat")
    args = parser.parse_args()
    return asyncio.run(run(args))


if __name__ == "__main__":
    raise SystemExit(main())
