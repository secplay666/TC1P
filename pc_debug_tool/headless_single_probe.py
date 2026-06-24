# -*- coding: utf-8 -*-
from __future__ import annotations

import argparse
import asyncio

import pendant_protocol as proto
from headless_chat_test import Endpoint, scan_pendants


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

    ep = Endpoint("P", address)
    try:
        await ep.connect()
        await asyncio.sleep(0.3)
        await ep.send_command(proto.CMD_LOG_ENABLE, b"\x00")
        await asyncio.sleep(0.4)
        for line in args.command:
            await ep.send_shell(line)
            await asyncio.sleep(args.gap)
        await asyncio.sleep(args.hold)
        return 0
    finally:
        await ep.disconnect()


def main() -> int:
    parser = argparse.ArgumentParser(description="Single Glimmer GATT shell probe.")
    parser.add_argument("--address", default="", help="BLE address")
    parser.add_argument("--scan-timeout", type=float, default=6.0)
    parser.add_argument("--command", action="append", default=[], help="Shell command; can be repeated")
    parser.add_argument("--gap", type=float, default=0.8)
    parser.add_argument("--hold", type=float, default=2.0)
    args = parser.parse_args()
    if not args.command:
        args.command = ["ble start", "peers"]
    return asyncio.run(run(args))


if __name__ == "__main__":
    raise SystemExit(main())
