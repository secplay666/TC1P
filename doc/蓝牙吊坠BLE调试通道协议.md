# 蓝牙吊坠 BLE 调试通道协议

## 1. GATT 服务

调试服务用于 PC/手机上位机连接单个吊坠，读取状态、下发调试命令、接收日志和事件。

| 项 | UUID | 属性 |
| --- | --- | --- |
| Debug Service | `50544E44-0001-4B45-5931-444556000001` | Primary Service |
| Command | `50544E44-0002-4B45-5931-444556000001` | Write / Write Without Response |
| Response | `50544E44-0003-4B45-5931-444556000001` | Notify |
| Log | `50544E44-0004-4B45-5931-444556000001` | Notify |
| Event | `50544E44-0005-4B45-5931-444556000001` | Notify |

上位机连接后应先打开 Response、Log、Event 三个 Notify。

## 2. Host Frame

当前按默认 ATT MTU 23 设计，单个 GATT Value 最大 20 字节。多字节字段为 little-endian。

| 偏移 | 字段 | 长度 | 说明 |
| ---: | --- | ---: | --- |
| 0 | Magic | 1 | 固定 `0xA5` |
| 1 | Version | 1 | 当前 `0x01` |
| 2 | Type | 1 | `1` Cmd, `2` Rsp, `3` Log, `4` Event |
| 3 | Seq | 1 | 帧序号；响应沿用命令 Seq |
| 4 | Cmd | 1 | 命令或事件 ID |
| 5 | Status | 1 | 响应状态 |
| 6 | Frag Index | 1 | 分片序号，从 0 开始 |
| 7 | Frag Count | 1 | 分片总数 |
| 8 | Payload Len | 1 | 本分片 Payload 长度，最大 9 |
| 9 | Payload | 0-9 | 分片数据 |
| 9+N | CRC16 | 2 | 对前面所有字段计算 CRC16 |

单个完整消息最大 192 字节。

## 3. 状态码

| 值 | 含义 |
| ---: | --- |
| `0x00` | OK |
| `0x01` | 参数错误 |
| `0x02` | 状态错误 |
| `0x03` | 忙 |
| `0x04` | 不支持 |
| `0x05` | CRC 错误 |
| `0x06` | 缓存不足 |

## 4. 当前命令

| 命令 | 值 | 方向 | Payload |
| --- | ---: | --- | --- |
| GET_DEVICE_INFO | `0x01` | App -> 设备 | 空 |
| GET_SYSTEM_STATE | `0x02` | App -> 设备 | 空 |
| GET_ADV_FRAME | `0x03` | App -> 设备 | 空 |
| GET_PEER_TABLE | `0x04` | App -> 设备 | 空 |
| SET_RSSI_CONFIG | `0x05` | App -> 设备 | `s8 T1, s8 T2, s8 T3, u16 TinMs, u16 ToutMs` |
| MOTOR_TEST | `0x06` | App -> 设备 | `u8 pattern`，1/2/3/Error |
| LOG_ENABLE | `0x07` | App -> 设备 | `u8 enable` |
| DEBUG_RESET_STATS | `0x08` | App -> 设备 | 空 |
| ENTER_SLEEP | `0x09` | App -> 设备 | 空 |

## 5. 当前事件

| 事件 | 值 | Payload |
| --- | ---: | --- |
| PEER_LEVEL | `0x81` | `eid[16], old_level, new_level, rssi_avg, reason` |
| SYSTEM | `0x82` | 预留 |
| ERROR | `0x83` | `u16 error_code, u16 detail` |

