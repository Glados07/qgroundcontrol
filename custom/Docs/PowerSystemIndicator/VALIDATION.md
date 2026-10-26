# PowerSystemIndicator 验证记录

重建日期：2026-10-26。适用仓库：`/home/glados/app/qgroundcontrol`。

需求及验收标准见 [DEV.md](DEV.md)。本文恢复原实现阶段的验证记录，并明确列出本次文档重建的检查范围。

**证据说明：** 原文档及 `/tmp/power-system-indicator/` 下的临时测试、截图和日志现已丢失。下文“原实现阶段记录”来自此前开发记录，本次未重新执行对应构建和测试，不能作为本次重新运行通过的证明。桌面构建产物仍存在，也不代表已经基于本次状态重新构建。

## 1. 实现范围

合并功能新增三个 QML 文件、修改七个集成/界面/翻译/说明文件，删除两个自定义旧入口，完整清单见 DEV.md 第 9 节。

实现范围为 `custom/`，保留上游 BatteryIndicator、通用抽屉及工具栏框架、既有 FuelCellControllerManager 和协议。本次文档重建仅新增本目录的 `DEV.md` 和 `VALIDATION.md`，不修改功能代码，不创建 Git 提交。

## 2. 原实现阶段记录：桌面构建

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

## 3. 原实现阶段记录：Android 相关组件编译

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

## 4. 原实现阶段记录：离屏 QML 测试

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

## 5. 原实现阶段记录：既有后端测试

记录结果：**31 passed、0 failed、0 skipped**。

```sh
/home/glados/app/qgroundcontrol/build/Desktop_Qt_6_8_3-Debug/Debug/FuelCellControllerManagerTest -silent
```

该测试使用生产 FuelCellControllerManager 和 MAVLink 编解码、轻量车辆替身，覆盖状态、门限、请求确认、重试、急停抢占及会话切换等逻辑。测试源码和后端源码未因合并入口而修改；不属于真实设备通信验收。本次未重新执行。

## 6. 原实现阶段记录：静态检查

此前记录已完成：

- `git diff --check`；
- 自定义 QRC 引用文件存在，新增入口已注册，两个旧自定义入口引用已移除；
- 新增/调整的三个界面翻译上下文共 56 对上下文/源文本具有对应翻译，占位符匹配；
- `custom_zh_CN.ts` 经 lrelease 生成 266 条已完成翻译、0 条未完成；
- 最终 QML 测试输出及实现文件与暂存内容的一致性核对。

以上为历史结果，不能替代后续修改后的重新检查。

## 7. 本次文档重建的检查范围

本次核对当前 QML、工具栏集成、QRC、CMake 和 README，恢复需求与当前实现的对应关系。

本次已执行并通过以下文档检查：

- 两个文件均已保存，非空、UTF-8 编码、以换行结尾且无行尾空白。
- 文档间及 README 相对链接的目标存在，DEV.md 所列新增/修改文件存在、旧入口已删除。
- 落盘内容与准备的文档一致；功能实现文件的前后 SHA-256 相同。
- 当前工作区 `git diff --check` 无输出；两个新增文档另行检查行尾格式，因为未跟踪文件不包含在该 Git 命令中。

本次不重新构建应用，不运行桌面后端测试或离屏 QML 测试。原临时测试缺失这一事实不能用现有二进制产物替代证明。

## 8. 尚未执行的验收

| 项目 | 当前限制 |
| --- | --- |
| 真实 PX4、CUAV PMU、FCS 联调 | 未验证真实 MAVLink/CAN 通信、PMU 故障回退和设备启停/急停全链路 |
| 真实参数写入 | 离屏测试只验证编辑器与替身交互，未确认真实飞控参数写入及其重启效果 |
| Android 完整交付 | 未完成应用链接、APK 打包、安装、触摸操作和真机屏幕验收 |
| 实际应用窗口及 GPU 显示 | 未以真实应用窗口覆盖所有屏幕、主题、字体和硬件渲染组合 |
| QGC 全量测试 | 未运行完整项目测试集；此前仅执行所列后端与临时 QML 检查 |
| 历史结果复现 | 本次未重跑历史构建/测试，原始临时日志及临时测试环境已丢失 |

因此，DEV.md 的 A01～A14 是需求清单，不能整体标记为已完成真实环境验收。原离屏测试主要覆盖 A02～A13 的模拟逻辑与布局；A01 的最终工具栏显示、A11/A12 的真实通信/参数行为以及完整 Android 交付仍需对应实机验证。

## 9. 保留的功能限制

- 单 PMU，使用活动车辆第一个电池条目；不支持自动聚合或节点选择。
- 不新增 PMU 报文级超时。飞控仍通信正常但 PMU 数据停止更新时，缓存的电池值可能继续显示。
- 不新增 PMU 回退标志，不能仅凭左右测量值与设定值不同判断驱动异常。
- 四项配置沿用 Fact 单项提交、原有取值校验及重启要求，不新增跨字段原子事务。
- 长动作选项在收起的下拉框中可省略，展开内容沿用原控件。
- FCS 遥测过期不显示红角标，详情显示未知或遥测不可用；这是已确认的交互选择。
- 上游韩文和土耳其文翻译重复条目警告未处理。

后续若修改实现，应保留上述证据边界，补记实际执行命令、结果和环境，不能将历史记录或替身测试写成新的实机验证结果。

## 10. 2026-10-26：修复入口加载失败

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
