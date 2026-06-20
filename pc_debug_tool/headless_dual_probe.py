# -*- coding: utf-8 -*-
from __future__ import annotations

import argparse
import asyncio

import pendant_protocol as proto
from headless_chat_test import Endpoint, scan_pendants


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
        await asyncio.sleep(0.6)

        for line in args.start_command:
            for ep in (a, b):
                await ep.send_shell(line)
                await asyncio.sleep(args.gap)
        for ep in (a, b):
            await ep.send_shell("p2pclear")
            await asyncio.sleep(args.gap)

        await asyncio.sleep(args.discover_wait)

        for ep in (a, b):
            await ep.send_shell("peers")
            await asyncio.sleep(args.gap)
            await ep.send_shell("radio")
            await asyncio.sleep(args.gap)
            await ep.send_shell("p2pstat")
            await asyncio.sleep(args.gap)

        await asyncio.sleep(args.hold)
        return 0
    finally:
        await b.disconnect()
        await a.disconnect()


def main() -> int:
    parser = argparse.ArgumentParser(description="Dual pendant GATT/radio probe without P2P chat.")
    parser.add_argument("--addr-a", default="")
    parser.add_argument("--addr-b", default="")
    parser.add_argument("--scan-timeout", type=float, default=8.0)
    parser.add_argument("--discover-wait", type=float, default=5.0)
    parser.add_argument("--start-command", action="append", default=[], help="Shell command used to start radio; can be repeated")
    parser.add_argument("--gap", type=float, default=1.0)
    parser.add_argument("--hold", type=float, default=2.0)
    args = parser.parse_args()
    if not args.start_command:
        args.start_command = ["ble start"]
    return asyncio.run(run(args))


if __name__ == "__main__":
    raise SystemExit(main())
