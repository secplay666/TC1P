# PendantAdvProbe

`PendantAdvProbe` 是一个 C# + WinRT 命令行验证工具，用于测试 Windows 官方 BLE Advertisement API 是否适合蓝牙吊坠的无连接上位机通信方案。

当前工具只做最小验证，不是最终上位机。

## 环境

已在当前电脑验证：

```text
.NET SDK 9.0.300
TargetFramework net9.0-windows10.0.19041.0
```

Windows 10 2004 之后才具备本工具使用的 WinRT 扩展广播相关 API。

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

向指定 EID 发送任意 HOST-ADV 命令：

```powershell
dotnet run --project pc_adv_probe\PendantAdvProbe.csproj -c Release -- send 00112233445566778899AABBCCDDEEFF 0x02
```

发送短 legacy manufacturer data，用于验证下位机 legacy scan 是否能收到 Windows 发出的普通广播：

```powershell
dotnet run --project pc_adv_probe\PendantAdvProbe.csproj -c Release -- legacy-ping 10
```

协议自测：

```powershell
dotnet run --project pc_adv_probe\PendantAdvProbe.csproj -c Release -- selftest
```

## 当前验证结果

已通过：

- 项目编译通过。
- `selftest` 通过。
- `scan` 可以扫描到 PENDANT 扩展广播 Beacon。
- PC 端 `BluetoothLEAdvertisementPublisher` 能进入 `Started` 状态并发送扩展广播请求。

待验证：

- 下位机是否能接收到 Windows 发出的 legacy 广播。
- 下位机当前 SDK 是否能接收到 Windows 发出的 extended advertising 二级数据。
- 下位机响应广播是否能被 WinRT watcher 稳定接收。

## 重要限制

根据当前 SDK 头文件和 `doc/AN-21112301-C_Telink B85m BLE Single Connection SDK Developer Handbook.pdf` 第 3 章，B85m Single Connection SDK 对发送 Extended Advertising 的 API 支持明确，但当前工程没有发现对应的 Link Layer Extended Scanning 接收 API。

这意味着：PC 能扫描到吊坠的扩展广播，不等价于吊坠也能扫描到 PC 发出的长扩展广播。下一步需要用 `legacy-ping` 和串口日志先验证 Windows -> 吊坠的 legacy 广播接收链路。
