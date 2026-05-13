# GPS追踪系统

基于 Xtensa LX6 双核 240MHz + TD1030（BDS B1 / GPS L1 / GLONASS L1 三频点）GNSS 模块，通过 WiFi 热点提供 Web 界面，支持实时位置显示、轨迹记录、区域绘制、网格划分。附带 OLED 屏幕显示关键数据。

- **WiFi SoftAP**: SSID `GPS`, 密码通过 `menuconfig` 配置（默认 `12345678`，建议编译前修改）
- **Web (SoftAP 模式)**: `http://192.168.4.1` (Canvas 离线网格)
- **Web (Station 模式)**: `http://10.0.0.67` (Leaflet + 高德地图)
- **OLED**: 0.96" SSD1306/SSD1315, `I2C` (`SDA`=21, `SCL`=22), 软件 `I2C` (bit-bang)
- **串口**: 浏览串口日志确认系统状态

> **关于 OLED 驱动**：当前使用软件 `I2C` (bit-bang) 实现，运行稳定，兼容各版本 `ESP-IDF`。硬件 `I2C` 驱动在不同 `IDF` 版本间存在兼容性差异，暂时不切换。

## 硬件规格

| 组件      | 型号/规格                                          |
| ------- | ---------------------------------------------- |
| MCU     | Xtensa LX6 双核 @240MHz, 520KB SRAM, 4MB Flash   |
| GNSS    | TD1030 — BDS B1 / GPS L1 / GLONASS L1 三频点      |
| OLED    | 0.96" 128×64, SSD1306/SSD1315, `I2C` 地址 `0x3C` |
| GNSS 接口 | `UART1`: `TX`=17, `RX`=16, `115200bps`         |
| OLED 接口 | `I2C0`: `SDA`=21, `SCL`=22 (软件 `I2C`)          |
| 持久存储    | `NVS` (WiFi 凭据、网格数据)                           |

## 资源占用

| 资源             | 已用               | 剩余       | 总计               |
| -------------- | ---------------- | -------- | ---------------- |
| `IRAM`（指令 RAM） | 101.8 KB (77.7%) | 29.3 KB  | 131 KB           |
| `DRAM`（数据 RAM） | 77.1 KB (42.6%)  | 103.7 KB | 176 KB           |
| Flash（固件）      | 1.08 MB          | ~0.89 MB | 1.87 MB (app 分区) |

## 功能模块

### WiFi

- **SoftAP + Station 同时工作**，SoftAP 始终可用
- Station 可扫描/连接外部 WiFi（无断开按钮，防止误操作）
- SoftAP 连接数上限 4 个

### HTTP 服务器

- 端口 `80`，嵌入式 HTML（~50KB，含 Leaflet 库）
- 在线模式：Leaflet + 高德地图 (style=8, `maxZoom`=20, `maxNativeZoom`=18)
- 离线模式：Canvas 坐标格网 + 10×10 演示网格
- Canvas 支持拖拽、滚轮缩放、悬停显示格子 ID

### GNSS 解析

- `NMEA 0183` V4.11，波特率 `115200`
- 解析 `RMC`（位置/速度）、`GGA`（海拔/卫星数）
- 原始 `NMEA` 语句按类型缓存，通过 `/nmea` 端点读取
- 线程安全的数据读取（互斥锁）
- 时间自动转换为北京时间 (`UTC+8`)

### OLED 显示

- 软件 `I2C` (bit-bang) 驱动，兼容性好，不受硬件 `I2C` 驱动版本影响
- 开机 30 秒冷启动倒计时
- 30 秒后：黑底白字实时数据（IP / 时间 / 网格 ID / 经纬度 / 速度）
- 网格 ID 放大居中显示

### 网格管理

- 从 GPS 轨迹生成闭合区域
- 按用户输入的边长进行正方形网格划分
- 网格编号 `row,col`（左下角开始）
- 数据存储在 `NVS` 分区，重启不丢失
- 演示模式：Canvas / Leaflet 自动显示 10×10 网格

### GNSS 接收机控制

- 波特率设置（`$CCCAS`）
- 定位间隔（`$CCINV`，以 Hz 显示）
- 工作模式切换 + 冷/温/热启动（`$CCSIR`）
- `NMEA` 语句输出控制（`$CCMSG`，左列按钮 + 实时预览）
- 恢复默认（`$CCDFT`，所有设置恢复出厂）
- 原始 `NMEA` 命令输入（带校验码验证）

## API 接口

| 端点              | 方法   | 说明                        |
| --------------- | ---- | ------------------------- |
| `/`             | GET  | Web 主界面                   |
| `/gps`          | GET  | 解析后的 GPS 实时数据             |
| `/nmea`         | GET  | 原始 NMEA 语句（当前按类型缓存的最新一条）  |
| `/grid`         | GET  | 当前位置 + 所在网格 ID            |
| `/grid`         | POST | 从区域划分网格                   |
| `/config`       | POST | 发送 NMEA 指令                |
| `/wifi/status`  | GET  | WiFi 状态（Station/SoftAP）   |
| `/wifi/scan`    | GET  | 扫描 WiFi 热点                |
| `/wifi/connect` | POST | 连接 Station 网络             |
| `/leaflet.js`   | GET  | Leaflet 地图引擎（内部使用，不要直接访问） |
| `/leaflet.css`  | GET  | Leaflet 样式（内部使用，不要直接访问）   |

