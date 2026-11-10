# (Deprecated) UAVCAN 总电压告警：QGroundControl 开发说明

> **Deprecated**：本文记录合并前的独立 BatteryIndicator。当前BatteryIndicator已合并到PowerSystemIndicator，完整说明见 [PowerSystemIndicator 开发文档](../PowerSystemIndicator/DEVELOP.md)。

本文以 QGC 提交 `bb0315f6` 后的独立 BatteryIndicator 实现为准，记录 `da3124ea`、`37a51270`、`62429b80`、`bb0315f6` 的设计与变更。用户操作见 [README.md](README.md)；飞控侧数据生成与安全动作见 PX4 工作空间的 `SecDev_docs/PowerSystem/BatVoltFailsafe/DEVELOP.md`。

## 相关文件与数据流

以下代码路径均相对于 QGC 仓库根目录，指上述历史版本。可用 `git show bb0315f6:<路径>` 查看当时内容。

| 文件 | 职责 |
| --- | --- |
| `custom/src/QmlControls/BatteryIndicator.qml` | 独立工具栏入口、每电池状态与功率、抽屉详情和展开后的故障保护设置 |
| `custom/custom.qrc` | 将自定义组件注册到 `QGroundControl/Controls/BatteryIndicator.qml` 资源路径 |
| `custom/src/FirmwarePlugin/CustomFirmwarePlugin.cc` / `.h` | 为三个 UAVCAN 电压参数补充 QGC 元数据；保留工具栏中的 BatteryIndicator，并在其后插入 FuelStatusIndicator |
| `src/AutoPilotPlugins/PX4/SafetyComponent.qml` | Safety 页的 Low Power Failsafe Trigger，编辑动作和三个电压阈值 |
| `custom/translations/custom.ts`、`custom/translations/custom_zh_CN.ts` | 自定义 BatteryIndicator 的源文本与简体中文翻译 |
| `translations/qgc.ts`、`translations/qgc_source_zh_CN.ts` | Safety 页相关源文本与简体中文翻译 |

```text
UAVCAN 电池 → PX4 battery_status → MAVLink BATTERY_STATUS → QGC 电池 Fact
                   │                                      │
                   └→ Commander 安全动作                 └→ BatteryIndicator 图标与详情
PX4 参数 → QGC Fact → BatteryIndicator 展开区 / Safety 页面
```

QGC 不生成 `battery_status.warning`，也不执行飞控安全动作。它从电池 Fact 和阈值 Fact 计算本地图标状态；PX4 负责告警确认、Commander 的优先级与模式选择。

## 参数元数据与配置入口

`CustomFirmwarePlugin::_getMetaDataForFact()` 先调用 PX4FirmwarePlugin 基类，只对 `UAVCAN_POW_LOW`、`UAVCAN_POW_CRITI`、`UAVCAN_POW_EMERG` 修改返回的 FactMetaData。元数据将三者归入 `UAVCAN` 组，设置总电压说明、`V` 单位、两位小数、`0.1 V` 编辑步长、重启要求，以及 `47.4 / 45.6 / 44.4 V` 的原始默认值。其他参数保留基类元数据。这些默认值不会自动覆盖飞控保存的参数。

BatteryIndicator 在参数下载完成后，通过 `FactPanelController.getParameterFact(-1, name, false)` 取得三个阈值和 `COM_LOW_BAT_ACT`。四个 Fact 都存在时才显示展开区的 **Low Power Failsafe** 设置组；`ToolIndicatorPage` 使用 `showExpand: true` 和 `waitForParameters: true`，将电池详情与参数设置分成基本内容和展开内容。动作使用 `LabelledFactComboBox`，阈值使用 `LabelledFactTextField`；每项沿用标准 Fact 的编辑和提交方式，不提供跨参数统一保存。Safety 页用 `FactPanelController` 读取相同的四项参数，并将原来的 `BAT_LOW_THR`、`BAT_CRIT_THR`、`BAT_EMERGEN_THR` 百分比字段换为总电压阈值。

`UAVCAN_POW_DLY` 与 `COM_FAIL_ACT_T` 不在上述四项设置中；飞控若支持这些参数，可在完整参数界面配置。QGC 的默认值与说明属于参数元数据，不负责修正用户设定的阈值顺序。有效顺序是 `LOW > CRITI > EMERG > 0`。

## 独立 BatteryIndicator 状态计算

组件用活动车辆的 `batteries` 作为 `Repeater` 模型，**每个电池实例**创建一组顶部图标、电压和功率；抽屉基本内容也逐电池重复。电压取当前电池的 voltage Fact，功率取同一电池电压 × 电流并显示为整数 W。缺少数值时相应文字返回 `n/a`。没有活动车辆或没有电池条目时，Repeater 不创建电池可视项。

每个电池独立调用 `_batteryState(battery)`：

1. 若飞控上报 FAILED、UNHEALTHY、CHARGING，直接保留该状态。
2. 若阈值 Fact 缺失、顺序不满足 `LOW > CRITI > EMERG > 0`，或电压为无效数值，回退到飞控上报的 `chargeState`。
3. 否则依次比较 `voltage < EMERG`、`< CRITI`、`< LOW`，返回最严重的匹配级别；均不匹配时返回 OK。等于阈值时不进入该级。

