# QGC FuelCellController 后端

`FuelCellControllerManager` 随 `CustomPlugin` 初始化，管理当前活动飞控的 FuelCellController。
QML 入口为 `QGroundControl.corePlugin.fuelCellController`。

## 地面站界面

连接活动飞控后，顶部显示合并的 PowerSystemIndicator：FCS 图形，右侧为 PMU 实测母线电压和功率。
主图标沿用电池电压状态配色；仅 FCS 断连、故障关机或急停状态显示红色角标。
点击后宽屏左右显示电池详情/四项低电量配置和 FCS 详情，窄屏使用标签页，同一车辆记住最近选择。
参数尚未就绪只影响配置区；飞控失联后电池旧测量值显示占位、配置禁用，FCS 沿用后端失联处理。
FCS 区显示连接状态、实际状态、锂电池 SOC、飞控下发的母线电压/电流/请求功率及当前指令。
左侧测量值和右侧下发值各自读取原有来源，不互相替代；PMU 回退时二者可能不同。
数值只读，无效数据以 `—` 显示。没有新增电池报文级超时或 PMU 回退标志。

开机、关机和 FCS 急停均需一次确认；确认框捕获会话编号，会话失效或面板销毁时关闭。
普通请求等待期间急停仍可操作。结果提示区分“飞控已接受”与设备实际完成，超时显示结果未知。
界面不重发、不自动发送指令；FCS 急停不调用飞控锁定或四旋翼电机停转功能。

- `PowerSystemIndicator.qml`：合并顶部入口、电源数值和 FCS 红角标。
- `PowerSystemIndicatorPage.qml`：左右栏/标签页布局。
- `PowerSystemBattery.qml`：常驻电池数据、配色及按需加载的详情和配置。
- `FuelCellControllerPage.qml`：详情、操作确认和结果反馈。
- `Custom.FuelCell.FuelCellControllerManager`：只暴露后端枚举，实例仍由 `CustomPlugin` 管理。

## 状态读取

| 属性 | 含义 |
| --- | --- |
| `vehicleAvailable` | 存在活动飞控，且不是离线编辑车辆 |
| `telemetryAvailable` | 本次车辆会话收到过有效状态，可用于决定是否展示入口 |
| `telemetryFresh` | 状态消息在 1 秒有效期内，且飞控通信未中断 |
| `connectionState` | `Unknown=-1`、未连接 `0`、已连接 `1`、断连 `2` |
| `deviceDataValid` | 新鲜遥测中的设备连接状态为已连接 |
| `busVoltage` / `busCurrent` / `requestedPower` | 飞控当前用于 CAN 发送的母线电压 V、电流 A、请求功率 W，只读 |
| `socPercent` / `fcsState` | 设备返回的锂电池 SOC %、FCS 系统状态码（协议枚举 0～12） |
| `acceptedCommand` | 飞控当前保存并持续发送的 FCS 指令（0 关机、1 开机、2 急停） |

尚无新鲜遥测时，浮点数返回 `NaN`、枚举/指令返回 `-1`，不能作为有效零值展示。
设备未连接或断连时，SOC 和 FCS 状态无效；母线值只要遥测新鲜就仍可读取。
`Unknown` 表示地面站不知道设备状态，不等于 PX4 判断的 CAN 断连。

母线三项由飞控维护，地面站没有修改接口。`acceptedCommand` 与 `fcsState` 分别表示
飞控保存的指令和设备实际执行进度：飞控接受急停后，设备可能先返回状态 11，再返回完成状态 12。

## FCS 操作

```cpp
bool requestStartFcs(quint32 sessionRevision);
bool requestStopFcs(quint32 sessionRevision);
bool requestEmergencyStopFcs(quint32 sessionRevision);
```

前端在开始操作或打开确认框时保存 `sessionRevision`，确认提交时传回该值。车辆切换、连接移除和
飞控通信中断会使旧会话失效。返回 `true` 仅表示已发起请求，最终处理结果通过属性通知。

- `canControl`：可发起开关机请求；已有待确认请求时为假。
- `canEmergencyStop`：可发起 **FCS 协议急停**；可以抢占待确认的普通请求，不能重复抢占急停请求。
- 两者都要求设备已连接、新鲜遥测及可用的 MAVLink 2 主链路；日志回放和离线编辑禁止控制。
- 急停发送指令 2，不触发 PX4 锁定、飞行终止或四旋翼电机停转功能。
- PX4 当前拒绝未连接/断连时的所有新指令，包括急停；本模块保留该策略。

## 请求与确认

`requestState` 为 `Idle`、`Pending`、`Accepted`、`Failed`、`Unconfirmed`，可配合
`commandPending`、`requestedCommand`、`lastError` 展示当前操作。
本地校验失败返回 `false` 并更新 `lastError`，不会取消正在等待确认的请求。

正常操作最多一个待确认请求。急停抢占时使用新请求编号、立即发送，并停止旧请求重试。
仅匹配当前飞控组件和本次 `response_request_id`；迟到的旧确认更新遥测，不结束新请求。

500 ms 未确认则重试，相同请求最多发送 3 次，总等待上限 2 秒。遥测超时、设备断连、控制链路失效
或车辆切换会提前结束请求。恢复连接、打开页面和模块退出均不会自动发送启停指令。
新编号使用随机起点并递增，跳过 0 和飞控最近上报的编号；重试保留指令、目标和 QGC 来源 ID。

- `Accepted`：PX4 已保存指令，不代表 FCS 已完成动作。
- `Failed`：首次发送失败，或飞控明确返回拒绝/不支持等处理结果。
- `Unconfirmed`：已提交请求，但因超时、失联等原因无法确认；不能当成未执行。

当前指令始终取自状态消息，不能用请求值覆盖。PX4 控制台可能已经改写一项刚被接受的指令。
超时不自动关机、不自动清零、不在重连后重放请求。

## 验证

桌面测试目标为 `FuelCellControllerManagerTest`，使用真实生产管理器和 MAVLink 编解码、轻量车辆替身。
覆盖三态连接、消息超时、来源过滤、字段换算、只读属性、控制门限、拒绝与确认、急停抢占、重试、
车辆切换、销毁清理和请求编号回绕。移动端不生成额外测试应用。
