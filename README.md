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

- `vid_248a&pid_8266`：Telink Debugger / Burning EVK
- `vid_248a&pid_8801`：当前 PENDANT 固件启用的 B85 USB 下载接口

## EVK / Swire 下载

默认使用 EVK / Swire：

```powershell
powershell -ExecutionPolicy Bypass -File .\tools\flash_pendant_multi.ps1
```

如果有多个调试器，指定 BDT `Device ID`：

```powershell
powershell -ExecutionPolicy Bypass -File .\tools\flash_pendant_multi.ps1 -DeviceId 1
```

脚本会先尝试执行 BDT `ac` 激活 MCU。实测有些状态下 `ac` 会返回失败但后续 `wf` 仍能正常下载，所以当前脚本只给 warning，不再直接中断。要跳过 `ac`：

```powershell
powershell -ExecutionPolicy Bypass -File .\tools\flash_pendant_multi.ps1 -SkipActivate
```

只下载不复位：

```powershell
powershell -ExecutionPolicy Bypass -File .\tools\flash_pendant_multi.ps1 -NoReset
```

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
powershell -ExecutionPolicy Bypass -File .\tools\reset_pendant_multi.ps1 -Transport EVK -DeviceId 1
```

USB 复位：

```powershell
powershell -ExecutionPolicy Bypass -File .\tools\reset_pendant_multi.ps1 -Transport USB -DeviceId 2
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
info
peers
beacon
send
clear
logs
disc
usb
usb on
usb off
```

当前调试建议：

- 需要 USB 下载时：复位后 15 秒内下载，或串口输入 `usb on` 后下载。
- 需要 BLE/GATT 调试时：等待 15 秒 USB 自动关闭，或串口输入 `usb off`。
- 如果 PC BLE 连接超时后设备不再广播，可用串口 `disc` 主动断开，或用 EVK reset。

## 当前 BLE 调试配置

为先恢复 PC GATT 调试稳定性，当前固件做了以下临时取舍：

- `ACL_PERIPHR_SMP_ENABLE = 0`，调试 GATT 不走配对/加密。
- `PENDANT_EXT_ADV_ENABLE = 0`，暂时关闭扩展广播发送调度。
- `APP_HOST_ENABLE_ADV_TRANSPORT = 0`，暂时关闭广播上位机 transport。
- BLE 连接调试服务 UUID 仍为当前实测版本：

```text
Service 01000056-4544-3159-4b45-000150544e44
Cmd     01000056-4544-3159-4b45-000250544e44
Rsp     01000056-4544-3159-4b45-000350544e44
Log     01000056-4544-3159-4b45-000450544e44
Evt     01000056-4544-3159-4b45-000550544e44
```
