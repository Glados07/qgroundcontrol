# QGC FuelCellController 后端

`FuelCellControllerManager` 随 `CustomPlugin` 初始化，管理当前活动飞控的 FuelCellController。
QML 入口为 `QGroundControl.corePlugin.fuelCellController`。

## 地面站界面

连接活动飞控后，顶部显示合并的 PowerSystemIndicator：FCS 图形，右侧为 PMU 实测母线电压和功率。
主图标沿用电池电压状态配色；FCS 断连、故障关机或急停状态显示红色角标，输出电流受限显示橙色角标。
点击后始终左右显示电池详情/四项低电量配置和 FCS 详情，不根据窗口宽度切换布局。
参数尚未就绪只影响配置区；飞控失联后电池旧测量值显示占位、配置禁用，FCS 沿用后端失联处理。
右栏“燃料电池”分组显示连接状态、实际状态和设备 DCDC 输出电流；“锂电池”分组单独显示 SOC；“FCS 控制”分组先显示飞控请求功率，再显示当前指令和控制按钮。
左侧测量功率和右侧请求功率各自读取原有来源，不互相替代；PMU 回退时二者可能不同。
数值只读，无效数据以 `—` 显示。没有新增电池报文级超时或 PMU 回退标志。

开机点击后立即请求；关机、故障复位和 FCS 急停需一次确认。复位确认框说明当前指令会切换为关机。确认框捕获会话编号，会话失效或面板销毁时关闭；确认框打开时不发起开机请求。
普通请求等待期间急停仍可操作。结果提示区分“飞控已接受”与设备实际完成，超时显示结果未知。
界面不重发、不自动发送指令；FCS 急停不调用飞控锁定或四旋翼电机停转功能。

- `PowerSystemIndicator.qml`：常驻电池和参数状态，并提供顶部入口、电源数值和 FCS 红/橙角标。
- `PowerSystemIndicatorPage.qml`：固定左右栏，集中实现电池配置、FCS 详情、操作确认和结果反馈。
- `Custom.FuelCell.FuelCellControllerManager`：只暴露后端枚举，实例仍由 `CustomPlugin` 管理。

## 状态读取

| 属性 | 含义 |
| --- | --- |
| `vehicleAvailable` | 存在活动飞控，且不是离线编辑车辆 |
| `telemetryAvailable` | 本次车辆会话收到过有效状态，可用于决定是否展示入口 |
| `telemetryFresh` | 状态消息在 1 秒有效期内，且飞控通信未中断 |
| `connectionState` | `Unknown=-1`、未连接 `0`、已连接 `1`、断连 `2` |
| `deviceDataValid` | 新鲜遥测中的设备连接状态为已连接 |
| `requestedPower` | 飞控当前用于 CAN 发送的请求功率 W，只读 |
| `socPercent` / `dcdcOutputCurrent` | 设备返回的 uint16 centi-unit 分别乘以 0.01 后得到 SOC % 和 DCDC 输出电流 A，仅设备连接正常时有效 |
| `fcsState` | FCS 原始 4 bit 状态码；0～13 已定义，14～15 保留为未知状态 |
| `acceptedCommand` | 飞控当前保存并持续发送的 FCS 指令（0 关机、1 开机、2 急停） |

尚无新鲜遥测时，浮点数返回 `NaN`、枚举/指令返回 `-1`，不能作为有效零值展示。
设备未连接或断连时，SOC、DCDC 输出电流和 FCS 状态无效；请求功率只要遥测新鲜就仍可读取。
连接正常时，SOC 超过 100% 仍照原始值按 0.01%/位显示，不因此判为断连、禁止控制或显示额外告警。
`Unknown` 表示地面站不知道设备状态，不等于 PX4 判断的 CAN 断连。
本版与支持独立复位位的 FCS-4500 V1.4 PX4 配套部署。新控制消息长度为 8、CRC_EXTRA 为 37；状态消息长度仍为 14、CRC_EXTRA 仍为 218。

