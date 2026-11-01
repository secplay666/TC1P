"""Read GET_DEVICE_INFO once and print version, OTA guard and battery fields."""
import asyncio
import sys

from bleak import BleakClient, BleakScanner

import pendant_protocol as proto
from ota_update import find_debug_chars


async def main(address: str) -> int:
    device = await BleakScanner.find_device_by_address(address, timeout=10.0)
    if device is None:
        print("info: not advertising")
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
        for packet in proto.make_empty_command(1, proto.CMD_GET_DEVICE_INFO):
            await client.write_gatt_char(cmd, packet, response=True)
        info = proto.parse_device_info((await asyncio.wait_for(future, 5.0)).payload)
    print(f"info: ver={info['fw_version_code']} guard_state={info['ota_guard_state']} "
          f"active_slot={info['ota_active_slot']} confirmed={info['ota_confirmed_version']} "
          f"vbat={info.get('battery_mv')}mV min={info.get('battery_min_mv')}mV "
          f"charging={info.get('charging')} motion={info.get('motion_count')} "
          f"wakeup_src=0x{info.get('wakeup_src', 0):02X} idle_blockers={info.get('idle_blockers')} idle_s={info.get('idle_seconds')}")
    return 0


if __name__ == "__main__":
    raise SystemExit(asyncio.run(main(sys.argv[1])))
