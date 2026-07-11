# 微光 OTA 系统设计与使用说明

## 当前版本范围

第一版 OTA 复用 Telink BLE OTA server，不自研 bootloader。下位机通过现有 BLE GATT 连接暴露 Telink OTA 特征，PC 工具和 Android App 都按 Telink OTA 包格式发送固件。

当前目标是开发和小批量调试可用：

- PC 端支持扫描升级，也支持指定多个 BLE 地址顺序升级。
- Android 端支持已绑定终端升级，用户选择本地 `.bin` 文件后写入。
- 固件升级区使用 `0x40000` 多启动地址。
- PC 端默认使用 16 字节 OTA PDU，兼容 Windows 当前 23-byte ATT MTU。
- Android 端会主动请求 83-byte ATT MTU，默认使用 64 字节 OTA PDU。

## Flash 与大小约束

当前固件尺寸已经超过默认 `0x20000` OTA 地址可承载的 124KB，因此启用：

- OTA boot address: `MULTI_BOOT_ADDR_0x40000`
- OTA firmware max: `192KB`

这为 512KB flash 顶部的 SDK 系统区、绑定信息和微光应用存储保留空间。后续如果固件继续变大，需要重新核算：

- 当前 bin 大小
- Telink boot/new firmware 双区位置
- SDK reserved/system data 起始位置
- 微光 profile、配置、身份、事件等应用分区

## 下位机行为

OTA 开始后，下位机会：

- 暂停扫描和扩展广播调度；
- 喂 watchdog，避免写 flash 时被系统空闲逻辑干扰；
- 屏蔽进入休眠的请求；
- 请求更快的连接参数，降低 write-response 模式下的升级时间；
- 保持当前 GATT 连接处理 OTA 写入；
- OTA 完成后由 Telink OTA 逻辑校验并切换启动区。

## PC 端使用

构建固件：

```powershell
powershell -ExecutionPolicy Bypass -File .\tools\build_pendant_multi.ps1
```

扫描并升级发现到的所有 Glimmer 设备：

```powershell
cd pc_debug_tool
.venv\Scripts\python.exe ota_update.py --scan --bin ..\tc_ble_multi_sdk\build\B85\pendant\pendant.bin
```

指定多个设备顺序升级：

```powershell
cd pc_debug_tool
.venv\Scripts\python.exe ota_update.py --address AA:BB:CC:DD:EE:01 --address AA:BB:CC:DD:EE:02 --bin ..\tc_ble_multi_sdk\build\B85\pendant\pendant.bin
```

先扫描不升级：

```powershell
cd pc_debug_tool
.venv\Scripts\python.exe ota_update.py --scan --dry-run
```

## Android 端使用

进入 App：

1. 绑定并连接自己的 Glimmer 终端。
2. 打开 `我的` -> `我的 Glimmer`。
3. 点击 `OTA 固件升级`。
4. 选择固件 `.bin`。
5. 等待进度到 100%，再等待终端校验和重启。

当前没有做后台下载固件，也没有做版本服务器。第一版只支持选择本地 bin。

## 安全边界

当前 OTA 是开发版能力：

- 没有固件签名校验；
- 没有强制绑定鉴权；
- 没有版本防回滚；
- BLE 连接仍使用当前无配对安全等级。

正式产品化前至少需要补：

- 固件签名或哈希白名单；
- 只允许已绑定 App 发起 OTA；
- 版本号比较和防回滚策略；
- OTA 过程中明确禁止普通聊天/文件传输；
- 失败后重试、回滚和用户提示策略。

## 已知限制

- PC 端 Windows/Bleak 实测 ATT MTU 为 23，默认 16 字节 PDU + write-response；151KB 固件完整 OTA 耗时约 4 分 50 秒。
- PC 端支持 `--address` 重复传入多个地址，按顺序升级多个设备；当前不是并行 OTA。
- PC 端 `--pdu-len 64` 需要 BLE 后端完成更大 MTU 协商；当前 Windows 内置 BLE 不支持主动 MTU 请求，会被工具提前拒绝。
- Android 端真机 OTA 尚需接入手机后验证。
