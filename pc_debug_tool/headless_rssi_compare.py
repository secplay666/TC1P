"""Passive RSSI sampler: listens for advertisements from several addresses
side by side and prints per-device count, median and spread, so the RF
margin of two boards placed at the same spot can be compared.
"""
from __future__ import annotations

import argparse
import asyncio
import statistics

from bleak import BleakScanner


async def main() -> int:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--address", action="append", required=True)
    parser.add_argument("--seconds", type=float, default=30.0)
    args = parser.parse_args()

    wanted = {a.upper() for a in args.address}
    samples: dict[str, list[int]] = {a: [] for a in wanted}

    def on_adv(device, adv):
        addr = device.address.upper()
        if addr in samples:
            samples[addr].append(adv.rssi)

    async with BleakScanner(on_adv):
        await asyncio.sleep(args.seconds)

    for addr in args.address:
        s = samples[addr.upper()]
        if not s:
            print(f"{addr} count=0")
            continue
        q = statistics.quantiles(s, n=10) if len(s) >= 10 else [min(s), max(s)]
        print(f"{addr} count={len(s)} median={statistics.median(s):.0f} "
              f"p10={q[0]:.0f} p90={q[-1]:.0f} min={min(s)} max={max(s)}")
    return 0


if __name__ == "__main__":
    raise SystemExit(asyncio.run(main()))
