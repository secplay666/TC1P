# -*- coding: utf-8 -*-
from __future__ import annotations

import argparse
import asyncio
from typing import Any, Optional

from bleak import BleakClient, BleakScanner

import pendant_protocol as proto
from headless_chat_test import DEBUG_UUIDS, find_debug_chars


class IdentityWriter:
    def __init__(self, address: str) -> None:
        self.address = address
        self.client: Optional[BleakClient] = None
        self.chars: dict[str, Any] = {}
        self.assembler = proto.MessageAssembler()
        self.seq = 1
        self.messages: list[proto.HostMessage] = []

    async def __aenter__(self) -> "IdentityWriter":
        self.client = BleakClient(self.address)
        await self.client.connect(timeout=15.0)
        services = self.client.services
        if services is None and hasattr(self.client, "get_services"):
            services = await self.client.get_services()
        self.chars = find_debug_chars(services)
        missing = [role for role in ("cmd", "rsp") if role not in self.chars]
        if missing:
            raise RuntimeError(f"missing debug chars: {', '.join(missing)}")
        await self.client.start_notify(self.chars["rsp"], self._on_notify)
        return self

    async def __aexit__(self, _exc_type: Any, _exc: Any, _tb: Any) -> None:
        if self.client and self.client.is_connected:
            await self.client.disconnect()

    async def send_command_wait(self, cmd: int, payload: bytes = b"", timeout_s: float = 8.0) -> proto.HostMessage:
        seq = self.seq
        self.seq = 1 if self.seq >= 255 else self.seq + 1
        packets = proto.encode_message(proto.TYPE_CMD, seq, cmd, 0, payload)
        for packet in packets:
            await self.client.write_gatt_char(self.chars["cmd"], packet, response=True)  # type: ignore[union-attr]
            await asyncio.sleep(0.02)

        end_time = asyncio.get_running_loop().time() + timeout_s
        while asyncio.get_running_loop().time() < end_time:
            for index, message in enumerate(self.messages):
                if message.frame_type == proto.TYPE_RSP and message.seq == seq and message.cmd == cmd:
                    return self.messages.pop(index)
            await asyncio.sleep(0.05)
        raise TimeoutError(f"timeout waiting for cmd 0x{cmd:02X}")

    def _on_notify(self, _sender: Any, data: bytearray) -> None:
        frame = proto.decode_packet(bytes(data))
        message = self.assembler.push(frame)
        if message is not None:
            self.messages.append(message)


async def scan_pendants(timeout_s: float) -> list[str]:
    result = await BleakScanner.discover(timeout=timeout_s, return_adv=True)
    addresses: list[str] = []
    for _, pair in result.items():
        dev, adv = pair
        name = dev.name or adv.local_name or ""
        service_uuids = [item.lower() for item in (getattr(adv, "service_uuids", []) or [])]
        is_debug = any(uuid in service_uuids for uuid in DEBUG_UUIDS["service"])
        if "PENDANT" in name.upper() or is_debug:
            addresses.append(dev.address)
    return sorted(set(addresses))


async def run(args: argparse.Namespace) -> int:
    address = args.address
    if not address:
        addresses = await scan_pendants(args.scan_timeout)
        print("scan result:")
        for item in addresses:
            print(f"  {item}")
        if not addresses:
            print("FAIL: no PENDANT found")
            return 2
        address = addresses[0]

    unique_id = proto.build_unique_id(args.product, args.terminal, args.random, args.reserved)
    payload = proto.make_write_identity_payload(unique_id, args.lock)

    async with IdentityWriter(address) as writer:
        before = await writer.send_command_wait(proto.CMD_GET_IDENTITY)
        print("before:", proto.format_response(before))

        rsp = await writer.send_command_wait(proto.CMD_WRITE_IDENTITY, payload)
        print("write:", proto.format_response(rsp))
        if rsp.status != 0:
            return 1

        after = await writer.send_command_wait(proto.CMD_GET_IDENTITY)
        print("after:", proto.format_response(after))
        return 0 if after.status == 0 else 1


def main() -> int:
    parser = argparse.ArgumentParser(description="Write a non-locking PENDANT unique identity over debug GATT.")
    parser.add_argument("--address", default="")
    parser.add_argument("--scan-timeout", type=float, default=6.0)
    parser.add_argument("--product", type=lambda x: int(x, 0), default=0x50444E54)
    parser.add_argument("--terminal", type=lambda x: int(x, 0), required=True)
    parser.add_argument("--random", type=lambda x: int(x, 0), required=True)
    parser.add_argument("--reserved", type=lambda x: int(x, 0), default=0)
    parser.add_argument("--lock", action="store_true")
    return asyncio.run(run(parser.parse_args()))


if __name__ == "__main__":
    raise SystemExit(main())
