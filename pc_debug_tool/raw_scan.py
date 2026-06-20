# -*- coding: utf-8 -*-
from __future__ import annotations

import argparse
import asyncio

from bleak import BleakScanner


async def main() -> int:
    parser = argparse.ArgumentParser(description="Raw BLE advertisement scanner.")
    parser.add_argument("--timeout", type=float, default=8.0)
    args = parser.parse_args()

    result = await BleakScanner.discover(timeout=args.timeout, return_adv=True)
    print(f"count {len(result)}")
    for dev, adv in result.values():
        manufacturer = ",".join(
            f"0x{company:04X}:{bytes(data).hex()[:48]}"
            for company, data in (adv.manufacturer_data or {}).items()
        )
        services = ",".join(adv.service_uuids or [])
        name = dev.name or adv.local_name or ""
        rssi = getattr(adv, "rssi", None)
        print(f"{dev.address} rssi={rssi} name={name} mfg={manufacturer} svc={services}")
    return 0


if __name__ == "__main__":
    raise SystemExit(asyncio.run(main()))
