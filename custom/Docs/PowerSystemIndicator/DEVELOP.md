# PowerSystemIndicator 开发文档

更新日期：2026-11-04。适用仓库：`/home/glados/app/qgroundcontrol`。

本文面向开发人员，记录 PowerSystemIndicator 的设计约定、实现边界、代码组织和开发日志。用户操作说明见 [README.md](README.md)，测试计划与执行记录见 [TEST.md](TEST.md)，FuelCellController 后端接口见 [后端 README](../../src/FuelCellController/README.md)。

## 1. 目标与范围

将顶部独立的 BatteryIndicator 和 FuelCellControllerIndicator 合并为一个 `PowerSystemIndicator`，减少工具栏占用，并在同一详情面板内查看 PMU 电池状态、低电量配置及 FCS 状态和控制。

按现有 QGC 做法完成最小必要变更，界面和集成修改限于 `custom/`。复用既有电池 Fact、参数编辑控件及 FuelCellController 后端，不修改 PX4、MAVLink 协议、通用工具栏框架或其他无关功能。

## 2. 已确认的交互选择

| 项目 | 已确认要求 |
| --- | --- |
| 电池配置区 | 默认完整展开，保留低电量飞行动作和三个电压阈值，共四项 |
| 展开布局 | 始终使用左侧电池、右侧燃料电池的双栏布局，不按窗口宽度切换 |
| FCS 异常提示位置 | 主图标上增加角标，详细原因在详情中查看 |
| 电池数据缺失 | 保留入口，无效项显示 `— V`、`— W`；FCS 区仍可查看，不以 FCS 数值代替电池数值 |
| 参数未就绪或缺失 | 仅配置区显示等待或不可用提示，不阻塞电池状态和 FCS 区 |
| 组件组织 | 前端只保留工具栏入口和详情页两个文件；常驻状态归入口，全部详情内容归详情页 |
| 角标规则 | 设备断连、故障关机或急停状态显示红色角标；输出电流受限显示橙色角标，红色优先 |
| 飞控整体失联 | 电池数值改为无效占位，主图标中性色，配置禁用；FCS 沿用现有失联处理 |

原计划拆分的电池模型、状态内容和故障保护配置先合并为 `PowerSystemBattery.qml`，后续又将其常驻状态并入工具栏入口、详情内容并入详情页，不再保留独立电池组件。

## 3. 顶部入口

### 3.1 内容与数据来源

- 图形复用 `FuelCellController.svg`，右侧上下两行分别显示母线电压和功率，均为“数值 + 单位”。
- 电压来自当前活动车辆第一个电池条目的 voltage Fact，沿用 Fact 数值格式并显示 `V`。
- 功率由同一电池条目的电压乘电流得到，取整数并显示 `W`。
- 数值文本使用正常文本颜色，主图标按电池状态变色。
- 有活动飞控但尚无电池数据时仍显示入口，各无效项分别显示占位；有效零值不得当成缺失值。
- 没有活动车辆或仅有离线编辑车辆时不显示入口。

不新增电池数据来源、不做多电池汇总。左侧 PMU 测量功率与右侧飞控请求功率各自保留原有来源。

### 3.2 主图标颜色

复用原 BatteryIndicator 的状态判断顺序和严格小于比较：

1. 无可用电池或飞控整体通信中断时，使用未定义状态和中性色。
2. 电池上报 FAILED、UNHEALTHY、CHARGING 时优先保留该状态。
3. 电压无效或阈值未就绪、顺序无效时，回退到电池上报状态。
4. 阈值有效时依次比较 emergency、critical、low，使用严格 `<`；等于某阈值时不进入该阈值对应的更严重状态。

有效阈值关系为 `low > critical > emergency > 0`。

| 最终电池状态 | 主图标颜色 |
| --- | --- |
| OK | 绿色 |
| LOW | 黄色 |
| CRITICAL | 橙色 |
| EMERGENCY、FAILED、UNHEALTHY | 红色 |
| CHARGING、UNDEFINED 及其他未映射状态 | 中性色，沿用默认文本色 |

FCS 状态不改变主图标的电池配色。

### 3.3 FCS 红/橙角标

只在 FCS 遥测新鲜时显示角标。满足以下任一条件时显示红色角标：

