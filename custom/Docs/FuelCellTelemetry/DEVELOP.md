# FuelCellTelemetry 开发文档

更新日期为 2026-11-12。本文记录 QGC 端燃料电池遥测的实现约定、接口和开发日志。用户操作见 [README.md](README.md)，测试计划与记录见 [TEST.md](TEST.md)。

## 1. 目标与范围

QGC 通过地面网络向 AI 盒子请求燃料电池遥测，盒子从控制器串口取得数据并回复。测量值接入既有动力系统面板，FCS 状态与控制继续使用原有飞控接口。

## 2. 请求和回包

| 项目 | 约定 |
| --- | --- |
| 传输 | UDP 单播，QGC 使用临时本地端口 |
| 默认目标 | `192.168.144.110:14602` |
| 请求内容 | ASCII 文本 `GET FUEL_CELL_TELEMETRY/1` |
| 请求时机 | 识别活动车辆后立即请求，随后每 500 ms 轮询 |
| 回包 | 盒子向请求来源返回 MAVLink 2 `FUEL_CELL_TELEMETRY`（61002） |
| 来源校验 | 回包 IP 和 UDP 端口与配置一致，MAVLink 校验通过 |
| 消息筛选 | 系统号匹配活动车辆，组件号为 191，消息实例 `id` 为 0 |

只有 `valid_flags` 标为有效且数值有限的字段进入测量值集合。相同 `time_boot_ms` 的重复样本不会刷新超时。切换车辆或端点时关闭旧 UDP socket，清除读数及时间戳，随后用新端点请求。约 2 秒没有新样本时清空读数。已有 MAVLink 接收路径继续沿用同一消息筛选逻辑。

## 3. 设置与界面

| 设置键 | 类型 | 默认值 |
| --- | --- | --- |
| `FuelCellTelemetry/boxAddress` | 单播 IPv4 字符串 | `192.168.144.110` |
| `FuelCellTelemetry/boxPort` | 1–65535 的整数 | `14602` |

`FuelCellTelemetrySettings` 通过 QGC 的 `SettingsGroup` 和 Fact 保存设置。地址由 `QHostAddress` 校验，端口由 Fact 范围和接收模块共同校验。动力系统入口将两个 Fact 绑定到 `FuelCellTelemetry`，修改有效值后立即更新轮询目标。

## 4. 涉及文件

| 文件 | 职责 |
| --- | --- |
| [FuelCellTelemetry.cc](../../src/FuelCellTelemetry/FuelCellTelemetry.cc) | 轮询、回包校验、字段解码和超时 |
| [FuelCellTelemetrySettings.cc](../../src/Settings/FuelCellTelemetrySettings.cc) | 配置 Fact 和地址校验 |
| [FuelCellTelemetry.SettingsGroup.json](../../src/Settings/FuelCellTelemetry.SettingsGroup.json) | 默认值和端口范围 |
| [FuelCellTelemetrySettingsGroup.qml](../../src/UI/AppSettings/FuelCellTelemetrySettingsGroup.qml) | 遥测页的 IPv4 地址和端口控件 |
| [TelemetrySettings.qml](../../../src/UI/AppSettings/TelemetrySettings.qml) | 在遥测页加载定制设置区 |
| [PowerSystemIndicator.qml](../../src/UI/toolbar/PowerSystemIndicator.qml) | 将活动车辆和配置传给接收模块 |
| [FuelCellControllerManagerTest.cc](../../test/FuelCellController/FuelCellControllerManagerTest.cc) | 回包校验、切换端点和超时回归 |

## 5. 开发日志

### 2026-11-11 · 接入 AI 盒子主动取数

- 增加 UDP 轮询和 61002 解码，接入动力系统面板。
- 在 Linux 桌面用模拟飞控心跳激活 QGC，验证经遥控器热点向盒子取数。盒子数据来自实物控制器。
- QGC 提交 `f47947b8e` 已推送至 `SecDev/ft/FCS_Telemetry`。

### 2026-11-12 · 将端点接入 QGC 设置

- 在「应用设置 → 数传 → 燃料电池遥测」增加 IPv4 地址与 UDP 端口，沿用原默认值。已在虚拟机中文界面确认入口。
- 修改有效设置时立即重新请求，并清除旧读数。Linux 桌面构建成功，端点切换回归测试通过。
- 下次上电时，按 [README.md](README.md) 部署和切换盒子记录器，使用正式服务复测 61002 持续回传。
