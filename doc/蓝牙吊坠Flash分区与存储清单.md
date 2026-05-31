# 蓝牙吊坠 Flash 分区与存储清单

## 1. 设计原则

下位机使用 Telink SDK Flash 驱动，应用层通过 `app_storage` 统一访问业务数据分区。

启动阶段先调用 SDK：

- `blc_readFlashSize_autoConfigCustomFlashSector()`：识别 Flash MID、容量，并配置 SDK 的 MAC、校准、SMP 分区地址。
- `blc_app_loadCustomizedParameters_normal()`：加载 RF 频偏、VDD_F、ADC 等校准参数。

`app_storage` 不再使用固定 `0x70000` 起始地址，而是在 SDK 保留区之前动态分配 5 个业务分区，每个分区 4KB。

## 2. 分区算法

```text
sdk_reserved_start = min(SDK SMP pairing, SDK MAC, SDK calibration, SDK master pairing)
app_total_size     = 5 * 0x1000 = 0x5000
app_base_addr      = sdk_reserved_start - app_total_size
```

分区顺序固定：

| 分区 | 大小 | 用途 |
| --- | ---: | --- |
| `APP_STORAGE_PART_IDENTITY` | 4KB | 唯一识别码记录 |
| `APP_STORAGE_PART_CONFIG` | 4KB | 运行配置 |
| `APP_STORAGE_PART_BOND` | 4KB | 上位机/后续绑定数据预留 |
| `APP_STORAGE_PART_EVENT_LOG` | 4KB | 离线事件/错误日志预留 |
| `APP_STORAGE_PART_FACTORY` | 4KB | 产测/工厂信息预留 |

## 3. B85 / TLSR8258 地址表

### 3.1 512KB Flash

| 区域 | 起始地址 | 结束地址 | 大小 | 归属 |
| --- | ---: | ---: | ---: | --- |
| Identity | `0x6F000` | `0x6FFFF` | 4KB | Pendant |
| Config | `0x70000` | `0x70FFF` | 4KB | Pendant |
| Bond | `0x71000` | `0x71FFF` | 4KB | Pendant |
| Event Log | `0x72000` | `0x72FFF` | 4KB | Pendant |
| Factory | `0x73000` | `0x73FFF` | 4KB | Pendant |
| SMP Pairing | `0x74000` | `0x74FFF` | 4KB | SDK |
| Reserved Gap | `0x75000` | `0x75FFF` | 4KB | Reserved |
| MAC | `0x76000` | `0x76FFF` | 4KB | SDK |
| Calibration | `0x77000` | `0x77FFF` | 4KB | SDK |
| Master Pairing | `0x78000` | `0x78FFF` | 4KB | SDK |

### 3.2 1MB Flash

| 区域 | 起始地址 | 结束地址 | 大小 | 归属 |
| --- | ---: | ---: | ---: | --- |
| Identity | `0xF7000` | `0xF7FFF` | 4KB | Pendant |
| Config | `0xF8000` | `0xF8FFF` | 4KB | Pendant |
| Bond | `0xF9000` | `0xF9FFF` | 4KB | Pendant |
| Event Log | `0xFA000` | `0xFAFFF` | 4KB | Pendant |
| Factory | `0xFB000` | `0xFBFFF` | 4KB | Pendant |
| SMP/Master Pairing | `0xFC000` | `0xFDFFF` | 8KB | SDK |
| Calibration | `0xFE000` | `0xFEFFF` | 4KB | SDK |
| MAC | `0xFF000` | `0xFFFFF` | 4KB | SDK |

### 3.3 2MB Flash

| 区域 | 起始地址 | 结束地址 | 大小 | 归属 |
| --- | ---: | ---: | ---: | --- |
| Identity | `0x1F7000` | `0x1F7FFF` | 4KB | Pendant |
| Config | `0x1F8000` | `0x1F8FFF` | 4KB | Pendant |
| Bond | `0x1F9000` | `0x1F9FFF` | 4KB | Pendant |
| Event Log | `0x1FA000` | `0x1FAFFF` | 4KB | Pendant |
| Factory | `0x1FB000` | `0x1FBFFF` | 4KB | Pendant |
| SMP/Master Pairing | `0x1FC000` | `0x1FDFFF` | 8KB | SDK |
| Calibration | `0x1FE000` | `0x1FEFFF` | 4KB | SDK |
| MAC | `0x1FF000` | `0x1FFFFF` | 4KB | SDK |

## 4. Pendant 数据记录清单

### 4.1 Identity 分区

