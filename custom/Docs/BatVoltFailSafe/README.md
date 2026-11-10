# (Deprecated) UAVCAN 总电压告警：QGroundControl 使用说明

> **Deprecated**：本文记录合并前的独立 BatteryIndicator。当前BatteryIndicator已合并到PowerSystemIndicator，完整说明见 [PowerSystemIndicator 用户说明](../PowerSystemIndicator/README.md)。

本文面向使用独立 BatteryIndicator 版本 QGC 的飞手和整机配置人员。实现细节见 [DEVELOP.md](DEVELOP.md)；飞控告警和动作的完整规则见 PX4 工作空间的 `SecDev_docs/PowerSystem/BatVoltFailsafe/README.md`。

## 适用范围

配套 PX4 固件在 `UAVCAN_SUB_BAT=1` 时，依据 UAVCAN 电池**总电压**产生 LOW、CRITICAL、EMERGENCY 告警。QGC 读取电池电压、电流和 `chargeState`，并提供三个总电压阈值及 `COM_LOW_BAT_ACT` 的编辑入口。这里的阈值是整包电压，不是单节电压或剩余电量百分比。

QGC 的电池图标颜色按当前测量电压即时计算；飞控的 `battery_status.warning`、告警确认时间和安全动作由 PX4 决定。`UAVCAN_SUB_BAT=0` 不建立该 UAVCAN 电池订阅；`=2` 使用 PX4 电池库滤波，不使用飞控侧的这组总电压告警逻辑。

## 顶部 BatteryIndicator

独立入口位于顶部工具栏，**每个电池条目各显示一组**图标、电压和功率。电压来自该电池的 voltage Fact；功率由同一电池的电压 × 电流计算，显示为整数 W。缺少有效数值时，对应字段显示 `n/a`。没有电池条目时，入口没有可见的电池图标和数值。

| 图标颜色 | 电池状态 |
| --- | --- |
| 绿色 | OK |
| 黄色 | LOW |
| 橙色 | CRITICAL |
| 红色 | EMERGENCY、FAILED、UNHEALTHY |
| 默认文字色 | CHARGING、UNDEFINED 等其他状态 |

FAILED、UNHEALTHY、CHARGING 优先保留飞控上报的状态。其余情况在三个电压参数已下载、顺序有效且电压有效时，按 `EMERGENCY → CRITICAL → LOW` 的顺序比较。电压必须**严格低于**对应阈值才进入该级；等于阈值时不会进入。参数缺失或顺序无效时，图标回退使用飞控上报的 `chargeState`。

**图标变色不代表飞控已确认低压。** QGC 的颜色计算不等待 `UAVCAN_POW_DLY`；短时压降可能使图标先变色，随后恢复，而飞控可能未曾报告告警。已解锁飞行中的 Commander 还会保留出现过的最高告警级别，不能仅凭图标恢复判断安全动作已解除。

## 查看详情与设置

点击 BatteryIndicator 打开工具栏抽屉。基本内容按电池逐项列出 Power State、Measured Voltage、Power、Current、Consumed；多电池时按电池 ID 区分。继续展开后，**Low Power Failsafe** 区域提供以下四项设置：

| 界面项 | 飞控参数 | 配套 PX4 默认值 | 用途 |
| --- | --- | ---: | --- |
| Vehicle Action | `COM_LOW_BAT_ACT` | `0` | 选择低电量告警后的动作 |
| Low Voltage | `UAVCAN_POW_LOW` | `47.4 V` | LOW 总电压阈值 |
| Critical Voltage | `UAVCAN_POW_CRITI` | `45.6 V` | CRITICAL 总电压阈值 |
| Emergency Voltage | `UAVCAN_POW_EMERG` | `44.4 V` | EMERGENCY 总电压阈值 |

车辆设置的 **Safety → Low Power Failsafe Trigger** 也可编辑同一组参数。三个阈值应满足 `UAVCAN_POW_LOW > UAVCAN_POW_CRITI > UAVCAN_POW_EMERG > 0`；若顺序无效，飞控的总电压告警逻辑不工作。QGC 元数据标记这三个阈值为重启后生效；表中的默认值用于元数据展示，不会由 QGC 自动写入飞控。

| `COM_LOW_BAT_ACT` | LOW | CRITICAL | EMERGENCY |
| ---: | --- | --- | --- |
| `0`（默认） | 告警 | 告警 | 告警 |
| `1`（旧值） | 告警 | 返航 | 返航 |
| `2` | 告警 | 降落 | 降落 |
| `3` | 告警 | 返航 | 降落 |
| `4` | 告警 | Hold | Hold |

值 `4` 需配套支持 Hold 选项的 PX4 固件。`COM_LOW_BAT_ACT` 作用于 Commander 接收的电池告警，并不限于 UAVCAN。`UAVCAN_POW_DLY`（总电压连续确认时间）和 `COM_FAIL_ACT_T`（Commander 动作延时）不在 BatteryIndicator 的四项设置中；如需调整，应使用完整参数界面。

## 配置与检查

1. 确认飞控已收到 UAVCAN 电池总电压，设置 `UAVCAN_SUB_BAT=1`，并核对 QGC 显示值与 PX4 `battery_status.voltage_v` 一致。
2. 按电池规格和整机负载试验确定三级阈值，设置 Vehicle Action。各 Fact 控件分别提交参数；修改后重新读取四项值，按飞控要求重启。
3. 在安全台架条件下模拟跨越各阈值，分别观察图标、PX4 `listener battery_status` 的 `warning`、地面站告警和预期动作。若要验证短时压降，另检查 `UAVCAN_POW_DLY` 对飞控告警的确认效果。

若界面无电池条目，检查 UAVCAN 订阅、飞控电池实例和通信；若显示 `n/a`，检查对应电压或电流数据。若配置区未显示，先确认飞控参数已下载且四个参数均存在。若图标已变色但没有安全动作，检查飞控告警是否已确认、`COM_LOW_BAT_ACT`、`COM_FAIL_ACT_T`、解锁时已有告警和其他安全条件。
