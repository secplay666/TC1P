"""Advertising RSSI per pendant and channel from the nRF Sniffer (nRF52840).

Captures with tshark for --seconds (or reads --pcap) and prints, for every
A4:C1:38 address, packet count and median RSSI on channels 37/38/39. The
sniffer is a fixed third-party receiver, so it compares boards' TX side
independently of the PC Bluetooth adapter.
"""
from __future__ import annotations

import argparse
import collections
import shutil
import statistics
import subprocess
import tempfile
from pathlib import Path

TSHARK = shutil.which("tshark") or r"C:\Program Files\Wireshark\tshark.exe"


def find_interface() -> str:
    out = subprocess.run([TSHARK, "-D"], capture_output=True, text=True, errors="replace").stdout
    for line in out.splitlines():
        if "nRF Sniffer" in line:
            return line.split(". ", 1)[1].split(" ", 1)[0]
    raise SystemExit("nRF Sniffer interface not found (tshark -D)")


def main() -> int:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--seconds", type=int, default=30)
    parser.add_argument("--pcap", help="analyse an existing capture instead of capturing")
    parser.add_argument("--save", help="keep the capture at this path")
    parser.add_argument("--prefix", default="a4:c1:38")
    args = parser.parse_args()

    pcap = args.pcap
    if pcap is None:
        pcap = args.save or str(Path(tempfile.gettempdir()) / "glimmer_sniff.pcapng")
        subprocess.run([TSHARK, "-i", find_interface(), "-a", f"duration:{args.seconds}",
                        "-w", pcap, "-q"], check=True, capture_output=True)

    out = subprocess.run([TSHARK, "-r", pcap, "-T", "fields", "-e", "btle.advertising_address",
                          "-e", "nordic_ble.channel", "-e", "nordic_ble.rssi"],
                         capture_output=True, text=True, errors="replace").stdout
    rssi = collections.defaultdict(list)
    for line in out.splitlines():
        parts = line.split("\t")
        if len(parts) != 3 or not parts[0].startswith(args.prefix) or not parts[2]:
            continue
        rssi[(parts[0], parts[1])].append(int(parts[2]))

    for addr in sorted({a for a, _ in rssi}):
        cells = []
        allv = []
        for ch in ("37", "38", "39"):
            v = rssi.get((addr, ch), [])
            allv += v
            cells.append(f"ch{ch} n={len(v):3d} med={statistics.median(v):4.0f}" if v else f"ch{ch} n=  0 med=   -")
        print(f"{addr}  all n={len(allv):3d} med={statistics.median(allv):4.0f} | " + " | ".join(cells))
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
