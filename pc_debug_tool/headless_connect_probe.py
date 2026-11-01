"""Repeated connect probe: classifies each attempt by the phase that fails.

Phases: scan (device not seen), connect (link/GATT setup timed out or was
cancelled), services (debug service missing), ok. Prints per-attempt timing
and a summary, so connection-timeout behaviour can be compared between
firmware builds.
"""
from __future__ import annotations

import argparse
import asyncio
import collections
import statistics
import time

from bleak import BleakClient, BleakScanner

from ota_update import find_debug_chars


async def attempt(address: str, scan_timeout: float, connect_timeout: float) -> tuple[str, float, float, str]:
    t0 = time.monotonic()
    device = await BleakScanner.find_device_by_address(address, timeout=scan_timeout)
    t_scan = time.monotonic() - t0
    if device is None:
        return "scan", t_scan, 0.0, ""
    t1 = time.monotonic()
    try:
        async with BleakClient(device, timeout=connect_timeout) as client:
            t_conn = time.monotonic() - t1
            cmd, rsp = find_debug_chars(client.services)
            if cmd is None or rsp is None:
                return "services", t_scan, t_conn, "debug chars missing"
            return "ok", t_scan, t_conn, f"mtu={client.mtu_size}"
    except Exception as exc:  # noqa: BLE001 - classify everything
        return "connect", t_scan, time.monotonic() - t1, f"{type(exc).__name__}: {exc}"[:80]


async def main() -> int:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--address", required=True)
    parser.add_argument("--count", type=int, default=20)
    parser.add_argument("--gap", type=float, default=2.0)
    parser.add_argument("--scan-timeout", type=float, default=10.0)
    parser.add_argument("--connect-timeout", type=float, default=20.0)
    args = parser.parse_args()

    results = []
    for i in range(1, args.count + 1):
        phase, t_scan, t_conn, note = await attempt(args.address, args.scan_timeout, args.connect_timeout)
        results.append((phase, t_scan, t_conn))
        print(f"{i:3d} {phase:8s} scan={t_scan:5.2f}s conn={t_conn:5.2f}s {note}", flush=True)
        await asyncio.sleep(args.gap)

    counts = collections.Counter(r[0] for r in results)
    print("==== summary ====")
    print(" ".join(f"{k}={v}" for k, v in sorted(counts.items())), f"of {len(results)}")
    ok = [r for r in results if r[0] == "ok"]
    if ok:
        print(f"ok: scan median {statistics.median(r[1] for r in ok):.2f}s, "
              f"connect median {statistics.median(r[2] for r in ok):.2f}s max {max(r[2] for r in ok):.2f}s")
    return 0


if __name__ == "__main__":
    raise SystemExit(asyncio.run(main()))
