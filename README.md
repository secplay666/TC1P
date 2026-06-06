# 蓝牙吊坠固件构建与烧录

当前下位机主工程位于 `tc_ble_multi_sdk/`，目标芯片为 Telink B85 / TLSR8258。旧 `tc_ble_single_sdk/` 暂时保留作对照，后续确认 Multi SDK 扩展广播互扫无误后再删除。

## 环境要求

- Windows PowerShell
- Telink IoT Studio，默认安装路径：
  - `C:\TelinkIoTStudio`
- Telink BDT 下载工具，当前脚本默认路径：
  - `C:\TelinkIoTStudio\tools\TBD_release\config\Cmd_download_tool.exe`
- B85 调试器已连接目标板

## 构建固件

在仓库根目录执行：

```powershell
powershell -ExecutionPolicy Bypass -File .\tools\build_pendant_multi.ps1
```

构建成功后输出：

```text
tc_ble_multi_sdk\build\B85\pendant\pendant.bin
```

如果 Telink IoT Studio 不在默认路径，可以指定：

```powershell
powershell -ExecutionPolicy Bypass -File .\tools\build_pendant_multi.ps1 -TelinkStudioPath "C:\TelinkIoTStudio"
```

如需尝试 Telink IDE headless build：

```powershell
powershell -ExecutionPolicy Bypass -File .\tools\build_pendant_multi.ps1 -Headless
```

当前推荐默认构建方式，即不加 `-Headless`。该方式直接调用已验证的 `make.exe` 和 TC32 工具链。

## 烧录固件

构建完成后，在仓库根目录执行：

```powershell
powershell -ExecutionPolicy Bypass -File .\tools\flash_pendant_multi.ps1
```

默认行为：

- 芯片：`B85`
- 调试器设备号：`1`
- 写入地址：`0x000000`
- 固件：`tc_ble_multi_sdk\build\B85\pendant\pendant.bin`
- 烧录成功后自动复位 MCU

如果 BDT 路径不同：

```powershell
powershell -ExecutionPolicy Bypass -File .\tools\flash_pendant_multi.ps1 -BdtConfigPath "你的BDT\config路径"
```

如果有多个调试器，可指定设备号：

```powershell
powershell -ExecutionPolicy Bypass -File .\tools\flash_pendant_multi.ps1 -DeviceId 2
```

如果只烧录不复位：

```powershell
powershell -ExecutionPolicy Bypass -File .\tools\flash_pendant_multi.ps1 -NoReset
```

## 串口验证

当前固件 UART 配置：

- 波特率：`115200`
- 数据位：`8`
- 校验：`None`
- 停止位：`1`

复位后应看到类似日志：

```text
Pendant boot
[ADV-TX] len=5b
```

串口调试 shell 支持常用命令：

```text
ping
info
beacon
peers
logs
```

例如输入 `info`，正常会返回设备状态、`short_id`、邻近设备数量和 EID 前几个字节。
