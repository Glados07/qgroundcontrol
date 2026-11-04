# PowerSystemIndicator 测试计划与记录

更新时间：2026-11-04。适用仓库：`/home/glados/app/qgroundcontrol`。

本文面向测试人员，集中保存 PowerSystemIndicator 的测试准备、测试用例、执行结果和证据边界。用户操作说明见 [README.md](README.md)，开发设计与变更记录见 [DEVELOP.md](DEVELOP.md)，FuelCellController 后端接口见 [后端 README](../../src/FuelCellController/README.md)。

## 文档约定

- 第一部分是后续台架和平台测试计划；结果栏为“待测”的条目不代表已经通过。
- 第二部分是历史或本次实际执行记录。只有明确写出命令、环境和结果的项目才可视为已执行。
- 历史日志或临时测试程序已经丢失时，保留原记录但明确其不可复现边界。
- 软件替身、离屏渲染、组件构建和真实设备联调分别记录，不能互相替代。

## 第一部分：测试计划

对象：CUAV X25-Evo、CAN1 CUAV PMU、CAN2 FCS，以及 QGC PowerSystemIndicator。本轮不安排飞行测试。

### 1. 执行顺序与准备

建议顺序：无外设 → 仅 PMU → 仅 CAN2 状态节点 → PMU + FCS 完整链路 → 通信异常与恢复 → 注入专项 → Android 和持续运行。

- 使用拆桨、保持未解锁的台架，真实 FCS 启停和急停在设备允许的台架工况下进行；故障状态优先通过仿真节点注入，不人为制造真实设备故障。
- 中断 PMU/CAN/FCS 通信时保持飞控独立供电，避免将飞控掉电误判为某条总线失效。需要改接线路时先按设备要求断电。
- CAN1 保留 DroneCAN，CAN2 使用私有协议；确认接线、共地、终端和各设备供电符合实际台架设计。
- CAN2 当前默认 250 kbit/s，经典 CAN 扩展帧，DLC 为 8。先核对 FCS 实际配置，不能以 PMU 所在 CAN1 的波特率代替 CAN2 配置。
- 准备 CAN 分析仪；专项项目另需 CAN 仿真节点或可筛选消息的 MAVLink 中继。没有工具的项目记为“阻塞”，不以正常设备测试代替。
- 参数编辑前保存原值，测试结束恢复。初始无 PMU 的入口测试可保持 Power Config 的 Battery Source 为 Disabled，不为显示图标而修改它。

#### 测试环境记录

| 项目 | 实测填写 |
| --- | --- |
| 日期、人员、记录目录 | 待填写 |
| PX4 commit、固件构建目标、是否含未提交修改 | 待填写 |
| QGC commit、未提交补丁、实际运行程序/APK 路径 | 待填写 |
| PX4 MAVLink XML / QGC C 库版本 | 待填写 |
| 飞控系统/组件 ID、连接方式、MAVLink 版本 | 待填写 |
| PMU 型号、固件、节点 ID、CAN1 配置 | 待填写；节点 ID 只用于诊断记录，不新增驱动选择参数 |
| FCS 型号、固件、CAN2 波特率及实测状态周期 | 待填写 |
| 原始四项低电量参数、Battery Source 配置 | 待填写 |
| QGC 平台、分辨率、字体比例、语言 | 待填写 |

### 2. 观测点和判定基准

不要只看界面。每次数据或控制测试至少核对“飞控内部值 → 总线/消息 → QGC 显示”中的两个相邻环节；控制测试应同时核对 CAN 帧与设备实际反馈。

在 MAVLink Console 按需执行以下只读命令；无话题时记录未发布，不将其当成数值 0：

```sh
ver all
fuel_cell_controller status
uavcan status
param show UAVCAN_ENABLE
param show UAVCAN_SUB_BAT
param show COM_LOW_BAT_ACT
param show UAVCAN_POW_LOW
param show UAVCAN_POW_CRITI
param show UAVCAN_POW_EMERG
listener battery_status -i 0 -n 5
listener fuel_cell_status -n 5
listener fuel_cell_control -n 5
uorb top
```

`listener` 可按 Ctrl+C 退出；单次输出和 QGC 界面刷新率不能用来判定 PMU 真实发布周期。测周期时结合 CAN 抓包、`uorb top` 和采样时间戳，记录采样窗口及是否漏采。

QGC 使用 MAVLink Inspector 检查 `BATTERY_STATUS`、`FUEL_CELL_STATUS`，配合通信日志/中继记录 `FUEL_CELL_CONTROL` 的请求编号、目标、指令和发送次数。保留启动日志，确认没有 `PowerSystemBattery unavailable` 或 `FactPanelController is not a type`。

| 项目 | 当前实现基准 |
| --- | --- |
| CAN2 控制帧 | 扩展 ID `0x19AFC002`，约每 100 ms 一帧，Life 每次发送推进并按 0～255 回绕 |
| CAN2 控制负载 | Byte 0～1 电压、2～3 电流、4～5 功率，大端序；Byte 6 低 4 位为指令，Byte 7 为 Life |
| CAN2 状态帧 | 扩展 ID `0x19AFC001`；Byte 0 低 4 位为 FCS 状态，Byte 1～2 为大端 SOC，Byte 3～4 为大端 DCDC 输出电流，Byte 7 为 Life |
| CAN 字段比例 | 电压 0.1 V/位，电流及 DCDC 输出电流 0.1 A/位，功率 1 W/位，SOC 0.1%/位 |
| FCS CAN 状态有效期 | 自最后接受的新鲜状态起约 500 ms；持续重复 Life 不刷新有效期 |
| PX4 状态发布 | 模块正常运行时约每 200 ms 发布，即使没有连接 FCS；连接变化和请求处理可额外触发发布 |
| QGC FCS 遥测有效期 | `FUEL_CELL_STATUS` 无更新满 1 秒后无效；不等于 PX4 已判断 CAN2 断连 |
| PMU 有效期 | 自最后有效 PMU 采样起 10 秒；收到 `connected=false` 时可提前回退 |
| 请求重试 | 无匹配确认时约每 500 ms 重试，同一请求最多发送 3 次，总等待上限约 2 秒；失联可提前结束 |

