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
| `0x07` | 权限不足或身份已锁定 |
| `0x08` | 记录不存在 |
| `0x09` | Flash 读写错误 |

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
| GET_FLASH_MAP | `0x0A` | App -> 设备 | 空 |
| GET_IDENTITY | `0x0B` | App -> 设备 | 空 |
| WRITE_IDENTITY | `0x0C` | App -> 设备 | `u8 flags, unique_id[16]` |
| LOCK_IDENTITY | `0x0D` | App -> 设备 | 空 |
| GET_FACTORY_INFO | `0x0E` | App -> 设备 | 空 |
| RUN_FACTORY_TEST | `0x0F` | App -> 设备 | `u32 test_mask`，可选 |

### 4.1 GET_FLASH_MAP 响应 Payload

多字节字段为 little-endian。

| 偏移 | 字段 | 长度 | 说明 |
| ---: | --- | ---: | --- |
| `0x00` | `flash_mid` | 4 | Flash MID 原始值 |
| `0x04` | `flash_vendor` | 4 | Flash 厂商值 |
| `0x08` | `flash_size` | 4 | Flash 容量，单位 byte |
| `0x0C` | `sdk_reserved_start` | 4 | SDK 保留区最小起始地址 |
| `0x10` | `sdk_mac_addr` | 4 | SDK MAC 分区起始地址 |
| `0x14` | `sdk_calibration_addr` | 4 | SDK 校准分区起始地址 |
| `0x18` | `sdk_smp_pairing_addr` | 4 | SDK SMP Pairing 分区起始地址 |
| `0x1C` | `sdk_master_pairing_addr` | 4 | SDK Master Pairing 分区起始地址 |
| `0x20` | `app_base_addr` | 4 | Pendant 应用分区起始地址 |
| `0x24` | `app_total_size` | 4 | Pendant 应用分区总大小 |
| `0x28` | `part_count` | 1 | 后续分区条目数量 |

每个分区条目长度 9 字节，连续排列：

| 偏移 | 字段 | 长度 | 说明 |
| ---: | --- | ---: | --- |
| `+0` | `part_id` | 1 | `0` Identity, `1` Config, `2` Bond, `3` Event Log, `4` Factory |
| `+1` | `addr` | 4 | 分区起始地址 |
| `+5` | `size` | 4 | 分区大小 |

### 4.2 GET_IDENTITY / WRITE_IDENTITY / LOCK_IDENTITY 响应 Payload

`GET_IDENTITY`、`WRITE_IDENTITY` 成功、`LOCK_IDENTITY` 成功时返回同一身份信息结构。`WRITE_IDENTITY` 失败或 `LOCK_IDENTITY` 失败时不返回 Payload，只通过 Status 表示错误。

| 偏移 | 字段 | 长度 | 说明 |
| ---: | --- | ---: | --- |
| `0x00` | `version` | 1 | 当前 `1` |
| `0x01` | `flags` | 1 | bit0 present, bit1 locked, bit2 dev_fallback, bit3 valid |
| `0x02` | `crc16` | 2 | `unique_id[16]` CRC16 |
| `0x04` | `unique_id` | 16 | 当前唯一识别码 |
| `0x14` | `product_sn` | 4 | `unique_id[0:4]` 小端解析 |
| `0x18` | `terminal_sn` | 4 | `unique_id[4:8]` 小端解析 |
| `0x1C` | `random` | 4 | `unique_id[8:12]` 小端解析 |
| `0x20` | `reserved` | 4 | `unique_id[12:16]`，当前必须为 0 |
| `0x24` | `short_id` | 4 | 当前 EID 派生短 ID |
| `0x28` | `eid` | 16 | 当前广播 EID |

`WRITE_IDENTITY` 请求 Payload：

| 偏移 | 字段 | 长度 | 说明 |
| ---: | --- | ---: | --- |
| `0x00` | `flags` | 1 | bit0 为 `lock_after_write` |
| `0x01` | `unique_id` | 16 | 待写入唯一识别码 |

`unique_id` 格式：

| 偏移 | 字段 | 长度 | 说明 |
| ---: | --- | ---: | --- |
| `0x00` | `product_sn` | 4 | 公司产品序列号，小端 |
| `0x04` | `terminal_sn` | 4 | 终端序列号，小端，建议格式 `0xyymmddss` |
| `0x08` | `random` | 4 | 出厂随机值，小端 |
| `0x0C` | `reserved` | 4 | 必须为 0 |

### 4.3 GET_FACTORY_INFO 响应 Payload

| 偏移 | 字段 | 长度 | 说明 |
| ---: | --- | ---: | --- |
| `0x00` | `version` | 1 | 当前 `1` |
| `0x01` | `flags` | 1 | bit0 identity_written, bit1 identity_locked, bit2 self_test_pass |
| `0x02` | `crc16` | 2 | Factory Record CRC16 |
| `0x04` | `write_count` | 4 | 身份写入次数 |
| `0x08` | `lock_count` | 4 | 身份锁定次数 |
| `0x0C` | `test_mask` | 4 | 最近一次工厂自检请求 mask |
| `0x10` | `result_mask` | 4 | 最近一次工厂自检结果 mask |
| `0x14` | `last_error` | 4 | 最近一次工厂操作错误码，取 `app_status_t` |
| `0x18` | `last_unique_id` | 16 | 最近一次成功写入或锁定的唯一识别码 |

### 4.4 RUN_FACTORY_TEST 响应 Payload

当前工厂自检先实现为框架命令，`result_mask` 暂时直接回填 `test_mask`，后续逐步接入马达、电池、充电和广播测试。

| 偏移 | 字段 | 长度 | 说明 |
| ---: | --- | ---: | --- |
| `0x00` | `version` | 1 | 当前 `1` |
| `0x01` | `test_mask` | 4 | 请求 mask |
| `0x05` | `result_mask` | 4 | 结果 mask |

## 5. 当前事件

| 事件 | 值 | Payload |
| --- | ---: | --- |
| PEER_LEVEL | `0x81` | `eid[16], old_level, new_level, rssi_avg, reason` |
| SYSTEM | `0x82` | 预留 |
| ERROR | `0x83` | `u16 error_code, u16 detail` |