- `connectionState == Disconnected`；
- 设备数据有效，且 FCS 状态为 9、10（故障关机中、故障关机完成）或 11、12（急停中、急停完成）。

设备数据有效且 FCS 状态为 13（输出电流受限）时显示橙色角标，详情中的状态值同时使用橙色文字。红色条件优先于橙色。状态离开 13 后立即清除橙色告警，不锁存、不弹窗、不平滑。

等待首次状态、从未连接、遥测过期、正常运行、普通停机完成或未知原始状态 14～15 均不显示角标。遥测未知与 PX4 已判断的设备断连是不同状态；详情仍须显示相应原因。

## 4. 展开面板

### 4.1 固定双栏

展开页始终为左侧电池、右侧燃料电池，两栏顶部对齐，中间使用分隔线。

左栏直接显示原电池详情和配置，无需再次点击展开：

| 分组 | 内容 |
| --- | --- |
| Power Status | 电源状态、实测电压、功率、电流、已消耗电量 |
| Low Power Failsafe | `COM_LOW_BAT_ACT`、`UAVCAN_POW_LOW`、`UAVCAN_POW_CRITI`、`UAVCAN_POW_EMERG` |

配置继续使用标准 Fact 控件，保留原有校验、逐项提交及参数本身的重启要求。不新增统一保存按钮、批量事务或额外解锁条件。

右栏直接集成原 `FuelCellControllerPage.qml` 的以下内容：

- 设备连接状态、FCS 实际状态、锂电池 SOC 和设备 DCDC 输出电流；
- 飞控当前下发的请求功率，只读；
- 当前 FCS 指令、开机、关机、FCS 急停按钮及确认对话框；
- 请求结果、不可操作原因及原有后端控制门限。

标题源文本使用 `Fuel Cell Controller`，中文翻译为“燃料电池控制器”。不恢复页面标题下的“飞控 #1”小字或急停按钮下的说明小字；操作确认框仍须明确目标车辆。

### 4.2 尺寸与文字

单栏宽度为 `44 × defaultFontPixelWidth`，组合页宽度由两栏、两个栏间距和中间 1 px 分隔线组成。实现不读取窗口宽度，不提供窄屏标签、动态压缩或其他响应式降级；窗口不足以容纳双栏不属于正常工作场景。

使用标准详情抽屉的纵向滚动；长状态、结果和确认文字应在各自栏内换行。

低电量动作下拉框的当前选项过长时允许省略显示，展开后沿用原有完整选项。不能让长文字无限撑宽整个面板。

## 5. 边界与状态处理

### 5.1 数据独立性

- 电池测量和 FCS 遥测分别判断可用性，任何一方暂不可用均不阻塞另一方显示。
- 电压、电流、功率等按字段检查有限值；部分字段无效时仅相应项使用占位。
- FCS 请求功率为飞控实际用于 CAN 下发的值，不能拿来替代 PMU 测量功率，也不开放编辑。
- DCDC 输出电流是 FCS 设备遥测，与 PMU 测量电流及飞控内部 CAN 控制电流分离；仅设备连接正常时有效，有效零值显示为 `0.00 A`。
- FCS 原始 4 bit 状态 0～15 均保留；13 为输出电流受限，14～15 以带数值的未知状态显示。大于 15 的 MAVLink 值拒绝为畸形状态。
- PMU 失效后飞控可能使用保持电压和固定 3000 W 回退，因此左侧测量功率与右侧请求功率不同可以是正常现象。QGC 不再接收回退期间的实际 CAN 电压和电流。
- 不新增 PMU 报文级超时判断、回退标志或 MAVLink 字段。飞控仍在线但 PMU 停止更新时，QGC 电池数据可能保持旧值，此限制见验证记录。

### 5.2 参数就绪和飞控失联

- 参数尚未加载完成时，配置区显示等待提示；任一必要参数缺失时，配置区显示不可用提示。
- 状态详情和 FCS 区不等待电池参数。
- 飞控整体失联时，电池测量显示占位，主图标中性色，已显示的配置控件禁用。
- FCS 遥测与操作门限继续由 `FuelCellControllerManager` 处理，不新增界面层重试和自动复位。

