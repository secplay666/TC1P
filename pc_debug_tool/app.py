# -*- coding: utf-8 -*-
from __future__ import annotations

import asyncio
import queue
import struct
import threading
import time
import tkinter as tk
from tkinter import messagebox, scrolledtext, ttk
from typing import Any, Dict, Iterable, Optional

import pendant_protocol as proto

try:
    from bleak import BleakClient, BleakScanner
except Exception as exc:  # pragma: no cover - shown in GUI at runtime
    BleakClient = None
    BleakScanner = None
    BLEAK_IMPORT_ERROR: Optional[Exception] = exc
else:
    BLEAK_IMPORT_ERROR = None


DEBUG_UUIDS = {
    "service": [
        "50544e44-0001-4b45-5931-444556000001",
        "56000001-4445-5931-454b-0100444e5450",
    ],
    "cmd": [
        "50544e44-0002-4b45-5931-444556000001",
        "56000001-4445-5931-454b-0200444e5450",
    ],
    "rsp": [
        "50544e44-0003-4b45-5931-444556000001",
        "56000001-4445-5931-454b-0300444e5450",
    ],
    "log": [
        "50544e44-0004-4b45-5931-444556000001",
        "56000001-4445-5931-454b-0400444e5450",
    ],
    "evt": [
        "50544e44-0005-4b45-5931-444556000001",
        "56000001-4445-5931-454b-0500444e5450",
    ],
}


def now_text() -> str:
    return time.strftime("%H:%M:%S")


def normalize_uuid(value: str) -> str:
    return value.lower()


def iter_services(services: Any) -> Iterable[Any]:
    try:
        return list(services)
    except TypeError:
        return list(getattr(services, "services", {}).values())


