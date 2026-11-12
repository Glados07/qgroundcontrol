# FuelCellTelemetry 用户说明

FuelCellTelemetry 让 QGroundControl 主动向 AI 盒子读取燃料电池测量值，并在顶部动力系统面板中显示。开发约定见 [DEVELOP.md](DEVELOP.md)，测试用例和执行结果见 [TEST.md](TEST.md)。

## 1. 使用前提

- QGC 已连接活动飞控。
- 地面站可以通过网络访问 AI 盒子。盒子已部署支持 UDP `request` 模式的记录器，燃料电池串口有有效数据。
- 盒子发送的 MAVLink 系统号与当前活动车辆一致，组件号为 191。

## 2. 配置 AI 盒子地址

中文版打开「应用设置 → 数传 → 燃料电池遥测」。英文界面中的「数传」为 Telemetry。

| 界面项 | 默认值 | 填写要求 |
| --- | --- | --- |
| AI 盒子 IPv4 地址 | `192.168.144.110` | 填写地面站可访问的盒子单播 IPv4 地址 |
| AI 盒子 UDP 端口 | `14602` | 填写盒子 `request` 服务的监听端口，范围为 1–65535 |

编辑后按回车或移开焦点确认。有效值保存在 QGC 本地，已有活动车辆时立即切换请求目标。盒子一侧的 `qgc.listen_host` 和 `qgc.listen_port` 要与这里的设置对应。

### AI 盒子首次切换

1. 上电后部署新版记录器，并将燃料电池串口配置为 `/dev/ttyS0`。
2. 将 `qgc.enabled` 设为 `true`，`qgc.mode` 设为 `request`。确认 `qgc.listen_host` 和 `qgc.listen_port` 与 QGC 设置一致。
3. 核对 `qgc.system_id` 与飞控系统号一致，`qgc.component_id` 为 191。重启盒子记录器服务并确认 QGC 能持续收到 61002。

## 3. 查看读数

点飞行界面顶部的动力系统图标，在右侧「燃料电池」区域查看电堆进风和出风温度、风扇占空比、进气压力、电堆电压和电流、DCDC 状态、故障码和温度，以及系统子状态。

切换车辆或盒子端点时，旧读数会清空。连续约 2 秒没有收到新样本时，测量值显示为占位符。动力系统面板中的设备连接、飞控请求功率及 FCS 操作由原有飞控链路提供，具体说明见 [PowerSystemIndicator 用户说明](../PowerSystemIndicator/README.md)。

## 4. 收不到数据时

1. 确认 QGC 已识别活动车辆。
2. 核对设置中的 IP 和端口与盒子监听配置是否一致。
3. 确认盒子记录器已启用 `request` 模式、能够从燃料电池串口读到有效帧，并使用当前车辆的 MAVLink 系统号。
