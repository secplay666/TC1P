"""Follow one pendant's BLE connection with the nRF Sniffer and measure link quality.

Locks the nRF52840 sniffer onto --address, opens a connection from this PC
that polls GET_DEVICE_INFO (headless_motion_watch.py) for --seconds, then
reports per direction: packets seen, CRC errors, retransmissions (same SN
repeated) and sniffer RSSI.

A central retransmission means the pendant did not acknowledge, i.e. the
pendant missed the PC's packet (pendant RX side). A peripheral
retransmission means the PC missed the pendant's packet (pendant TX side).
"""
from __future__ import annotations

import argparse
import statistics
import subprocess
import sys
import threading
import time
from pathlib import Path

EXTCAP = Path(r"C:\Program Files\Wireshark\extcap")
sys.path.insert(0, str(EXTCAP))
from SnifferAPI import Devices, Pcap, Sniffer, UART  # noqa: E402

HERE = Path(__file__).resolve().parent


def main() -> int:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--address", required=True)
    parser.add_argument("--port", default="")
    parser.add_argument("--seconds", type=float, default=30.0)
    parser.add_argument("--random", action="store_true", help="address is random, not public")
    parser.add_argument("--pcap", help="also save every sniffed packet to this pcap (open in Wireshark)")
    args = parser.parse_args()

    port = args.port or UART.find_sniffer()[0]
    sniffer = Sniffer.Sniffer(port, UART.find_sniffer_baudrates(port)["default"])
    packets = []
    lock = threading.Lock()

    pcap = open(args.pcap, "wb") if args.pcap else None
    if pcap:
        pcap.write(Pcap.get_global_header())

    def on_packet(notification):
        p = notification.msg["packet"]
        if pcap:
            with lock:
                pcap.write(Pcap.create_packet(bytes([p.boardId] + p.getList()), p.time))
        if p.blePacket is None or not hasattr(p.blePacket, "sn"):
            return
        with lock:
            packets.append((time.monotonic(), p.eventCounter, p.direction, p.crcOK,
                            p.RSSI, p.blePacket.sn, p.blePacket.nesn, p.blePacket.length))

    sniffer.subscribe("NEW_BLE_PACKET", on_packet)
    sniffer.setAdvHopSequence([37, 38, 39])
    sniffer.setSupportedProtocolVersion(3)
    sniffer.getFirmwareVersion()
    sniffer.getTimestamp()
    sniffer.start()
    sniffer.scan()
    time.sleep(1.0)
    addr = [int(b, 16) for b in args.address.split(":")] + [1 if args.random else 0]
    device = Devices.Device(address=addr, name='""', RSSI=0)
    sniffer.addDevice(device)
    sniffer.follow(device)
    time.sleep(1.0)

    watch = subprocess.run([sys.executable, str(HERE / "headless_motion_watch.py"), args.address,
                            str(args.seconds)], capture_output=True, text=True)
    time.sleep(1.0)
    sniffer.doExit()
    if pcap:
        pcap.close()
    if watch.returncode != 0:
        print("connection failed:", (watch.stdout + watch.stderr).strip()[-200:])

    with lock:
        data = list(packets)
    if not data:
        print("no connection packets captured (sniffer did not follow the connect request)")
        return 1

    # The central always transmits first in a connection event.
    first_dir = {}
    for _, ev, d, *_ in data:
        first_dir.setdefault(ev, d)
    central_dir = statistics.mode(first_dir.values())
    events = sorted(first_dir)
    span = events[-1] - events[0] + 1

    print(f"connection events seen {len(events)} of {span} in span "
          f"({100.0 * len(events) / span:.0f}% followed by sniffer)")
    for name, want in (("central (PC) ", central_dir), ("pendant      ", not central_dir)):
        side = [p for p in data if p[2] == want]
        ok = [p for p in side if p[3]]
        # Only compare packets from back-to-back events: a packet the sniffer
        # itself missed would otherwise look like an unchanged SN.
        retx = 0
        pairs = 0
        prev_sn = None
        prev_ev = None
        for _, ev, _, _, _, sn, _, _ in ok:
            if prev_ev is not None and ev == prev_ev + 1:
                pairs += 1
                retx += sn == prev_sn
            prev_sn, prev_ev = sn, ev
        rssi = [p[4] for p in ok]
        print(f"{name} packets={len(side):5d} crc_err={len(side) - len(ok):4d} "
              f"retransmit={retx:4d}/{pairs} ({100.0 * retx / max(pairs, 1):.1f}%) "
              f"sniffer_rssi_med={statistics.median(rssi) if rssi else float('nan'):.0f}")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