class BleWorker:
    def __init__(self, events: "queue.Queue[dict]") -> None:
        self.events = events
        self.loop = asyncio.new_event_loop()
        self.ready = threading.Event()
        self.thread = threading.Thread(target=self._thread_main, daemon=True)
        self.thread.start()
        self.ready.wait(timeout=5)

        self.client: Optional[Any] = None
        self.chars: Dict[str, Any] = {}
        self.assembler = proto.MessageAssembler()
        self.seq = 1

    def _thread_main(self) -> None:
        asyncio.set_event_loop(self.loop)
        self.ready.set()
        self.loop.run_forever()

    def post(self, event: str, **payload: Any) -> None:
        payload["event"] = event
        self.events.put(payload)

    def submit(self, coro: Any) -> None:
        future = asyncio.run_coroutine_threadsafe(coro, self.loop)

        def done(fut: Any) -> None:
            try:
                fut.result()
            except Exception as exc:
                self.post("error", message=str(exc))

        future.add_done_callback(done)

    def scan(self, timeout_s: float = 5.0) -> None:
        self.submit(self._scan(timeout_s))

    def connect(self, address: str) -> None:
        self.submit(self._connect(address))

    def disconnect(self) -> None:
        self.submit(self._disconnect())

    def send_command(self, cmd: int, payload: bytes = b"") -> None:
        self.submit(self._send_packets(proto.encode_message(proto.TYPE_CMD, self._next_seq(), cmd, 0, payload)))

    def send_packets(self, packets: list[bytes]) -> None:
        self.submit(self._send_packets(packets))

    def _next_seq(self) -> int:
        value = self.seq
        self.seq = 1 if self.seq >= 255 else self.seq + 1
        return value

    async def _scan(self, timeout_s: float) -> None:
        if BLEAK_IMPORT_ERROR:
            raise RuntimeError(f"bleak not installed: {BLEAK_IMPORT_ERROR}")

        self.post("status", text="扫描中...")
        devices: list[dict] = []
        try:
            result = await BleakScanner.discover(timeout=timeout_s, return_adv=True)
            for _, pair in result.items():
                dev, adv = pair
                devices.append(
                    {
                        "name": dev.name or adv.local_name or "",
                        "address": dev.address,
                        "rssi": getattr(adv, "rssi", None),
                        "services": list(getattr(adv, "service_uuids", []) or []),
                    }
                )
        except TypeError:
            result = await BleakScanner.discover(timeout=timeout_s)
            for dev in result:
                devices.append(
                    {
                        "name": dev.name or "",
                        "address": dev.address,
                        "rssi": getattr(dev, "rssi", None),
                        "services": [],
                    }
                )

        devices.sort(key=lambda d: (0 if "PENDANT" in d["name"].upper() else 1, d["name"], d["address"]))
        self.post("scan_result", devices=devices)
        self.post("status", text=f"扫描完成，发现 {len(devices)} 个设备")

    async def _connect(self, address: str) -> None:
        if BLEAK_IMPORT_ERROR:
            raise RuntimeError(f"bleak not installed: {BLEAK_IMPORT_ERROR}")

        await self._disconnect()
        self.post("status", text=f"连接 {address} ...")
        self.assembler.reset()

        self.client = BleakClient(address, disconnected_callback=self._on_disconnected)
        await self.client.connect(timeout=15.0)
        self.post("status", text="已连接，发现 GATT 服务...")

        try:
            services = self.client.services
        except Exception:
            services = None
        if services is None and hasattr(self.client, "get_services"):
            services = await self.client.get_services()
        if services is None:
            raise RuntimeError("GATT 服务发现失败")

        service_text = self._describe_services(services)
        self.chars = self._find_debug_chars(services)
        self.post("services", text=service_text)

        missing = [role for role in ("cmd", "rsp", "log", "evt") if role not in self.chars]
        if missing:
            self.post("connected", address=address, debug_ready=False)
            raise RuntimeError("未找到调试特征: " + ", ".join(missing))

        await self.client.start_notify(self.chars["rsp"], self._on_notify)
        await self.client.start_notify(self.chars["log"], self._on_notify)
        await self.client.start_notify(self.chars["evt"], self._on_notify)
        self.post("connected", address=address, debug_ready=True)
        self.post("status", text="调试通道已就绪")

    async def _disconnect(self) -> None:
        if self.client:
            client = self.client
            self.client = None
            self.chars = {}
            self.assembler.reset()
            try:
                if getattr(client, "is_connected", False):
                    await client.disconnect()
            finally:
                self.post("disconnected")

    async def _send_packets(self, packets: list[bytes]) -> None:
        if not self.client or not getattr(self.client, "is_connected", False):
            raise RuntimeError("设备未连接")
        if "cmd" not in self.chars:
            raise RuntimeError("未找到 Command 特征")

        for packet in packets:
            try:
                await self.client.write_gatt_char(self.chars["cmd"], packet, response=False)
            except Exception:
                await self.client.write_gatt_char(self.chars["cmd"], packet, response=True)
            await asyncio.sleep(0.02)
        self.post("tx", packets=packets)

    def _on_disconnected(self, _: Any) -> None:
        self.client = None
        self.chars = {}
        self.assembler.reset()
        self.post("disconnected")
        self.post("status", text="连接已断开")

    def _on_notify(self, sender: Any, data: bytearray) -> None:
        raw = bytes(data)
        try:
            frame = proto.decode_packet(raw)
            message = self.assembler.push(frame)
        except Exception as exc:
            self.post("rx_error", message=str(exc), raw=raw)
            return

        self.post("rx_frame", frame=frame, raw=raw)
        if message is not None:
            self.post("rx_message", message=message)

    def _find_debug_chars(self, services: Any) -> Dict[str, Any]:
        found: Dict[str, Any] = {}
        wanted = {uuid: role for role, uuids in DEBUG_UUIDS.items() for uuid in uuids}

        for service in iter_services(services):
            for char in getattr(service, "characteristics", []):
                role = wanted.get(normalize_uuid(char.uuid))
                if role and role != "service":
                    found[role] = char
        return found

    def _describe_services(self, services: Any) -> str:
        lines: list[str] = []
        for service in iter_services(services):
            lines.append(f"Service {service.uuid} {getattr(service, 'description', '')}")
            for char in getattr(service, "characteristics", []):
                props = ",".join(getattr(char, "properties", []) or [])
                lines.append(f"  Char {char.uuid} [{props}] handle={getattr(char, 'handle', '')}")
                for desc in getattr(char, "descriptors", []) or []:
                    lines.append(f"    Desc {desc.uuid} handle={getattr(desc, 'handle', '')}")
        return "\n".join(lines)