### 5.3 车辆切换与编辑生命周期

- 切换活动车辆时关闭当前电源详情面板并重新绑定电池数据。
- `FactPanelController` 构造时捕获活动车辆，必须随车辆重新创建；取参数前校验控制器属于当前车辆。
- 参数编辑器固定绑定打开时的 Facts，旧车辆尚未提交的编辑不得被重新指向新车辆。
- 编辑器销毁时释放其全局校验错误状态，避免关闭面板后留下输入校验锁定。
- 沿用 FCS 确认框的 `sessionRevision` 校验。会话失效或面板销毁时关闭确认框，旧操作不得发给切换后的车辆。

### 5.4 FCS 控制

FCS 开机、关机、急停均保留现有确认机制。普通请求等待确认时，急停仍可按后端门限抢占。

“FCS 急停”仅发送燃料电池协议急停指令，不调用飞控锁定、飞行终止或电机停转接口。打开或关闭页面、调整窗口、恢复通信均不自动发送指令。重试、确认及超时仍完全由既有后端处理。

## 6. 实现组织与限制

| 组件 | 职责 |
| --- | --- |
| `PowerSystemIndicator.qml` | 常驻车辆、电池和参数状态，电池配色与数值计算，顶部图标、角标和详情入口 |
| `PowerSystemIndicatorPage.qml` | 固定双栏、电池详情与配置、FCS 详情与控制、确认流程和页面生命周期 |

电池常驻状态封装在工具栏文件内的 `batteryModel`，不依赖详情面板是否打开，避免关闭面板后顶部失去数据或重复维护状态。工具栏只向详情页提供 `manager` 和 `batteryModel` 两个依赖。详情页以文件内 `BatteryPane`、`FuelCellPane`、`StatusField` 划分界面职责，主布局只负责固定双栏组合，不再注册只被本功能使用一次的独立 QML 类型。

通过 `CustomFirmwarePlugin` 在原 BatteryIndicator 位置替换为合并入口，保留独立 FuelStatusIndicator 的原有顺序。同步 QRC、QML 模块和翻译注册，移除不再使用的自定义电池覆盖及独立 FCS 顶部入口。

不改通用 `ToolIndicatorPage`、`MainWindow`、`FlyViewToolBar`、上游电池 FactGroup 或上游 BatteryIndicator。FuelCellControllerManager 暴露请求功率、DCDC 电流和 FCS 状态等只读属性，不再暴露协议已删除的母线电压和电流；QGC 只消费固定版本的生成库，不在本仓库定义 MAVLink 消息，也不引入新的运行依赖或永久 QML 测试依赖。

## 7. 验收标准

下列为功能验收要求，实际已执行项目和证据范围以 [TEST.md](TEST.md) 为准。

| 编号 | 场景与预期 |
| --- | --- |
| A01 | 活动车辆只有一个合并电源入口，FCS 图形右侧显示电池电压与功率；原电池和 FCS 独立入口不重复出现 |
| A02 | 电压阈值和等于阈值的边界沿用原比较逻辑；FAILED、UNHEALTHY、CHARGING 的优先级及颜色保持一致 |
| A03 | 新鲜遥测中的断连及状态 9～12 出现红角标，状态 13 出现橙色角标和橙色详情文字；红色优先，从未连接、等待、状态 14～15 和过期不显示 |
| A04 | 一次点击直接看到左侧五项电池状态、四项配置和右侧完整 FCS 内容，两栏之间显示分隔线 |
| A05 | 展开页不根据窗口宽度切换布局，不显示标签页；切换车辆时关闭当前面板并重新绑定数据 |
| A06 | 正常工作窗口和字体设置下内容可读、可纵向滚动；打开、关闭及调整窗口不产生额外 FCS 指令 |
| A07 | 无电池、部分 NaN/无穷值或无 FCS 状态时，按字段显示占位，各区域不互相阻塞；SOC 和 DCDC 电流的有效零值仍正常显示 |
| A08 | 参数未就绪或部分缺失只影响配置区；参数可用后恢复配置内容 |
| A09 | 飞控整体失联后旧电池数值不继续展示，主图标中性、配置禁用，FCS 显示和操作沿用后端失联门限 |
| A10 | 左侧 PMU 测量功率与右侧 FCS 请求功率独立，模拟 3000 W 回退时允许二者不同；QGC 不显示实际 CAN 电压和电流 |
| A11 | FCS 三种操作均需确认并标明目标；急停可抢占普通请求；车辆/会话变化使旧确认失效 |
| A12 | 参数校验及逐项提交沿用 Fact 控件，关闭无效编辑后清理校验状态，不向新车辆写入旧编辑 |
| A13 | 默认英文、中文翻译完整；单位和长文字布局正确；不恢复已删除的两处小字 |
| A14 | 桌面构建及 Android 相关 QML、资源和集成组件可编译；完整 Android 打包与设备显示需单独验证 |
| A15 | DCDC 输出电流始终与 PMU 电流和飞控内部 CAN 控制电流独立；设备断连或遥测过期显示占位，状态 13～15 不降低控制权限 |