请求功率和未上报给 QGC 的 CAN 母线值均由飞控维护，地面站没有修改接口。`acceptedCommand` 与 `fcsState` 分别表示
飞控保存的指令和设备实际执行进度：飞控接受急停后，设备可能先返回状态 11，再返回完成状态 12。
状态 13 表示输出电流受限，不改变连接健康或控制权限；状态 14～15 按未知原始码显示，不丢弃整条遥测。

## FCS 操作

```cpp
bool requestStartFcs(quint32 sessionRevision);
bool requestStopFcs(quint32 sessionRevision);
bool requestEmergencyStopFcs(quint32 sessionRevision);
bool requestFaultReset(quint32 sessionRevision);
```

前端在开机点击时传入当前 `sessionRevision`；关机和急停在打开确认框时保存版本，确认提交时传回。车辆切换、连接移除和
飞控通信中断会使旧会话失效。返回 `true` 仅表示已发起请求，最终处理结果通过属性通知。

- `canControl`：可发起开关机请求；已有待确认请求或复位受理后的短暂禁用期内为假。
- `canResetFault`：可发起故障复位；与开关机使用同一门限，不抢占待确认的开关机请求。
- `canEmergencyStop`：可发起 **FCS 协议急停**；可以抢占待确认的普通请求，不能重复抢占急停请求。
- 两者都要求设备已连接、新鲜遥测及可用的 MAVLink 2 主链路；日志回放和离线编辑禁止控制。
- 急停发送指令 2，不触发 PX4 锁定、飞行终止或四旋翼电机停转功能。
- PX4 当前拒绝未连接/断连时的所有新指令，包括急停；本模块保留该策略。
- 故障复位发送 `control_command=0`、`reset_fault=1`，PX4 将持续指令切换为关机并发送三个 100 ms 控制周期的独立复位位。普通启停和急停均发送 `reset_fault=0`。

## 请求与确认

`requestState` 为 `Idle`、`Pending`、`Accepted`、`Failed`、`Unconfirmed`，可配合
`commandPending`、`requestedCommand`、`lastError` 展示当前操作。
`requestedFaultReset` 区分复位与普通关机；`resetCooldownActive` 表示 QGC 收到复位受理回执后约 400 ms 的本地禁用期，急停不受此限制。状态消息不包含复位脉冲进度，因此禁用期只是保守估计。
本地校验失败返回 `false` 并更新 `lastError`，不会取消正在等待确认的请求。

正常操作最多一个待确认请求。急停抢占时使用新请求编号、立即发送，并停止旧请求重试。
仅匹配当前飞控组件和本次 `response_request_id`；迟到的旧确认更新遥测，不结束新请求。

500 ms 未确认则重试，相同请求最多发送 3 次，总等待上限 2 秒。遥测超时、设备断连、控制链路失效
或车辆切换会提前结束请求。恢复连接、打开页面和模块退出均不会自动发送启停指令。
新编号使用随机起点并递增，跳过 0 和飞控最近上报的编号；重试保留指令、目标和 QGC 来源 ID。

- `Accepted`：普通启停表示 PX4 已保存指令；复位表示 PX4 已开始脉冲，不代表设备已清除故障。
- `Failed`：首次发送失败，或飞控明确返回拒绝/不支持等处理结果。
- `Unconfirmed`：已提交请求，但因超时、失联等原因无法确认；不能当成未执行。

当前指令始终取自状态消息，不能用请求值覆盖。PX4 控制台可能已经改写一项刚被接受的指令。
超时不自动关机、不自动清零、不在重连后重放请求。

## 验证

桌面测试目标为 `FuelCellControllerManagerTest`，使用真实生产管理器和 MAVLink 编解码、轻量车辆替身。
覆盖三态连接、消息超时、来源过滤、SOC/DCDC 字段换算、原始状态 0～15、只读属性、控制门限、拒绝与确认、急停抢占、重试、
车辆切换、销毁清理和请求编号回绕。移动端不生成额外测试应用。
