# -*- coding: utf-8 -*-
"""BLE connection/command stress test against one pendant.

Each cycle connects, sends a burst of the read-only host commands the
Android app uses (GET_DEVICE_INFO / GET_PEER_TABLE / GET_PROFILE_SUMMARY),
checks every response, then disconnects. Prints a summary at the end.
"""
from __future__ import annotations

import argparse
import asyncio
import statistics
import time

from bleak import BleakClient, BleakScanner

import pendant_protocol as proto
from ota_update import find_debug_chars

CMD_GET_PROFILE_SUMMARY = 0x12
COMMANDS = (proto.CMD_GET_DEVICE_INFO, proto.CMD_GET_PEER_TABLE, CMD_GET_PROFILE_SUMMARY)


async def run_cycle(address: str, burst: int, cmd_timeout: float, stats: dict) -> None:
    device = await BleakScanner.find_device_by_address(address, timeout=10.0)
    if device is None:
        raise RuntimeError("not advertising")
    t0 = time.monotonic()
    async with BleakClient(device, timeout=20.0) as client:
        stats["connect_s"].append(time.monotonic() - t0)
        cmd_char, rsp_char = find_debug_chars(client.services)
        if cmd_char is None or rsp_char is None:
            raise RuntimeError("debug service missing")
        assembler = proto.MessageAssembler()
        pending: dict[int, asyncio.Future] = {}

        def on_notify(_sender, data: bytearray) -> None:
            try:
                message = assembler.push(proto.decode_packet(bytes(data)))
            except Exception:
                stats["bad_frames"] += 1
                return
            if message is None or message.frame_type != proto.TYPE_RSP:
                return
            future = pending.pop(message.seq, None)
            if future is not None and not future.done():
                future.set_result(message)

        await client.start_notify(rsp_char, on_notify)
        seq = 1
        for i in range(burst):
            cmd = COMMANDS[i % len(COMMANDS)]
            future = asyncio.get_running_loop().create_future()
            pending[seq] = future
            t1 = time.monotonic()
            for packet in proto.make_empty_command(seq, cmd):
                await client.write_gatt_char(cmd_char, packet, response=True)
            try:
                message = await asyncio.wait_for(future, timeout=cmd_timeout)
            except asyncio.TimeoutError:
                pending.pop(seq, None)
                stats["cmd_timeout"] += 1
            else:
                if message.cmd != cmd or message.status != 0:
                    stats["cmd_error"] += 1
                else:
                    stats["cmd_ok"] += 1
                    stats["cmd_s"].append(time.monotonic() - t1)
                    if cmd == proto.CMD_GET_DEVICE_INFO:
                        info = proto.parse_device_info(message.payload)
                        stats["versions"].add(info.get("fw_version_code"))
                        if "battery_mv" in info:
                            stats["battery"].append(
                                (info["battery_mv"], info["battery_min_mv"], info["charging"], info["battery_valid"]))
            seq = (seq % 250) + 1


async def main() -> int:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--address", required=True)
    parser.add_argument("--cycles", type=int, default=50)
    parser.add_argument("--burst", type=int, default=30, help="commands per connection")
    parser.add_argument("--cmd-timeout", type=float, default=3.0)
    parser.add_argument("--gap", type=float, default=1.0, help="seconds between cycles")
    args = parser.parse_args()

    stats = {"connect_s": [], "cmd_s": [], "cmd_ok": 0, "cmd_timeout": 0, "cmd_error": 0,
             "bad_frames": 0, "versions": set(), "battery": []}
    failures: list[str] = []
    started = time.monotonic()
    for cycle in range(1, args.cycles + 1):
        try:
            await run_cycle(args.address, args.burst, args.cmd_timeout, stats)
            status = "ok"
        except Exception as exc:
            status = f"FAIL {type(exc).__name__}: {exc}"
            failures.append(f"cycle {cycle}: {status}")
        battery = ""
        if stats["battery"]:
            mv, min_mv, charging, valid = stats["battery"][-1]
            battery = f" vbat={mv}mV min={min_mv}mV{' chg' if charging else ''}{'' if valid else ' INVALID'}"
        print(f"cycle {cycle}/{args.cycles} {status} ok={stats['cmd_ok']} "
              f"timeout={stats['cmd_timeout']} err={stats['cmd_error']}{battery}", flush=True)
        await asyncio.sleep(args.gap)

    total_cmds = stats["cmd_ok"] + stats["cmd_timeout"] + stats["cmd_error"]
    print("==== summary ====")
    print(f"elapsed {time.monotonic() - started:.0f}s, cycles {args.cycles}, "
          f"connect failures {len(failures)}")
    if stats["connect_s"]:
        c = stats["connect_s"]
        print(f"connect s: median {statistics.median(c):.2f} max {max(c):.2f}")
    print(f"commands {total_cmds}: ok {stats['cmd_ok']} timeout {stats['cmd_timeout']} "
          f"error {stats['cmd_error']} bad_frames {stats['bad_frames']}")
    if stats["cmd_s"]:
        s = sorted(stats["cmd_s"])
        print(f"command latency ms: median {statistics.median(s) * 1000:.0f} "
              f"p95 {s[int(len(s) * 0.95) - 1] * 1000:.0f} max {s[-1] * 1000:.0f}")
    print(f"firmware versions seen: {sorted(v for v in stats['versions'] if v is not None)}")
    if stats["battery"]:
        mvs = [b[0] for b in stats["battery"]]
        print(f"battery mV: min {min(mvs)} max {max(mvs)}, lowest since boot {min(b[1] for b in stats['battery'])}")
    for line in failures:
        print(line)
    return 0 if not failures and total_cmds == stats["cmd_ok"] else 1


if __name__ == "__main__":
    raise SystemExit(asyncio.run(main()))