时间判定使用相关消息的时间戳。PX4 调度、状态发布、传输和 QGC 轮询会增加可见延迟，不将界面变化时间要求为恰好 500/1000/10000 ms。

驱动 `TX queued` 表示提交发送队列，不代表 FCS 已收到或执行。CAN2 没有其他正常节点时可能缺少 ACK 并产生硬件错误；无外设阶段不要求空总线收发统计完全无错误。

### 3. 首轮必测：入口和无设备状态

前提：飞控重新上电，暂不接 PMU/FCS，确认没有其他来源发布有效电池数据；使用修复导入后重新构建的 QGC。

| 编号 | 操作 | 预期结果 | 结果 |
| --- | --- | --- | --- |
| B01 | 连接活动飞控，等待初始化遮罩结束，观察顶部并打开入口 | 显示唯一 PowerSystemIndicator，图形旁为 `— V` / `— W`，主图标中性色；无 PMU、Battery Source Disabled 不隐藏入口 | 待测 |
| B02 | 查看左右详情及 PX4 `status` | 电池无数据时显示占位；驱动已启动且 MAVLink 状态正常到达时，右侧为 Never connected，SOC/设备状态/DCDC 电流无效，三个下发值为 0；若没有收到状态则应显示 Waiting for status，不能伪装成已连接 | 待测 |
| B03 | 查看四项配置和三个 FCS 按钮 | 参数齐全、链路正常时四项配置可编辑，不依赖电池采样；FCS 未连接时三个按钮均禁用，包括急停 | 待测 |
| B04 | 关闭/打开面板，调整正常工作窗口尺寸；移除活动车辆 | 始终显示固定左右栏；上述界面动作不产生新的 FCS 控制请求；活动车辆移除后入口消失 | 待测 |

若 B01 失败，先查实际运行的 QGC 版本、工具栏资源和 QML 启动错误，不通过启用电源参数掩盖组件加载问题。

### 4. 首轮必测：仅接 PMU，验证测量和 CAN1/CAN2 分工

前提：CAN1 接 PMU，CAN2 接分析仪/可应答节点，FCS 可暂不接。按实际 PMU 接入要求配置 DroneCAN 和电池桥，核实 `UAVCAN_SUB_BAT`，不盲目改动全部 Battery Source。

| 编号 | 操作 | 预期结果 | 结果 |
| --- | --- | --- | --- |
| P01 | 接通 PMU，核对 `battery_status` 实例 0、驱动 PMU 模式及 CAN2 抓包 | 实例 0 来源符合单 PMU 假设，connected 为真，时间戳持续前进，电压/电流有效，驱动由 waiting 转 normal；CAN1 继续 DroneCAN，CAN2 周期发送私有帧 | 待测 |
| P02 | 在至少三个稳定工况记录同一时段电压、电流和功率，包含允许条件下的零电流 | QGC 左侧/顶部来自电池数据，功率为其电压 × 电流；右侧和 CAN2 来自驱动控制值。零电流/零功率有效，不能显示占位 | 待测 |
| P03 | 比较原始采样、CAN2 负载、`fuel_cell_status` 和右侧单位 | 驱动按原始同一采样计算功率，再分别四舍五入为 0.1 V、0.1 A、1 W；多字节序及 QGC 比例正确，不出现 10/100 倍错误 | 待测 |
| P04 | 同时观察两条总线，改变 PMU 工况 | PMU 更新引起 CAN2 母线三项变化；CAN2 指令及 TX Life 逻辑不受破坏；仅有 PMU 不使 FCS 连接状态变为 Connected | 待测 |
| P05 | 空载及代表性负载下分别连续采集至少 3 分钟 | 记录 PMU CAN 发布和 uORB 更新的典型/最大间隔、采样数、时间戳及丢帧迹象，判断正常间隔是否远小于 10 秒；形成是否调整临时超时值的依据，不在测试中自动改值 | 待测 |

数值比对应选稳定工况，考虑 CAN/MAVLink 不同采样时刻、显示精度和消息量化，不能要求所有异步界面数值逐帧完全一致。顶部功率为整数，右侧 CAN 设定电压/电流为一位小数；功率来自原始采样，不必严格等于两个已量化字段的乘积。

控制帧核对示例：若驱动当前值为 48.0 V、20.0 A、960 W、指令 1、Life 7，则负载应为 `01 E0 00 C8 03 C0 01 07`。该示例用于解码核对，不要求为测试强行将真实系统设置为此工况。

### 5. 首轮必测：FCS 状态、显示与完整控制

先用 CAN2 仿真状态节点或实际 FCS 验证状态链路。真实启停阶段接入有效 PMU 和设备所需电源/负载；不把 PMU 未就绪时的 0 V/0 A/0 W 作为真实 FCS 必须成功开机的条件。