class PendantDebugApp:
    def __init__(self, root: tk.Tk) -> None:
        self.root = root
        self.root.title("蓝牙吊坠 PC 调试工具")
        self.root.geometry("1160x760")
        self.events: "queue.Queue[dict]" = queue.Queue()
        self.worker = BleWorker(self.events)
        self.devices: list[dict] = []
        self.connected = False

        self.status_var = tk.StringVar(value="未连接")
        self.log_enable_var = tk.BooleanVar(value=True)
        self._build_ui()
        self.root.after(80, self._poll_events)

        if BLEAK_IMPORT_ERROR:
            self._log(f"缺少 bleak 依赖: {BLEAK_IMPORT_ERROR}")
            self._log("请先运行: python -m pip install -r pc_debug_tool/requirements.txt")

    def _build_ui(self) -> None:
        root_frame = ttk.Frame(self.root, padding=8)
        root_frame.pack(fill=tk.BOTH, expand=True)

        ttk.Label(root_frame, textvariable=self.status_var).pack(fill=tk.X, pady=(0, 6))

        paned = ttk.PanedWindow(root_frame, orient=tk.HORIZONTAL)
        paned.pack(fill=tk.BOTH, expand=True)

        left = ttk.Frame(paned, padding=(0, 0, 8, 0))
        paned.add(left, weight=1)

        right = ttk.Frame(paned)
        paned.add(right, weight=4)

        scan_bar = ttk.Frame(left)
        scan_bar.pack(fill=tk.X)
        ttk.Button(scan_bar, text="扫描", command=self._scan).pack(side=tk.LEFT)
        ttk.Button(scan_bar, text="连接", command=self._connect_selected).pack(side=tk.LEFT, padx=4)
        ttk.Button(scan_bar, text="断开", command=self._disconnect).pack(side=tk.LEFT)

        self.device_list = tk.Listbox(left, height=24)
        self.device_list.pack(fill=tk.BOTH, expand=True, pady=(8, 0))

        notebook = ttk.Notebook(right)
        notebook.pack(fill=tk.BOTH, expand=True)

        self.control_tab = ttk.Frame(notebook, padding=8)
        self.message_tab = ttk.Frame(notebook, padding=8)
        self.peer_tab = ttk.Frame(notebook, padding=8)
        self.service_tab = ttk.Frame(notebook, padding=8)
        notebook.add(self.control_tab, text="控制")
        notebook.add(self.message_tab, text="消息")
        notebook.add(self.peer_tab, text="发现设备")
        notebook.add(self.service_tab, text="GATT")

        self._build_control_tab()
        self._build_message_tab()
        self._build_peer_tab()
        self._build_service_tab()

    def _build_control_tab(self) -> None:
        buttons = ttk.LabelFrame(self.control_tab, text="常用命令", padding=8)
        buttons.pack(fill=tk.X)
        commands = [
            ("设备信息", lambda: self._send_empty(proto.CMD_GET_DEVICE_INFO)),
            ("系统状态", lambda: self._send_empty(proto.CMD_GET_SYSTEM_STATE)),
            ("广播帧", lambda: self._send_empty(proto.CMD_GET_ADV_FRAME)),
            ("邻近表", lambda: self._send_empty(proto.CMD_GET_PEER_TABLE)),
            ("清统计", lambda: self._send_empty(proto.CMD_DEBUG_RESET_STATS)),
            ("进入休眠", lambda: self._send_empty(proto.CMD_ENTER_SLEEP)),
        ]
        for idx, (text, cmd) in enumerate(commands):
            ttk.Button(buttons, text=text, command=cmd).grid(row=0, column=idx, padx=3, pady=3)

        log_row = ttk.Frame(self.control_tab)
        log_row.pack(fill=tk.X, pady=8)
        ttk.Checkbutton(log_row, text="启用日志 Notify", variable=self.log_enable_var, command=self._send_log_enable).pack(side=tk.LEFT)

        motor = ttk.LabelFrame(self.control_tab, text="马达测试", padding=8)
        motor.pack(fill=tk.X, pady=(0, 8))
        self.motor_var = tk.StringVar(value="1 - 一次")
        ttk.Combobox(
            motor,
            textvariable=self.motor_var,
            state="readonly",
            values=["1 - 一次", "2 - 两次", "3 - 三次", "4 - 错误"],
            width=14,
        ).pack(side=tk.LEFT)
        ttk.Button(motor, text="发送", command=self._send_motor).pack(side=tk.LEFT, padx=8)

        rssi = ttk.LabelFrame(self.control_tab, text="RSSI 参数", padding=8)
        rssi.pack(fill=tk.X, pady=(0, 8))
        self.rssi_t1 = self._entry(rssi, "T1", "-80", 0)
        self.rssi_t2 = self._entry(rssi, "T2", "-65", 2)
        self.rssi_t3 = self._entry(rssi, "T3", "-50", 4)
        self.rssi_tin = self._entry(rssi, "Tin ms", "1500", 6)
        self.rssi_tout = self._entry(rssi, "Tout ms", "2500", 8)
        ttk.Button(rssi, text="写入", command=self._send_rssi).grid(row=0, column=10, padx=8)

        info_frame = ttk.LabelFrame(self.control_tab, text="解析结果", padding=8)
        info_frame.pack(fill=tk.BOTH, expand=True)
        self.info_text = scrolledtext.ScrolledText(info_frame, height=14, wrap=tk.WORD)
        self.info_text.pack(fill=tk.BOTH, expand=True)

    def _build_message_tab(self) -> None:
        self.log_text = scrolledtext.ScrolledText(self.message_tab, wrap=tk.WORD)
        self.log_text.pack(fill=tk.BOTH, expand=True)

        raw = ttk.Frame(self.message_tab)
        raw.pack(fill=tk.X, pady=(8, 0))
        ttk.Label(raw, text="Raw Command Frame Hex").pack(side=tk.LEFT)
        self.raw_var = tk.StringVar()
        ttk.Entry(raw, textvariable=self.raw_var).pack(side=tk.LEFT, fill=tk.X, expand=True, padx=6)
        ttk.Button(raw, text="发送", command=self._send_raw).pack(side=tk.LEFT)

    def _build_peer_tab(self) -> None:
        columns = ("short_id", "level", "rssi", "rssi_avg", "flags")
        self.peer_tree = ttk.Treeview(self.peer_tab, columns=columns, show="headings")
        for col, title, width in [
            ("short_id", "Short ID", 140),
            ("level", "Level", 90),
            ("rssi", "RSSI", 80),
            ("rssi_avg", "RSSI Avg", 90),
            ("flags", "Flags", 80),
        ]:
            self.peer_tree.heading(col, text=title)
            self.peer_tree.column(col, width=width, anchor=tk.CENTER)
        self.peer_tree.pack(fill=tk.BOTH, expand=True)

    def _build_service_tab(self) -> None:
        self.service_text = scrolledtext.ScrolledText(self.service_tab, wrap=tk.NONE)
        self.service_text.pack(fill=tk.BOTH, expand=True)

    def _entry(self, parent: ttk.Frame, label: str, value: str, col: int) -> ttk.Entry:
        ttk.Label(parent, text=label).grid(row=0, column=col, padx=(0, 2))
        var = tk.StringVar(value=value)
        entry = ttk.Entry(parent, textvariable=var, width=8)
        entry.grid(row=0, column=col + 1, padx=(0, 8))
        entry.var = var  # type: ignore[attr-defined]
        return entry

    def _scan(self) -> None:
        self.worker.scan()

    def _connect_selected(self) -> None:
        selection = self.device_list.curselection()
        if not selection:
            messagebox.showinfo("提示", "请先选择一个设备")
            return
        self.worker.connect(self.devices[selection[0]]["address"])

    def _disconnect(self) -> None:
        self.worker.disconnect()

    def _send_empty(self, cmd: int) -> None:
        self.worker.send_command(cmd)
        self._log(f"TX {proto.CMD_NAMES.get(cmd, hex(cmd))}")

    def _send_log_enable(self) -> None:
        payload = bytes([1 if self.log_enable_var.get() else 0])
        self.worker.send_command(proto.CMD_LOG_ENABLE, payload)
        self._log(f"TX LOG_ENABLE {payload[0]}")

    def _send_motor(self) -> None:
        pattern = int(self.motor_var.get().split()[0])
        self.worker.send_command(proto.CMD_MOTOR_TEST, bytes([pattern]))
        self._log(f"TX MOTOR_TEST {pattern}")

    def _send_rssi(self) -> None:
        try:
            t1 = int(self.rssi_t1.var.get())  # type: ignore[attr-defined]
            t2 = int(self.rssi_t2.var.get())  # type: ignore[attr-defined]
            t3 = int(self.rssi_t3.var.get())  # type: ignore[attr-defined]
            tin = int(self.rssi_tin.var.get())  # type: ignore[attr-defined]
            tout = int(self.rssi_tout.var.get())  # type: ignore[attr-defined]
            payload = struct.pack("<bbbHH", t1, t2, t3, tin, tout)
        except Exception as exc:
            messagebox.showerror("参数错误", str(exc))
            return
        self.worker.send_command(proto.CMD_SET_RSSI_CONFIG, payload)
        self._log(f"TX SET_RSSI_CONFIG t1={t1}, t2={t2}, t3={t3}, tin={tin}, tout={tout}")

    def _send_raw(self) -> None:
        text = self.raw_var.get().replace("0x", "").replace(",", " ").replace(";", " ")
        try:
            raw = bytes.fromhex(text)
        except ValueError as exc:
            messagebox.showerror("Hex 错误", str(exc))
            return
        self.worker.send_packets([raw])
        self._log(f"TX RAW {proto.hex_bytes(raw)}")

    def _poll_events(self) -> None:
        while True:
            try:
                event = self.events.get_nowait()
            except queue.Empty:
                break
            self._handle_event(event)
        self.root.after(80, self._poll_events)

    def _handle_event(self, event: dict) -> None:
        kind = event.get("event")
        if kind == "status":
            self.status_var.set(event["text"])
        elif kind == "scan_result":
            self._set_devices(event["devices"])
        elif kind == "connected":
            self.connected = True
            self.status_var.set("已连接" if event.get("debug_ready") else "已连接，但未找到调试服务")
            self._log(f"CONNECTED {event.get('address')} debug_ready={event.get('debug_ready')}")
        elif kind == "disconnected":
            self.connected = False
            self._log("DISCONNECTED")
        elif kind == "services":
            self._set_text(self.service_text, event["text"])
        elif kind == "error":
            self._log("ERROR " + event["message"])
            self.status_var.set("错误: " + event["message"])
        elif kind == "rx_error":
            self._log(f"RX ERROR {event['message']} raw={proto.hex_bytes(event['raw'])}")
        elif kind == "rx_frame":
            frame = event["frame"]
            self._log(
                f"RX FRAME type={frame.frame_type} seq={frame.seq} cmd=0x{frame.cmd:02X} "
                f"frag={frame.frag_index + 1}/{frame.frag_count}"
            )
        elif kind == "rx_message":
            self._handle_message(event["message"])
        elif kind == "tx":
            for packet in event["packets"]:
                self._log("TX PACKET " + proto.hex_bytes(packet))

    def _set_devices(self, devices: list[dict]) -> None:
        self.devices = devices
        self.device_list.delete(0, tk.END)
        for dev in devices:
            name = dev["name"] or "(no name)"
            rssi = "" if dev["rssi"] is None else f" {dev['rssi']}dBm"
            self.device_list.insert(tk.END, f"{name}  {dev['address']}{rssi}")

    def _handle_message(self, message: proto.HostMessage) -> None:
        if message.frame_type == proto.TYPE_RSP:
            summary = proto.format_response(message)
            self._log("RSP " + summary)
            self._append_info(summary)
            if message.cmd == proto.CMD_GET_PEER_TABLE and message.status == 0:
                self._update_peer_table(proto.parse_peer_table(message.payload))
            return

        if message.frame_type == proto.TYPE_LOG:
            try:
                log = proto.parse_log(message.payload)
                self._log(f"LOG[{log['count']}] L{log['level']} {log['text']}")
            except Exception as exc:
                self._log(f"LOG parse error: {exc}; raw={proto.hex_bytes(message.payload)}")
            return

        if message.frame_type == proto.TYPE_EVENT:
            try:
                parsed = proto.parse_event(message.cmd, message.payload)
                self._log("EVT " + str(parsed))
            except Exception as exc:
                self._log(f"EVT parse error: {exc}; raw={proto.hex_bytes(message.payload)}")
            return

        self._log(f"RX MSG type={message.frame_type} raw={proto.hex_bytes(message.payload)}")

    def _update_peer_table(self, peers: list[dict]) -> None:
        for item in self.peer_tree.get_children():
            self.peer_tree.delete(item)
        for peer in peers:
            self.peer_tree.insert(
                "",
                tk.END,
                values=(
                    f"0x{peer['short_id']:08X}",
                    peer["level_name"],
                    peer["rssi"],
                    peer["rssi_avg"],
                    f"0x{peer['flags']:02X}",
                ),
            )

    def _set_text(self, widget: scrolledtext.ScrolledText, text: str) -> None:
        widget.configure(state=tk.NORMAL)
        widget.delete("1.0", tk.END)
        widget.insert(tk.END, text)
        widget.configure(state=tk.NORMAL)

    def _append_info(self, text: str) -> None:
        self.info_text.insert(tk.END, f"[{now_text()}] {text}\n")
        self.info_text.see(tk.END)

    def _log(self, text: str) -> None:
        self.log_text.insert(tk.END, f"[{now_text()}] {text}\n")
        self.log_text.see(tk.END)


def main() -> None:
    root = tk.Tk()
    app = PendantDebugApp(root)
    root.mainloop()


if __name__ == "__main__":
    main()
