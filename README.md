# 蓝牙吊坠固件构建与下载

当前下位机主工程位于 `tc_ble_multi_sdk/`，目标芯片为 Telink B85 / TLSR8258。`tc_ble_single_sdk/` 仍保留作历史对照，后续确认 Multi SDK 路线稳定后再删除。

## 环境要求

- Windows PowerShell
- Telink IoT Studio，默认路径：`C:\TelinkIoTStudio`
- BDT 命令行工具，默认路径：`C:\TelinkIoTStudio\tools\TBD_release\config\Cmd_download_tool.exe`
- TC32 工具链由 `tools/build_pendant_multi.ps1` 自动从 Telink IoT Studio 路径下查找

## 构建固件

日常增量构建：

```powershell
powershell -ExecutionPolicy Bypass -File .\tools\build_pendant_multi.ps1
```

全量清理后构建：

```powershell
powershell -ExecutionPolicy Bypass -File .\tools\build_pendant_multi.ps1 -Clean
```

输出固件：

```text
tc_ble_multi_sdk\build\B85\pendant\pendant.bin
```

如果 Telink IoT Studio 不在默认路径：

```powershell
powershell -ExecutionPolicy Bypass -File .\tools\build_pendant_multi.ps1 -TelinkStudioPath "C:\TelinkIoTStudio"
```

## 查看 BDT 设备

```powershell
powershell -ExecutionPolicy Bypass -File .\tools\list_bdt_devices.ps1
```

常见设备：

- `vid_248a&pid_8266`：Telink Debugger / Burning EVK，当前已验证命令行可控制
- `vid_248a&pid_5320`：曾实测枚举为 `Telink Semiconductor USB DevSys`，不是可用的 EVK / Swire 命令行下载器
- `vid_248a&pid_826a`：曾实测出现过，PID 会随插拔/状态变化，不能作为固定板子身份
- `vid_248a&pid_8801`：当前 PENDANT 固件启用的 B85 USB 下载接口

## EVK / Swire 下载

推荐调试方式是电脑只接入一个 Telink Debugger，并把它接到当前要调试的 dangle 板。默认使用 EVK / Swire 下载：

```powershell
powershell -ExecutionPolicy Bypass -File .\tools\flash_pendant_multi.ps1
```

脚本会先尝试执行 BDT `ac` 激活 MCU。实测有些状态下 `ac` 会返回失败但后续 `wf` 仍能正常下载，所以当前脚本只给 warning，不再直接中断。要跳过 `ac`：

```powershell
powershell -ExecutionPolicy Bypass -File .\tools\flash_pendant_multi.ps1 -SkipActivate
```

只下载不复位：

```powershell
powershell -ExecutionPolicy Bypass -File .\tools\flash_pendant_multi.ps1 -NoReset
```

如果临时接入多个 BDT 设备，先查看当前 BDT 编号，再用 `-DeviceId` 指定：

```powershell
powershell -ExecutionPolicy Bypass -File .\tools\list_bdt_devices.ps1
powershell -ExecutionPolicy Bypass -File .\tools\flash_pendant_multi.ps1 -DeviceId 1
```

脚本会连续读取两次 BDT `all`，确认同一个 `Device ID` 的 `VID/PID/PortNum/HubNum` 稳定后才执行下载/复位，避免 BDT 枚举抖动时误刷到另一块板。也可以手动指定当前枚举中的 PID 或 USB 物理位置：

```powershell
powershell -ExecutionPolicy Bypass -File .\tools\flash_pendant_multi.ps1 -DebuggerPid 8266
powershell -ExecutionPolicy Bypass -File .\tools\flash_pendant_multi.ps1 -PortNum 4 -HubNum 6
```

注意：PID 不是固定板子身份，重新插拔、目标 USB 下载口打开、Burning EVK 状态变化后都可能变化。当前不建议同时插两套 Telink Debugger 做命令行烧录。

## 串口继电器切换两块板

当前调试台支持用一个 Telink Debugger 通过串口继电器切换两块 B85 dangle 板。继电器串口参数：

- 端口：`COM18`
- 波特率：`9600`
- 数据格式：`8N1`
- 控制方式：发送原始十六进制字节，不是 ASCII 字符串

当前接线映射：

- `COM16` / `A` / `ON`：继电器第一路打开，发送 `A0 01 01 A2`，返回 `CH1: 1`
- `COM17` / `B` / `OFF`：继电器第一路关闭，发送 `A0 01 00 A1`，返回 `CH1: 0`

只切换目标板：