| 编号 | 操作 | 预期结果 | 结果 |
| --- | --- | --- | --- |
| F01 | 首次收到有效 CAN2 状态帧，对比抓包、驱动和 QGC | 驱动由 never connected 转 connected，Device link 为 OK；MAVLink connection_state 为 1，QGC 显示 Connected，SOC/实际状态/DCDC 电流正确；确认实际状态周期满足 500 ms 有效期 | 待测 |
| F02 | 连续观察状态、SOC 及 DCDC 电流；仿真器补测 SOC 0%/100%、DCDC 0.0 A/6553.5 A 和中间值 | 字节序、0.1% 与 0.1 A 比例正确；零值是有效数值而非占位。DCDC 数值不替代 PMU 电流或飞控 Bus current | 待测 |
| F03 | 正常状态、9/10、11/12、13、14/15 分别观察顶部和详情 | 故障关机/急停显示红角标；13 显示“输出电流受限”、橙色角标和橙色文字；14/15 显示带原始码的未知状态且无角标。三者均不降低控制权限，主图标仍由电池状态决定 | 待测 |
| F04 | 在允许控制时点击开机/关机/急停，然后取消确认 | 对话框标明指令和当前车辆；取消不发送新的控制请求、不改 PX4 保存值 | 待测 |
| F05 | 确认 FCS On，记录整个链路 | 控制消息目标和编号正确；PX4 保存 command=1，CAN2 周期发送 1，母线值仍由 PMU 决定；QGC 区分“飞控已接受”与设备实际启动/运行进度 | 待测 |
| F06 | 确认 FCS Off，记录整个链路 | PX4 保存 command=0，CAN2 持续发送 0；设备按协议返回停机状态，QGC 显示实际进度，不以确认消息直接认定停机完成 | 待测 |
| F07 | 在允许的台架状态确认 FCS Emergency Stop | command=2 正确下发，实际反馈可见急停进度/完成；不调用飞控锁定、飞行终止或电机急停命令 | 待测 |
| F08 | 在可控延迟确认条件下发普通请求，再确认急停 | 普通请求等待时急停仍可用；急停使用新编号，旧请求停止重试，迟到旧确认不结束急停请求，不重复抢占同一待确认急停 | 待测 |
| F09 | 保持指定指令，打开/关闭详情、调整窗口 | 不产生额外 MAVLink 控制请求；CAN2 继续周期发送已保存指令。母线值无 QGC 编辑入口 | 待测 |
| F10 | 在安全台架条件允许时使真实 FCS 进入限流，并与注入结果对照 | 真实设备状态和 DCDC 电流与 CAN/PX4/QGC 一致；不得为测试强行制造不安全或影响设备寿命的工况，条件不具备时记录“阻塞” | 待测 |

若真实设备正常状态周期超过 500 ms，应记录驱动有效期与设备周期不匹配，不直接归因于接线问题。真实状态可能在两次遥测之间跨过短暂阶段，不要求 QGC 必须捕获每一个瞬态码。

### 6. 首轮必测：断连、回退和恢复

| 编号 | 操作 | 预期结果 | 结果 |
| --- | --- | --- | --- |
| R01 | PMU 正常采样后停止其数据；分别覆盖发布 connected=false 和静默无新采样 | 前者按断连消息回退，后者自最后有效采样满 10 秒回退；保留最后有效编码电压，功率 3000 W，电流由该电压计算；指令不变。只能实现一种时，另一种保留待测 | 待测 |
| R02 | 在 R01 中核对 PX4、CAN2、QGC 两侧，再恢复 PMU | PX4 status 显示 fallback，CAN2/右侧反映回退值；左侧仍独立使用 BATTERY_STATUS，可能占位或保持缓存，不被右侧覆盖。收到新有效采样后恢复 normal 和正常母线值，不自动改变启停指令 | 待测 |
| R03 | 在曾连接 FCS 后停止 CAN2 状态发送，保持 PMU/MAVLink 正常 | 超过 500 ms 无接受的新状态后，ever connected 保持，Device link 变 error occured，connection_state=2；QGC 显示 Disconnected、红角标，SOC/实际状态/DCDC 电流无效，控制禁用；电池区继续正常 | 待测 |
| R04 | 恢复 CAN2，先重复最后 Life，再发送已变化的有效 Life | 重复 Life 不恢复连接；已变化的有效状态恢复 OK/Connected 和有效设备值；不自动产生新的 QGC 控制请求。PX4 原保存指令仍按 CAN 周期发送，不把“无新请求”理解成停止 CAN 发送 | 待测 |
| R05 | 仅屏蔽下行 FUEL_CELL_STATUS，保持心跳及电池消息 | 满 1 秒后 QGC FCS 状态为 Unknown/telemetry expired，控制禁用，角标不显示；电池状态仍有效。此时不能显示为已确认 CAN2 断连；恢复状态消息后重新有效 | 待测 |
| R06 | 中断整条 QGC—飞控通信，并恢复 | 若活动车辆保留，电池旧值变占位、图标中性、配置禁用，FCS 无效；若车辆被移除，入口消失。待确认请求结果未知；恢复后不重放请求，仅恢复心跳不能使缓存 FCS 状态重新新鲜 | 待测 |
| R07 | 分别在“控制请求待确认”和“确认框打开未提交”时断链/切车 | 请求重试停止，旧确认框失效，不向恢复后或新车辆发送旧操作；仅已提交的请求可以显示结果未知，未提交确认不得变成已发送 | 待测 |
| R08 | 完整记录 QGC 退出/重新连接、飞控重启的区别 | QGC 退出或重连不自动关机、不清零飞控指令；飞控模块重新启动时指令初始化为 0，首次连接标志重新建立。记录设备真实状态，不将驱动重启等同于设备动作已完成 | 待测 |

回退示例：最后有效编码电压为 48.0 V 时，下发 62.5 A、3000 W。CAN2 设备断连、PMU 回退、QGC 遥测过期分别判定，不互相代替。

### 7. 界面与参数实测

| 编号 | 操作 | 预期结果 | 结果 |
| --- | --- | --- | --- |
| U01 | 在正常工作窗口下分别用中英文和目标字体设置打开详情 | 始终左电池右 FCS，中间有分隔线；长状态/结果在栏内换行并可纵向滚动；没有两处已删除的小字，顶部入口不重复 | 待测 |
| U02 | 打开详情后关闭重开，再切换车辆 | 每次均显示完整双栏；换车关闭当前面板，旧参数编辑器不复用于新车辆；界面动作不发送指令。无第二车辆时换车分支记为阻塞 | 待测 |
| U03 | 读取四项参数，逐项修改后回读，结束恢复原值 | 显示/写入对应真实飞控参数；沿用标准范围校验、逐项提交及重启提示，FCS 三个母线值和 DCDC 电流保持只读 | 待测 |
| U04 | 制造非法文本编辑后关闭面板并重开；有条件时编辑期间切车 | 错误输入不写入，关闭后不残留全局校验锁定；旧编辑不写入新车。不要将“顺序不合理但各自仍在范围内”误当成标准 Fact 必然拒绝 | 待测 |
| U05 | 在参数下载过程中打开详情；用匹配的测试固件/环境补测缺少某一参数 | 仅配置区等待或不可用；状态和 FCS 不被阻塞。参数恢复后重新可用，失联时配置禁用 | 待测 |
| U06 | 台架可控电压或仿真数据跨越三个阈值，并核对等于阈值边界 | 严格 `<` 比较；颜色绿/黄/橙/红与 DEVELOP.md 一致，FCS 角标不覆盖主图标配色；结束恢复阈值，不为测试降低真实设备到不允许的电压 | 待测 |

