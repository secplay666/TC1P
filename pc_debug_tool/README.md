# 蓝牙吊坠 PC 调试工具

这是用于当前下位机 BLE 调试通道的临时 PC 上位机，目标是替代多串口调试。

## 运行方式

Windows 下可直接双击：

```bat
pc_debug_tool\run.bat
```

也可以手动运行：

```bat
cd pc_debug_tool
py -3 -m venv .venv
.venv\Scripts\activate
python -m pip install -r requirements.txt
python app.py
```

## 功能

- 扫描 BLE 设备，优先显示 `PENDANT`。
- 连接单个吊坠设备。
- 自动订阅 Response / Log / Event 三个 Notify。
- 支持当前下位机调试命令：
  - 设备信息
  - 系统状态
  - 广播帧
  - 邻近表
  - Flash 分区
  - 身份读取
  - 唯一识别码写入
  - 唯一识别码锁定
  - 工厂信息
  - 工厂自检框架
  - RSSI 参数写入
  - 马达测试
  - 日志开关
  - 清统计
  - 进入休眠
- 显示完整 GATT 服务列表，便于排查 Debug Service 是否注册成功。
- 支持发送 Raw Command Frame Hex。

## 身份写入

“身份”页面可以生成和写入 128bit 唯一识别码：

- `产品序列`：`unique_id[0:4]`，小端保存。
- `终端序列`：`unique_id[4:8]`，建议格式 `0xyymmddss`。
- `随机值`：`unique_id[8:12]`。
- `保留`：`unique_id[12:16]`，当前必须为 `0`。

“写入并锁定”或“锁定身份”执行成功后，当前固件会拒绝再次改写唯一识别码。

## 自动烧录

“身份”页面的“自动烧录下一台”会执行以下流程：

1. 读取设备系统状态。
2. 读取设备当前身份。
3. 如果设备是 `DEV_FALLBACK`、没有正式身份或身份无效，则从本地数据库读取当前产品和日期下的最后序号。
4. 自动生成下一个 `terminal_sn = 0xyymmddss` 和新的 32bit 随机值。
5. 写入设备身份；如果勾选“成功后锁定”，写入后同时锁定。
6. 写入成功并校验返回身份一致后，记录到 `pc_debug_tool/provisioning.db`。

如果设备已经有 `PRESENT|VALID` 身份，默认不会改写。勾选“强制改写”后会尝试改写，但已锁定设备仍会被固件拒绝。

## 注意

当前 GUI 工具仍是 GATT 调试工具。新的无连接广播上位机协议已经先加入协议辅助层：

- `pendant_adv_protocol.py`：生成/解析 `PENDANT-ADV + HOST-ADV` 扩展广播帧。
- Windows 普通 Bleak 后端通常不能发送自定义扩展广播，因此 GUI 暂未切到广播控制。
- 后续需要接入 Android 原生 BLE advertiser、支持 HCI 原始命令的 BLE dongle，或专用调试器后端。

如果 Windows 或手机缓存了旧 GATT 表，可能看不到新 Debug Service。测试前建议：

1. 在系统蓝牙设置里删除/忘记旧的 `PENDANT`。
2. 关闭再打开蓝牙。
3. 重新扫描并连接。

工具兼容两组 UUID：

- 修正后的文档 UUID：`50544E44-0001~0005-4B45-5931-444556000001`
- 早期字节序错误版本：`56000001-4445-5931-454B-xx00444E5450`
- 当前 Telink/Windows 实测显示版本：`01000056-4544-3159-4B45-000x50544E44`