```powershell
powershell -ExecutionPolicy Bypass -File .\tools\relay_select_b85.ps1 -Target COM16
powershell -ExecutionPolicy Bypass -File .\tools\relay_select_b85.ps1 -Target COM17
```

切换后复位：

```powershell
powershell -ExecutionPolicy Bypass -File .\tools\reset_pendant_multi_relay.ps1 -Target COM16 -DeviceId 1
powershell -ExecutionPolicy Bypass -File .\tools\reset_pendant_multi_relay.ps1 -Target COM17 -DeviceId 1
```

切换后烧录：

```powershell
powershell -ExecutionPolicy Bypass -File .\tools\flash_pendant_multi_relay.ps1 -Target COM16 -DeviceId 1
powershell -ExecutionPolicy Bypass -File .\tools\flash_pendant_multi_relay.ps1 -Target COM17 -DeviceId 1
```

当前已实测：`COM17` 使用继电器 `OFF` 能刷写和复位，`COM16` 使用继电器 `ON` 能刷写和复位。BDT `ac` 激活命令仍可能偶发返回 `0x1002`，但后续 `wf` 烧录和 `rst` 复位可以成功。

## USB 下载

当前固件已启用 B85 USB 下载功能，但为了避免 USB 运行态影响 BLE/GATT 调试，策略如下：

- 上电或复位后，USB 下载窗口默认开启 15 秒。
- 15 秒内，BDT 可以看到 `Telink PENDANT`，可使用 USB 下载。
- 15 秒后，固件自动关闭 USB pull-up，BLE/GATT 正常工作。
- 串口 shell 可用 `usb on` / `usb off` / `usb` 手动打开、关闭、查看 USB 状态。

上电后 15 秒内，或串口执行 `usb on` 后，使用：

```powershell
powershell -ExecutionPolicy Bypass -File .\tools\flash_pendant_multi.ps1 -Transport USB -DeviceId 2
```

如果只验证 USB 下载，不希望 BDT USB reset 影响应用启动，可加 `-NoReset`：

```powershell
powershell -ExecutionPolicy Bypass -File .\tools\flash_pendant_multi.ps1 -Transport USB -DeviceId 2 -NoReset
```

注意：当前实测 BDT 的 USB reset 能返回成功，但不一定触发完整应用启动日志。需要稳定重启应用时，优先用 EVK reset 或重新上电。

## 复位

EVK / Swire 复位：

```powershell
powershell -ExecutionPolicy Bypass -File .\tools\reset_pendant_multi.ps1
```

USB 复位：

```powershell
powershell -ExecutionPolicy Bypass -File .\tools\reset_pendant_multi.ps1 -Transport USB -DeviceId 1
```

## 串口调试

当前 dangle 串口配置：

- 波特率：`115200`
- 数据位：`8`
- 校验：`None`
- 停止位：`1`

常用 shell 命令：

```text
help
ping
build
info
peers
beacon
send
clear
logs
radio
disc
reset
usb
usb on
usb off
```

PC 上位机 `pc_debug_tool` 也可以在连接 Debug Service 后使用 `Shell` 页签远程执行同一组 shell 命令。当前 BLE shell 单次返回最多 72 字节，长输出会带 `[truncated]` 标记；后续如需要完整长输出，再扩展为分页或流式返回。

当前调试建议：

- 需要 USB 下载时：复位后 15 秒内下载，或串口输入 `usb on` 后下载。
- 需要 BLE/GATT 调试时：等待 15 秒 USB 自动关闭，或串口输入 `usb off`。
- 如果 PC BLE 连接超时后设备不再广播，可用串口 `disc` 主动断开，或用 EVK reset。

## 当前 BLE 调试配置

当前固件的 BLE 调试配置：

- `ACL_PERIPHR_SMP_ENABLE = 0`，调试 GATT 不走配对/加密。
- `PENDANT_EXT_ADV_ENABLE = 1`，启用扩展广播发送调度。
- `APP_BLE_ENABLE_DISCOVERY_SCAN = 1`，启用扩展扫描。
- `APP_HOST_ENABLE_ADV_TRANSPORT = 0`，暂时关闭广播上位机 transport。
- GATT Debug Service 可连接 PC 调试工具，并支持 `Shell` 页签远程执行下位机 shell 命令。
- BLE 连接调试服务 UUID 仍为当前实测版本：

```text
Service 01000056-4544-3159-4b45-000150544e44
Cmd     01000056-4544-3159-4b45-000250544e44
Rsp     01000056-4544-3159-4b45-000350544e44
Log     01000056-4544-3159-4b45-000450544e44
Evt     01000056-4544-3159-4b45-000550544e44
```