### 8. 需要注入工具的专项

这些用例验证真实 PX4/消息处理链路，但输入可由仿真器提供，应在记录中写明“实机 + 注入”，不声称是实际 FCS 自然产生。

| 编号 | 操作 | 预期结果 | 结果 |
| --- | --- | --- | --- |
| E01 | CAN Life 按 254、255、0、1 发送；重复 Life；在有效期内前跳/倒退 | 回绕正常；重复计入 duplicates，不更新状态或有效期；前跳 1～127 接受并按差值计 missed，差值 128～255 计 out of order 且拒绝；分别重建序列基线后测各分支 | 待测 |
| E02 | 持续相同 Life 超过有效期，再改变 Life；另测设备重启后的 Life 重置 | 相同 Life 不能保持/恢复连接；超时后任意已变化 Life 可建立新基线，未超时的大倒退不应直接接受 | 待测 |
| E03 | 注入错误 ID/标准帧、错误 DLC、状态 13～16、SOC 超过 1000 和 DCDC 原始值 0/65535 | 状态 13 和未知状态 14～15 均为有效遥测，16 被 QGC 拒绝；DCDC 全部 uint16 范围均有效；SOC 1000 合法而超范围值拒绝。无效消息不改变业务状态或刷新有效期 | 待测 |
| E04 | 在受控电池发布源中给出无效值、负电流、重复/倒退时间戳，再给新有效采样 | 无效采样不覆盖母线值或延长有效期；超时回退，重复/倒退采样不解除回退；新有效采样恢复。若不能隔离 PMU 发布源，不向真实总线混发来伪造该测试 | 待测 |
| E05 | 保持新鲜 FUEL_CELL_STATUS，但使确认编号暂不匹配当前请求 | 约 500 ms 间隔、同编号最多 3 次，约 2 秒后结果未知；不清零 PX4 指令，不等同于未执行 | 待测 |
| E06 | 同一来源最近请求同编号同指令重发，再同编号换指令；另测非法指令/错误目标 | 同指令返回原结果、不重新应用；冲突指令拒绝；非法指令不支持；目标不匹配被忽略，不能用未收到确认当作“已拒绝” | 待测 |
| E07 | CAN 断连时直接通过工具发送新控制请求，再恢复 CAN 后重试同请求 | PX4 暂时拒绝；该最近请求的重试返回原拒绝，不因恢复连接而自动生效。此项绕过 UI 禁用，仅在受控台架执行 | 待测 |
| E08 | 注入其他系统/组件的状态或迟到旧确认；测试仅 MAVLink 1 主链路、日志回放/离线编辑 | 其他来源和旧确认不完成当前请求；不符合控制条件时禁止发起请求，ID 61000/61001 需要 MAVLink 2。无法真实构造的场景保留待测 | 待测 |

E05 不能简单屏蔽全部状态消息：那会先触发 1 秒遥测超时，无法验证完整的 2 秒请求等待。应继续传送真实当前遥测，只控制确认关联字段/时机；另一分支单独验证遥测失效会提前终止请求。

### 9. 平台和持续运行

| 编号 | 操作 | 预期结果 | 结果 |
| --- | --- | --- | --- |
| L01 | 实际目标桌面系统完成 B01、P02、F05/F06 和 R03～R06，检查 GPU 图标和弹窗 | 实际程序显示和操作符合预期；软件离屏检查不能替代此项 | 待测 |
| L02 | 若需 Android，重新构建/打包/安装 APK，重复无设备入口、固定双栏显示、状态和触控确认测试 | 正式 APK 中组件/资源可加载，双栏、字体和触控可用；记录安装版本，不能用桌面通过代替 | 待测 |
| L03 | 持续连接运行至少 30 分钟，反复开关页面及改变稳定工况 | 无界面冻结/累积 QML 错误、无异常请求、无正常工况下反复断连；记录 CAN/MAVLink 频率及错误计数的起止值 | 待测 |
| L04 | 至少连续观察 256 个有效 CAN 状态/控制周期，并重复至少 3 轮通信中断恢复 | 覆盖 TX/RX Life 回绕，恢复一致，未发生车辆串写或确认结果串号；每轮保留关联日志 | 待测 |

### 10. 结果记录与首轮完成条件

各表“结果”使用：待测 / 通过 / 失败 / 阻塞 / 不适用。失败、阻塞和不适用均写明原因；不要将“设备当前无法产生该状态”填写为通过。每条记录至少包含：

```text
编号：
执行时间、人员、台架/版本记录：
输入条件和实际操作：
预期结果：
实际结果、关键时间间隔/数值：
结论：
证据：截图/视频、QGC 日志、Console 输出、CAN/MAVLink 抓包文件及时间段
问题编号、复测结果：
```

首轮建议完成 B01～B04、P01～P05、F01～F09、R01～R08、U01～U06 及目标平台正常显示；其中需要工具或第二车辆的分支可以明确记录阻塞，不能视为全部验收完成。随后补齐专项、Android（如适用）和持续运行。

结束时核对并恢复参数、移除注入/中继设置，按设备流程结束 FCS 台架运行。将实际执行结果及未完成项写入本文件的测试记录部分，保留条目编号以便复测追踪。

