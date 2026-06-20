# -*- coding: utf-8 -*-
from __future__ import annotations

import argparse
import asyncio
import time
from dataclasses import dataclass, field
from typing import Any, Optional

from bleak import BleakClient, BleakScanner

import pendant_protocol as proto


DEBUG_UUIDS = {
    "service": [
        "50544e44-0001-4b45-5931-444556000001",
        "56000001-4445-5931-454b-0100444e5450",
        "01000056-4544-3159-4b45-000150544e44",
    ],
    "cmd": [
        "50544e44-0002-4b45-5931-444556000001",
        "56000001-4445-5931-454b-0200444e5450",
        "01000056-4544-3159-4b45-000250544e44",
    ],
    "rsp": [
        "50544e44-0003-4b45-5931-444556000001",
        "56000001-4445-5931-454b-0300444e5450",
        "01000056-4544-3159-4b45-000350544e44",
    ],
    "log": [
        "50544e44-0004-4b45-5931-444556000001",
        "56000001-4445-5931-454b-0400444e5450",
        "01000056-4544-3159-4b45-000450544e44",
    ],
    "evt": [
        "50544e44-0005-4b45-5931-444556000001",
        "56000001-4445-5931-454b-0500444e5450",
        "01000056-4544-3159-4b45-000550544e44",
    ],
}


def normalize_uuid(value: str) -> str:
    return value.lower()


def iter_services(services: Any) -> list[Any]:
    try:
        return list(services)
    except TypeError:
        return list(getattr(services, "services", {}).values())


def find_debug_chars(services: Any) -> dict[str, Any]:
    found: dict[str, Any] = {}
    wanted = {uuid: role for role, uuids in DEBUG_UUIDS.items() for uuid in uuids}
    for service in iter_services(services):
        for char in getattr(service, "characteristics", []):
            role = wanted.get(normalize_uuid(char.uuid))
            if role and role != "service":
                found[role] = char
    return found


@dataclass
class PendantDevice:
    name: str
    address: str
    rssi: Optional[int]


@dataclass
class Endpoint:
    label: str
    address: str
    client: Optional[BleakClient] = None
    chars: dict[str, Any] = field(default_factory=dict)
    assembler: proto.MessageAssembler = field(default_factory=proto.MessageAssembler)
    seq: int = 1
    logs: list[str] = field(default_factory=list)
    chats: list[str] = field(default_factory=list)
    responses: list[str] = field(default_factory=list)

    async def connect(self) -> None:
        self.client = BleakClient(self.address)
        await self.client.connect(timeout=15.0)
        services = self.client.services
        if services is None and hasattr(self.client, "get_services"):
            services = await self.client.get_services()
        if services is None:
            raise RuntimeError(f"{self.label}: service discovery failed")

        self.chars = find_debug_chars(services)
        missing = [role for role in ("cmd", "rsp", "log", "evt") if role not in self.chars]
        if missing:
            raise RuntimeError(f"{self.label}: missing debug chars: {', '.join(missing)}")

        await self.client.start_notify(self.chars["rsp"], self._on_notify)
        await self.client.start_notify(self.chars["log"], self._on_notify)
        await self.client.start_notify(self.chars["evt"], self._on_notify)
        print(f"{self.label}: connected {self.address}")

    async def disconnect(self) -> None:
        if self.client and self.client.is_connected:
            await self.client.disconnect()
        print(f"{self.label}: disconnected")

    async def send_command(self, cmd: int, payload: bytes = b"") -> None:
        await self._send_packets(proto.encode_message(proto.TYPE_CMD, self._next_seq(), cmd, 0, payload))

    async def send_shell(self, line: str) -> None:
        print(f"{self.label}: shell> {line}")
        await self._send_packets(proto.make_shell_exec(self._next_seq(), line))

    async def send_chat(self, text: str) -> None:
        print(f"{self.label}: chat> {text}")
        await self._send_packets(proto.make_p2p_chat_send(self._next_seq(), text))

    async def wait_for_log(self, needle: str, timeout_s: float = 8.0) -> bool:
        end_time = time.monotonic() + timeout_s
        while time.monotonic() < end_time:
            if any(needle in item for item in self.logs):
                return True
            await asyncio.sleep(0.1)
        return False

    async def wait_for_chat(self, needle: str, timeout_s: float = 8.0) -> bool:
        end_time = time.monotonic() + timeout_s
        while time.monotonic() < end_time:
            if any(needle in item for item in self.chats):
                return True
            await asyncio.sleep(0.1)
        return False

    def _next_seq(self) -> int:
        value = self.seq
        self.seq = 1 if self.seq >= 255 else self.seq + 1
        return value

    async def _send_packets(self, packets: list[bytes]) -> None:
        if not self.client or not self.client.is_connected:
            raise RuntimeError(f"{self.label}: not connected")
        for packet in packets:
            try:
                await self.client.write_gatt_char(self.chars["cmd"], packet, response=False)
            except Exception:
                await self.client.write_gatt_char(self.chars["cmd"], packet, response=True)
            await asyncio.sleep(0.02)

    def _on_notify(self, _sender: Any, data: bytearray) -> None:
        try:
            frame = proto.decode_packet(bytes(data))
            message = self.assembler.push(frame)
        except Exception as exc:
            print(f"{self.label}: notify decode error: {exc}")
            return
        if message is None:
            return

        if message.frame_type == proto.TYPE_LOG:
            try:
                log = proto.parse_log(message.payload)
                text = log["text"]
            except Exception:
                text = proto.hex_bytes(message.payload)
            self.logs.append(text)
            print(f"{self.label}: LOG {text}")
            return

        if message.frame_type == proto.TYPE_RSP:
            text = proto.format_response(message)
            self.responses.append(text)
            print(f"{self.label}: RSP {text}")
            return

        if message.frame_type == proto.TYPE_EVENT:
            try:
                event = proto.parse_event(message.cmd, message.payload)
            except Exception:
                print(f"{self.label}: EVT cmd=0x{message.cmd:02X} len={len(message.payload)}")
                return
            if event.get("event") == "P2P_CHAT":
                text = event["text"]
                self.chats.append(text)
                print(f"{self.label}: CHAT {text}")
            else:
                print(f"{self.label}: EVT {event}")


