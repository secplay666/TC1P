"""Set or query the pendant radio TX power (RF test firmware, cmd 0x16).

The level is RAM only and returns to +3 dBm on reboot. --dbm picks the
nearest level from the B85 table in rf_drv.h; omit it to just query.
"""
from __future__ import annotations

import argparse
import asyncio

from bleak import BleakClient, BleakScanner

import pendant_protocol as proto
from ota_update import find_debug_chars

# dBm -> RF_PowerTypeDef register value (drivers/B85/lib/include/rf_drv.h)
TX_LEVELS = {
    10.46: 63, 9.48: 53, 8.44: 45, 7.41: 39, 6.60: 35, 5.65: 31, 4.57: 27,
    3.94: 25, 3.01: 0x80 | 63, 1.99: 0x80 | 54, 0.04: 0x80 | 41,
    -1.89: 0x80 | 32, -3.03: 0x80 | 28, -5.03: 0x80 | 22, -7.65: 0x80 | 16,
    -9.89: 0x80 | 12, -13.29: 0x80 | 8, -15.88: 0x80 | 6, -19.27: 0x80 | 4,
    -25.18: 0x80 | 2,
}
LEVEL_TO_DBM = {v: k for k, v in TX_LEVELS.items()}


async def main() -> int:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("address")
    parser.add_argument("--dbm", type=float)
    args = parser.parse_args()

    payload = b""
    if args.dbm is not None:
        dbm = min(TX_LEVELS, key=lambda k: abs(k - args.dbm))
        payload = bytes([TX_LEVELS[dbm]])

    device = await BleakScanner.find_device_by_address(args.address, timeout=10.0)
    if device is None:
        print("tx_power: not advertising")
        return 1
    async with BleakClient(device, timeout=30.0) as client:
        cmd, rsp = find_debug_chars(client.services)
        assembler = proto.MessageAssembler()
        future = asyncio.get_running_loop().create_future()

        def on_notify(_sender, data: bytearray) -> None:
            message = assembler.push(proto.decode_packet(bytes(data)))
            if message is not None and not future.done():
                future.set_result(message)

        await client.start_notify(rsp, on_notify)
        for packet in proto.encode_message(proto.TYPE_CMD, 1, proto.CMD_SET_TX_POWER, 0, payload):
            await client.write_gatt_char(cmd, packet, response=True)
        message = await asyncio.wait_for(future, 5.0)
    if message.status != 0 or not message.payload:
        print(f"tx_power: status={message.status} (firmware without RF test support?)")
        return 1
    level = message.payload[0]
    print(f"tx_power: level=0x{level:02X} ({LEVEL_TO_DBM.get(level, '?')} dBm)")
    return 0


if __name__ == "__main__":
    raise SystemExit(asyncio.run(main()))