## 第二部分：测试记录

**证据说明：** 原文档及 `/tmp/power-system-indicator/` 下的临时测试、截图和日志现已丢失。下文“原实现阶段记录”来自此前开发记录，本次未重新执行对应构建和测试，不能作为本次重新运行通过的证明。桌面构建产物仍存在，也不代表已经基于本次状态重新构建。

### 1. 实现范围

合并功能新增三个 QML 文件、修改七个集成/界面/翻译/说明文件，删除两个自定义旧入口，完整清单见 [DEVELOP.md](DEVELOP.md) 第 9 节。

实现范围为 `custom/`，保留上游 BatteryIndicator、通用抽屉及工具栏框架、既有 FuelCellControllerManager 和协议。原文档重建当时只整理文档，未修改功能代码或创建 Git 提交；相关记录现已并入本文件。

### 2. 原实现阶段记录：桌面构建

记录结果：退出码 0，桌面应用和现有后端测试目标构建完成。

```sh
/home/glados/Qt/Tools/CMake/bin/cmake --build \
  /home/glados/app/qgroundcontrol/build/Desktop_Qt_6_8_3-Debug \
  --target Custom-QGroundControl FuelCellControllerManagerTest --parallel 4
```

桌面产物路径：

```text
build/Desktop_Qt_6_8_3-Debug/Debug/Custom-QGroundControl
build/Desktop_Qt_6_8_3-Debug/Debug/FuelCellControllerManagerTest
```

原日志位于 `/tmp/power-system-indicator/implementation/desktop-final.log`，现已丢失。原记录未发现此次修改新增的编译警告；本次未重新核查完整构建输出。

### 3. 原实现阶段记录：Android 相关组件编译

记录结果：退出码 0，ARM64 自定义模块、修改的工具栏集成 C++ 对象及自定义资源对象编译完成。

```sh
/home/glados/Qt/Tools/CMake/bin/cmake --build \
  /home/glados/app/qgroundcontrol/build/Qt_6_8_3_for_Android_arm64_v8a-Debug \
  --target CustomModule \
  CMakeFiles/Custom-QGroundControl.dir/custom/src/FirmwarePlugin/CustomFirmwarePlugin.cc.o \
  CMakeFiles/Custom-QGroundControl.dir/Custom-QGroundControl_autogen/ECAI6Z3TWJ/qrc_custom.cpp.o \
  --parallel 4
```

这只验证了所列目标，**未完成整个 Android 应用的链接、APK 打包、安装或真机交互**。对象路径包含当前构建配置生成的目录名，重新配置后可能需要按实际目标调整。

原日志位于 `/tmp/power-system-indicator/implementation/android-components.log` 和 `android-final.log`，现已丢失。原构建报告过未修改的上游韩文、土耳其文翻译目录重复条目警告；未将这些无关条目纳入修改范围。

### 4. 原实现阶段记录：离屏 QML 测试

记录结果：**36 passed、0 failed、0 skipped**，最终记录未出现 QWARN、ReferenceError 或布局递归警告。统计包括数据表分支及初始化/清理项，不表示 36 项真实设备联调。

原运行方式：

```sh
QT_QPA_PLATFORM=offscreen QT_QUICK_BACKEND=software QT_QUICK_CONTROLS_STYLE=Basic \
  /tmp/power-system-indicator/check/build/PowerSystemCheck \
  -input /tmp/power-system-indicator/check/tst_power.qml
```

运行环境为 Qt 6.8.3、离屏软件渲染和 Basic 控件样式。临时测试加载实际生产 QML，并使用现有 QGC 标签、按钮、抽屉、确认对话框、Fact 编辑器等组件。

车辆、电池数据、Fact 后端、FactPanelController、FCS 管理器、调色板和字体环境采用替身；其中 Fact 控件的校验/提交调用由替身接收。因此不能据此声称真实 ParameterManager 写参数、真实 Vehicle 或 MAVLink/CAN 端到端行为已经通过。

原测试覆盖：

- 电池电压/功率显示与 FCS 设定值独立，例如左侧 960 W、右侧 3000 W。
- 正常、低电量、严重、紧急、FAILED、UNHEALTHY、CHARGING 配色，阈值相等和阈值无效分支。
- 等待首次状态、从未连接、断连、故障关机 9/10、急停 11/12、遥测过期和普通停机完成的角标规则。
- 无电池、部分 NaN/无穷值、无活动车辆、参数等待/缺失/恢复，以及飞控整体失联与恢复。
- 宽窄布局切换、标签选择记忆和重新打开；布局切换不重建内容，不额外发送 FCS 请求。
- 车辆切换关闭面板、重置标签、重新创建参数控制器，旧编辑不绑定新车辆。
- Fact 编辑器校验/提交、长下拉选项省略显示，以及关闭无效编辑后释放全局校验错误状态。
- FCS 普通/急停门限、确认后发送、会话失效后关闭确认框。
- 中英文文本、320 像素窄屏及 480 像素宽度下 1.4 倍字体指标的布局。

原最终日志为 `/tmp/power-system-indicator/check-results-final.log`。临时测试源码、执行文件及宽窄截图均未提交到仓库，现已丢失；上述命令目前不能直接重跑，需先重建测试环境。软件离屏截图也不能替代真实 GPU 对图标及整机界面的显示验证。

### 5. 原实现阶段记录：既有后端测试

记录结果：**31 passed、0 failed、0 skipped**。

```sh
/home/glados/app/qgroundcontrol/build/Desktop_Qt_6_8_3-Debug/Debug/FuelCellControllerManagerTest -silent
```

该测试使用生产 FuelCellControllerManager 和 MAVLink 编解码、轻量车辆替身，覆盖状态、门限、请求确认、重试、急停抢占及会话切换等逻辑。测试源码和后端源码未因合并入口而修改；不属于真实设备通信验收。本次未重新执行。

### 6. 原实现阶段记录：静态检查

