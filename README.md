# GPS追踪系统

基于 ESP32 + N305-5Q（TD1050, BDS B1I/B1C / GPS L1 / GLONASS L1 / GAL E1）多模 GNSS 模块。纯 Leaflet 地图引擎（在线高德瓦片/离线灰色背景），轨迹记录、航向指北、参数化网格划分。OLED 屏幕显示关键数据。

- **WiFi SoftAP**: SSID `GPS`, 密码通过 `menuconfig` 配置（默认 `12345678`）
- **Web (SoftAP 模式)**: `http://192.168.4.1` (无外网/灰色背景)
- **Web (Station 模式)**: `http://10.0.0.67` (有外网/高德地图)
- **在线/离线功能完全一致**: 仅差地图瓦片，轨迹/网格/箭头全部相同
- **OLED**: 0.96" SSD1306/SSD1315, I2C 地址 `0x3C`, 软件 I2C (`SDA`=21, `SCL`=22)

> **关于 OLED 驱动**：当前使用软件 `I2C` (bit-bang) 实现，运行稳定，兼容各版本 `ESP-IDF`。硬件 `I2C` 驱动在不同 `IDF` 版本间存在兼容性差异，暂时不切换。

## 硬件规格

| 组件      | 型号/规格                                                       |
| ------- | ----------------------------------------------------------- |
| MCU     | Xtensa LX6 双核 @240MHz, 520KB SRAM, 4MB Flash                |
| GNSS    | N305-5Q (TD1050) — BDS B1I/B1C / GPS L1 / GLO L1OF / GAL E1 |
| OLED    | 0.96" 128×64, SSD1306/SSD1315, I2C 地址 `0x3C`                |
| GNSS 接口 | UART1: `TX`=17, `RX`=16, `115200bps`                        |
| OLED 接口 | SDA=21, SCL=22 (软件 I2C bit-bang)                            |
| 持久存储    | `nvs` (WiFi 凭据) + `nvs_grid` (网格参数, 256KB)                  |

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
- 纯 Leaflet 渲染，在线/离线统一代码路径
- 在线模式：高德地图瓦片
- 离线模式：灰色背景（`#e8ecf0`），所有功能不变

### GNSS 解析

- `NMEA 0183` V4.11，波特率 `115200`
- 解析 `RMC`（位置/速度）、`GGA`（海拔/卫星数）
- 原始 `NMEA` 语句按类型缓存，通过 `/nmea` 端点读取
- 线程安全的数据读取（互斥锁）
- 时间自动转换为北京时间 (`UTC+8`)

### OLED 显示

- 软件 I2C (bit-bang)，兼容性好，不受硬件 I2C 驱动版本影响
- 开机 30 秒冷启动倒计时（TCT logo + 秒数）
- 30 秒后：黑底白字实时数据，布局如下：

```
┌──────────────────────────────────────┐
│ A:192.168.4.1              A  08     │  GPS模式:IP  WiFi模式  卫星
│ 26-05-13 20:34:56                    │  日期  时间
│         3,7                          │  网格ID 放大居中
│ 30.294300  120.166300                │  经纬度 (小数点后6位)
│ 23.5kmh  50m                         │  速度(km/h) 海拔
└──────────────────────────────────────┘
```

### 网格管理

- 参数化网格：存起点、旋转角、步长、行列数，不存单个格子，面积无上限
- 从轨迹点提取最远 4 点 → 外扩 10m → 生成正北矩形区域 → 按步长划分
- 网格编号 `row,col`（左下角开始）
- 数据存储在 `nvs_grid` 分区（256KB），重启不丢失
- 查询 O(1)：旋转坐标 → 公式计算行列号，不遍历任何格子
- 网格 ID 始终可见（无背景标签），缩放 < 15 自动隐藏

### GNSS 接收机控制

- 波特率设置（`$CCCAS`）
- 定位间隔（`$CCINV`，以 Hz 显示）
- 工作模式切换 + 冷/温/热启动（`$CCSIR`）
- NMEA 语句显示控制（按钮仅控制页面显示，不发送 `$CCMSG` 给模块）
- 原始 NMEA 命令输入（带校验码验证）

## API 接口

