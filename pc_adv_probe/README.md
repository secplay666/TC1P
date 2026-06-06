# 蓝牙吊坠 WinRT 扩展广播验证工具

这是一个最小 C# + WinRT 命令行验证程序，用于验证 Windows 官方 BLE Advertisement API 是否能满足吊坠无连接上位机通信需求。

## 环境

当前已在本机验证：

```text
.NET SDK 9.0.300
Windows 10.0.26200
```

项目目标框架：

```text
net9.0-windows10.0.19041.0
```

Windows 10 2004 之后才有 WinRT 扩展广播相关 API。

## 构建

在仓库根目录执行：

```powershell
dotnet build pc_adv_probe\PendantAdvProbe.csproj -c Release
```

## 命令

扫描吊坠广播：

```powershell
dotnet run --project pc_adv_probe\PendantAdvProbe.csproj -c Release -- scan 15
```

自动扫描目标并发送 `GET_DEVICE_INFO`：

```powershell
dotnet run --project pc_adv_probe\PendantAdvProbe.csproj -c Release -- info
```

向指定 EID 发送 `GET_DEVICE_INFO`：

```powershell
dotnet run --project pc_adv_probe\PendantAdvProbe.csproj -c Release -- info 00112233445566778899AABBCCDDEEFF
```

向指定 EID 发送任意命令：

```powershell
dotnet run --project pc_adv_probe\PendantAdvProbe.csproj -c Release -- send 00112233445566778899AABBCCDDEEFF 0x02
```

协议自测：

```powershell
dotnet run --project pc_adv_probe\PendantAdvProbe.csproj -c Release -- selftest
```

## 当前验证结果

已通过：

- 项目编译通过。
- `selftest` 通过。
- `scan 3` 可以启动 WinRT watcher，未报权限或 API 错误。

尚未验证：

- Windows 是否能真正发送足够长的扩展广播 Manufacturer Data。
- 吊坠固件是否能收到 WinRT Publisher 发出的 HOST-ADV 命令。
- 吊坠响应是否能被 WinRT Watcher 稳定接收。

## 实现说明

使用的 WinRT API：

- `BluetoothLEAdvertisementWatcher`
- `BluetoothLEAdvertisementWatcher.AllowExtendedAdvertisements = true`
- `BluetoothLEAdvertisementPublisher`
- `BluetoothLEAdvertisementPublisher.UseExtendedAdvertisement = true`
- `BluetoothLEManufacturerData`

`BluetoothLEManufacturerData` 的 `CompanyId` 使用当前开发测试值 `0xFFFF`，Data 部分放入 `PENDANT-ADV` Vendor Payload。

## 风险

Windows Publisher 是系统调度的 best effort 行为，实际能否稳定发送扩展广播取决于：

- Windows 版本。
- 蓝牙适配器芯片。
- 蓝牙驱动。
- 系统蓝牙资源占用。
- 是否允许同时扫描和广播。

如果实测不稳定，后续建议切换到 USB-BLE 广播网关方案。