此前记录已完成：

- `git diff --check`；
- 自定义 QRC 引用文件存在，新增入口已注册，两个旧自定义入口引用已移除；
- 新增/调整的三个界面翻译上下文共 56 对上下文/源文本具有对应翻译，占位符匹配；
- `custom_zh_CN.ts` 经 lrelease 生成 266 条已完成翻译、0 条未完成；
- 最终 QML 测试输出及实现文件与暂存内容的一致性核对。

以上为历史结果，不能替代后续修改后的重新检查。

### 7. 本次文档重建的检查范围

该次文档重建核对了当时的 QML、工具栏集成、QRC、CMake 和说明文件，恢复需求与实现的对应关系。

本次已执行并通过以下文档检查：

- 原记录中的两份重建文档均已保存，非空、UTF-8 编码、以换行结尾且无行尾空白。
- 文档间及 README 相对链接的目标存在，DEVELOP.md 所列新增/修改文件存在、旧入口已删除。
- 落盘内容与准备的文档一致；功能实现文件的前后 SHA-256 相同。
- 当前工作区 `git diff --check` 无输出；两个新增文档另行检查行尾格式，因为未跟踪文件不包含在该 Git 命令中。

本次不重新构建应用，不运行桌面后端测试或离屏 QML 测试。原临时测试缺失这一事实不能用现有二进制产物替代证明。

### 8. 尚未执行的验收

| 项目 | 当前限制 |
| --- | --- |
| 真实 PX4、CUAV PMU、FCS 联调 | 未验证真实 MAVLink/CAN 通信、PMU 故障回退和设备启停/急停全链路 |
| 真实参数写入 | 离屏测试只验证编辑器与替身交互，未确认真实飞控参数写入及其重启效果 |
| Android 完整交付 | 未完成应用链接、APK 打包、安装、触摸操作和真机屏幕验收 |
| 实际应用窗口及 GPU 显示 | 未以真实应用窗口覆盖所有屏幕、主题、字体和硬件渲染组合 |
| QGC 全量测试 | 未运行完整项目测试集；此前仅执行所列后端与临时 QML 检查 |
| 历史结果复现 | 本次未重跑历史构建/测试，原始临时日志及临时测试环境已丢失 |

因此，DEVELOP.md 的 A01～A14 是需求清单，不能整体标记为已完成真实环境验收。原离屏测试主要覆盖 A02～A13 的模拟逻辑与布局；A01 的最终工具栏显示、A11/A12 的真实通信/参数行为以及完整 Android 交付仍需对应实机验证。

### 9. 保留的功能限制

- 单 PMU，使用活动车辆第一个电池条目；不支持自动聚合或节点选择。
- 不新增 PMU 报文级超时。飞控仍通信正常但 PMU 数据停止更新时，缓存的电池值可能继续显示。
- 不新增 PMU 回退标志，不能仅凭左右测量值与设定值不同判断驱动异常。
- 四项配置沿用 Fact 单项提交、原有取值校验及重启要求，不新增跨字段原子事务。
- 长动作选项在收起的下拉框中可省略，展开内容沿用原控件。
- FCS 遥测过期不显示红角标，详情显示未知或遥测不可用；这是已确认的交互选择。
- 上游韩文和土耳其文翻译重复条目警告未处理。

后续若修改实现，应保留上述证据边界，补记实际执行命令、结果和环境，不能将历史记录或替身测试写成新的实机验证结果。

### 10. 2026-10-26：修复入口加载失败

本节记录此次修复实际执行的检查，区别于第 2～6 节历史记录和第 7 节文档重建检查。

原因：`PowerSystemBattery.qml` 使用 `FactPanelController`，但遗漏其注册模块 `QGroundControl.FactSystem` 的导入，导致整个顶部入口加载失败。修复仅增加 `import QGroundControl.FactSystem`，不改变显示条件、电池配置或 FCS 控制行为。

本次实际执行：

1. 桌面构建通过，退出码 0，完成 QML 资源、缓存及应用重新链接：

   ```sh
   /home/glados/Qt/Tools/CMake/bin/cmake --build \
     /home/glados/app/qgroundcontrol/build/Desktop_Qt_6_8_3-Debug \
     --target Custom-QGroundControl --parallel 4
   ```

2. 在上述新构建应用的真实 QML 引擎中，使用离屏软件渲染、独立临时配置，且不启动正常事件循环，完成组件加载检查，退出码 0。检查未使用类型替身或 QML 源码替换：
   - 确认应用内嵌的 `PowerSystemBattery.qml` 包含新增导入。
   - 从正式 QRC 路径加载 `PowerSystemIndicator.qml`，状态为 `QQmlComponent::Ready`。
   - 成功创建正式组件实例，确认无电池数据时电压、功率文本分别为 `— V`、`— W`。
3. `git diff --check` 通过；未跟踪的 QML 和本文另行检查行尾格式。

构建与加载日志暂存于 `/tmp/fcs-indicator-fix/build.log`、`/tmp/fcs-indicator-fix/qml-load.log`；临时加载检查程序为同目录的 `check.cpp` 和 `check.so`。这些临时文件未纳入仓库，可能被清理。该程序通过临时拦截 `QApplication::exec()`，使用已初始化应用的实际引擎检查正式资源，不执行正常事件循环或设备操作。

离屏启动日志另有本地化、PX4 参数元数据、字体/平台和退出时数据库警告；本次没有处理这些条目，也没有声称整个应用运行无警告。上述组件加载与实例创建检查未出现原先的类型错误。

本次没有连接真实飞控或验证连接后的工具栏显示，没有执行 Android 重新构建/打包、真实参数写入、FCS 控制或完整回归测试。无活动车辆时的实例创建与占位检查，不等于已完成“活动飞控无 PMU”的实机界面验收。

### 11. 2026-11-03：移除窄屏标签布局