async def scan_pendants(timeout_s: float) -> list[PendantDevice]:
    result = await BleakScanner.discover(timeout=timeout_s, return_adv=True)
    devices: list[PendantDevice] = []
    for _, pair in result.items():
        dev, adv = pair
        name = dev.name or adv.local_name or ""
        service_uuids = [normalize_uuid(x) for x in (getattr(adv, "service_uuids", []) or [])]
        is_debug = any(uuid in service_uuids for uuid in DEBUG_UUIDS["service"])
        if "PENDANT" in name.upper() or is_debug:
            devices.append(PendantDevice(name=name, address=dev.address, rssi=getattr(adv, "rssi", None)))
    devices.sort(key=lambda d: (-(d.rssi or -999), d.address))
    return devices


async def run(args: argparse.Namespace) -> int:
    if args.addr_a and args.addr_b:
        addr_a, addr_b = args.addr_a, args.addr_b
    else:
        devices = await scan_pendants(args.scan_timeout)
        print("scan result:")
        for index, dev in enumerate(devices):
            print(f"  {index}: {dev.address} rssi={dev.rssi} name={dev.name}")
        if len(devices) < 2:
            print("FAIL: need at least two PENDANT devices")
            return 2
        addr_a, addr_b = devices[0].address, devices[1].address

    a = Endpoint("A", addr_a)
    b = Endpoint("B", addr_b)
    try:
        await a.connect()
        await b.connect()
        await asyncio.sleep(0.5)

        await a.send_command(proto.CMD_LOG_ENABLE, b"\x00")
        await b.send_command(proto.CMD_LOG_ENABLE, b"\x00")
        await asyncio.sleep(0.5)

        await a.send_shell("ble start")
        await asyncio.sleep(0.6)
        await b.send_shell("ble start")
        await asyncio.sleep(0.8)
        await a.send_shell("p2pclear")
        await asyncio.sleep(0.6)
        await b.send_shell("p2pclear")
        await asyncio.sleep(2.0)
        await a.send_shell("peers")
        await asyncio.sleep(0.4)
        await b.send_shell("peers")
        await asyncio.sleep(1.0)

        msg_a = args.message_a
        msg_b = args.message_b
        await a.send_chat(msg_a)
        ok_ab = await b.wait_for_chat(msg_a, args.wait_timeout)
        await asyncio.sleep(0.5)

        await b.send_chat(msg_b)
        ok_ba = await a.wait_for_chat(msg_b, args.wait_timeout)

        if ok_ab and ok_ba:
            print("PASS: PC A -> B85 A -> B85 B -> PC B and reverse both passed")
            return 0
        print(f"FAIL: A->B={ok_ab}, B->A={ok_ba}")
        return 1
    finally:
        await b.disconnect()
        await a.disconnect()


def main() -> int:
    parser = argparse.ArgumentParser(description="Headless two-PC P2P chat test over pendant debug GATT.")
    parser.add_argument("--addr-a", default="", help="BLE address for endpoint A")
    parser.add_argument("--addr-b", default="", help="BLE address for endpoint B")
    parser.add_argument("--scan-timeout", type=float, default=6.0)
    parser.add_argument("--wait-timeout", type=float, default=8.0)
    parser.add_argument("--message-a", default="pcA hello")
    parser.add_argument("--message-b", default="pcB hello")
    args = parser.parse_args()
    return asyncio.run(run(args))


if __name__ == "__main__":
    raise SystemExit(main())