起始偏移：`0x0000`

当前记录结构：

| 字段 | 偏移 | 大小 | 说明 |
| --- | ---: | ---: | --- |
| `magic` | `0x0000` | 4 | 固定 `0x49445050` |
| `version` | `0x0004` | 1 | 当前 `1` |
| `locked` | `0x0005` | 1 | 出厂锁定标志，`1` 表示禁止再次写入唯一识别码 |
| `crc16` | `0x0006` | 2 | `unique_id` CRC16 |
| `unique_id` | `0x0008` | 16 | 128bit 唯一识别码 |

当前有效记录大小：24 字节。其余空间预留。

### 4.2 Config 分区

起始偏移：`0x0000`

当前记录结构：

| 字段 | 偏移 | 大小 | 说明 |
| --- | ---: | ---: | --- |
| `magic` | `0x0000` | 4 | 固定 `0x43464750` |
| `config.version` | `0x0004` | 1 | 配置版本 |
| `config.rssi_t1` | `0x0005` | 1 | S1 阈值 |
| `config.rssi_t2` | `0x0006` | 1 | S2 阈值 |
| `config.rssi_t3` | `0x0007` | 1 | S3 阈值 |
| `config.tin_ms` | `0x0008` | 2 | 进入状态确认时间 |
| `config.tout_ms` | `0x000A` | 2 | 退出状态确认时间 |
| `config.idle_sleep_s` | `0x000C` | 2 | 空闲休眠时间 |
| `config.app_idle_s` | `0x000E` | 2 | 上位机空闲超时 |
| `config.adv_interval_ms` | `0x0010` | 2 | 广播间隔 |
| `config.scan_interval_ms` | `0x0012` | 2 | 扫描间隔 |
| `config.scan_window_ms` | `0x0014` | 2 | 扫描窗口 |
| `config.vibration_enable` | `0x0016` | 1 | 震动开关 |
| `config.reliable_msg_default` | `0x0017` | 1 | 可靠消息默认开关 |
| `config.privacy_mode` | `0x0018` | 1 | 隐私模式 |
| padding | `0x0019` | 1 | 结构体对齐 |
| `config.crc16` | `0x001A` | 2 | 配置 CRC16 |

当前有效记录大小：28 字节。其余空间预留。

### 4.3 Bond 分区

当前未写入业务数据，整区预留。

计划用途：

- 上位机绑定关系。
- 后续如重新启用 BLE SMP，可迁移自定义绑定记录。

### 4.4 Event Log 分区

当前未写入业务数据，整区预留。

计划用途：

- 离线错误日志。
- 最近重启原因。
- 关键通信统计。
- 重要发现事件缓存。

### 4.5 Factory 分区

起始偏移：`0x0000`

当前记录结构：

| 字段 | 偏移 | 大小 | 说明 |
| --- | ---: | ---: | --- |
| `magic` | `0x0000` | 4 | 固定 `0x46544350` |
| `version` | `0x0004` | 1 | 当前 `1` |
| `flags` | `0x0005` | 1 | bit0 identity_written, bit1 identity_locked, bit2 self_test_pass |
| `crc16` | `0x0006` | 2 | Factory Record CRC16，计算时该字段置 0 |
| `test_mask` | `0x0008` | 4 | 最近一次工厂自检请求 mask |
| `result_mask` | `0x000C` | 4 | 最近一次工厂自检结果 mask |
| `write_count` | `0x0010` | 4 | 身份写入次数 |
| `lock_count` | `0x0014` | 4 | 身份锁定次数 |
| `last_error` | `0x0018` | 4 | 最近一次工厂操作错误码，取 `app_status_t` |
| `last_unique_id` | `0x001C` | 16 | 最近一次成功写入或锁定的唯一识别码 |

当前有效记录大小：44 字节。其余空间预留。

### 4.6 当前记录绝对地址清单

应用层当前会实际读写 `Identity Record`、`Config Record` 和 `Factory Record`。`Bond`、`Event Log` 是整扇区预留，当前不会写入业务数据。

#### 512KB Flash

| 记录/区域 | 起始地址 | 结束地址 | 大小 | 当前状态 |
| --- | ---: | ---: | ---: | --- |
| Identity Record | `0x6F000` | `0x6F017` | 24B | 已实现读取 |
| Identity 分区剩余预留 | `0x6F018` | `0x6FFFF` | 4072B | 预留 |
| Config Record | `0x70000` | `0x7001B` | 28B | 已实现读写接口 |
| Config 分区剩余预留 | `0x7001C` | `0x70FFF` | 4068B | 预留 |
| Bond 分区 | `0x71000` | `0x71FFF` | 4KB | 预留 |
| Event Log 分区 | `0x72000` | `0x72FFF` | 4KB | 预留 |
| Factory Record | `0x73000` | `0x7302B` | 44B | 已实现读写 |
| Factory 分区剩余预留 | `0x7302C` | `0x73FFF` | 4052B | 预留 |