本次将展开页改为固定左右双栏，删除窗口宽度断点、标签按钮、标签选择状态及切车重置标签逻辑；保留切车关闭抽屉、参数编辑器隔离和 FCS 会话校验。窗口不足以容纳双栏不属于正常工作场景，未增加压缩或其他降级布局。

本次实际执行：

1. 静态检查通过：
   - `git diff --check` 无输出；
   - 相关 QML 中不存在 `selectedTab`、`wide`、`availableWidth`、`panelWidth` 或动态双栏/标签切换引用；
   - `custom.ts` 和 `custom_zh_CN.ts` 可由 Python XML 解析器读取；
   - 已删除的 `PowerSystemIndicatorPage` 标签翻译上下文不再存在。
2. 桌面构建通过，退出码 0，完成修改后 QML 的缓存生成、C++ 编译和应用链接：

   ```sh
   CCACHE_DISABLE=1 QTFRAMEWORK_BYPASS_LICENSE_CHECK=1 \
     /home/glados/Qt/Tools/CMake/bin/cmake --build \
     /home/glados/app/qgroundcontrol/build/Desktop_Qt_6_8_3-Debug \
     --target Custom-QGroundControl FuelCellControllerManagerTest --parallel 4
   ```

   第一次未设置上述两个环境变量的构建因沙箱外 ccache 目录只读及 Qt 许可证服务锁失败而退出，不能计为通过；重试禁用 ccache 并按 Qt 输出提示跳过本地许可证检查后才成功。构建过程为 `custom_zh_CN.qm` 生成 264 条已完成翻译、0 条未完成翻译。
3. 现有后端测试实际运行通过：

   ```sh
   QTFRAMEWORK_BYPASS_LICENSE_CHECK=1 \
     /home/glados/app/qgroundcontrol/build/Desktop_Qt_6_8_3-Debug/Debug/FuelCellControllerManagerTest -silent
   ```

   结果为 31 passed、0 failed、0 skipped。此次没有修改后端；该结果只说明既有管理器测试未发现回归，不验证 QML 布局。

翻译脚本曾分别使用系统及 Qt 6.8.3 `lupdate` 试运行。系统版本无法解析仓库已有的 `pragma ComponentBehavior`；Qt 版本可以扫描，但会同时引入大量与本功能无关、尚未同步到目录的既有源文本。两次生成结果均未保留，最终只删除本次消失的两个标签消息，以维持最小必要差异。因此未声称全量 `lupdate` 同步通过。

本次未执行：

- 原实现阶段的 36 项离屏 QML 测试，临时测试程序和源码已不存在；
- 正式资源的运行时组件实例化、真实应用窗口的双栏视觉与交互检查；
- Android 组件构建、APK 打包、安装和真机显示；
- 真实飞控、PMU、FCS、参数写入或 MAVLink/CAN 端到端联调；
- QGC 全量测试集和长时间运行测试。

因此，本次能确认源码已移除响应式标签分支、修改后的 QML 可完成构建及缓存生成、既有后端测试通过；不能据此声称实际窗口布局、真实参数编辑或设备控制已经完成运行时验收。

### 12. 2026-11-03：文档结构整理检查

本次仅整理 PowerSystemIndicator 文档，未修改功能代码。实际执行并通过：

- `custom/Docs/PowerSystemIndicator/` 只包含 `DEVELOP.md`、`README.md`、`TEST.md` 三份文件；
- 三份文档均可按 UTF-8 读取，以换行结尾且没有行尾空白；
- 三份文档中的本地 Markdown 链接目标均存在；
- `git diff --check` 无输出。

本次没有重新构建 QGroundControl，也没有重新运行 QML、后端或实机测试。文档整理不构成任何功能项的新验收结果；功能验证状态继续以前述记录为准。

### 13. 2026-11-03：前端组件文件合并

本次将 PowerSystemIndicator 前端从四个 QML 文件合并为两个文件：常驻电池、参数及配色状态并入工具栏入口，电池配置和 FCS 详情、控制及确认流程并入详情页。没有修改 FuelCellController 后端、MAVLink 协议或参数定义。

本次实际执行：

1. 源码和注册静态检查通过：
   - `custom/src` 范围内只剩 `PowerSystemIndicator.qml` 和 `PowerSystemIndicatorPage.qml` 两个 PowerSystemIndicator 前端文件；
   - 非历史文档和翻译位置中不存在 `PowerSystemBattery` 或 `FuelCellControllerPage` 类型引用；
   - 两个旧文件已从 `Custom.Widgets` 的资源别名和 `QML_FILES` 清单移除；
   - `git diff --check` 无输出。
2. 翻译检查通过：
   - Qt 6.8.3 `lupdate` 从新详情页提取到 54 条源文本；`custom.ts` 和 `custom_zh_CN.ts` 的 `PowerSystemIndicatorPage` 上下文与其逐项一致；
   - 两个 TS 文件可由 `xmllint` 解析，不再包含 `PowerSystemBattery` 或 `FuelCellControllerPage` 翻译上下文；
   - 构建生成 264 条完成翻译、0 条未完成翻译。
3. 桌面目标重新配置并完整构建通过，退出码 0。构建完成合并后 QML 的缓存生成、资源编译及应用最终链接：

   ```sh
   CCACHE_DISABLE=1 QTFRAMEWORK_BYPASS_LICENSE_CHECK=1 \
     /home/glados/Qt/Tools/CMake/bin/cmake --build \
     /home/glados/app/qgroundcontrol/build/Desktop_Qt_6_8_3-Debug \
     --target Custom-QGroundControl --parallel 4
   ```

4. 现有后端测试目标重新检查后实际运行通过：

   ```sh
   QTFRAMEWORK_BYPASS_LICENSE_CHECK=1 \
     /home/glados/app/qgroundcontrol/build/Desktop_Qt_6_8_3-Debug/Debug/FuelCellControllerManagerTest -silent
   ```

   结果为 31 passed、0 failed、0 skipped。此次未修改后端，该结果只用于检查既有 FCS 管理器行为未出现已覆盖范围内的回归。