## 8. 已知取舍

- 按单 PMU 系统处理，使用第一个电池条目，不增加节点选择或多电池汇总。
- 仅利用既有飞控失联标志处理整体断链，不额外承诺 PMU 数据新鲜度。
- 窗口不足以容纳固定双栏不属于正常工作场景，不提供窄屏布局或动态压缩。
- 四项配置沿用各自参数的范围校验和单项提交，不新增跨字段提交事务。
- FCS 遥测过期不显示红角标，应在详情中查看未知原因；这与用户确认的角标规则一致。
- 测试替身和离屏渲染不能代替真实参数写入、MAVLink/CAN 联调及真实设备显示验收。
- 本次只使用临时离屏工具验证新增 QML 行为，不增加永久可重复的 QML/CTest 目标。

## 9. 涉及文件

以下路径均相对 QGC 仓库根目录，记录当前电源入口合并功能的主要代码范围。

PowerSystemIndicator 前端只保留两个 QML 文件：

- `custom/src/UI/toolbar/PowerSystemIndicator.qml`
- `custom/src/QmlControls/PowerSystemIndicatorPage.qml`

主要集成和说明文件：

- `custom/src/FirmwarePlugin/CustomFirmwarePlugin.cc`
- `custom/custom.qrc`
- `custom/CMakeLists.txt`
- `custom/translations/custom.ts`
- `custom/translations/custom_zh_CN.ts`
- `custom/src/FuelCellController/README.md`

已删除的旧前端文件：

- `custom/src/QmlControls/BatteryIndicator.qml`
- `custom/src/UI/toolbar/FuelCellControllerIndicator.qml`
- `custom/src/QmlControls/PowerSystemBattery.qml`
- `custom/src/QmlControls/FuelCellControllerPage.qml`

PowerSystemIndicator 文档固定为三份：

- `custom/Docs/PowerSystemIndicator/DEVELOP.md`：设计、实现和开发日志；
- `custom/Docs/PowerSystemIndicator/README.md`：用户操作说明；
- `custom/Docs/PowerSystemIndicator/TEST.md`：测试计划和执行记录。

上游 `src/QmlControls/BatteryIndicator.qml` 继续保留；既有 FCS 后端、测试源码、CustomPlugin 和 `FuelCellController.svg` 不在本功能修改范围内。

## 10. 开发日志

### 2026-10-26：合并入口与文档重建

- 将自定义 BatteryIndicator 和 FuelCellControllerIndicator 合并为 PowerSystemIndicator。
- 拆分顶部入口、组合页和常驻电池模型的职责，复用既有 FuelCellControllerPage。
- 补充中文翻译、开发约定、台架测试规划和历史验证边界。
- 修复 `PowerSystemBattery.qml` 遗漏 `QGroundControl.FactSystem` 导入导致的入口加载失败。

对应构建、离屏加载及未执行项目记录见 [TEST.md](TEST.md) 的测试记录部分。

### 2026-11-03：移除窄屏布局

- 删除窗口宽度断点、窄屏标签页、标签选择状态和切车重置标签逻辑。
- 展开页固定为左侧电池、右侧 FCS 的双栏结构。
- 保留切车关闭抽屉、参数编辑器隔离、确认框会话校验和长文本换行。
- 未增加小窗口压缩或其他响应式降级；窗口不足以容纳双栏不属于正常工作场景。

