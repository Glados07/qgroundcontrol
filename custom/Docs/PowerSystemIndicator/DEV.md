# PowerSystemIndicator 开发约定

重建日期：2026-10-26。适用仓库：`/home/glados/app/qgroundcontrol`。

本文恢复此前已确认的合并电源指示器需求，并与当前实现核对。后续维护以本文件约定为准；检查记录及尚未验证的项目见 [VALIDATION.md](VALIDATION.md)，后端接口说明见 [README.md](README.md)。

## 1. 目标与范围

将顶部独立的 BatteryIndicator 和 FuelCellControllerIndicator 合并为一个 `PowerSystemIndicator`，减少工具栏占用，并在同一详情面板内查看 PMU 电池状态、低电量配置及 FCS 状态和控制。

按现有 QGC 做法完成最小必要变更，界面和集成修改限于 `custom/`。复用既有电池 Fact、参数编辑控件及 FuelCellController 后端，不修改 PX4、MAVLink 协议、通用工具栏框架或其他无关功能。

## 2. 已确认的交互选择

| 项目 | 已确认要求 |
| --- | --- |
| 电池配置区 | 默认完整展开，保留低电量飞行动作和三个电压阈值，共四项 |
| 窄屏布局 | 使用两个标签页切换电池和燃料电池内容 |
| FCS 异常提示位置 | 主图标上增加角标，详细原因在详情中查看 |
| 电池数据缺失 | 保留入口，无效项显示 `— V`、`— W`；FCS 区仍可查看，不以 FCS 数值代替电池数值 |
| 参数未就绪或缺失 | 仅配置区显示等待或不可用提示，不阻塞电池状态和 FCS 区 |
| 组件组织 | 分别维护电池内容与 FCS 内容，组合组件负责布局，避免复制逻辑 |
| 角标规则 | 仅设备断连、故障关机或急停状态显示红色角标，其他情况不显示 |
| 标签页记忆 | 首次显示电池页，同一车辆再次打开记住最近选择，切换车辆后重置 |
| 飞控整体失联 | 电池数值改为无效占位，主图标中性色，配置禁用；FCS 沿用现有失联处理 |

原计划拆分的电池模型、状态内容和故障保护配置三个文件，已按后续讨论合并为一个 `PowerSystemBattery.qml`。

## 3. 顶部入口

### 3.1 内容与数据来源

- 图形复用 `FuelCellController.svg`，右侧上下两行分别显示母线电压和功率，均为“数值 + 单位”。
- 电压来自当前活动车辆第一个电池条目的 voltage Fact，沿用 Fact 数值格式并显示 `V`。
- 功率由同一电池条目的电压乘电流得到，取整数并显示 `W`。
- 数值文本使用正常文本颜色，主图标按电池状态变色。
- 有活动飞控但尚无电池数据时仍显示入口，各无效项分别显示占位；有效零值不得当成缺失值。
- 没有活动车辆或仅有离线编辑车辆时不显示入口。

不新增电池数据来源、不做多电池汇总。左侧 PMU 测量值与右侧飞控下发给 FCS 的设定值各自保留原有来源。

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

### 3.3 FCS 红色角标

只在 FCS 遥测新鲜，且满足以下任一条件时显示红色角标：

- `connectionState == Disconnected`；
- 设备数据有效，且 FCS 状态为 9、10（故障关机中、故障关机完成）或 11、12（急停中、急停完成）。

等待首次状态、从未连接、遥测过期、正常运行或普通停机完成均不显示角标，不增加黄色或灰色角标。遥测未知与 PX4 已判断的设备断连是不同状态；详情仍须显示相应原因。

## 4. 展开面板

### 4.1 宽屏双栏

宽度足够时，左侧电池、右侧燃料电池，两栏顶部对齐，中间使用分隔线。

左栏直接显示原电池详情和配置，无需再次点击展开：

| 分组 | 内容 |
| --- | --- |
| Power Status | 电源状态、实测电压、功率、电流、已消耗电量 |
| Low Power Failsafe | `COM_LOW_BAT_ACT`、`UAVCAN_POW_LOW`、`UAVCAN_POW_CRITI`、`UAVCAN_POW_EMERG` |

配置继续使用标准 Fact 控件，保留原有校验、逐项提交及参数本身的重启要求。不新增统一保存按钮、批量事务或额外解锁条件。

右栏复用现有 `FuelCellControllerPage.qml`，保留：

- 设备连接状态、FCS 实际状态、锂电池 SOC；
- 飞控当前下发的母线电压、电流和请求功率，只读；
- 当前 FCS 指令、开机、关机、FCS 急停按钮及确认对话框；
- 请求结果、不可操作原因及原有后端控制门限。