5. 使用离屏平台、软件渲染和独立临时配置启动新构建应用 10 秒，应用进入正常事件循环后由 `timeout` 结束，因而命令退出码为预期的 124，不能记录为应用自行退出 0。日志没有出现 PowerSystem 新旧类型名、`not a type`、QML 模块缺失或属性赋值错误；另有既有的无网络、视频源不可达、参数元数据、字体和退出时数据库警告。

本次未执行：

- 连接活动飞控后打开详情抽屉并进行固定双栏视觉、滚动、长文字和触控检查；
- 真实参数读取、修改、校验、断开重连及编辑器销毁检查；
- FCS 开机、关机、急停、确认框和请求结果的真实 MAVLink/CAN 联调；
- Android 组件构建、APK 打包、安装和真机显示；
- QGC 全量测试集、长时间运行以及多个字体比例下的界面检查。

因此，本次可以确认文件和注册已经合并、翻译上下文完整、桌面目标能够编译链接、无车辆的离屏应用启动未记录相关 QML 类型错误、现有后端测试通过；不能据此声称真实详情页视觉、参数编辑、设备控制或 Android 已完成验收。

### 14. 2026-11-03：前端内部结构优化

本次只整理合并后的两个 QML 文件：以文件内 `batteryModel` 收拢常驻电池状态，以 `BatteryPane` 和 `FuelCellPane` 划分详情页内容，并集中配置 Facts、FCS 指令常量及发送分派。没有修改用户可见文本、FuelCellController 后端、参数定义或协议。

本次实际执行：

1. `git diff --check` 无输出。
2. Qt 6.8.3 `lupdate` 从优化后的详情页提取 54 条源文本，与 `custom.ts` 的 `PowerSystemIndicatorPage` 上下文逐项一致，因此本次无需修改翻译文件。
3. 桌面目标增量构建通过，退出码 0；详情页完成 QML 缓存生成，应用完成最终链接：

   ```sh
   CCACHE_DISABLE=1 QTFRAMEWORK_BYPASS_LICENSE_CHECK=1 \
     /home/glados/Qt/Tools/CMake/bin/cmake --build \
     /home/glados/app/qgroundcontrol/build/Desktop_Qt_6_8_3-Debug \
     --target Custom-QGroundControl FuelCellControllerManagerTest --parallel 4
   ```

4. `FuelCellControllerManagerTest` 实际运行结果为 31 passed、0 failed、0 skipped。
5. 使用离屏平台、软件渲染和独立临时配置启动新构建应用 6 秒，并筛查 PowerSystem、QQml、类型不可用、模块缺失、属性赋值及内嵌模型函数错误；没有匹配日志。筛查命令因 `rg` 无匹配返回 1，该返回值表示没有筛查到相关错误，不是应用自行退出 0，也不能替代详情页交互测试。

本次未连接活动飞控，未实际打开详情抽屉，未执行真实参数编辑、FCS 控制、断开重连、Android 构建、完整 QGC 测试集或长时间运行测试。因此本次结果只能证明优化后的源码能够编译链接、无车辆启动未记录目标错误且既有后端测试通过，不能证明真实界面与设备交互已经完成运行时验收。

### 15. 2026-11-04：FCS 限流状态与 DCDC 输出电流适配

本次增加状态 13 的橙色告警呈现、原始状态 14～15 的未知码显示，并在 FuelCellControllerManager 和详情页中增加 DCDC 输出电流。实际执行如下：

1. Linux Debug 桌面目标完整构建通过，退出码 0；QML 缓存生成、资源编译和最终应用链接均完成：

   ```sh
   CCACHE_DISABLE=1 QTFRAMEWORK_BYPASS_LICENSE_CHECK=1 \
     /home/glados/Qt/Tools/CMake/bin/cmake --build \
     /home/glados/app/qgroundcontrol/build/Desktop_Qt_6_8_3-Debug \
     --target Custom-QGroundControl FuelCellControllerManagerTest --parallel 4
   ```

2. `FuelCellControllerManagerTest` 实际运行结果为 **33 passed、0 failed、0 skipped**。新增覆盖 DCDC 0/65535 原始值及无效期、状态 13～15 保持控制权限、状态 16 拒绝和只读属性：

   ```sh
   QTFRAMEWORK_BYPASS_LICENSE_CHECK=1 \
     build/Desktop_Qt_6_8_3-Debug/Debug/FuelCellControllerManagerTest -silent
   ```

3. 按已确认取舍，在 `/tmp/power-system-indicator-focused/` 创建了不纳入仓库的聚焦离屏 QML 工具。它直接加载生产 `PowerSystemIndicator.qml` 和 `PowerSystemIndicatorPage.qml`，但使用轻量 QGC/车辆替身；结果为 **6 passed、0 failed、0 skipped**，无 QML 警告。覆盖：

   - 状态 13 的橙色文字与橙色告警级别；
   - 故障/断连红色优先级及遥测过期清除角标；
   - 状态 14～15 的原始码文字和无角标处理；
   - DCDC `0.0 A`、`6553.5 A` 和无效占位格式。

   该临时工具不是 CMake/CTest 目标，不属于可重复的仓库回归测试，也不代表真实 Vehicle、GPU 窗口或 MAVLink/CAN 联调已通过。

4. 中文 TS 经 Qt 6.8.3 `lrelease` 生成 **267 条已完成翻译、0 条未完成翻译**。本次只增加 `Output current limited`、`Unknown state (%1)` 和 `DCDC output current` 三个源文本；全量 `lupdate` 会引入仓库既有未同步文本，因此未保留其无关改动。
5. `git diff --check` 无输出。

尚未执行：真实 PX4/FCS/PMU 联调、CAN/MAVLink 注入、真实限流工况、真实 GPU 窗口视觉检查、长时间运行和 Android 构建。F01～F03、F10、R03 及 E03 已更新为后续台架验收清单；真实限流只在设备和安全条件允许时执行，否则记录为阻塞。
