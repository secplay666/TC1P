# 微光 OTA 系统设计与使用说明

## 当前版本范围

微光 OTA 复用 Telink BLE OTA server，不自研 bootloader。终端通过现有 BLE GATT 连接暴露 Telink OTA 特征，PC 工具和 Android App 按 Telink OTA 数据流写入固件。

产品化前的安全补强已经加入两层：

- 写入前校验：OTA 默认使用外层签名包 `.glimmerota`，PC/Android 在写入前校验签名、固件 sha256、固件大小、`version_code` 和防回滚条件。
- 首次启动确认：新 slot 启动后进入 pending 状态，稳定运行一段时间后自动 confirmed；如果 pending 启动次数超过阈值或版本回退，则恢复旧 slot 的 boot mark 并重启回滚。

注意：`.glimmerota` 只是 OTA 传输包，里面包含原始 Telink `.bin`。Telink 有线烧录仍然使用原始 `.bin`，不要把 `.glimmerota` 直接用 Telink 烧录器写入。

## Flash 与 Slot 策略

当前启用：

- 有线/基础固件 slot：`0x00000`
- OTA 目标 slot：`0x40000`
- OTA firmware max：`184KB`
- OTA guard metadata：应用存储区前 1 个 sector
- 旧 slot 首 sector 备份：应用存储区前 2 个 sector

`PENDANT_OTA_FIRMWARE_MAX_K` 从 192KB 收紧到 184KB，是为了在不移动现有身份、配置、profile 等应用分区的前提下，给 Telink OTA 保留 sector、OTA guard metadata 和旧 slot 首 sector 备份留出空间。512KB Flash 当前关键布局为：slot1 固件区 `0x40000..0x6DFFF`，Telink OTA 保留区 `0x6E000..0x6EFFF`，旧 slot 首 sector 备份 `0x6F000..0x6FFFF`，OTA guard/snapshot `0x70000..0x70FFF`。

## 签名包格式

OTA 产品路径默认要求 `.glimmerota`：

```text
magic:        "GLIMMER-OTA-PKG1\n"
manifest_len: u32 little-endian
manifest:     JSON，按 key 排序的 canonical bytes
sig_len:      u16 little-endian
signature:    RSA-2048 PKCS#1 v1.5 SHA-256，签 manifest bytes
firmware:     原始 Telink .bin
```

manifest 当前字段：

- `format`: 固定为 `1`
- `product`: 固定为 `glimmer-b85`
- `version_code`: 单调递增的整型版本号
- `firmware_size`: 内部 `.bin` 字节数
- `firmware_sha256`: 内部 `.bin` 的 SHA-256
- `signature`: 固定为 `RSA-2048-SHA256`
- `created_at`: 打包时间戳

私钥不进入仓库。PC 和 Android 只内置公钥。当前工作区为了开发调试，在已忽略的 `artifacts/glimmer_ota_private_dev.pem` 生成了一把本机开发私钥；量产前需要替换为正式 OTA 公钥，并把对应私钥放在受控的构建/发布环境中。

## 构建 OTA 包

先构建原始固件：

```powershell
powershell -ExecutionPolicy Bypass -File .\tools\build_pendant_multi.ps1
```

然后用外部 RSA 私钥打包：

```powershell
$env:GLIMMER_OTA_PRIVATE_KEY=".\artifacts\glimmer_ota_private_dev.pem"
python .\tools\make_glimmer_ota_package.py `
  --bin .\tc_ble_multi_sdk\build\B85\pendant\pendant.bin `
  --out .\tc_ble_multi_sdk\build\B85\pendant\pendant.glimmerota
```

打包脚本默认从 `tc_ble_multi_sdk/vendor/pendant/build_info/app_build_info.h` 读取 `APP_BUILD_VERSION_CODE`。如果需要手动指定：

```powershell
python .\tools\make_glimmer_ota_package.py --version-code 101 --private-key C:\secure\glimmer_ota_private.pem
```

打包脚本需要 Python 的可选依赖 `cryptography`。这是签名工具依赖，运行 OTA 的 PC 工具不需要它。

## PC 端使用

图形工具：

```powershell
cd pc_debug_tool
.venv\Scripts\python.exe app.py
```