标题源文本使用 `Fuel Cell Controller`，中文翻译为“燃料电池控制器”。不恢复页面标题下的“飞控 #1”小字或急停按钮下的说明小字；操作确认框仍须明确目标车辆。

### 4.2 窄屏标签页

- 窄屏使用 `Power Status` 与 `Fuel Cell` 两个标签页，避免强行并排或水平滚动。
- 第一次打开显示电池页；关闭再打开时，同一活动车辆保留选择；切换车辆后恢复电池页。
- 选择状态保存在顶部指示器，生命周期覆盖详情面板的打开与关闭，不要求应用重启后保留。
- 在宽屏和窄屏之间切换时，不重建两个内容区域，不丢弃正在编辑的控件或重复发送命令。
- 使用标准详情抽屉的纵向滚动；长状态、结果和确认文字应换行。

布局阈值按字体尺寸计算。当前实现令 `f = defaultFontPixelWidth`，单栏目标宽度为 `44f`，可用宽度为 `max(0, mainWindow.width - 4f)`；可用宽度不小于 `2 × 44f + 2f + 1` 时使用双栏。该计算优先适配内容和字体，不按桌面/Android 平台名称硬编码。

低电量动作下拉框的当前选项过长时允许省略显示，展开后沿用原有完整选项。不能让长文字无限撑宽整个面板。

## 5. 边界与状态处理

### 5.1 数据独立性

- 电池测量和 FCS 遥测分别判断可用性，任何一方暂不可用均不阻塞另一方显示。
- 电压、电流、功率等按字段检查有限值；部分字段无效时仅相应项使用占位。
- FCS 三个母线数值为飞控实际用于 CAN 下发的值，不能拿来替代 PMU 测量值，也不开放编辑。
- PMU 失效后飞控可能使用保持电压和固定 3000 W 回退，因此左右功率不同可以是正常现象。
- 不新增 PMU 报文级超时判断、回退标志或 MAVLink 字段。飞控仍在线但 PMU 停止更新时，QGC 电池数据可能保持旧值，此限制见验证记录。

### 5.2 参数就绪和飞控失联

- 参数尚未加载完成时，配置区显示等待提示；任一必要参数缺失时，配置区显示不可用提示。
- 状态详情和 FCS 区不等待电池参数。
- 飞控整体失联时，电池测量显示占位，主图标中性色，已显示的配置控件禁用。
- FCS 遥测与操作门限继续由 `FuelCellControllerManager` 处理，不新增界面层重试和自动复位。

### 5.3 车辆切换与编辑生命周期

- 切换活动车辆时关闭当前电源详情面板，重置标签页并重新绑定电池数据。
- `FactPanelController` 构造时捕获活动车辆，必须随车辆重新创建；取参数前校验控制器属于当前车辆。
- 参数编辑器固定绑定打开时的 Facts，旧车辆尚未提交的编辑不得被重新指向新车辆。
- 编辑器销毁时释放其全局校验错误状态，避免关闭面板后留下输入校验锁定。
- 沿用 FCS 确认框的 `sessionRevision` 校验。会话失效或面板销毁时关闭确认框，旧操作不得发给切换后的车辆。

### 5.4 FCS 控制

FCS 开机、关机、急停均保留现有确认机制。普通请求等待确认时，急停仍可按后端门限抢占。

“FCS 急停”仅发送燃料电池协议急停指令，不调用飞控锁定、飞行终止或电机停转接口。打开页面、切换标签、调整窗口、恢复通信均不自动发送指令。重试、确认及超时仍完全由既有后端处理。

## 6. 实现组织与限制

| 组件 | 职责 |
| --- | --- |
| `PowerSystemIndicator.qml` | 顶部图标、两行数值、角标、面板入口和标签页选择状态 |
| `PowerSystemIndicatorPage.qml` | 双栏/标签页布局、宽度适配和车辆切换时关闭当前面板 |
| `PowerSystemBattery.qml` | 常驻电池数据和配色逻辑，按需创建的电池详情与配置内容 |
| `FuelCellControllerPage.qml` | 复用 FCS 详情、控制及确认流程，仅补充组合布局需要的宽度和文字适配 |

电池常驻模型不依赖详情面板是否打开，避免关闭面板后顶部失去数据或重复维护状态。三个原计划的电池文件合并后，以函数和按需加载的 Component 保持内部职责清楚，不再新增独立模型或后端。