本次实际构建、静态检查、后端测试及未执行项目见 [TEST.md](TEST.md) 的 2026-11-03 记录。

### 2026-11-03：整理文档结构

- 将开发约定和开发日志集中到 `DEVELOP.md`。
- 新建面向用户的 `README.md`，集中说明入口、状态、参数和 FCS 操作。

### 2026-11-03：合并前端组件文件

- 将常驻电池、参数和配色逻辑并入 `PowerSystemIndicator.qml`。
- 将电池详情、参数编辑、FCS 状态、控制及确认流程并入 `PowerSystemIndicatorPage.qml`。
- 删除仅由本功能引用的 `PowerSystemBattery.qml` 和 `FuelCellControllerPage.qml` 及其 QML 模块注册。
- 保留参数控制器重建、Fact 固定绑定、会话修订校验和确认框销毁等生命周期保护。

### 2026-11-03：整理前端内部结构

- 将电池相关属性、参数控制器和格式化函数收拢到工具栏文件内的 `batteryModel`，减少工具栏根对象与详情页之间的接口数量。
- 将详情页左右内容分别封装为文件内 `BatteryPane` 和 `FuelCellPane`，固定宽度约束集中在主双栏布局。
- 将四个配置 Fact 按界面顺序集中为 `configurationFacts`，配置编辑器加载时一次固定绑定。
- 用具名常量和统一发送函数代替散落的 FCS 指令数字；有限值检查统一使用 `Number.isFinite`。
- 未改变用户可见文本、参数名称、状态判断、按钮门限或后端调用。
- 将原 `VALIDATION.md` 的历史验证记录并入 `TEST.md`，与测试计划统一维护。
- 删除旧 `DEV.md` 和 `VALIDATION.md`，目录固定保留 `DEVELOP.md`、`README.md`、`TEST.md`。
- 本次只整理文档，不修改 PowerSystemIndicator 功能代码。

### 2026-11-04：适配 FCS 限流状态和 DCDC 输出电流

- FuelCellControllerManager 增加只读 `dcdcOutputCurrent`，仅在新鲜遥测且设备连接正常时返回按 0.1 A/位换算的数值。
- 状态 13 作为输出电流受限显示橙色角标和橙色文字，不改变控制门限；状态 14～15 保留原始码显示为未知状态。
- 设备断连、故障关机和急停仍为红色且优先于橙色；遥测过期和未知原始状态不显示角标。
- 同步更新后端单元测试、中英文翻译、用户说明和台架验收计划。

### 2026-11-04：移除重复的 FCS 母线电压和电流

- MAVLink C 库更新到 `878811a906c723ff6f92f1b7a08970873bef430d`，对应的 `FUEL_CELL_STATUS` 长度为 14、CRC_EXTRA 为 218。
- FuelCellControllerManager 删除 `busVoltage`、`busCurrent` 只读属性，详情页删除对应两行；`requestedPower` 保留在 Flight controller setpoints 分组。
- 顶部和左栏继续使用标准 `BATTERY_STATUS` 电压、电流及计算功率；DCDC 输出电流、请求功率和控制流程不变。
- 新旧 PX4/QGC 协议定义不兼容，必须配套部署。QGC 不再能直接观察 PX4 实际用于 CAN 下发的母线电压和电流。

### 2026-11-05：适配 FCS-4500 V1.4 精度

- MAVLink C 库更新到 `91e4271c94625a5d0c2d950fbff05e616a30b493`，对应 MAVLink XML 提交 `d3f6ed1b9a4d892698f26cd8859a112c94b31353`。
- `FUEL_CELL_STATUS` 的 SOC 和 DCDC 输出电流继续使用 `uint16_t`，分别按 0.01%/位和 0.01 A/位换算；消息长度仍为 14，CRC_EXTRA 仍为 218。
- FuelCellControllerManager 将 SOC 合法上限调整为原始值 10000，并将 SOC、DCDC 输出电流的比例统一改为 0.01；详情页均固定显示两位小数。
- 消息长度和 CRC 未随精度变化，运行时无法识别旧协议；本版本只支持 PX4 V1.4 与新 QGC 同步部署，不提供旧精度兼容分支。
- Android 构建不在本轮验收范围内。