#### 1MB Flash

| 记录/区域 | 起始地址 | 结束地址 | 大小 | 当前状态 |
| --- | ---: | ---: | ---: | --- |
| Identity Record | `0xF7000` | `0xF7017` | 24B | 已实现读取 |
| Identity 分区剩余预留 | `0xF7018` | `0xF7FFF` | 4072B | 预留 |
| Config Record | `0xF8000` | `0xF801B` | 28B | 已实现读写接口 |
| Config 分区剩余预留 | `0xF801C` | `0xF8FFF` | 4068B | 预留 |
| Bond 分区 | `0xF9000` | `0xF9FFF` | 4KB | 预留 |
| Event Log 分区 | `0xFA000` | `0xFAFFF` | 4KB | 预留 |
| Factory Record | `0xFB000` | `0xFB02B` | 44B | 已实现读写 |
| Factory 分区剩余预留 | `0xFB02C` | `0xFBFFF` | 4052B | 预留 |

#### 2MB Flash

| 记录/区域 | 起始地址 | 结束地址 | 大小 | 当前状态 |
| --- | ---: | ---: | ---: | --- |
| Identity Record | `0x1F7000` | `0x1F7017` | 24B | 已实现读取 |
| Identity 分区剩余预留 | `0x1F7018` | `0x1F7FFF` | 4072B | 预留 |
| Config Record | `0x1F8000` | `0x1F801B` | 28B | 已实现读写接口 |
| Config 分区剩余预留 | `0x1F801C` | `0x1F8FFF` | 4068B | 预留 |
| Bond 分区 | `0x1F9000` | `0x1F9FFF` | 4KB | 预留 |
| Event Log 分区 | `0x1FA000` | `0x1FAFFF` | 4KB | 预留 |
| Factory Record | `0x1FB000` | `0x1FB02B` | 44B | 已实现读写 |
| Factory 分区剩余预留 | `0x1FB02C` | `0x1FBFFF` | 4052B | 预留 |

## 5. SDK 数据记录清单

### 5.1 MAC 分区

SDK 使用 `flash_sector_mac_address`。

| 偏移 | 大小 | 说明 |
| ---: | ---: | --- |
| `0x0000` | 6 | Public MAC |
| `0x0006` | 2 | Random Static MAC 高 2 字节 |

### 5.2 Calibration 分区

SDK 使用 `flash_sector_calibration`。

| 偏移 | 大小 | 说明 |
| ---: | ---: | --- |
| `0x0000` | 1 | RF 频偏电容校准 |
| `0x0040` | 预留 | TP 信息 |
| `0x00C0` | 7/12 | ADC VREF 校准，芯片平台相关 |
| `0x0180` | 16 | 固件签名 key |
| `0x01C0` | 2 | Flash VDD_F 校准 |

### 5.3 SMP Pairing 分区

SDK 使用 `flash_sector_smp_storage`。当前调试阶段没有启用 SMP pairing，但该区域仍作为 SDK 保留区，应用层不使用。

### 5.4 Master Pairing 分区

SDK 使用 `flash_sector_master_pairing`。512KB Flash 下地址为 `0x78000`；1MB 和 2MB Flash 下与 SMP Pairing 起始地址相同。当前吊坠按从机调试为主，应用层不使用该区域，但 Flash 分区计算仍将其纳入 SDK 保留区边界检查。

## 6. 当前限制

- 低电压禁止写 Flash 的策略尚未接入真实电池采样，量产前必须补齐。
- Flash 写保护策略尚未按最终固件和分区重新设计。
- Identity 分区支持通过调试通道写入和锁定，但量产权限控制、工装授权和防误操作流程还未定版。
- Event Log、Bond 分区目前只是保留，还没有日志格式和磨损均衡策略。

## 7. 调试读取方式

下位机调试通道新增 `GET_FLASH_MAP (0x0A)` 命令。PC 调试工具连接设备后点击“Flash 分区”，可以读取实际运行时的 Flash MID、容量、SDK 保留分区地址和 Pendant 应用分区表，用于核对当前样件是否符合本文档地址规划。
