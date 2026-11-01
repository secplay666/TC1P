"""Hold a connection and print the motion counter every 200 ms."""
import asyncio
import sys
import time

from bleak import BleakClient

import pendant_protocol as proto
from ota_update import find_debug_chars


async def main(address: str, seconds: float) -> None:
    async with BleakClient(address, timeout=30.0) as client:
        cmd, rsp = find_debug_chars(client.services)
        assembler = proto.MessageAssembler()
        latest: dict = {}

        def on_notify(_sender, data: bytearray) -> None:
            message = assembler.push(proto.decode_packet(bytes(data)))
            if message is not None and message.cmd == proto.CMD_GET_DEVICE_INFO:
                latest["info"] = proto.parse_device_info(message.payload)

        await client.start_notify(rsp, on_notify)
        print("READY", flush=True)
        start = time.monotonic()
        last = None
        seq = 1
        while time.monotonic() - start < seconds:
            for packet in proto.make_empty_command(seq, proto.CMD_GET_DEVICE_INFO):
                await client.write_gatt_char(cmd, packet, response=True)
            seq = seq % 250 + 1
            await asyncio.sleep(0.2)
            count = latest.get("info", {}).get("motion_count")
            if count != last:
                print(f"t={time.monotonic() - start:5.1f}s motion_count={count}", flush=True)
                last = count


if __name__ == "__main__":
    asyncio.run(main(sys.argv[1], float(sys.argv[2]) if len(sys.argv) > 2 else 30.0))
