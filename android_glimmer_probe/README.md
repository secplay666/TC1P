# 微光 Glimmer Android 验证 App

这是面向手机端 BLE 调试的最小原生 Android 验证程序。它先不做正式产品 UI，只验证手机到 B85 的 GATT 调试通道，以及 B85 到 B85 的 P2P 聊天链路。

## 功能

- 扫描 BLE 广播设备，并用 `★` 标记广播名包含 `PENDANT` / `GLIMMER` / `微光`，或广播中带调试 Service UUID 的设备。
- 连接调试 GATT 服务。
- 订阅 `RSP` / `LOG` / `EVT` 三个通知特征。
- 发送：
  - `GET_DEVICE_INFO`
  - `GET_PEER_TABLE`
  - `LOG_ENABLE(false)`
  - `SHELL_EXEC`
  - `P2P_CHAT_SEND`
- 接收并显示：
  - 普通响应
  - Shell 输出
  - `P2P_CHAT`
  - `P2P_CHAT_TX_RESULT`

## 命令行构建

在本目录执行：

```powershell
.\gradlew.bat assembleDebug
```

APK 输出位置：

```text
app\build\outputs\apk\debug\app-debug.apk
```

如果当前 PowerShell 没有拿到 Android 环境变量，可以临时设置：

```powershell
$env:JAVA_HOME='C:\Program Files\Android\Android Studio\jbr'
$env:ANDROID_HOME="$env:LOCALAPPDATA\Android\Sdk"
$env:ANDROID_SDK_ROOT=$env:ANDROID_HOME
.\gradlew.bat assembleDebug
```

## 安装到手机

手机开启开发者模式和 USB 调试后：

```powershell
adb devices
adb install -r .\app\build\outputs\apk\debug\app-debug.apk
```

首次运行时点击“授权”，允许附近设备/蓝牙和位置信息权限，然后点击“扫描”。

位置信息权限只用于解除 Android 对 BLE 扫描结果的限制；验证版不会读取 GPS。

## 使用流程

1. 烧录当前 B85 固件，并让设备处于可连接状态。
2. 打开 App，点击“授权”。
3. 点击“扫描”，在列表中点击目标设备。
4. 看到“调试通道已就绪”后，可以点击“设备信息”和“邻近表”。
5. 两块 B85 已互相发现后，在输入框里发送聊天内容。
6. 日志区应看到 `P2P_CHAT_SEND` 的 queued 响应，以及后续 `CHAT_TX_OK` 或 `CHAT_TX_FAIL`。

## 当前限制

- 这是验证版，UI 只追求直接可测。
- 目前沿用固件的 20 字节 GATT 调试帧，一条 Host message 最大 192 bytes。
- 多 peer 目标选择还未做；如果固件侧因为多个 peer 拒绝发送，App 会把响应显示出来。
