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
- 支持当前下位机 9 个调试命令：
  - 设备信息
  - 系统状态
  - 广播帧
  - 邻近表
  - RSSI 参数写入
  - 马达测试
  - 日志开关
  - 清统计
  - 进入休眠
- 显示完整 GATT 服务列表，便于排查 Debug Service 是否注册成功。
- 支持发送 Raw Command Frame Hex。

## 注意

如果 Windows 或手机缓存了旧 GATT 表，可能看不到新 Debug Service。测试前建议：

1. 在系统蓝牙设置里删除/忘记旧的 `PENDANT`。
2. 关闭再打开蓝牙。
3. 重新扫描并连接。

工具兼容两组 UUID：

- 修正后的文档 UUID：`50544E44-0001~0005-4B45-5931-444556000001`
- 早期字节序错误版本：`56000001-4445-5931-454B-xx00444E5450`