| 端点                 | 方法   | 说明                               |
| ------------------ | ---- | -------------------------------- |
| `/`                | GET  | Web 主界面（Cache-Control: no-cache） |
| `/gps`             | GET  | 格式化 JSON 实时 GPS 数据               |
| `/gps.json`        | GET  | `/gps` 别名，向后兼容                   |
| `/grid`            | GET  | 当前位置 + 所在网格 ID                   |
| `/grid`            | POST | 从 4 角点+步长生成参数化网格                 |
| `/grid/data`       | GET  | 完整网格参数 + 前 500 格边界用于显示           |
| `/nmea`            | GET  | 原始 NMEA 语句缓存                     |
| `/config`          | POST | 发送 NMEA 指令                       |
| `/wifi/status`     | GET  | WiFi 状态（Station/SoftAP）          |
| `/wifi/scan`       | GET  | 扫描 WiFi 热点                       |
| `/wifi/connect`    | POST | 连接 Station 网络                    |
| `/wifi/disconnect` | POST | 断开 Station 连接                    |
| `/leaflet.js`      | GET  | Leaflet 地图引擎（内部使用）               |
| `/leaflet.css`     | GET  | Leaflet 样式（内部使用）                 |

## GPS JSON 格式

```
GET /gps
{
  "bj_time":            "203456.50",     // 北京时间 HHMMSS.SSS
  "status":             "A",             // A=自主 D=差分 E=估算 M=手动 N=无效
  "latitude":           "30.294300 N",   // 纬度 + 方向
  "longitude":          "120.166300 E",  // 经度 + 方向
  "speed_knots":        1.23,            // 速度(节)
  "speed_kmh":          2.28,            // km/h (前端可直接用)
  "speed_ms":           0.63,            // m/s
  "course":             45.0,            // 航向(度)
  "date":               "260513",        // 日期 DDMMYY
  "magnetic_variation": "0.1 E",        // 磁偏角 + 方向
  "altitude":           545.4,           // 海拔(米)
  "satellites":         8,               // 卫星数
  "grid":               "1,1"            // 网格ID(row,col)，0=不在网格内
}
```

## Grid JSON 格式

```
GET /grid
{
  "latitude":  "30.294300 N",   // 当前位置纬度 + 方向
  "longitude": "120.166300 E",  // 当前位置经度 + 方向
  "grid":      "3,7"            // 所在网格 ID (row,col)
}

GET /grid/data
{
  "grid_size": 2.0,                    // 步长(米)
  "angle": 0.000,                      // 旋转角(弧度), 0=正北
  "rows": 10, "cols": 10,             // 行列数
  "total": 100,                        // 总格子数
  "polygon": [[lng,lat],...],          // 区域4角点
  "grids": [                           // 前500格边界(浏览器绘制用)
    {"id":"1,1","bounds":[[lng,lat],[lng,lat]]},
    ...
  ]
}
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
| 2.1 | 2026-05-13 | 统一北京时间、整理 API 端点、去掉危险按钮、网格可视化增强、软件 `I2C` 稳定运行            |

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

### 🔴 待办
- [ ] 旋转区域网格（参数化网格已预留 `angle` 字段，前端计算旋转 OBB 即可）

### ✅ 已完成功能
- [x] 纯 Leaflet 渲染，在线/离线完全统一代码路径
- [x] 参数化网格：无格子数量上限，O(1) 查询，256KB NVS 持久化
- [x] 轨迹：橙色自由轨迹（全程记录） + 蓝色录制轨迹（仅录制时）
- [x] 航向指北/地图指北切换（N↑/H↑，箭头+地图旋转）
- [x] 自由定位持续画线（橙色，不计入区域数据）
- [x] 网格数据页面加载自动恢复（`/grid/data` API）
- [x] 网格区域外扩 10m 抵消 GPS 漂移
- [x] 网格 ID 无背景标签，缩放 < 15 自动隐藏，箭头永远最上层
- [x] WGS84→GCJ-02 坐标转换（高德地图偏移校正）
- [x] OLED 显示 GPS 模式指示 + 完整日期时间 + 经纬度 6 位小数
- [x] `/gps` JSON 格式化输出（人类可读）
- [x] `/wifi/disconnect` 端点（断开不擦除 STA 凭据）
- [x] 底部面板弹出时地图自适应避免遮挡
- [x] GPS 自动居中 + 速度偏移

### ⚠️ 已知问题

- Captive Portal（强制门户）已放弃：DNS socket 耗尽 lwIP socket 池导致 HTTP 服务崩溃
- iPhone 热点兼容性差：需开启「最大兼容性」，成功率和稳定性不如安卓
- IRAM 占用 78%，后续加功能需注意
- 无 NAT 转发，SoftAP 客户端不能通过 STA 上网
- 无身份认证，WiFi 密码为唯一安全屏障