## GPS JSON 格式

```
GET /gps
{
  "bj_time":           "203456.000",    // 北京时间 HHMMSS.SSS
  "status":            "A/V",           // A=有效 V=无效
  "latitude":          "30.294300 N",   // 纬度 + 方向
  "longitude":         "120.166300 E",  // 经度 + 方向
  "speed_knots":       1.23,            // 速度(节)
  "course":            45.0,            // 航向(度)
  "date":              "230394",        // 日期 DDMMYY
  "magnetic_variation":"0.1 E",        // 磁偏角 + 方向
  "mode":              "A",             // 定位模式
  "altitude":          545.4,           // 海拔(米)
  "speed_kmh":         2.28,            // km/h
  "speed_ms":          0.63,            // m/s
  "satellites":        8,               // 卫星数
  "grid":              "1,1"            // 网格ID(row,col)
}
```

## Grid JSON 格式

```
GET /grid
{
  "latitude":  "30.294300 N",   // 当前位置纬度 + 方向
  "longitude": "120.166300 E",  // 当前位置经度 + 方向
  "grid":      "3,7"            // 所在网格 ID (row,col)，0 表示不在网格内
}
```

## OLED 显示布局

```
┌──────────────────────────────────────┐
│ 192.168.4.1                  S  08   │  IP  模式  卫星数
│ 20:34:56                             │  北京时间
│         3,7                          │  网格ID 放大居中
│ N30.2943  E120.1663                  │  经纬度
│ 1.2kn  2.3kmh  545m                  │  速度(节+kmh) 海拔
└──────────────────────────────────────┘
```

## 项目结构

```
GPS/
├── gps_tracker/
│   ├── CMakeLists.txt
│   ├── partitions.csv
│   └── main/
│       ├── CMakeLists.txt
│       ├── Kconfig.projbuild   WiFi AP 密码配置
│       ├── main.c              主程序
│       ├── wifi_manager.c/.h   WiFi SoftAP + Station 管理
│       ├── gps_parser.c/.h     NMEA 解析 (UART1)
│       ├── http_server.c/.h    HTTP 服务器 + API
│       ├── grid_manager.c/.h   网格划分 + NVS 存储
│       ├── receiver_config.c/.h TD1030 配置指令
│       ├── oled_display.c/.h   SSD1306 驱动 (bit-bang I2C)
│       └── index.html          Web UI 界面 (~400行)
├── README.html
├── README.md
└── .gitignore
```

## 开发日志

| 版本  | 日期         | 变更                                                       |
| --- | ---------- | -------------------------------------------------------- |
| 1.0 | 2026-05-11 | 初始版本：`ESP-IDF` 项目结构、WiFi SoftAP+Station、OLED、GPS 解析、网格管理 |
| 2.0 | 2026-05-12 | Web UI 大重构：Leaflet 高德地图、在线/离线检测、Canvas 网格回退              |
| 2.1 | 2026-05-13 | 统一北京时间、整理 API 端点、去掉危险按钮、网格可视化增强、软件 `I2C` 稳定运行 |

## 当前状态

### ✅ 已固化（勿随意改动）
- GPS NMEA 解析（RMC + GGA，北京时间 `bj_time`）
- OLED 实时数据展示（冷启动倒计时 → 定位数据）
- HTTP API 结构（`/gps`, `/nmea`, `/grid`, `/wifi/*`, `/config`）
- Web UI 整体布局（状态栏、底部面板、5 个标签页）
- Canvas 离线网格 + Leaflet 在线地图 + 网格叠加
- GNSS 接收机控制（波特率/间隔/模式/启动/NMEA 语句）
- Wi-Fi SoftAP ↔ Station 切换（凭据连接成功后才写 NVS）
- 网格持久化（NVS，含区域多边形 + 格子）
- Kconfig 密码配置（`menuconfig` → GPS Tracker Configuration）

### 🔴 GPS 定位 TODO（下个会话）
- [ ] 实地测试：带 ESP32 出门走动，验证地图自动跟随 + 航向箭头
- [ ] 验证轨迹记录 + 从轨迹生成区域 + 网格划分完整流程
- [ ] 验证网格落点查询（`/gps` 返回 `grid` 字段）
- [ ] 验证 OLED 显示真实 GPS 数据（经纬度/速度/海拔/卫星数）
- [ ] 验证 NMEA 语句原始输出（`/nmea` 实时框）
- [ ] 长期稳定性测试（连续运行是否崩溃/内存泄漏）

### ⚠️ 已知问题
- Captive Portal（强制门户）已放弃：DNS socket 耗尽 lwIP socket 池导致 HTTP 服务崩溃
- iPhone 热点兼容性差：需开启「最大兼容性」，成功率和稳定性不如安卓
- IRAM 占用 78%，后续加功能需注意
- 无 NAT 转发，SoftAP 客户端不能通过 STA 上网
- 无身份认证，WiFi 密码为唯一安全屏障