`_stateText()` 从电池 `chargeState` 枚举字符串中查找计算后的等级；找不到时显示 `n/a`。`_stateVisual()` 同时选取图标资源与颜色：OK 为绿色，LOW 为黄色，CRITICAL 为橙色，EMERGENCY、FAILED、UNHEALTHY 为红色，其余状态使用默认图标和文字色。图标颜色是 QGC 的即时计算，没有连续低压确认或飞行中告警锁存；不能以它判断 PX4 的动作是否触发。即使飞控使用 `UAVCAN_SUB_BAT=2`，只要 QGC 取得有效电压和阈值，组件仍会按这三个阈值计算本地颜色，因此可能与飞控的滤波路径告警不同。

## 本次开发修改对 QGC 原有数据和行为的影响

以下对比以 `da3124ea` 引入的独立 BatteryIndicator、随后 `37a51270` 修改前的行为为基线，结果为 `bb0315f6` 后的合并前版本。变更发生在 QGC 的显示和参数入口；电池遥测仍来自已有的电池 Fact，飞控告警与飞行动作仍由 PX4 处理。

| 范围 | 原有数据和行为 | 本次修改后的影响 |
| --- | --- | --- |
| 顶部电池状态 | 以飞控上报的 `chargeState` 为主；状态为 OK 时，再按 `percentRemaining` 与 QGC 本地 `batteryIndicatorSettings.threshold1/threshold2` 选择图标和颜色 | 在阈值有效时，按每块电池的**总电压**与三个 `UAVCAN_POW_*` 参数确定 OK、LOW、CRITICAL、EMERGENCY；不再用剩余百分比或本地显示阈值给图标分级。FAILED、UNHEALTHY、CHARGING 仍优先使用上报状态；阈值或电压无效时回退到上报状态 |
| 顶部图标与颜色 | 百分比区间可显示绿、黄绿、黄等颜色；上报 LOW 为橙色，CRITICAL 等为红色 | 统一为 OK 绿色、LOW 黄色、CRITICAL 橙色、EMERGENCY 红色；`bb0315f6` 进一步修正 LOW、CRITICAL、EMERGENCY 的图标。每块电池仍各有一组图标、电压和功率 |
| 电池详情 | `Charge State` 直接显示飞控上报的状态；电压、功率、电流和已消耗电量由原有 Fact 显示 | 改为 `Power State`，显示与顶部图标相同的本地计算等级；电压、电流、已消耗电量的来源及功率 `电压 × 电流` 的计算方式未改，界面文字改用 Power 术语 |
| Indicator 展开区 | `Battery Display` 提供 QGC 本地百分比着色阈值与色阶示意，还可在条件满足时进入 `Vehicle Power` 配置页 | 改为 `Low Power Failsafe`，编辑飞控的 `COM_LOW_BAT_ACT` 和三级总电压阈值；移除本地着色阈值控件、色阶示意和该抽屉中的 `Vehicle Power` 跳转。该组仅在四个参数 Fact 都存在时显示 |
| PX4 Safety 页 | 低电量区域编辑 `COM_LOW_BAT_ACT` 及 `BAT_LOW_THR`、`BAT_CRIT_THR`、`BAT_EMERGEN_THR` 百分比参数 | 保留动作编辑，将三个百分比字段替换为 `UAVCAN_POW_LOW`、`UAVCAN_POW_CRITI`、`UAVCAN_POW_EMERG` 电压字段，并更新标签与翻译；旧百分比参数本身并未因此从飞控删除 |
| 参数元数据 | QGC 没有这组三个总电压参数的定制说明和默认值 | 为其补充 UAVCAN 分组、V 单位、说明、编辑精度与重启提示；`62429b80` 将 QGC 元数据默认值定为 `47.4 / 45.6 / 44.4 V`，不会自动写入或覆盖飞控参数 |

这组 QGC 修改没有更改 MAVLink `BATTERY_STATUS` 消息格式、电池 Fact 的电压/电流/剩余容量字段，也没有给 QGC 增加安全动作决策。由于本地图标按当前电压即时比较，它与 PX4 延时确认后的 `battery_status.warning` 可能暂时不一致；飞控若使用 `UAVCAN_SUB_BAT=2`，两者还可能采用不同的判定来源。

## 回归检查

维护这个历史实现时，应检查：参数未就绪、四个参数缺失之一、阈值顺序无效、等于各阈值、逐级跌破与回升、FAILED / UNHEALTHY / CHARGING 优先级、无电池及多电池；核对每个电池各自的电压、功率、图标与抽屉内容。再检查展开区和 Safety 页对同一飞控参数的读取、编辑与重启提示。与配套 PX4 联调时，用 `listener battery_status` 区分 QGC 即时颜色和飞控经过 `UAVCAN_POW_DLY` 确认后的 `warning`，并按 `COM_LOW_BAT_ACT` 检查实际动作。

以上是回归范围说明，不代表这些测试已在本次文档编写中执行。

## 开发日志

### 2026-08-12 — `da3124ea`

新增独立自定义 BatteryIndicator、QRC 注册和中英文翻译，显示电池图标、电压、功率、详情与原有电池配置内容。

### 2026-08-21 — `37a51270`

新增三级 UAVCAN 总电压参数元数据；按总电压计算顶部状态；将展开区整理为 `COM_LOW_BAT_ACT` 和三个电压阈值；Safety 页从电量百分比字段切换为总电压阈值，并更新 Power 术语与翻译。此时 QGC 元数据中的初始默认值为 `50 / 45 / 40 V`。

### 2026-08-21 — `62429b80`

将 QGC 元数据中 LOW / CRITICAL / EMERGENCY 的默认值调整为 `47.4 / 45.6 / 44.4 V`。

### 2026-08-21 — `bb0315f6`

将 LOW 图标与颜色改为黄色，CRITICAL 改为橙色，EMERGENCY 使用红色严重电池图标；FAILED 和 UNHEALTHY 仍使用红色故障图标。