通过 `CustomFirmwarePlugin` 在原 BatteryIndicator 位置替换为合并入口，保留独立 FuelStatusIndicator 的原有顺序。同步 QRC、QML 模块和翻译注册，移除不再使用的自定义电池覆盖及独立 FCS 顶部入口。

不改通用 `ToolIndicatorPage`、`MainWindow`、`FlyViewToolBar`、上游电池 FactGroup 或上游 BatteryIndicator。不改 FuelCellControllerManager、CustomPlugin、MAVLink 消息和 SVG 图形，不引入新的运行依赖或永久测试依赖。

## 7. 验收标准

下列为功能验收要求，实际已执行项目和证据范围以 [VALIDATION.md](VALIDATION.md) 为准。

| 编号 | 场景与预期 |
| --- | --- |
| A01 | 活动车辆只有一个合并电源入口，FCS 图形右侧显示电池电压与功率；原电池和 FCS 独立入口不重复出现 |
| A02 | 电压阈值和等于阈值的边界沿用原比较逻辑；FAILED、UNHEALTHY、CHARGING 的优先级及颜色保持一致 |
| A03 | 仅新鲜遥测中的断连及有效 FCS 状态 9～12 出现红角标；从未连接、等待、未知和过期不显示 |
| A04 | 宽屏一次点击直接看到左侧五项电池状态、四项配置和右侧完整 FCS 内容 |
| A05 | 窄屏显示两个标签页，首次进入电池页，同一车辆重新打开保留选择，切换车辆后重置 |
| A06 | 窗口大小和字体缩放变化后可读、可纵向滚动；切换布局不重建编辑器，不产生额外 FCS 指令 |
| A07 | 无电池、部分 NaN/无穷值或无 FCS 状态时，按字段显示占位，各区域不互相阻塞；有效零值仍正常显示 |
| A08 | 参数未就绪或部分缺失只影响配置区；参数可用后恢复配置内容 |
| A09 | 飞控整体失联后旧电池数值不继续展示，主图标中性、配置禁用，FCS 显示和操作沿用后端失联门限 |
| A10 | 左侧 PMU 测量与右侧 FCS 下发值独立，模拟 3000 W 回退时允许二者不同 |
| A11 | FCS 三种操作均需确认并标明目标；急停可抢占普通请求；车辆/会话变化使旧确认失效 |
| A12 | 参数校验及逐项提交沿用 Fact 控件，关闭无效编辑后清理校验状态，不向新车辆写入旧编辑 |
| A13 | 默认英文、中文翻译完整；单位和长文字布局正确；不恢复已删除的两处小字 |
| A14 | 桌面构建及 Android 相关 QML、资源和集成组件可编译；完整 Android 打包与设备显示需单独验证 |

## 8. 已知取舍

- 按单 PMU 系统处理，使用第一个电池条目，不增加节点选择或多电池汇总。
- 仅利用既有飞控失联标志处理整体断链，不额外承诺 PMU 数据新鲜度。
- 标签选择仅在当前车辆对象和应用运行期间记忆。
- 四项配置沿用各自参数的范围校验和单项提交，不新增跨字段提交事务。
- FCS 遥测过期不显示红角标，应在详情中查看未知原因；这与用户确认的角标规则一致。
- 测试替身和离屏渲染不能代替真实参数写入、MAVLink/CAN 联调及真实设备显示验收。

## 9. 涉及文件

以下路径均相对 QGC 仓库根目录，记录本次电源入口合并功能的代码范围。

新增三个 QML 文件：

- `custom/src/UI/toolbar/PowerSystemIndicator.qml`
- `custom/src/QmlControls/PowerSystemIndicatorPage.qml`
- `custom/src/QmlControls/PowerSystemBattery.qml`

修改七个文件：

- `custom/src/FirmwarePlugin/CustomFirmwarePlugin.cc`
- `custom/src/QmlControls/FuelCellControllerPage.qml`
- `custom/custom.qrc`
- `custom/CMakeLists.txt`
- `custom/translations/custom.ts`
- `custom/translations/custom_zh_CN.ts`
- `custom/src/FuelCellController/README.md`

删除两个自定义入口文件：

- `custom/src/QmlControls/BatteryIndicator.qml`
- `custom/src/UI/toolbar/FuelCellControllerIndicator.qml`

本次重建并永久保存的文档：

- `custom/src/FuelCellController/DEV.md`
- `custom/src/FuelCellController/VALIDATION.md`

上游 `src/QmlControls/BatteryIndicator.qml` 继续保留；既有 FCS 后端、测试源码、CustomPlugin 和 `FuelCellController.svg` 不在本功能修改范围内。