进入 `OTA` 页，选择 `.glimmerota`，添加一个或多个目标地址，点击 `Start OTA`。PC 会按顺序升级多个设备。每个目标开始写入前会尽量通过调试通道读取 `GET_DEVICE_INFO`，如果读到当前 `fw_version_code`，则先做版本防回滚判断。

命令行扫描并升级：

```powershell
cd pc_debug_tool
.venv\Scripts\python.exe ota_update.py --scan --bin ..\tc_ble_multi_sdk\build\B85\pendant\pendant.glimmerota
```

指定多个设备顺序升级：

```powershell
cd pc_debug_tool
.venv\Scripts\python.exe ota_update.py `
  --address AA:BB:CC:DD:EE:01 `
  --address AA:BB:CC:DD:EE:02 `
  --bin ..\tc_ble_multi_sdk\build\B85\pendant\pendant.glimmerota
```

开发期如果必须 OTA 原始 `.bin`，需要显式打开未签名模式：

```powershell
.venv\Scripts\python.exe ota_update.py --scan --allow-unsigned-bin --bin ..\tc_ble_multi_sdk\build\B85\pendant\pendant.bin
```

## Android 端使用

1. 绑定并连接自己的 Glimmer 终端。
2. 打开 `我的` -> `我的 Glimmer`。
3. 点击 `OTA 固件升级`。
4. 选择 `.glimmerota` 文件。
5. 等待专用 OTA 进度页完成写入、校验、重启和自动重连。

Android App 不再接受裸 `.bin` 作为产品 OTA 输入。它会在写入前校验签名包，并使用当前已同步的 `deviceInfo.firmwareVersionCode` 做防回滚判断。

OTA 写入期间 App 会切换到专用进度页，隐藏底部导航并拦截返回键，避免用户切走后误认为 App 卡死。升级完成、失败或终端重连后恢复正常页面。

## 下位机 Guard 状态机

OTA 开始时：

- 暂停附近扫描和扩展广播调度；
- 喂 watchdog，避免写 flash 时被系统空闲逻辑干扰；
- 备份当前 active slot 的首 sector；
- 记录 `UPDATE_PREPARED`，保存 previous slot 和 pending slot。

OTA 写入成功后：

- 记录 `PENDING`；
- 保留 OTA 开始前记录的 previous/pending slot；
- 等 Telink OTA 逻辑完成校验、切换 boot mark 并重启。

新 slot 启动后：

- 如果版本小于已确认版本，触发回滚；
- 如果 pending 启动次数超过 `PENDANT_OTA_MAX_PENDING_BOOT_ATTEMPTS`，触发回滚；
- 如果进入 `ADV_SCAN` 或 `APP_CONNECTED` 并稳定运行 `PENDANT_OTA_CONFIRM_DELAY_MS`，自动标记 `CONFIRMED`。

回滚方式：

- 恢复旧 slot 首 sector 备份；
- 清掉当前 slot boot mark；
- 保存 `ROLLED_BACK` 状态和原因；
- 重启。

如果两个 slot 都坏了，当前方案无法自救，需要用 Telink 有线方式重新 active/烧录。这个是复用 Telink OTA server、没有自研 ROM bootloader 的边界。

## 已知限制

- PC 工具默认按 ATT MTU 自动选 PDU：MTU ≥ 71 用 64 字节，否则 16 字节；固定使用 write-response。2026-10-31 实测（Windows，MTU 83）约 118KB 用时约 62 秒（约 1.9KB/s）。write-without-response 会超过设备写 Flash 的速度，报 `WRITE_FLASH_ERR` 或数据包超时，暂不使用。
- OTA 开始时固件会请求 7.5–15ms 连接间隔，主机可以拒绝。
- PC 支持多地址顺序 OTA，当前不是并行 OTA。
- Android 在 OTA 前请求 MTU 83、高连接优先级和 2M PHY；协商到 MTU ≥ 71 时用 64 字节 PDU，否则回落到 16 字节（该路径尚未在手机上实测）。
- 签名公钥目前是仓库内的开发公钥，量产前必须替换。
- BLE 链路本身仍基于当前连接安全级别；签名包解决的是固件来源与完整性，不等同于 BLE 链路加密。
