# QGC 二次开发说明

适用工程：`F:\qgroundcontrol_viewer3d`

当前分支：`SecDev/ft/gimbalcontrol`

最后更新：2026-07-24

## 1. 当前开发进度

### 1.1 总体进度

当前开发分支为 `SecDev/ft/gimbalcontrol`，二次开发已形成九个面向用户的功能模块和一套 `custom` 工程化集成架构：

1. Viewer3D 三维飞行视图。
2. 思翼 A8 Mini 云台控制。
3. RTSP 视频流集成与 Android H.265 低延迟硬件解码。
4. 飞行界面底部航向罗盘条。
5. Android 遥控器默认界面缩放。
6. Android USB 飞控连接。
7. Fuel 燃料状态与低电压告警。
8. 默认通信链路安装。
9. PX4 FirmwarePlugin/AutoPilotPlugin 定制。

各模块当前所处阶段如下：

- **已集成**：Viewer3D、思翼云台、Fuel、默认通信链路和 PX4 定制均已接入 `custom` 构建、资源及运行链路。
- **代码已集成，待目标遥控器真机回归验收**：底部航向罗盘条、Android 86% 界面缩放缺省值、Android H.265 低延迟硬解和 Android USB 串口管理器。代码已经进入当前工作树，但仍需按第 12 章完成目标遥控器上的净安装、布局、性能、日志、延迟、反复拔插和重连测试，不能仅凭静态接入、代码注册或 decoder rank 判定验收通过。
- **已有实测基础**：Ubuntu 24.04 下 A8 Mini 云台控制及 H.265 RTSP 播放正常；Android 下同一云台使用 H.264 编码播放正常。
- **功能边界**：本次实现的是 QGC 根据活动飞行器 `Vehicle.heading` 绘制的航向罗盘条，不是思翼视频码流内的 OSD，也不表示航点方向、航线偏差或下一航段。

### 1.2 Viewer3D 三维飞行视图（已集成）

- 在 Fly View 工具条增加 2D/3D 切换入口，并保持切换状态和图标状态一致。
- 支持 QGC 本地 OSM 三维建筑、外部三维模型和可选 Google 3D Maps 三种地图来源。
- 外部模型可直接加载 OBJ、glTF、GLB 和 QML；FBX、DAE、STL、PLY 通过 Qt Balsam 转换后加载。
- 支持配置外部模型的 WGS84 原点、模型单位、比例、yaw、建筑层高和高度偏移，使模型坐标与真实经纬度坐标对齐。
- 在三维场景中显示飞行器、任务点和航线，并完成外部模型模式下的 AMSL 高度配准。
- 提供 Viewer3D 设置持久化、Google WebEngine 缺失提示、模型加载状态及错误反馈。

### 1.3 思翼 A8 Mini 云台控制（已集成）

- 在 `custom` 中实现思翼私有 UDP SDK 和协议封装，包括帧组装、CRC16、固定为0的协议sequence字段，以及0x05、0x0a、0x0b、0x0c、0x0f、0x16和0x18。0x05只保留协议兼容接口，当前Manager/UI不调用；0x16用于实际拉流尺寸链未能确认时查询设备“当前支持缩放范围”，0x0f执行绝对倍率，0x18读取实际倍率。0x0f的一字节ACK只表示命令受理，不能更新显示。为兼容现有设备当前仍未递增sequence，因此不能在协议层关联同命令迟到包；Manager对用户目标保留单目标/单查询在途、350 ms隔离和两次目标命中。
- A8 Mini数字变焦上限优先以QGC实际解码拉流尺寸确认。GStreamer路径有两条互相独立的首选可信来源：custom在主视频 `qgcvideosinkbin` 的sink pad等待真实首帧，并依次检查外层pad、已连接解码器peer及ghost target的current CAPS；同时观察 `GstGLQt6VideoItem` 在最终 `GstVideoInfo` 生效后写入的 `implicitWidth/implicitHeight`。任一来源得到受支持尺寸都会直接投递Manager，pad路径还会修正原生 `VideoManager::videoSize`。两个custom观察器都未回调时，已解码且连续稳定1秒的 `VideoManager::videoSize`作为第二级后备；所有尺寸路径仍未确认时，Manager每2秒查询0x16并用设备返回的当前支持范围恢复能力，避免0x18因上限unknown而永远不发送。尺寸映射为1280×720（及coded-height 736）→6.0x、1920×1080（及1088）→5.5x、2560×1440→3.5x、3840/4096×2160→1.0x且不可继续缩放；其他尺寸由0x16后备，不从机内卡录设置猜测。短暂断流保留最近确认能力；新的受支持直接尺寸或稳定1秒的兜底尺寸会替换旧上限，新的不支持直接尺寸会释放旧上限并回到0x16后备，避免4K切回1080P后仍保持1.0x。
- `currentZoom`只保存经过协议校验和状态机确认的0x18设备回读，不在0x0f写入成功或ACK成功后乐观显示请求目标。0x16/0x18逐包优先按新版“整数字节+一位小数字节”解析；若该候选超出当前A8有效范围，再兼容真机固件使用的小端uint16/10格式，例如 `01 00`与 `0a 00`都可表示1.0x。采用兼容格式时日志明确记录 `encoding little-endian tenths compatibility`，不能因某一命令的格式锁定整个会话。启动、重连、能力来源恢复、外部倍率变化及每个用户绝对目标都要求两个串行0x18查询得到一致实际值后才发布；命令确认期间 `zoomStatusKnown=false`并显示 `--`，避免单个迟到包或运动中间值冒充实际倍率。`zoomStep`网格固定锚定1.0x：所有中间移动严格使用完整步长；只有下一完整步会越过相机真实小数上限时，才允许一次边界吸附。
- `GimbalControlManager` 直接接收上述主显示sink的协商尺寸；GStreamer构建还监听 `VideoManager::videoSizeChanged/decodingChanged`，但只用于无直接结果时的1秒稳定兜底，非GStreamer构建则直接使用这两个原生状态。2秒轮询入口补查尚未确认或已变化的能力。能力确认或更新会显式通知QML重新计算两个方向的可用性；功能已启用时立即启动0x18稳定同步，后台每2秒在空闲时继续查询0x18并查询0x0a相机状态。绝对目标或稳定确认期间由专用串行查询状态机接管。每次先等待350 ms隔离旧包，同一时刻只允许一个0x18查询在途；单次查询1秒无响应才重试，整次缩放操作10秒未确认才置unknown。该探测不依赖控件可见或活动飞行器。接收端以 `QHostAddress::isEqual()`比较来源逻辑IP，并要求精确帧长、CRC及ACK control严格等于0x02；只有业务payload也合法的回包才置 `sdkResponding=true`。
- 飞行界面右侧使用单个始终可见的纵向半透明控制栏：只要Gimbal功能已启用，无论是否连接飞控或云台都会显示；从上到下依次为放大、当前倍率、缩小、横向分隔线、思翼机内拍照和录像。离线、能力仍未知、倍率尚未确认或命令pending时显示 `--`并禁用缩放；已到真实上限或下限且该方向没有下一目标时保持显示真实倍率，只禁用该方向。最后一个整数网格点仍可向小数上限吸附，小数上限也可先回到相邻整数网格点。取得拉流尺寸能力或0x16后备能力及稳定倍率后，短按和长按按同一目标序列工作；4K模式确认上限为1.0x时两个方向都不可用是设备不支持数字变焦的真实边界，不属于异常锁定。
- 拍照成功只在存在本次拍照请求时由0x0b功能反馈累计本进程照片数并触发成功提示；录像按钮必须先取得0x0a录像状态，避免把“尚未同步”误当成“未录像”。0x0c录像切换没有ACK，发送后先建立目标状态和pending门控、再乐观更新UI，约400 ms后主动查询0x0a；只有与本次目标一致的状态0/1才能完成pending，延迟旧状态继续忽略，状态2/3错误立即结束，2.5秒仍未确认则重新标记unknown。0x0b只用于拍照结果、HDR结果和录像失败反馈，不能虚构录像成功反馈码。录制发生在思翼相机机内存储卡，而不是QGC本地录像支路。
- Gimbal关闭时才在存在活动飞行器的前提下回退QGC原生 `PhotoVideoControl`。缩放手势统一为Idle/Pressed/Holding/Consumed状态：短按发送一个0x0f目标；长按也不使用自由运行的0x05，而是在上一0x0f经过两次0x18目标确认后串行发送下一目标。中间目标间距严格等于 `zoomStep`，仅最后一次允许吸附真实小数上限，反向时先从该上限回到相邻网格点。松开、取消、移出或外部安全停止只取消尚未发送的后续步骤，已经发出的唯一目标继续确认，不发送0x05停止包，也不会在释放后补发短按。保留60秒长按看门狗。
- 在Fly View设置页以 `SIYI Gimbal Camera` 标题提供云台相机启用、SDK IP、SDK端口和缩放步长设置，并明确短按发送一个目标、长按重复已确认目标、中间移动使用完整步长、最后一次可吸附真实小数上限并在反向时先返回相邻网格点；实际拉流尺寸优先决定上限，0x16只在尺寸链未确认时提供可恢复后备。

### 1.4 RTSP 与 Android H.265 视频链路（代码已集成，待真机验收）

- 为 A8 Mini 安装 RTSP 默认地址 `rtsp://192.168.144.25:8554/main.264`、20 秒超时和 Android 低延迟默认值；实际 H.264/H.265 编码类型仍由 RTSP SDP 协商确定，不由 URL 后缀强制指定。
- 支持手动视频源与 MAVLink 相机流信息两种接入方式；`Use MAVLink automatic video stream` 和 `Force Android H.265 hardware decoder` 已统一放入 Application Settings -> Video -> Video Stream Integration，并在所有平台显示。
- Android H.265 优先选择经过筛选的厂商 `amcviddec-*` MediaCodec；若厂商 decoder 原生接受 `hvc1`，则提升其 rank 并直接解码。
- 若厂商 decoder 只接受 Annex-B，则使用 custom 适配器接收 QGC 播放支路的 `hvc1`，通过 GStreamer `h265parse` 转换为 `video/x-h265,stream-format=byte-stream,alignment=au` 后再送入该 MediaCodec。
- 排除 Google/Android/Goldfish、secure、软件、FFmpeg 及厂商软件变体，优先选择厂商单独提供的 `lowlatency`/`low_latency` 解码组件。
- 硬解输出队列采用 downstream-leaky 且最多保留 2 帧，显示端反压时主动丢弃旧帧，防止延迟随播放时间持续累积。
- 记录候选 factory、实际 decoder、协商输入 caps 和首个 raw frame；只有厂商 MediaCodec 实际输出首帧后，才能在 QGC 管线侧确认硬解链路已经跑通。
- 没有兼容厂商硬解时不注册适配器，并保留原生软件解码 rank 作为安全回退，避免设备直接黑屏；因此该开关代表优先并验证硬解，而不是在所有硬件上无条件禁止软件回退。
- H.264 和非 Android 平台不受该策略影响；播放支路的格式转换不修改原生 `hvc1` 录像支路。

### 1.5 飞行界面底部航向罗盘条（代码已集成，待界面回归）

- 从 `custom-example` 选择性移植底部横向航向条、中央航向数值框和固定指针，不移植右下圆形罗盘、姿态仪及其无关资源。
- 罗盘条读取活动飞行器 `Vehicle.heading.rawValue`，显示 N、NE、E、SE、S、SW、W、NW 方位及中央当前航向角；不存在活动飞行器或航向无效时不显示。
- 使用 11 个相对方位 Label 实现连续滚动和 359°/0° 跨界，替代示例的 720 个 Label，降低 Android 上每次航向更新的 QML 重算量。
- 新增 `FlyView/showHeadingCompassBar` 持久化 Fact，默认开启、无需重启；开关位于 Application Settings -> Fly View -> Instrument Panel。
- `FlyViewCustomLayer` 通过显式 custom QRC Loader 加载罗盘条；位置和首选宽度与 custom-example 一致，固定为 `50 × defaultFontPixelWidth` 并贴近飞行界面底边，仅在整个 Fly View 本身不足以容纳时按左右基础 margin 收窄。PIP、虚拟摇杆和右下仪表的角落 inset 不再参与罗盘条宽度计算，避免单侧最大 inset 被左右重复扣除后在 86% 缩放或 PIP 拉伸时把罗盘条压成一小块；显示时仍只增加 `bottomEdgeCenterInset`。罗盘条是纯显示层，不截获其下方地图的拖动、缩放或触摸事件；关闭开关时完整透传原生 insets。
- 普通地图主视图、视频主视图和 Viewer3D 使用同一 custom overlay；QGC 原生全屏视频模式会隐藏整个 custom overlay，因此该模式下罗盘条随之隐藏。

### 1.6 Android 遥控器默认界面缩放（代码已集成，待净安装验证）

- 在 `custom/src/UI/AppSettings` 保存同路径 `GeneralSettings.qml`，由 custom URL 拦截器覆盖原生通用设置页；页面内容保持当前 `src` 的 General、Units、Brand Image 和整数 UI Scaling 行为，不创建重复 Fact。
- `CustomPlugin::adjustSettingMetaData()` 仅在 Android 构建中把 `appFontPointSize` 的元数据缺省值改为 12 pt；目标遥控器采用 14 pt 平台基准，原生页面计算 `12 / 14 × 100` 后四舍五入显示为 86%。
- `-`/`+` 继续按照 QGC 原生行为每次调整 1 pt，页面显示的百分比由整数点数除以平台基准后取整，不提供任意 1% 步进。
- 该逻辑只在根级 QSettings 键 `appFontPointSize` 不存在时提供缺省值。已有安装以及用户后续手动选择的缩放值始终优先，不会在每次启动时被强制改回 86%。
- 新安装、清除应用数据或执行“清除全部设置”后，常规 Android 遥控器使用 86% 缺省值；Ubuntu、Windows、macOS、iOS 等非 Android 平台不执行覆盖，保持 QGC 原生 100% 缺省缩放。
- QGC 对物理宽度小于 120 mm 的极小 Android 屏幕使用 11 pt 平台基准，整数点数无法表达 86%；12 pt 缺省值针对当前采用 14 pt 基准的目标遥控器。

### 1.7 Android USB 飞控连接（代码已集成，待真机验收）

- 按原生 Android 文件树在 `custom/android` 中同名覆盖 `QGCUsbSerialManager.java`，保持 QGC JNI 类名和 public static 接口不变。
- 分离已发现 driver 与已打开端口资源，普通关闭只释放端口和 I/O 资源，保留可再次打开的 driver，解决同一根 USB 线不拔时无法重新连接的问题。
- 补全冷启动已插入、运行中插入、权限申请/拒绝、拔出、重新枚举、Activity 重建及应用清理的生命周期处理。
- detach、每次扫描确认某设备消失、打开失败和 I/O 创建失败都执行幂等资源回滚，避免旧driver、文件描述符或权限请求持续残留。
- 只向QGC返回已由默认prober或保守CDC-ACM兜底匹配、已获权限且至少包含一个串口的USB设备；未匹配串口、也不具备CDC COMM/ACM+CDC_DATA双接口的思翼内置设备不会进入端口列表。
- 在usb-serial-for-android默认prober之外，仅对同时具有CDC communication/ACM和CDC data interface的标准CDC-ACM设备提供兜底；当前只暴露每个USB设备的第一个串口port0。
- 增加 `QGCUsbSerial-Custom` 分层日志，用于区分 Android Host 未枚举、驱动未匹配、权限失败、Qt 未发现端口和 MAVLink heartbeat 缺失。

### 1.8 Fuel 燃料状态与告警（已集成）

- 在顶部工具栏 Battery 后插入 Fuel 指示器，并移除 RC RSSI 指示器；没有 Fuel 遥测时自动隐藏。
- 工具栏显示燃料图标和剩余百分比，详情页显示剩余量、最大量、已消耗量、流量、温度及液体/气体单位。
- 在 Fly View 增加燃料电池母线低电压告警：低于 20.0 V 触发，恢复到 20.4 V 以上关闭，通过回差避免临界电压附近反复闪烁。

### 1.9 默认通信链路（已集成）

- 每次启动均在 LinkManager 读取设置前检查已有链路；不存在 `local`/`Local` 时，自动补建名为 `local` 的 UDP 链路。
- 默认远端为 `192.168.144.20:19856`，本地端口为 0，不自动连接且不标记为高延迟链路。
- 安装逻辑保持幂等；用户已经创建同名链路时不重复添加，也不覆盖用户现有参数。

### 1.10 PX4 飞控定制（已集成）

- 使用 custom PX4 Factory 替代原生 PX4 Factory，并关闭 APM Factory。Factory 的能力列表声明 PX4 + MultiRotor；当前 `firmwarePluginForAutopilot()` 只检查 `MAV_AUTOPILOT_PX4`、没有检查 `vehicleType`，所以运行时其他 PX4 机型也会进入 `CustomFirmwarePlugin`，不能把它描述成已经强制拒绝非多旋翼。
- 使用 `CustomFirmwarePlugin` 和 `CustomAutoPilotPlugin` 接入定制车辆能力、工具栏及车辆设置页。
- 普通模式只显示 Safety；高级模式显示 Airframe、Sensors、Radio、Flight Modes、Power、Motors、Safety 和 Tuning。
- 可由定制列表设置的飞行模式限制为 Loiter、RTL 和 Mission，并通过 `hasGimbal()` 静态声明 pitch/yaw 云台能力；该返回值不检测思翼设备、SDK 连接或云台实际响应状态。
- Fuel 指示器由 `CustomFirmwarePlugin::toolIndicators()` 插入 Battery 后，避免把项目功能误当作 `custom-example` 示例资源。

### 1.11 custom 架构、设置和翻译（已集成）

- 二次开发主体位于 `custom`，目录和命名参照 `src` 模块树；当前共 107 个文件。
- 仅保留 `src/CMakeLists.txt` 和 `src/Vehicle/VehicleSetup/VehicleSummary.qml` 两处 feature 必需的受控修改，其余功能通过 custom C++、QRC、QML URL 拦截和 Android overlay 接入。
- General、Fly View 和 Video 设置页均按 `src/UI/AppSettings` 文件树使用同路径 custom 覆盖；Viewer3D、Gimbal、视频链路和航向罗盘条参数使用稳定 Fact/QSettings 分组持久化。General 页面继续绑定原生 `appFontPointSize`，Android 缺省值由 custom metadata hook 调整。
- Android 构建先在构建目录合并原生模板和 `custom/android` overlay，再只编译合并后的唯一 Java 源，避免原生/custom 同包同类冲突。
- 与 `src/Viewer3D` 完全相同的 C++、QML、qmldir 和 shader 由构建或 QRC 直接复用，不在 custom 保存重复副本；外部 WGS84 城镇样例只是源码树手动测试资产，不参与构建或 QRC 打包。
- 只从 `custom-example` 引入底部航向罗盘条；不引入其未使用的示例控件、自定义动作、圆形罗盘、姿态仪、品牌资源和全局配色，也不保存无必要的 `AppSettings.qml` 根页副本。
- custom 翻译加载、简体中文目录和 `lupdate` 更新脚本已经接入；当前两个 TS 都只有 5 个 context、18 条 message，覆盖母线告警、航向罗盘条、Fuel 和 Video Integration，Viewer3D、Gimbal 及部分 C++ `tr()` 新文本仍需刷新目录，不能把当前 TS 描述为完整覆盖。

## 2. 开发边界

1. 除 `src/CMakeLists.txt` 和 `src/Vehicle/VehicleSetup/VehicleSummary.qml` 两处 feature 必需改动外，不修改其他 `src` 文件。
2. custom 新增代码按 QGC 模块放置，例如 `FlightDisplay`、`FlightMap/Images`、`Settings`、`Gimbal`、`Comms`、`QmlControls`、`UI/AppSettings`、`VideoManager/VideoReceiver/GStreamer`；Android Java 同名覆盖按根目录 `android` 的文件树放在 `custom/android`。
3. Application Settings 的 General、Fly View 和 Video 页面由项目在 custom 显式接管并保存同名覆盖；其他没有差异、也不需要项目接管的 QML 继续使用 `src`。
4. 与 `src/Viewer3D` 相同的公共实现由 `custom/CMakeLists.txt` 或 `custom.qrc` 直接引用，不在 custom 保存副本。
5. custom QML 覆盖使用 `/Custom/qml` 前缀，Viewer3D 独立模块仍使用 `/qml/Viewer3D`。
6. 设置 Fact 名和 QSettings 分组保持稳定，升级程序不会丢失已有 Viewer3D、Gimbal、Fly View 航向罗盘条和链路设置。
7. 复杂协议、坐标转换和跨模块行为使用中文注释；普通布局和赋值不增加无意义注释。
8. Android 构建先在构建目录合并原生 `android` 模板和 `custom/android` overlay，Gradle 只编译合并结果；不把两个 Java 源目录同时加入 source set，避免同包同类冲突。

## 3. custom 完整目录结构

当前共 107 个文件：

```text
custom/
  CMakeLists.txt
  custom.qrc
  cmake/
    CustomOverrides.cmake
  android/
    src/org/mavlink/qgroundcontrol/
      QGCUsbSerialManager.java
  src/
    CustomPlugin.h
    CustomPlugin.cc
    AutoPilotPlugin/
      CustomAutoPilotPlugin.h
      CustomAutoPilotPlugin.cc
    Comms/
      DefaultCommunicationLinkInstaller.h
      DefaultCommunicationLinkInstaller.cc
    FirmwarePlugin/
      CustomFirmwarePlugin.h
      CustomFirmwarePlugin.cc
      CustomFirmwarePluginFactory.h
      CustomFirmwarePluginFactory.cc
    FlightDisplay/
      FlyViewCompassBar.qml
      FlyViewCustomLayer.qml
      FlyViewToolStripActionList.qml
      FlyViewTopRightColumnLayout.qml
      GeneratorBusVoltageAlert.qml
      GimbalCameraControl.qml
      GimbalZoomControl.qml
    FlightMap/
      Images/compassPointer.svg
    Gimbal/
      A8MiniZoomPolicy.h
      A8MiniZoomPolicy.cc
      GimbalControl.SettingsGroup.json
      GimbalControlManager.h
      GimbalControlManager.cc
      GimbalControlSettings.h
      GimbalControlSettings.cc
      GimbalVideoStreamSupport.h
      GimbalVideoStreamSupport.cc
      SiyiProtocol.h
      SiyiProtocol.cc
      SiyiSdk.h
      SiyiSdk.cc
    QmlControls/
      FuelStatusIndicatorPage.qml
      Viewer3D/Models3D/qmldir
    Settings/
      FlyViewCustom.SettingsGroup.json
      FlyViewCustomSettings.h
      FlyViewCustomSettings.cc
    UI/
      AppSettings/
        GeneralSettings.qml
        FlyViewSettings.qml
        VideoSettings.qml
        Viewer3DSettingsGroup.qml
        GimbalControlSettingsGroup.qml
      toolbar/
        FuelStatusIndicator.qml
        Images/FuelIcon.svg
    VideoManager/
      VideoReceiver/
        GStreamer/
          AndroidH265HardwareDecoderAdapter.h
          AndroidH265HardwareDecoderAdapter.cc
          AndroidVideoDecoderPolicy.h
          AndroidVideoDecoderPolicy.cc
          PulledVideoResolutionProbe.h
          PulledVideoResolutionProbe.cc
    Viewer3D/
      CityMapGeometry.cc
      CustomViewer3DManager.h
      CustomViewer3DManager.cc
      External3DMapManager.h
      External3DMapManager.cc
      OsmParser.cc
      Viewer3D.SettingsGroup.json
      Viewer3DSettings.h
      Viewer3DSettings.cc
      Viewer3DQmlBackend.h
      Viewer3DQmlBackend.cc
      Viewer3DTerrainGeometry.cc
      Images/city_3d_map_icon.svg
      Viewer3DQml/
        Viewer3D.qml
        Google3DMapView.qml
        Google3DMapUnavailable.qml
        Models3D/
          External3DMap.qml
          Viewer3DModel.qml
          Viewer3DVehicleItems.qml
        Drones/
          DroneModelDjiF450.qml
          Djif450/*/node.mesh
      ExternalWGS84_UE5_MapSample/
        README.md
        osm_overpass_source.json
        qgc_viewer3d_import_settings.json
        realistic_town_wgs84_map.fbx
        realistic_town_wgs84_map.mtl
        realistic_town_wgs84_map.obj
        textures/*.png
  test/
    Gimbal/
      SiyiProtocolTest.cc
  translations/
    README.md
    custom.ts
    custom_zh_CN.ts
    custom-lupdate.sh
```

## 4. 每个文件的作用

本章按“文件本身实现什么、由谁创建或调用、读取什么数据、最终影响什么功能”逐文件描述。阅读时需要特别区分：`*.SettingsGroup.json` 只定义 Fact 元数据；`*Settings.h` 声明稳定接口，`*Settings.cc` 创建 Fact 并接入 QSettings；`UI/AppSettings/*.qml` 只提供设置界面；Manager/Policy C++ 执行业务编排；`FlightDisplay/*.qml` 执行飞行界面运行时显示；`*.svg/*.png/*.mesh` 只是被上层加载的图形或几何数据；QRC/CMake 决定文件能否进入应用，但不执行业务。每行同时说明“不负责什么”，用于避免把相邻层的职责重复实现到错误目录。

### 4.1 构建入口

| 文件 | 详细作用 |
|---|---|
| `custom/CMakeLists.txt` | custom 构建总入口。向根工程注入 `QGC_CUSTOM_BUILD`、`CUSTOMHEADER=CustomPlugin.h` 和 `CUSTOMCLASS=CustomPlugin`，收集 AutoPilot/Firmware、Viewer3D、Gimbal、Comms、Settings、GStreamer拉流尺寸探针和Android H.265等 custom C++，以及 15 个明确复用的原生 Viewer3D C++ 文件，并向根目标导出 include、library、resource 和 translation 列表；创建只包含 Fuel 详情页的 `Custom.Widgets` 静态 QML 模块。桌面 `QGC_BUILD_TESTING` 构建还创建独立 `SiyiProtocolTest`，移动端不生成额外测试应用。它要求 Quick3D/Quick3DAssetUtils，检测可选 WebEngineQuick并定义Google 3D能力；翻译只导出 `custom_*.ts`，英文 `custom.ts`模板不编译。Android configure时把根 `android`模板复制到build目录，再用 `custom/android`同路径覆盖，校验USB custom标记并让Gradle只使用唯一合并源目录。外部WGS84样例目录不参与构建或安装。 |
| `custom/custom.qrc` | custom RCC运行时资源清单，共62个 `<file>`：2个 `/custom/img`图标、1个 `/Custom/qmlimages`图标、12个 `/Custom/qml`同路径覆盖/扩展QML、42个 `/qml/Viewer3D`模块资源、3个 `/json` Settings元数据和2个地形shader。本分支新增 `GimbalCameraControl.qml` 并继续显式注册 `GimbalZoomControl.qml`；Viewer3D的42项由22个custom资源和20个直接引用的原生资源组成。URL拦截器只处理 `/Custom/qml`覆盖；本文件只决定资源URL，不编译C++、不保存设置值。Fuel详情页由 `qt_add_qml_module`注册，翻译 `.qm`由CMake生成，外部WGS84样例不在本QRC中。 |
| `custom/cmake/CustomOverrides.cmake` | 根工程配置阶段读取的产品能力开关。固定 `QGC_APP_NAME=Custom-QGroundControl` 以保持应用标识和既有 QSettings 路径；关闭原生 Viewer3D后端，防止它与 custom Viewer3D 类和设置产生重复符号；关闭APM dialect/plugin/factory，并关闭原生PX4 Factory，让 custom Factory成为PX4固件插件的唯一创建入口。它只决定编译内容和插件选择，不在这里检查具体 `MAV_TYPE`。 |

### 4.2 CustomPlugin 与通信链路

| 文件 | 详细作用 |
|---|---|
| `custom/src/CustomPlugin.h` | custom 功能的中央组合入口声明。继承 `QGCCorePlugin`，向 QML 暴露稳定的 Viewer3D设置/外部模型管理器、FlyViewCustom设置、Gimbal设置/控制器属性；声明 `init/cleanup` 生命周期、Android字号 metadata覆盖、MAVLink相机流消息过滤、QML engine创建和视频sink创建覆盖。文件末尾的 `CustomOverrideInterceptor` 声明负责把原生 QRC URL重定向到实际存在的 `/Custom/qml` 文件；本头文件只定义接口与所有权，不实现各模块算法。 |
| `custom/src/CustomPlugin.cc` | 上述中央入口的实现。`init()` 在 LinkManager加载前安装默认UDP链路，安装 custom翻译，创建并持有 Viewer3D/FlyView/Gimbal设置和管理器，注册 Viewer3D QML类型，在 GStreamer已初始化而 `decodebin3` 尚未建管线时应用 Android H.265策略，并安装 A8 Mini视频缺省值；`createVideoSink()`先完整复用原生sink创建，再仅为主拉流安装真实首帧CAPS探针，并观察主 `GstGLQt6VideoItem` 由最终 `GstVideoInfo` 写入的隐式尺寸。两个来源都直接排队投递同一个Manager，受支持尺寸确认或变化后自动启动0x18同步；安装时会记录receiver、widget类型/名称、初始隐式尺寸及两个观察器是否成功，避免再次静默停在unknown；`adjustSettingMetaData()` 仅在 Android且根 `appFontPointSize` 尚无保存值时提供12 pt缺省；`mavlinkMessage()`按自动视频流开关过滤相机流信息；`createQmlApplicationEngine()`安装 URL拦截器，仅当对应 custom资源存在时覆盖原生QML。所有设置对象以本插件为父对象并通过Q_PROPERTY供QML复用。 |
| `custom/src/Comms/DefaultCommunicationLinkInstaller.h` | 声明无状态的 `DefaultCommunicationLinkInstaller::ensureInstalled()` 静态接口。调用者只有 `CustomPlugin::init()`；头文件不创建或连接链路，目的是把“写入项目缺省通信配置”与 CustomPlugin生命周期代码分离。 |
| `custom/src/Comms/DefaultCommunicationLinkInstaller.cc` | `ensureInstalled()` 的幂等实现。在原生 LinkManager读取 `LinkConfigurations` 前遍历已有配置，名称以不区分大小写方式匹配 `local` 即立即返回；不存在时按原生 QSettings结构追加一个 UDP配置：本地端口0、单一远端 `192.168.144.20:19856`、不自动连接、非高延迟，并更新count和同步磁盘。它只安装可编辑的配置，不直接打开socket；后续加载、连接、修改和删除仍由原生 LinkManager负责，日志类别为 `gcs.custom.communicationlink`。 |

#### 4.2.1 Android USB 串口管理器

| 文件 | 详细作用 |
|---|---|
| `custom/android/src/org/mavlink/qgroundcontrol/QGCUsbSerialManager.java` | Android USB串口生命周期的同名overlay实现，保持 `org.mavlink.qgroundcontrol` 包名、JNI类名和全部public static签名；CMake只把它覆盖到构建目录，不修改根 `android`。`QGCActivity`调用initialize/cleanup，Qt AndroidSerial/QSerialPortInfo经JNI调用枚举、open/close、读写和串口参数，Java listener再把数据/异常回调Qt。状态分为发现态 `drivers`、打开态 `deviceResourcesMap`、权限请求时间和本次attach拒绝集合，并由同一锁串行化；普通close只停I/O、关闭port/fd、失效listener并保留driver，所以不拔线可重开，任何重新扫描确认设备消失、detach或cleanup都会释放并移除陈旧状态。扫描先用默认prober，未匹配时仅对同时具备CDC COMM/ACM和CDC_DATA接口的设备创建保守 `CdcAcmSerialDriver`；只向Qt报告已匹配、有权限且至少有一个port的设备，当前每设备只打开 `ports.get(0)`。权限请求15秒内去重，明确拒绝后当前attach会话不再弹，detach/cleanup清除；打开失败的每一步都走幂等回滚，不强制中间9600波特率，真实参数由Qt随后下发。custom故意不在receiver线程直接用raw Qt指针通知断开，而让Qt工作线程通过端口列表消失完成close。日志标签 `QGCUsbSerial-Custom` 能证明overlay和定位枚举/权限/open状态；Java成功打开后仍必须经过USBBoardInfo/AutoConnect、SerialLink和MAVLink heartbeat才会出现Vehicle，飞控绿灯只表示VBUS供电。 |

### 4.3 PX4 FirmwarePlugin 与 AutoPilotPlugin

| 文件 | 详细作用 |
|---|---|
| `custom/src/FirmwarePlugin/CustomFirmwarePluginFactory.h` | 声明 `FirmwarePluginFactory` 子类及其全局注册实例，只向 QGC报告 `FirmwareClassPX4 + VehicleClassMultiRotor` 支持范围，并保存一个 `CustomFirmwarePlugin` 单例指针。该 Factory 是 HEARTBEAT识别到飞控后选择项目固件行为的入口，不处理具体飞行模式或UI。 |
| `custom/src/FirmwarePlugin/CustomFirmwarePluginFactory.cc` | 实现并在静态初始化阶段创建 `CustomFirmwarePluginFactoryImp`，使QGC Factory注册机制能发现它；`firmwarePluginForAutopilot()` 只对 `MAV_AUTOPILOT_PX4` 延迟创建并返回同一个 `CustomFirmwarePlugin`，其他autopilot返回空。该函数当前 `Q_UNUSED(vehicleType)`，因此能力列表虽只声明MultiRotor，运行选择阶段并不会拒绝其他PX4 `MAV_TYPE`；若产品必须强制仅多旋翼，需要在本函数增加vehicleType判断。 |
| `custom/src/FirmwarePlugin/CustomFirmwarePlugin.h` | 声明 `PX4FirmwarePlugin` 的项目行为覆盖接口：为每辆 Vehicle创建哪个 AutoPilotPlugin、顶部车辆指示器列表、云台轴能力和动态飞行模式属性。成员 `_toolIndicatorList` 缓存定制后的QML URL列表，避免每次查询重复构造。 |
| `custom/src/FirmwarePlugin/CustomFirmwarePlugin.cc` | 实现 PX4车辆级定制。为车辆创建 `CustomAutoPilotPlugin`；从原生工具栏列表移除 RC RSSI，并把 custom Fuel 指示器稳定插入 Battery 后；`hasGimbal()`静态声明仅 pitch/yaw可用，但不检测思翼设备、UDP SDK连接或云台响应；构造及 `updateAvailableFlightModes()`重新标注机型适用性，并只让 Loiter、RTL、Mission 保持 `canBeSet=true`。它不发送模式切换命令，而是限制QGC向用户公开的可选模式。 |
| `custom/src/AutoPilotPlugin/CustomAutoPilotPlugin.h` | 声明 `PX4AutoPilotPlugin` 子类，覆盖 `vehicleComponents()` 返回车辆 Setup 页面模型，并提供高级模式变化槽；`_components` 缓存当前页面对象。该层控制“车辆设置页面有哪些”，不控制飞行界面工具条或实际PX4参数值。 |
| `custom/src/AutoPilotPlugin/CustomAutoPilotPlugin.cc` | 在参数准备完成后按需创建 Setup组件并调用各组件 `setupTriggerSignals()`：普通模式只创建 Safety；高级模式依次创建 Airframe、Sensors、Radio、Flight Modes、Power、Motors、Safety、Tuning。监听 `showAdvancedUIChanged` 后清空缓存并发出 `vehicleComponentsChanged()`，使UI立即重建；参数未就绪或版本错误时不生成页面。 |

### 4.4 FlightDisplay QML 与图像资源

| 文件 | 详细作用 |
|---|---|
| `custom/src/FlightDisplay/FlyViewCompassBar.qml` | 罗盘条本体和绘制算法。直接读取活动飞行器 `Vehicle.heading.rawValue`，验证并归一化到 `[0°, 360°)`，使用中心附近 11 个 45°相对标签计算 N/NE/E/SE/S/SW/W/NW 的横向位置，中央显示四舍五入的整数航向并用 `compassPointer.svg` 绘制固定指针。它不读取显示开关、不保存设置、不判断是否应被加载；外层 `FlyViewCustomLayer.qml` 负责生命周期和显示条件。组件没有鼠标拦截层，因此不会吞掉下方地图手势。 |
| `custom/src/FlightDisplay/FlyViewCustomLayer.qml` | Fly View custom overlay 的编排层，同时管理罗盘条与燃料电池母线告警。它从 `corePlugin.flyViewCustomSettings.showHeadingCompassBar` 读取用户意愿，再结合 overlay 可见、活动飞行器存在和 heading 有效四个条件，通过明确 QRC URL加载 `FlyViewCompassBar.qml`；罗盘条采用组件的 `implicitWidth`（与示例相同为 `50 × defaultFontPixelWidth`）并保持屏幕水平居中，只按 Fly View 总宽度和基础 margin 做最终屏幕边界钳制，不再使用 PIP/摇杆/仪表的角落 inset 压缩宽度。它把“罗盘条高度+底边 margin”的完整占用深度合并进 `bottomEdgeCenterInset`，关闭时透传原生 inset。同一文件还监听 `vehicle.generator.busVoltage`，低于20.0 V置告警、超过20.4 V清除，形成回差；`mapControl` 当前只是兼容接口，未参与逻辑。 |
| `custom/src/FlightDisplay/FlyViewToolStripActionList.qml` | Fly View 左侧工具条动作模型的同路径覆盖。保留检查单、起飞、降落、返航、暂停、附加动作和夹爪的原生顺序，在最前面新增仅当 `viewer3DSettings.enabled=true` 才可见的 2D/3D 切换动作；动作调用现有 `viewer3DWindow.open()/close()`，打开 3D 时用 PaperPlane 表示返回 Fly，关闭时用 custom 城市图标表示进入 3D。 |
| `custom/src/FlightDisplay/FlyViewTopRightColumnLayout.qml` | Fly View右侧中部控件容器的同路径覆盖。始终保留 `TerrainProgress`；Gimbal启用时不检查 `globals.activeVehicle`，而是始终实例化并显示明确QRC地址的 `GimbalCameraControl.qml`。`sdkResponding`不决定Loader尺寸或控件可见性；缩放仍需拉流能力及倍率已确认，录像需状态已知，拍照允许离线尝试。只有Gimbal关闭且存在活动飞行器时才加载原生 `PhotoVideoControl`。启用时ColumnLayout宽度取原生 `_rightPanelWidth` 与合并栏隐式宽度的较大值，Loader同时把最小尺寸固定为隐式尺寸并右对齐，86%字号或移动端最小触控尺寸不会把控制栏强制压缩。 |
| `custom/src/FlightDisplay/GeneratorBusVoltageAlert.qml` | Fly View燃料电池母线低压提示本体。读取传入Vehicle的 `generator.busVoltage`，低于20.0 V显示告警、严格高于20.4 V清除，NaN或无Fact时隐藏；双阈值回差避免临界电压反复闪烁。它只绘制告警，加载位置和活动飞行器生命周期由 `FlyViewCustomLayer.qml` 管理。 |
| `custom/src/FlightDisplay/GimbalCameraControl.qml` | Gimbal启用后始终显示的合并相机控制栏，不依赖飞控、活动Vehicle或SDK在线状态决定可见性。单个半透明圆角面板使用ColumnLayout纵向排列 `GimbalZoomControl`、横向分隔线、拍照和录像按钮，完整高度由子项隐式高度与间距计算；图标直接复用QGC的 `camera_photo.svg` 和 `camera_video.svg`，但命令调用 `GimbalControlManager::takePhoto()/toggleVideoRecording()`，不依赖原生MAVLink相机对象。SDK离线时面板切换为灰色边框和灰色状态点，拍照按钮仍允许发送；录像状态未知或切换命令pending时按钮灰显并显示省略号，只有取得0x0a状态后才显示REC或本次会话计时并允许切换，因为录像命令是toggle语义。照片0x0b成功反馈触发绿色闪烁。该栏不实例化原生 `PhotoVideoControl`，因此不存在原生缩放滑块。 |
| `custom/src/FlightDisplay/GimbalZoomControl.qml` | 合并栏顶部的思翼缩放子控件，使用单列GridLayout从上到下显示加号、当前倍率和减号，并把完整列高传给外层布局。从Manager读取 `currentZoom/zoomStatusKnown/sdkResponding/zoomInAvailable/zoomOutAvailable`；离线、倍率未知或命令pending时显示 `--`并禁用缩放，已到真实边界且该方向没有下一目标时仍显示真实倍率，只禁用该方向。两侧MouseArea都使用Idle/Pressed/Holding/Consumed手势状态：短按只调用一次目标接口，长按启动串行目标重复；中间目标使用完整步长，最后一次允许吸附小数上限，释放只取消后续步骤且不补发短按。后台2秒探测已移到Manager。 |
| `custom/src/FlightMap/Images/compassPointer.svg` | 罗盘条中央固定三角指针的纯矢量资源，不含角度或交互逻辑。按原生 `src/FlightMap/Images` 资源分类保存，由 `custom.qrc` 注册为 `qrc:/custom/img/compassPointer.svg`，`FlyViewCompassBar.qml` 通过 `QGCColoredImage` 加载并按当前主题文本颜色着色。 |

未在 custom 保存 `FlyView.qml`、`FlyViewWidgetLayer.qml` 和 `FlyViewToolStrip.qml`，因为当前 `src` 已经具备 Viewer3D 容器、地图交互禁用、比例尺隐藏和工具栏装载逻辑。

### 4.5 Gimbal 后端

| 文件 | 详细作用 |
|---|---|
| `custom/src/Gimbal/A8MiniZoomPolicy.h` | A8 Mini缩放策略的纯静态接口，将视频尺寸到最大倍率映射、固定网格和真实边界吸附计算从Manager状态机中分离，便于独立测试；不访问网络、QSettings或UI。实际拉流尺寸来源优先级、稳定兜底和运行期能力更新由Manager强制执行。 |
| `custom/src/Gimbal/A8MiniZoomPolicy.cc` | 显式映射A8 Mini拉流规格：1280×720/736→6.0x、1920×1080/1088→5.5x、2560×1440→3.5x、3840/4096×2160→1.0x（4K不可继续缩放），其他尺寸返回不支持并由Manager的0x16后备处理。倍率网格以1.0x为固定锚点并使用十分之一整数计算：中间目标严格相差完整 `zoomStep`；若下一完整步会越过小数上限，则唯一一次吸附到真实上限；从该上限缩小时先回到最高合法网格点。 |
| `custom/src/Gimbal/GimbalControl.SettingsGroup.json` | `GimbalControl` 设置组的元数据源，不执行任何云台或视频操作。定义6个Fact及约束：`enabled=true`（短描述为启用思翼云台相机控制）、数值IP `sdkHost=192.168.144.25`、`sdkPort=37260`（1-65535）、`zoomStep=1.0x`（0.1-4.5）、`mavlinkAutoVideoStream=false`、`forceAndroidH265HardwareDecoder=true`；后两项标记需重启。`zoomStep`说明明确中间移动使用完整分度，最后一次可吸附小数上限且反向时先回到相邻网格点；步长1.0x可通过5.0↔5.5和3.0↔3.5到达1080P/2K真实上限。`SettingsFact` 将用户值自动保存为 `[GimbalControl]/同名键`；硬解开关虽在所有平台存在，策略只在Android+GStreamer路径执行。 |
| `custom/src/Gimbal/GimbalControlSettings.h` | 声明 `GimbalControlSettings : SettingsGroup`，用6个 `DEFINE_SETTINGFACT` 生成惰性创建的 `Fact*` Q_PROPERTY。它是JSON/QSettings与QML、Manager之间的设置入口，保证属性名称稳定；不创建UDP socket、不发送云台命令，也不选择视频解码器。 |
| `custom/src/Gimbal/GimbalControlSettings.cc` | 通过 `DECLARE_SETTINGGROUP(GimbalControl, "GimbalControl")` 同时确定元数据资源 `:/json/GimbalControl.SettingsGroup.json` 和QSettings分组；实现6个Fact getter，首次访问时创建 `SettingsFact`并自动读取已有值或JSON缺省值；把类型以 reference-only 方式注册到 `QGroundControl.GimbalControl`，实际对象由 `CustomPlugin` 创建而不是由QML new。 |
| `custom/src/Gimbal/GimbalControlManager.h` | 思翼云台相机业务的QML门面和运行态声明。`currentZoom`只表示设备确认值，另向QML暴露按固定网格及真实边界转换计算的 `zoomInAvailable/zoomOutAvailable`。能力状态保存当前已确认拉流能力、直接观察尺寸、`VideoManager`兜底候选及1秒稳定Timer；倍率状态分别保存唯一绝对目标、目标命中次数、稳定值候选、查询在途门禁、350 ms隔离Timer、1秒单次查询Timer和10秒操作Timer。默认5.5x仅是能力未知时的界面占位，不能用于发送缩放命令。 |
| `custom/src/Gimbal/GimbalControlManager.cc` | 由 `CustomPlugin`创建一次并持有 `SiyiSdk`，没有修改或复制 `src/VideoManager`。GStreamer构建按“主显示sink直接尺寸→稳定1秒的VideoManager尺寸→设备0x16当前支持范围”三级恢复能力；取得受支持的实际拉流尺寸后取代0x16后备，后续受支持尺寸变化可更新上限，不支持的直接尺寸则释放旧映射并恢复0x16查询。能力恢复或上限改变会安全取消旧目标并主动查询0x18；启动、重连、外部倍率变化及每个用户0x0f目标均要求两次0x18一致，避免把迟到值或命令目标直接显示。单击和长按共用串行0x0f目标路径，一个目标在途时拒绝追加操作；状态机保证同一时刻最多一个0x18查询，超时经隔离后重试。 |
| `custom/src/Gimbal/GimbalVideoStreamSupport.h` | 声明两个无对象状态的启动期适配接口：安装A8 Mini视频缺省设置，以及判断一条MAVLink消息是否应被过滤。它不创建 VideoReceiver、不连接RTSP，也不参与H.265解码。 |
| `custom/src/Gimbal/GimbalVideoStreamSupport.cc` | `CustomPlugin::init()` 每次启动调用的幂等迁移和消息策略实现。版本键 `[GimbalControl]/a8MiniVideoDefaultsVersion=4` 控制迁移：只在URL为空或旧拼写时设置A8 RTSP地址，只在A8 URL且timeout过小时提升到20秒，只在视频源为空/Disabled/No Video时选RTSP；Android仅在用户从未保存 `[Video]/lowLatencyMode` 且URL匹配时写true，已有选择不覆盖。过滤逻辑仅针对 `VIDEO_STREAM_INFORMATION`：Gimbal开启且 `mavlinkAutoVideoStream=false` 时阻止它进入原生自动视频配置，其余消息放行。 |
| `custom/src/Gimbal/SiyiProtocol.h` | 思翼私有协议的纯静态编解码接口，覆盖0x05/0x0a/0x0b/0x0c/0x0f/0x16/0x18及ACK帧判定；不继承QObject，不访问网络、QSettings或UI。 |
| `custom/src/Gimbal/SiyiProtocol.cc` | 实现 `55 66`帧头、control 0x01、小端payload长度、固定0的seq、command、payload和小端CRC16（多项式0x1021、初值0）。整帧长度必须与payload声明精确相等，尾随字节也会拒绝；入站业务ACK必须严格为control 0x02。0x05 ACK按小端uint16/10解析；0x16/0x18逐包先按官方“整数字节+一位小数字节”解析，只有官方候选超出当前A8有效范围时，才计算完整小端uint16/10兼容候选并再次校验范围，不钳制异常值。`01 00`/`01 08`保持1.0x/1.8x，`0a 00`/`12 00`/`1c 00`/`37 00`分别兼容1.0x/1.8x/2.8x/5.5x；A8物理范围不超过6.0x，因此两种合法表示没有歧义。解析接口同时返回本包是否采用兼容格式，0x0f ACK只接受单字节0/1。 |
| `custom/src/Gimbal/SiyiSdk.h` | `QUdpSocket`传输层接口声明，保留0x05兼容接口，并提供0x16当前支持范围、0x0f绝对倍率、0x18当前倍率、拍照、录像和状态查询；它不负责拉流尺寸、步长网格、手势状态或设置持久化。Manager不调用0x05。 |
| `custom/src/Gimbal/SiyiSdk.cc` | 将Protocol帧通过 `QUdpSocket::writeDatagram()`发到配置endpoint；接收侧按逻辑IP、精确帧长、CRC和control 0x02过滤，并且只在对应业务payload解析成功后发出 `packetReceived`。`gcs.custom.gimbal.sdk`记录每个收发命令、原始payload、解出的倍率及本包采用的 `integer plus decimal`或 `little-endian tenths compatibility`编码；0x0f受理反馈、0x16能力和0x18实际倍率仍按命令职责分别分发。空包、短写或无效endpoint通过 `communicationError`交给Manager。 |
| `custom/test/Gimbal/SiyiProtocolTest.cc` | custom独立QtTest协议与策略回归用例。覆盖封包量化、ACK control、坏CRC、严格整帧长度、0x05/0x0f/0x16/0x18新版与真机旧版倍率payload、越界和非法载荷、720P/1080P/2K/4K及coded-height映射、固定网格对齐，以及短按/长按共用的中间完整步长和双向小数上限吸附序列。测试只在桌面 `QGC_BUILD_TESTING`注册，并提供 `check_siyi_protocol`独立目标。 |

云台相机完整调用链为：`VideoReceiver`解码当前视频 -> 真实首帧CAPS或最终 `GstVideoInfo` 隐式尺寸 -> 稳定1秒的 `VideoManager::videoSize` -> 若尺寸链仍未确认则0x16当前支持范围 -> `GimbalControlManager`激活能力并查询0x18 -> 两份一致合法实际倍率恢复显示 -> `A8MiniZoomPolicy`计算固定网格和真实边界 -> 单击/长按发送串行0x0f目标 -> 每个用户目标由两次0x18命中后发布。

### 4.6 GStreamer 拉流分辨率与 Android 视频解码策略

| 文件 | 详细作用 |
|---|---|
| `custom/src/VideoManager/VideoReceiver/GStreamer/PulledVideoResolutionProbe.h` | 声明无QObject状态的主拉流协商尺寸探针安装接口及 `ResolutionHandler` 回调。由 `CustomPlugin::createVideoSink()` 调用；非GStreamer构建、空sink、非 `VideoReceiver` parent或thermal receiver返回false且不改变原生视频路径。回调由GStreamer流线程触发，调用方必须排队切回Manager线程。 |
| `custom/src/VideoManager/VideoReceiver/GStreamer/PulledVideoResolutionProbe.cc` | 只在 `QGC_GST_STREAMING` 下对主视频 `qgcvideosinkbin` 的 `sink` ghost pad安装downstream CAPS、BUFFER和BUFFER_LIST探针。只有真实帧到达才发布尺寸；宽高直接读取CAPS structure，不把成功条件绑死在完整format的 `gst_video_info_from_caps()`。若外层ghost pad没有current CAPS，则继续读取已连接解码器peer和ghost target的current CAPS，覆盖不同平台的caps存放差异。得到正宽高后既通过回调直达Manager，也经既有 `VideoReceiver::videoSizeChanged` 修正 `VideoManager`，从而覆盖原生 `GstVideoReceiver::_addVideoSink()` 在管线刚拼接时用 `gst_pad_query_caps()` 得到的暂态/无效值；该原生信号同时可触发Manager受控的1秒稳定兜底。不修改 `src`、不猜测卡录分辨率，也不影响thermal流。首个真实帧仍无法取得宽高时会明确告警。 |
| `custom/src/VideoManager/VideoReceiver/GStreamer/AndroidH265HardwareDecoderAdapter.h` | 声明进程级 custom H.265 decoder factory接口：固定factory名、注册函数、已选内部厂商MediaCodec factory查询，以及policy与adapter共用的厂商名称过滤函数。它只是适配器注册API，不实现H.265算法，也不调用Android `MediaCodecInfo.isHardwareAccelerated()`做系统级硬件认证。 |
| `custom/src/VideoManager/VideoReceiver/GStreamer/AndroidH265HardwareDecoderAdapter.cc` | 在启动阶段枚举能接受 `video/x-h265,stream-format=byte-stream,alignment=au` 的 `amcviddec-*`，排除secure、software/FFmpeg、`*.sw.dec`、Qualcomm `*swvdec`、OMX Google及C2 Android/Google/Goldfish，优先名称含low-latency变体的组件，再按原rank/名称排序；预检只证明元素可创建、静态链可链接且bin能进READY，不证明真实profile/level已解码。选中后缓存厂商factory并以rank `PRIMARY+100=356` 注册 `qgcandroidh265hwdec`。每个实例内部为：外部hvc1 ghost sink -> `h265parse(config-interval=-1)` -> Annex-B byte-stream/AU capsfilter -> 厂商MediaCodec -> downstream-leaky raw queue（最多2 buffer）-> `video/x-raw(ANY)` ghost src；probe记录真实输入caps和首个raw buffer的caps/PTS/bytes/GLMemory。首帧日志只证明经过名称筛选的factory已产出raw frame，不等同Android API硬件认证，也不保证画面已到QML sink。 |
| `custom/src/VideoManager/VideoReceiver/GStreamer/AndroidVideoDecoderPolicy.h` | 声明一次性的Android H.265 factory rank/适配策略入口。正确调用窗口是 `VideoManager`构造已完成 `GStreamer::initialize()` 之后、`VideoManager::init()` 创建VideoReceiver和 `decodebin3`之前；过早无法枚举插件，过晚则已建管线不会重新选择decoder。 |
| `custom/src/VideoManager/VideoReceiver/GStreamer/AndroidVideoDecoderPolicy.cc` | `CustomPlugin::init()` 从 `forceAndroidH265HardwareDecoder` 读取一次并调用的策略实现，因此开关要求重启。仅Android+`QGC_GST_STREAMING`且GStreamer已初始化时生效：先尝试注册上述hvc1适配器，再枚举H.265 decoder并按静态sink caps判断hvc1/byte-stream兼容；适配器至少rank356，经过厂商名称筛选且直接接受hvc1的MediaCodec至少提升到 `PRIMARY+2=258`，高于原生Force Software将 `avdec_h265`设成的257。其他软件factory rank不删除也不置0，只保留回退资格，不能承诺所有真机运行期协商失败都一定无黑屏回退。逐候选日志记录分类、caps兼容和rank变化；H.264及非Android不受影响。 |

Android H.265选择链为：`CustomPlugin::init()`读取重启后生效的Fact -> `AndroidVideoDecoderPolicy` 在管线创建前调整factory候选/rank -> `decodebin3`依据caps与rank选择decoder。直接兼容hvc1的厂商MediaCodec可直接入选；只兼容Annex-B的厂商decoder由 `qgcandroidh265hwdec` 包装，实际数据路径为 `hvc1 -> h265parse -> video/x-h265,stream-format=byte-stream,alignment=au -> 厂商MediaCodec -> 最多2帧的leaky raw队列 -> video/x-raw -> 原生QGC显示链`。rank只能影响选择优先级；确认运行链路必须看实际factory和首个raw buffer日志，不能只看READY预检。

### 4.7 Application Settings、通用默认值、Fly View custom Settings、Fuel 和 qmldir

General -> UI Scaling 使用 custom 同路径覆盖页，但仍绑定原生整数 Fact；Android 12 pt 缺省值由 4.2 节的 `CustomPlugin` metadata hook 在 Fact 创建时提供。页面不负责写入缺省值，也不新增 SettingsGroup 或 JSON，避免只有打开 General 页面后设置才生效。

| 文件 | 详细作用 |
|---|---|
| `custom/src/QmlControls/FuelStatusIndicatorPage.qml` | Fuel 顶部指示器点击后创建的详情页。输入为活动飞行器 `fuelStatus` Fact，按燃料类型选择 ml 或 MPa，显示剩余比例、剩余量、最大量、已消耗量、流量和温度；它只负责详情展示，不决定工具栏图标是否出现。该类型由精简的 `Custom.Widgets` QML 模块注册，创建入口在 `FuelStatusIndicator.qml`。 |
| `custom/src/QmlControls/Viewer3D/Models3D/qmldir` | 声明 `Viewer3D.Models3D` QML 模块，并把 `CameraLightModel`、`Line3D`、`External3DMap`、`Viewer3DModel`、`Viewer3DVehicleItems`、`Waypoint3DModel` 六个类型映射到对应 QML。`CameraLightModel`、`Line3D`、`Waypoint3DModel` 继续由 QRC 引用原生源码，另外三个带项目差异的场景类型映射到 custom 文件。它只解决 `import Viewer3D.Models3D` 后的类型发现，不创建场景、不加载模型，也不保存设置；`QGroundControl.Viewer3D` 是 C++ 类型模块，不能与本模块名混用。 |
| `custom/src/Settings/FlyViewCustom.SettingsGroup.json` | 只定义航向罗盘条显示开关 `showHeadingCompassBar` 的 Fact 元数据：类型为 bool、缺省为 `true`、无需重启。它不绘制罗盘条，也不保存当前用户值；`FlyViewCustomSettings.cc` 根据资源名加载它，实际选择保存为 `FlyView/showHeadingCompassBar`。 |
| `custom/src/Settings/FlyViewCustomSettings.h` | 声明 `FlyViewCustomSettings : SettingsGroup`，并通过 `DEFINE_SETTINGFACT(showHeadingCompassBar)` 生成稳定的 `Fact*` Q_PROPERTY、延迟创建指针和访问器。该类是 C++/QML 之间的设置接口层，只表达“用户是否允许显示罗盘条”，不包含航向计算或绘制代码。 |
| `custom/src/Settings/FlyViewCustomSettings.cc` | 实现上述 SettingsGroup：`DECLARE_SETTINGGROUP(FlyViewCustom, "FlyView")` 使用独立元数据 `:/json/FlyViewCustom.SettingsGroup.json`，但把用户值写入原生 `FlyView` QSettings 分组；注册 reference-only QML 类型并实现 `showHeadingCompassBar()` 的延迟 Fact 创建。实例由 `CustomPlugin` 创建并暴露为 `QGroundControl.corePlugin.flyViewCustomSettings`。 |
| `custom/src/UI/AppSettings/GeneralSettings.qml` | Application Settings -> General 的同路径 custom 覆盖页。完整保留原生 Language、Color Scheme、GCS位置流、音频、Android SD Card、清除设置、数据路径、Units和Brand Image。UI Scaling直接绑定原生整数 `appFontPointSize`，按 `appFontPointSize / ScreenTools.platformFontPointSize × 100` 四舍五入显示，`-`/`+` 每次修改1 pt并由原生 `SettingsFact` 保存；页面本身不写缺省值。`SettingsFact` 构造期间先调用 `CustomPlugin::adjustSettingMetaData()` 把 Android raw default改为12 pt，再读取已有QSettings或该缺省值，因此未打开本页面也会生效；非Android默认仍为100%。 |
| `custom/src/UI/AppSettings/FlyViewSettings.qml` | Application Settings -> Fly View 的同路径覆盖页。保留全部原生 Fly View 设置，从 `corePlugin.flyViewCustomSettings.showHeadingCompassBar` 取得 Fact，在 Instrument Panel 中用 `FactCheckBoxSlider` 提供“显示航向罗盘条”开关；切换会由 `SettingsFact` 自动持久化并被 `FlyViewCustomLayer.qml` 立即观察。页面底部还装载 Viewer3D 与思翼云台设置组，本文件不绘制罗盘条。三个处于原生 `SettingsPage/ColumnLayout` 中的Loader都把已加载项的 `implicitHeight` 显式提供给 `Layout.preferredHeight/minimumHeight`，避免零高槽位造成内容溢出、顶部裁切、分组拉伸及滚动范围不足；不再手工反复写 `item.width`。 |
| `custom/src/UI/AppSettings/VideoSettings.qml` | Application Settings -> Video 的同路径覆盖页。保留原生 Video Source、Connection、播放设置和录像存储组，再读取 `GimbalControlSettings` 中的两个视频集成 Fact，增加 `Use MAVLink automatic video stream` 与 `Force Android H.265 hardware decoder` 开关及重启提示；自动流开关只有 Gimbal Enabled 时允许操作，硬解开关在所有平台可见但仅 Android策略使用。本页只改Fact，真正的MAVLink消息过滤由 `GimbalVideoStreamSupport` 执行，解码器选择由 `AndroidVideoDecoderPolicy` 执行。 |
| `custom/src/UI/AppSettings/Viewer3DSettingsGroup.qml` | 由 `FlyViewSettings.qml` 显式加载的 Viewer3D 设置面板。根Loader将内容的 `implicitWidth/implicitHeight` 向外透传，供设置页正确计算分组高度和滚动范围；不再用 `onLoaded/onWidthChanged` 手工写子项宽度。只有设置对象及14个所需Fact可用时才创建内容；Google与外部模型两个开关相互排斥，两者都关闭时隐式进入本地OSM模式。页面编辑API Key、外部模型文件、WGS84原点、单位换算、比例、yaw、OSM路径、建筑层高和车辆高度偏移；外部文件选择交给 `External3DMapManager.importModelFile()` 检查/转换并返回状态，本文件不创建或渲染三维场景。页面的 Clear只修改保存的路径值，不删除磁盘文件。 |
| `custom/src/UI/AppSettings/GimbalControlSettingsGroup.qml` | `SIYI Gimbal Camera`私有UDP设置面板。根Loader将内容隐式尺寸向外透传；标题说明保持简短，详细的步长、边界吸附和拉流分辨率规则集中写在本开发说明及Fact元数据中，避免长说明参与 `SettingsPage` 隐式宽度计算而把整页横向撑开。绑定 `enabled`、`sdkHost`、`sdkPort`和 `zoomStep`。 |
| `custom/src/UI/toolbar/FuelStatusIndicator.qml` | `CustomFirmwarePlugin::toolIndicators()` 插入 Battery 后的顶部工具栏组件。监听活动飞行器 `fuelStatus.telemetryAvailable`，无 `FUEL_STATUS` 数据时隐藏且不占可见空间，有数据时显示 `FuelIcon.svg` 与剩余百分比；点击后通过主窗口弹出 `FuelStatusIndicatorPage.qml`。它不生成 Fuel 遥测，也不负责母线低电压告警。 |
| `custom/src/UI/toolbar/Images/FuelIcon.svg` | Fuel 顶部指示器使用的气瓶矢量图形，只提供可缩放轮廓，不包含状态逻辑；由 `custom.qrc` 注册为 `qrc:/custom/img/FuelIcon.svg`，`FuelStatusIndicator.qml` 根据主题对其着色和显示。 |

航向罗盘条的文件连接关系固定为：`FlyViewCustom.SettingsGroup.json` 定义开关元数据 -> `FlyViewCustomSettings.h/.cc` 创建、读取、保存并向 QML 暴露开关 -> `FlyViewSettings.qml` 提供用户开关 -> `FlyViewCustomLayer.qml` 将开关与活动飞行器/有效航向组合成最终显示条件 -> `FlyViewCompassBar.qml` 只负责绘制。任何一个文件都不能单独完成完整功能。

### 4.8 Viewer3D C++ 扩展

| 文件 | 详细作用 |
|---|---|
| `custom/src/Viewer3D/Viewer3D.SettingsGroup.json` | 只定义 14 个 Viewer3D Fact 的元数据：总开关、Google 地图及 API Key、外部模型路径与 WGS84 原点、单位到米换算、附加比例、yaw、本地 OSM 路径、建筑层高和车辆高度偏移。它提供类型、单位、说明和缺省值，不保存用户当前值、不加载地图，也不创建渲染对象；`Viewer3DSettings.cc` 通过资源 `:/json/Viewer3D.SettingsGroup.json` 使用它。 |
| `custom/src/Viewer3D/Viewer3DSettings.h` | 声明 `Viewer3DSettings : SettingsGroup` 及上述 14 个 `Fact*` 访问器。它是 `CustomPlugin`、Application Settings QML、坐标后端和场景 QML 共享的稳定设置接口，只声明属性，不包含 JSON 缺省值、文件导入或渲染逻辑。 |
| `custom/src/Viewer3D/Viewer3DSettings.cc` | 通过 `DECLARE_SETTINGGROUP(Viewer3D, "Viewer3D")` 把 14 个用户值读写到 `Viewer3D` QSettings 分组，实现所有 Fact 的延迟创建，并把 `Viewer3DSettings` 以 reference-only 类型注册到 `QGroundControl.Viewer3D`。实际实例由 `CustomPlugin` 创建，QML 通过 `corePlugin.viewer3DSettings` 访问，不能在 QML 中自行 new。 |
| `custom/src/Viewer3D/CustomViewer3DManager.h` | 声明 QML 可创建的 Viewer3D 运行时管理对象，向场景暴露一个 `OsmParser` 和一个 `Viewer3DQmlBackend`，并声明统一的 C++/QML 类型注册入口。类名增加 `Custom` 是为避免与原生 C++ `Viewer3DManager` 冲突；对 QML 暴露时仍使用兼容名称。 |
| `custom/src/Viewer3D/CustomViewer3DManager.cc` | 构造时创建 `Viewer3DQmlBackend` 和 `OsmParser`，再调用 backend `init()` 连接 OSM/车辆/设置参考点链路；注册 `Viewer3DQmlBackend`、`OsmParser`、`GeoCoordinateType`、`CityMapGeometry`、`Viewer3DTerrainGeometry`、`Viewer3DTerrainTexture`，并把本类以 `Viewer3DManager` 注册到 `QGroundControl.Viewer3D`，从而不修改现有场景 QML 的类型名。 |
| `custom/src/Viewer3D/CityMapGeometry.cc` | 监听 custom `osmFilePath`，在路径或 parser 变化时清空旧几何并让 `OsmParser` 解析文件；收到地图或建筑层高变化后取出建筑三角形顶点，写成仅含 position 的 `QQuick3DGeometry` triangle vertex buffer。它负责本地 OSM 建筑几何，不负责地形瓦片贴图、外部模型或 Google 地图。 |
| `custom/src/Viewer3D/OsmParser.cc` | 管理原生 `OsmParserThread` 的异步文件解析，读取 custom `buildingLevelHeight`，接收地图 GPS 参考点和建筑轮廓；对带内洞的建筑轮廓使用 earcut 三角剖分，再生成屋顶、地板和内外墙顶点。与原生版本的项目差异是设置来源改为 `CustomPlugin::viewer3DSettingsFactGroup()`；输出供 `CityMapGeometry` 和坐标 backend 使用。 |
| `custom/src/Viewer3D/Viewer3DTerrainGeometry.cc` | 根据瓦片 ROI、参考经纬度和行列数生成带 position、normal、UV 的 Quick3D 地形三角网，供地图瓦片纹理贴附；参考点变化时重建，OSM 路径变化时清空旧场景。它复用原生头文件和算法接口，但读取 custom Viewer3D 设置，不生成 OSM 建筑，也不参与外部模型加载。 |
| `custom/src/Viewer3D/Viewer3DQmlBackend.h` | 声明 QML 只读 `gpsRef`、内部参考点来源状态，以及活动飞行器、OSM parser、外部模型设置变化的处理接口。它维护的是 WGS84 到本地 ENU 的参考原点，不声明模型导入器、相机或渲染节点。 |
| `custom/src/Viewer3D/Viewer3DQmlBackend.cc` | 维护本地 Quick3D 场景的坐标参考点：外部模型模式优先固定使用用户填写的 WGS84 原点；关闭外部模式后优先恢复 OSM 提供的参考点，没有有效 OSM 时回退活动飞行器首次有效坐标。监听 Google/外部源开关和原点三项 Fact，变化后重新选择参考点；Google Web 地图使用独立 WebEngine 链路，不采用该本地 ENU 原点。 |
| `custom/src/Viewer3D/External3DMapManager.h` | 声明设置页调用的外部模型导入接口，暴露 `importing`、`lastImportStatus`、直接加载/需转换格式判断、Balsam 可执行文件查询和支持格式文本。它只定义文件选择与转换任务状态，不声明 Quick3D 模型或任何渲染对象。 |
| `custom/src/Viewer3D/External3DMapManager.cc` | 对 OBJ/glTF/GLB/QML 验证本地文件存在后，只把绝对路径写入 `external3DMapFilePath`；对 FBX/DAE/STL/PLY 启动 Qt Balsam，输出到应用数据目录 `Viewer3DExternalMaps/<名称>_<源路径哈希>`，找到生成 QML 后再写回设置。Balsam 按 `QGC_VIEWER3D_BALSAM`、应用目录、Qt binaries、PATH 查找，同一源重新导入前清理旧输出，并把启动/转换错误写入 `lastImportStatus`。本文件不直接加载或渲染模型，真正加载在 `External3DMap.qml`。 |
| `custom/src/Viewer3D/Images/city_3d_map_icon.svg` | 只提供白色分层地图/城市轮廓矢量图，注册为 `qrc:/Custom/qmlimages/Viewer3D/City3DMapIcon.svg`，由 `FlyViewToolStripActionList.qml` 在“进入 3D”动作上显示。文件没有 Enabled 状态、点击处理或场景切换逻辑。 |

### 4.9 Viewer3D custom QML

| 文件 | 详细作用 |
|---|---|
| `custom/src/Viewer3D/Viewer3DQml/Viewer3D.qml` | Viewer3D 生命周期和地图源路由根组件。`open()` 只在总开关开启时创建 `Viewer3DManager` 并加载视图；`close()` 隐藏视图但保留对象，再次打开可复用状态；用户关闭 Viewer3D Enabled 时才停用 manager Loader。Google 开启时选择 `Google3DMapView.qml` 或无 WebEngine 提示页，否则选择本地 `Viewer3DModel.qml`，并把同一个 manager 注入已加载场景。 |
| `custom/src/Viewer3D/Viewer3DQml/Google3DMapView.qml` | 使用 `WebEngineView` 加载运行时生成的 Google Maps JavaScript API HTML，并创建 `Map3DElement` Hybrid 三维地图。初始中心优先取活动飞行器，否则取 QGC 地图中心；API Key 或坐标无效时显示说明，并避免车辆每次位置更新都重新加载网页。该页面不使用本地 Quick3D backend，也不绘制本地 F450、任务点或任务航线。 |
| `custom/src/Viewer3D/Viewer3DQml/Google3DMapUnavailable.qml` | 仅在用户选择 Google 3D、但构建配置没有 `WebEngineQuick` 时由根 Loader 加载，显示静态依赖缺失说明。它不联网、不尝试调用 Google API，也不自动回退加载本地 OSM/外部模型。 |
| `custom/src/Viewer3D/Viewer3DQml/Models3D/External3DMap.qml` | 外部模型的实际 Quick3D 加载节点：把本地路径转换为 file URL，OBJ/glTF/GLB 交给 `RuntimeLoader`，Balsam 生成的 QML 交给 `Loader3D`；FBX/DAE/STL/PLY 原文件只提示先在设置页转换。按 `unitToMeters × userScale × 10` 对齐 QGC 场景尺度并绕 Z 轴应用 yaw；向 `Viewer3DModel.qml` 暴露缺文件、格式、加载错误和原点为 0,0 的状态文本，其中 0,0 只是配准警告，有效模型仍会继续渲染。 |
| `custom/src/Viewer3D/Viewer3DQml/Models3D/Viewer3DModel.qml` | 本地 Quick3D 根场景。创建相机、灯光及鼠标/触摸平移、旋转、缩放交互；根据设置在 OSM 建筑+瓦片地形与 `External3DMap` 之间切换，叠加影像下载进度或外部模型状态。它为每个飞行器加载车辆、任务点和任务航段，并在 backend `gpsRef` 改变时重置相机，确保场景局部坐标与新参考点一致。 |
| `custom/src/Viewer3D/Viewer3DQml/Models3D/Viewer3DVehicleItems.qml` | 为一架飞行器生成 F450 模型、可接受的 Waypoint/Takeoff/RTL/ROI 任务点和相邻任务航段。使用 `GeoCoordinateType` 将 WGS84 转为局部 ENU；外部模型模式用任务点 AMSL 海拔减模型原点海拔，OSM 模式沿用任务高度。任务列表、GPS 参考点或 Home 改变时清空并重建相应 ListModel。 |
| `custom/src/Viewer3D/Viewer3DQml/Drones/DroneModelDjiF450.qml` | F450 总装和遥测姿态组件。把车辆经纬度转换到局部 ENU，应用 heading/roll/pitch、位置与角度平滑动画；外部模型模式使用车辆 AMSL 减模型原点海拔完成垂直配准。组件组合 4 个机臂、4 个电机、上下板和 4 个螺旋桨部件；零件几何来自下节 custom mesh，螺旋桨动画由 armed/flying 状态驱动。 |

以下基础 QML 不在 custom 保存：`CameraLightModel.qml`、`Line3D.qml`、`Waypoint3DModel.qml`、`Viewer3DProgressBar.qml` 和 14 个 F450 部件 QML。它们由 `custom.qrc` 直接引用 `src/Viewer3D`。

### 4.10 F450 运行时 mesh

以下 14 个 `.mesh` 是 Qt Quick3D 二进制几何数据，只保存对应 F450 零件的顶点/索引等网格，不包含材质、局部变换、旋转动画、车辆遥测或业务逻辑。它们注册到 `qrc:/qml/Viewer3D/Models3D/Drones/Djif450/<零件名>/node.mesh`，由 QRC 复用的同名原生零件 QML 加载，再由 custom `DroneModelDjiF450.qml` 总装。14 个文件与当前 `src` 同名 mesh 的内容不同，所以保留 custom 副本，不能直接改为引用原生 mesh。

| 文件 | 作用 |
|---|---|
| `custom/src/Viewer3D/Viewer3DQml/Drones/Djif450/DroneModel_arm_1/node.mesh` | 为 `DroneModel_arm_1.qml` 提供第 1 个 F450 机臂的二进制几何；机臂位置、方向和材质由同名 QML 定义。 |
| `custom/src/Viewer3D/Viewer3DQml/Drones/Djif450/DroneModel_arm_2/node.mesh` | 为 `DroneModel_arm_2.qml` 提供第 2 个 F450 机臂的二进制几何；机臂位置、方向和材质由同名 QML 定义。 |
| `custom/src/Viewer3D/Viewer3DQml/Drones/Djif450/DroneModel_arm_3/node.mesh` | 为 `DroneModel_arm_3.qml` 提供第 3 个 F450 机臂的二进制几何；机臂位置、方向和材质由同名 QML 定义。 |
| `custom/src/Viewer3D/Viewer3DQml/Drones/Djif450/DroneModel_arm_4/node.mesh` | 为 `DroneModel_arm_4.qml` 提供第 4 个 F450 机臂的二进制几何；机臂位置、方向和材质由同名 QML 定义。 |
| `custom/src/Viewer3D/Viewer3DQml/Drones/Djif450/DroneModel_Base_bottom_1/node.mesh` | 为 `DroneModel_Base_bottom_1.qml` 提供 F450 机身下板的二进制几何；下板材质和装配位置由同名 QML 定义。 |
| `custom/src/Viewer3D/Viewer3DQml/Drones/Djif450/DroneModel_Base_Top_1/node.mesh` | 为 `DroneModel_Base_Top_1.qml` 提供 F450 机身上板的二进制几何；上板材质和装配位置由同名 QML 定义。 |
| `custom/src/Viewer3D/Viewer3DQml/Drones/Djif450/DroneModel_BLDC_1/node.mesh` | 为 `DroneModel_BLDC_1.qml` 提供第 1 个无刷电机的二进制几何；电机变换和材质由同名 QML 定义。 |
| `custom/src/Viewer3D/Viewer3DQml/Drones/Djif450/DroneModel_BLDC_2/node.mesh` | 为 `DroneModel_BLDC_2.qml` 提供第 2 个无刷电机的二进制几何；电机变换和材质由同名 QML 定义。 |
| `custom/src/Viewer3D/Viewer3DQml/Drones/Djif450/DroneModel_BLDC_3/node.mesh` | 为 `DroneModel_BLDC_3.qml` 提供第 3 个无刷电机的二进制几何；电机变换和材质由同名 QML 定义。 |
| `custom/src/Viewer3D/Viewer3DQml/Drones/Djif450/DroneModel_BLDC_4/node.mesh` | 为 `DroneModel_BLDC_4.qml` 提供第 4 个无刷电机的二进制几何；电机变换和材质由同名 QML 定义。 |
| `custom/src/Viewer3D/Viewer3DQml/Drones/Djif450/DroneModel_propeller2_2/node.mesh` | 为 `DroneModel_propeller2_2.qml` 提供对应 rotor 的二进制螺旋桨几何；armed/flying 驱动的旋转动画在复用的同名 QML 中实现。 |
| `custom/src/Viewer3D/Viewer3DQml/Drones/Djif450/DroneModel_propeller2_7/node.mesh` | 为 `DroneModel_propeller2_7.qml` 提供对应 rotor 的二进制螺旋桨几何；armed/flying 驱动的旋转动画在复用的同名 QML 中实现。 |
| `custom/src/Viewer3D/Viewer3DQml/Drones/Djif450/DroneModel_propeller22_1/node.mesh` | 为 `DroneModel_propeller22_1.qml` 提供对应反向 rotor 的二进制螺旋桨几何；旋向、安装变换和动画在同名 QML 中实现。 |
| `custom/src/Viewer3D/Viewer3DQml/Drones/Djif450/DroneModel_propeller22_2/node.mesh` | 为 `DroneModel_propeller22_2.qml` 提供对应反向 rotor 的二进制螺旋桨几何；旋向、安装变换和动画在同名 QML 中实现。 |

未注册的 `DroneModel_arm_1/meshes/node.mesh` 辅助副本已经删除。

### 4.11 外部 WGS84 城镇样例

本目录 17 个文件都是源码树中的手动导入/配准测试资产，未注册进 `custom.qrc`，也没有安装规则，因此不会自动进入 Android APK 或桌面安装包。测试 OBJ 时必须保持 OBJ、MTL 与 `textures` 的相对目录不变；测试 FBX 时要从设置页触发 Balsam。README 和两个 JSON 只供开发者追溯来源、人工填写参数，QGC 运行时不会自动读取。

| 文件 | 详细作用 |
|---|---|
| `custom/src/Viewer3D/ExternalWGS84_UE5_MapSample/README.md` | 面向开发者说明推荐导入文件、设置页操作、WGS84 原点、ENU 轴向、单位/比例/yaw、模型统计、ODbL 数据来源和已知限制；它是人工操作文档，运行时不读取。 |
| `custom/src/Viewer3D/ExternalWGS84_UE5_MapSample/osm_overpass_source.json` | 保存生成城镇模型所用的 Overpass URL、查询语句、bbox、下载时间和完整原始响应（当前 1328 个 element），用于来源追溯和重新生成资产；它不是 Viewer3D `osmFilePath` 可直接选择的 OSM 地图输入。 |
| `custom/src/Viewer3D/ExternalWGS84_UE5_MapSample/qgc_viewer3d_import_settings.json` | 保存本样例推荐的 WGS84 原点、ENU 轴、单位到米、比例、yaw、资产统计、文件清单和许可信息；内容需要开发者人工填入 Viewer3D 设置页，QGC 不会自动导入该 JSON。 |
| `custom/src/Viewer3D/ExternalWGS84_UE5_MapSample/realistic_town_wgs84_map.obj` | 推荐由 `RuntimeLoader` 直接加载的城镇模型；几何已转换为以指定 WGS84 原点为基准的本地 ENU 米制坐标，包含 UV，并通过 `mtllib` 相对引用同目录 MTL 与纹理。 |
| `custom/src/Viewer3D/ExternalWGS84_UE5_MapSample/realistic_town_wgs84_map.mtl` | 定义草地、道路、人行道、建筑立面/屋顶、商铺、树木等材质，并通过相对 `map_Kd` 路径引用下方 11 张 PNG；OBJ 要正常显示纹理必须保留本文件及 `textures` 相对目录。 |
| `custom/src/Viewer3D/ExternalWGS84_UE5_MapSample/realistic_town_wgs84_map.fbx` | 与 OBJ 相同场景的 ASCII FBX 7.4 创作格式，只用于验证 `External3DMapManager` 的 Qt Balsam 转换链路；它不是 `RuntimeLoader` 直接支持的输入。 |
| `custom/src/Viewer3D/ExternalWGS84_UE5_MapSample/textures/asphalt_worn.png` | `realistic_town_wgs84_map.mtl` 中 `mat_asphalt` 道路材质的磨损沥青漫反射贴图；由 MTL 相对路径加载，不是独立 QRC 资源。 |
| `custom/src/Viewer3D/ExternalWGS84_UE5_MapSample/textures/facade_brick_windows.png` | MTL 中 `mat_facade_brick` 的砖墙窗户立面贴图；由 MTL 相对路径加载，不是独立 QRC 资源。 |
| `custom/src/Viewer3D/ExternalWGS84_UE5_MapSample/textures/facade_light_windows.png` | MTL 中 `mat_facade_light` 的浅色墙面窗户立面贴图；由 MTL 相对路径加载，不是独立 QRC 资源。 |
| `custom/src/Viewer3D/ExternalWGS84_UE5_MapSample/textures/facade_modern_windows.png` | MTL 中 `mat_facade_modern` 的现代玻璃窗格立面贴图；由 MTL 相对路径加载，不是独立 QRC 资源。 |
| `custom/src/Viewer3D/ExternalWGS84_UE5_MapSample/textures/facade_tan_windows.png` | MTL 中 `mat_facade_tan` 的棕黄色墙面窗户立面贴图；由 MTL 相对路径加载，不是独立 QRC 资源。 |
| `custom/src/Viewer3D/ExternalWGS84_UE5_MapSample/textures/grass_mixed.png` | MTL 中 `mat_grass` 的地面/公园草地贴图；由 MTL 相对路径加载，不是独立 QRC 资源。 |
| `custom/src/Viewer3D/ExternalWGS84_UE5_MapSample/textures/roof_flat_gray.png` | MTL 中 `mat_roof_gray` 与 `mat_roof_flat` 共用的灰色平屋顶贴图；由 MTL 相对路径加载，不是独立 QRC 资源。 |
| `custom/src/Viewer3D/ExternalWGS84_UE5_MapSample/textures/roof_tile_red.png` | MTL 中 `mat_roof_red` 的红瓦屋顶贴图；由 MTL 相对路径加载，不是独立 QRC 资源。 |
| `custom/src/Viewer3D/ExternalWGS84_UE5_MapSample/textures/shopfront_facade.png` | MTL 中 `mat_shopfront` 的商铺橱窗和沿街店面贴图；由 MTL 相对路径加载，不是独立 QRC 资源。 |
| `custom/src/Viewer3D/ExternalWGS84_UE5_MapSample/textures/sidewalk_concrete.png` | MTL 中 `mat_sidewalk` 的人行道混凝土贴图；由 MTL 相对路径加载，不是独立 QRC 资源。 |
| `custom/src/Viewer3D/ExternalWGS84_UE5_MapSample/textures/tree_leaf.png` | MTL 中 `mat_tree_leaf` 的低多边形树冠叶片贴图；由 MTL 相对路径加载，不是独立 QRC 资源。 |

### 4.12 翻译

| 文件 | 详细作用 |
|---|---|
| `custom/translations/README.md` | 面向翻译维护者说明英文模板与 locale TS 的区别、为什么不提交生成的 `.qm`、如何运行更新脚本以及使用 Qt Linguist 人工复核/翻译的流程。它是维护文档，不被应用读取，也不提供任何运行时译文。 |
| `custom/translations/custom.ts` | `lupdate` 生成的英文源字符串模板，供各语言目录对齐 context/source，CMake 不把它编译为运行时 `.qm`。当前实际为 5 个 context、18 条 message，覆盖母线低电压告警、航向罗盘条、Fuel 和 Video Integration；Viewer3D、Gimbal 及部分 C++ `tr()` 新文本尚未全部刷新，因此它不是当前全部 custom 文本的完整清单。 |
| `custom/translations/custom_zh_CN.ts` | 简体中文 locale 目录；CMake 将其编译成 `custom_zh_CN.qm` 放入 `:/i18n`，`CustomPlugin` 在匹配中文 locale 时安装对应 translator。当前与模板相同的 5 个 context、18 条 message 都已有中文译文，但覆盖边界同样只到母线告警、罗盘条、Fuel 和 Video Integration，不代表 Viewer3D/Gimbal 已完整汉化。 |
| `custom/translations/custom-lupdate.sh` | Bash 翻译维护脚本：优先使用 `LUPDATE` 环境变量指定的工具，否则从 `PATH` 查找 Qt 6 `lupdate`；先扫描 `custom/src` 更新 `custom.ts`，再更新所有 `custom_*.ts`，并用 `-no-obsolete` 清理失效条目。它只更新 TS，不生成 `.qm`，新增/unfinished 条目仍需人工翻译和复核。 |

## 5. 复用的 QGC 原生 Viewer3D 文件

### 5.1 C++ 复用

`custom/CMakeLists.txt` 直接编译以下 15 个无项目差异的原生文件，custom 不保存副本；带项目差异的对应 `.cc` 才保存在第 4.8 节的 custom 路径。

| 原生文件 | 具体复用作用 |
|---|---|
| `src/Viewer3D/CityMapGeometry.h` | 声明 `CityMapGeometry : QQuick3DGeometry` 的 `modelName`、`osmParser` 属性、OSM路径状态和几何更新接口；第4.8节 custom `CityMapGeometry.cc` 实现该类并把设置来源切到 custom。 |
| `src/Viewer3D/earcut.hpp` | Mapbox Earcut 的 header-only 多边形三角剖分实现；custom `OsmParser.cc` 用它把建筑外轮廓及内洞转换成屋顶/地板三角形索引，不负责读取OSM或渲染。 |
| `src/Viewer3D/OsmParser.h` | 声明主线程侧 `OsmParser` 门面、地图参考点/边界、建筑层高、异步 worker 指针、建筑转mesh接口及 `mapChanged/gpsRefChanged` 信号；实现位于 custom `OsmParser.cc`。 |
| `src/Viewer3D/OsmParserThread.h` | 声明后台解析线程和 `BuildingType_t` 数据结构，保存节点、建筑外/内轮廓、局部坐标、建筑高度/层数、地图bbox和GPS参考点，并定义 `fileParsed` 完成信号。 |
| `src/Viewer3D/OsmParserThread.cc` | 在独立线程打开OSM XML，解析node、way与relation，把经纬度轮廓转换为参考点下的局部坐标，提取建筑高度/层数和地图边界，完成后向 `OsmParser` 发出有效/无效结果；它不生成Quick3D顶点。 |
| `src/Viewer3D/Viewer3DQmlVariableTypes.h` | 以header-only方式定义QML类型 `GeoCoordinateType`：输入 `gpsRef` 和 WGS84 `coordinate`，调用 `Viewer3DUtils` 输出局部 `QVector3D`，供车辆、任务点和航段场景复用。 |
| `src/Viewer3D/Viewer3DTerrainGeometry.h` | 声明地形 `QQuick3DGeometry` 的网格行列、ROI边界、参考坐标、顶点/法线/UV缓存和重建接口；算法实现使用 custom `Viewer3DTerrainGeometry.cc`。 |
| `src/Viewer3D/Viewer3DTerrainTexture.h` | 声明 `QQuick3DTextureData` 包装层及 OSM parser、ROI、tileCount、下载进度、纹理/几何完成状态，作为本地OSM场景和瓦片查询器之间的QML可见接口。 |
| `src/Viewer3D/Viewer3DTerrainTexture.cc` | 监听Flight Map地图类型和OSM解析完成，创建 `MapTileQuery` 下载覆盖建筑bbox的瓦片，把拼接图写成Quick3D `RGBA32F` texture data，并把实际瓦片ROI、网格尺寸及下载进度反馈给场景。 |
| `src/Viewer3D/Viewer3DTileQuery.h` | 声明多瓦片请求协调器、单次查询的tile列表/拼接画布、tile统计结构、Web Mercator像素/瓦片换算和完成/进度信号；不直接发HTTP请求。 |
| `src/Viewer3D/Viewer3DTileQuery.cc` | 从最高zoom向下选择不超过200张瓦片的级别，为每个tile创建 `Viewer3DTileReply`，把256×256响应拼到一张纹理图；遇到空瓦片会降低zoom重试，并向地形纹理层报告覆盖坐标、tile数量和总体进度。 |
| `src/Viewer3D/Viewer3DTileReply.h` | 声明一次地图瓦片网络请求的坐标/mapId/数据结构、网络对象、10秒计时器和 `tileDone/tileEmpty/tileError/tileGiveUp` 结果信号。 |
| `src/Viewer3D/Viewer3DTileReply.cc` | 通过QGC地图provider生成单tile URL并用 `QNetworkAccessManager` 下载；识别Bing“No Tile”占位图为空瓦片，网络/超时触发重试，连续超时后通知上层放弃。它只返回单张tile，不决定zoom或拼图。 |
| `src/Viewer3D/Viewer3DUtils.h` | 声明 WGS84 geodetic、ECEF、ENU/局部坐标双向转换函数及角度常量，供 parser、地形和QML坐标包装类型共享。 |
| `src/Viewer3D/Viewer3DUtils.cc` | 实现椭球经纬高到ECEF、ECEF到ENU、ENU回ECEF/WGS84的数学转换；Viewer3D用它让地图参考点、飞行器和任务坐标落入同一局部三维坐标系。 |

### 5.2 QML 和资源复用

`custom.qrc` 直接引用以下 22 个无差异原生资源。这些资源升级QGC时会随 `src` 更新，不需要在custom保存副本；表中的运行时URL由 `custom.qrc` 的alias决定。

| 原生文件 | 具体复用作用 |
|---|---|
| `src/QmlControls/Viewer3D/qmldir` | 声明 `Viewer3D` QML模块中的 `Viewer3D` 与 `Viewer3DProgressBar` 类型；QRC同名alias使前者实际命中custom根组件、后者命中下方原生进度条。 |
| `src/QmlControls/Viewer3D/Models3D/Drones/qmldir` | 声明 `Viewer3D.Models3D.Drones` 模块及 `DroneModelDjiF450` 类型；QRC把该类型对应文件alias到custom总装QML。它只负责类型发现。 |
| `src/Viewer3D/Viewer3DQml/Models3D/CameraLightModel.qml` | 创建六个方向光和三层相机旋转/平移节点，暴露tilt、pan、zoom及 `resetCamera()`；`Viewer3DModel.qml` 用它承载本地场景相机和照明。 |
| `src/Viewer3D/Viewer3DQml/Models3D/Line3D.qml` | 输入两个三维端点、线宽和颜色，计算向量长度与四元数旋转，用缩放后的Cylinder连接两点；`Viewer3DVehicleItems.qml` 用它绘制任务航段。 |
| `src/Viewer3D/Viewer3DQml/Models3D/Waypoint3DModel.qml` | 把任务点局部坐标和高度偏移放大到场景尺度，用Cone及文字显示Waypoint/Takeoff/RTL/ROI标记、序号和颜色；任务筛选及坐标计算在custom车辆场景文件。 |
| `src/Viewer3D/Viewer3DQml/Viewer3DProgressBar.qml` | 根据 `progressValue` 显示/隐藏下载进度浮层、ProgressBar和整数百分比文字；只显示进度，实际瓦片下载由C++地形纹理链完成。 |
| `src/Viewer3D/Viewer3DQml/Drones/Djif450/DroneModel_arm_1/DroneModel_arm_1.qml` | 加载QRC同目录的custom `node.mesh`，定义第1机臂的局部节点、材质和装配变换；不读取车辆姿态。 |
| `src/Viewer3D/Viewer3DQml/Drones/Djif450/DroneModel_arm_2/DroneModel_arm_2.qml` | 加载QRC同目录的custom `node.mesh`，定义第2机臂的局部节点、材质和装配变换；不读取车辆姿态。 |
| `src/Viewer3D/Viewer3DQml/Drones/Djif450/DroneModel_arm_3/DroneModel_arm_3.qml` | 加载QRC同目录的custom `node.mesh`，定义第3机臂的局部节点、材质和装配变换；不读取车辆姿态。 |
| `src/Viewer3D/Viewer3DQml/Drones/Djif450/DroneModel_arm_4/DroneModel_arm_4.qml` | 加载QRC同目录的custom `node.mesh`，定义第4机臂的局部节点、材质和装配变换；不读取车辆姿态。 |
| `src/Viewer3D/Viewer3DQml/Drones/Djif450/DroneModel_Base_bottom_1/DroneModel_Base_bottom_1.qml` | 加载custom下板mesh并定义机身下板材质/局部变换，由F450总装组件实例化。 |
| `src/Viewer3D/Viewer3DQml/Drones/Djif450/DroneModel_Base_Top_1/DroneModel_Base_Top_1.qml` | 加载custom上板mesh并定义机身上板材质/局部变换，由F450总装组件实例化。 |
| `src/Viewer3D/Viewer3DQml/Drones/Djif450/DroneModel_BLDC_1/DroneModel_BLDC_1.qml` | 加载第1无刷电机custom mesh并定义其材质与安装变换；电机本身不执行飞行状态逻辑。 |
| `src/Viewer3D/Viewer3DQml/Drones/Djif450/DroneModel_BLDC_2/DroneModel_BLDC_2.qml` | 加载第2无刷电机custom mesh并定义其材质与安装变换；电机本身不执行飞行状态逻辑。 |
| `src/Viewer3D/Viewer3DQml/Drones/Djif450/DroneModel_BLDC_3/DroneModel_BLDC_3.qml` | 加载第3无刷电机custom mesh并定义其材质与安装变换；电机本身不执行飞行状态逻辑。 |
| `src/Viewer3D/Viewer3DQml/Drones/Djif450/DroneModel_BLDC_4/DroneModel_BLDC_4.qml` | 加载第4无刷电机custom mesh并定义其材质与安装变换；电机本身不执行飞行状态逻辑。 |
| `src/Viewer3D/Viewer3DQml/Drones/Djif450/DroneModel_propeller2_2/DroneModel_propeller2_2.qml` | 加载对应custom螺旋桨mesh；总装传入 `flightMode` 时沿Y轴持续旋转，定义这一rotor的材质、安装位置和旋向。 |
| `src/Viewer3D/Viewer3DQml/Drones/Djif450/DroneModel_propeller2_7/DroneModel_propeller2_7.qml` | 加载对应custom螺旋桨mesh；总装传入 `flightMode` 时沿Y轴持续旋转，定义这一rotor的材质、安装位置和旋向。 |
| `src/Viewer3D/Viewer3DQml/Drones/Djif450/DroneModel_propeller22_1/DroneModel_propeller22_1.qml` | 加载对应反向rotor的custom mesh，按 `flightMode` 驱动Y轴旋转；负责该桨的材质、变换和相反旋向。 |
| `src/Viewer3D/Viewer3DQml/Drones/Djif450/DroneModel_propeller22_2/DroneModel_propeller22_2.qml` | 加载对应反向rotor的custom mesh，按 `flightMode` 驱动Y轴旋转；负责该桨的材质、变换和相反旋向。 |
| `src/Viewer3D/Shaders/earthMaterial.vert` | 地形材质顶点shader，把输入 `UV0` 传给fragment阶段；不改变顶点位置，供本地OSM瓦片纹理材质使用。 |
| `src/Viewer3D/Shaders/earthMaterial.frag` | 地形材质fragment shader，从 `someTextureMap` 按UV采样瓦片颜色，将饱和度调为1.5后写入 `BASE_COLOR`；不下载或拼接瓦片。 |

## 6. 受控 src 修改

当前分支 `SecDev/ft/gimbalcontrol` 沿用的二次开发 `src` 差异只有以下两处；它们是 custom PX4 模块正常链接所需的受控例外：

| 文件 | 修改原因 |
|---|---|
| `src/CMakeLists.txt` | 原生 PX4 Factory 被关闭时仍链接 `AutoPilotPluginsPX4Module`，保证 VehicleSummary 和 CustomAutoPilotPlugin 使用的 PX4 QML 页面存在。 |
| `src/Vehicle/VehicleSetup/VehicleSummary.qml` | 注释 APM QML import；当前构建关闭 APM 模块，继续导入会造成运行时 `module QGroundControl.AutoPilotPlugins.APM is not installed`。 |

除这两处外，当前二次开发功能没有其他 `src` 差异。底部航向罗盘条、Android H.265 与 USB 飞控连接修复都完全位于 `custom`；根目录 `android/src` 保持原样，Android APK 通过构建目录 overlay 使用 custom Java 实现，未新增任何 `src` 修改。

## 7. Viewer3D 参数

| Fact | 类型/默认值 | 说明 |
|---|---|---|
| `enabled` | bool / `false` | 启用 Viewer3D 和工具栏图标。 |
| `useGoogle3DMapSource` | bool / `false` | 使用 Google 3D Maps。 |
| `google3DMapsApiKey` | string / 空 | Google Maps JavaScript API Key。 |
| `useExternal3DMapSource` | bool / `false` | 使用外部三维模型而非 OSM。 |
| `external3DMapFilePath` | string | 外部模型文件路径。 |
| `external3DMapOriginLatitude` | double / `0 deg` | 模型局部原点纬度。 |
| `external3DMapOriginLongitude` | double / `0 deg` | 模型局部原点经度。 |
| `external3DMapOriginAltitude` | double / `0 m` | 模型局部原点 AMSL 高度。 |
| `external3DMapUnitToMeters` | double / `0.01 m/unit` | 一个模型单位对应米数；UE 厘米导出用 0.01。 |
| `external3DMapScale` | double / `1` | 单位换算后的额外比例。 |
| `external3DMapYaw` | double / `0 deg` | 模型旋转到 QGC ENU 的 yaw。0 表示 +Y 北、+X 东。 |
| `osmFilePath` | string | 本地 OSM 文件路径。 |
| `buildingLevelHeight` | double / `3 m` | 无明确高度时的平均建筑层高。 |
| `altitudeBias` | double / `0 m` | 飞行器三维显示高度偏移。 |

地图源优先级：Google 开启时使用 Google；否则外部模型开启时使用外部模型；两者都关闭时使用本地 OSM。

## 8. Application General、Fly View、Gimbal 与视频参数及使用

### 8.1 Android 遥控器默认界面缩放

| 原生 Fact | Android custom 缺省值 | QSettings 键 | 说明 |
|---|---|---|---|
| `appFontPointSize` | `12 pt` | 根级 `appFontPointSize` | 目标遥控器的平台基准为 14 pt，General 页面按 `12 / 14 × 100` 四舍五入显示为 86%。 |

实现使用 `QGCCorePlugin` 提供给 custom build 的 `adjustSettingMetaData()` 扩展点，只在 `Q_OS_ANDROID` 下调整原生 Fact 的 `rawDefaultValue`。`SettingsFact` 随后调用 `settings.value("appFontPointSize", 12)`：键不存在才采用 12，键已存在则读取用户保存值。因此这是“Android 新安装/重置后的缺省值”，不是“每次启动强制值”。非 Android 构建不修改该元数据；原生缺省值 0 会在 `ScreenTools` 初始化时替换为平台字号，即 100%。

使用规则：

1. 全新安装 APK、清除应用数据或清除全部 QGC 设置后，打开 Application Settings -> General，采用 14 pt 基准的目标遥控器应显示 86%。
2. 用户在 General 页面使用 `-`/`+` 修改时，每次仍增减 1 pt；显示百分比按整数点数与平台字号的比值取整。新值写入根级 QSettings，重启或保留应用数据升级 APK 后继续使用用户值。
3. 已经保存过其他缩放值的 Android 旧安装不会因升级自动改成 86%；如需使用新缺省值，可手动调到 12 pt 对应的 86%，或清除设置。
4. Ubuntu、Windows、macOS、iOS 等非 Android 构建不执行该覆盖，新安装/重置后保持 QGC 原生 100% 缺省缩放；用户已保存的其他比例仍然优先。
5. 物理宽度小于 120 mm 的极小 Android 设备使用 11 pt 平台基准，整数点数无法得到 86%；本项目 12 pt 缺省值针对当前走 14 pt 分支的思翼遥控器。

该方案按项目原则在 custom 保存 `GeneralSettings.qml` 同路径覆盖页，但不在页面初始化时写默认值。默认值仍由 custom C++ metadata hook 提前注入；页面只负责展示和修改原生 Fact，因此完整保留 QGC 原生整数点数选择及持久化机制。

### 8.2 底部航向罗盘条

| Fact | 类型/默认值 | 说明 |
|---|---|---|
| `showHeadingCompassBar` | bool / `true` | 显示飞行界面底部中央的航向罗盘条；保存于 `FlyView` QSettings 分组，切换后立即生效，无需重启。 |

使用流程：

1. 打开 Application Settings -> Fly View -> Instrument Panel。
2. 使用 `Show Heading Compass Bar` 开关控制显示；简体中文界面对应“显示航向罗盘条”。
3. 返回 Fly View 并连接飞控。只有活动飞行器的 `heading` 有效时才显示罗盘条，未连接飞控不会以 0° 伪造航向。
4. 中央数值和固定三角指针表示当前机头航向，N/NE/E/SE/S/SW/W/NW 方位随航向连续移动；该数据不包含航点方向和航线偏差。

实现从 `custom-example/FlyViewCustomLayer.qml` 中选择性提取横向航向条。示例通过 720 个 `QGCLabel` 切换可见性模拟滚动，本实现使用以当前 45° 区间为基准的 11 个相对方位 Label，保持 359°/0° 连续过渡并降低 Android QML 更新开销；标签精简只改变绘制数量，不负责组件宽度。罗盘条通过 `FlyViewCustomLayer` 的 Loader 显式加载并贴近 Fly View 底边，首选宽度直接使用组件与示例一致的 `50 × defaultFontPixelWidth`。此前外层取 `max(leftEdgeBottomInset, rightEdgeBottomInset)` 后从左右各扣一次，PIP 或仪表任一侧变宽都会受到双倍扣减，极端时宽度变为 0；当前只用 Fly View 总宽度减去两侧基础 margin 作为屏幕边界，角落 inset 的变化不会再挤压罗盘条。这样在 86% 缩放和 PIP 从最小到最大拖动时仍保持示例宽度；极端放大的角落控件可能与罗盘条发生视觉层叠，这是优先保证航向可读性的明确取舍。显示时只扩展 `bottomEdgeCenterInset`，关闭时恢复原生 inset。组件不放置 `DeadMouseArea`，因此不会吞掉其覆盖区域的地图拖动、滚轮缩放、PIP 调整或 Android 触摸手势。

QGC 原生 `FlyView.qml` 在全屏视频模式下会隐藏整个 custom overlay，所以该模式下罗盘条和母线告警均不显示；普通地图、普通视频主窗口和 Viewer3D 不受影响。

### 8.3 Gimbal 与视频参数

| Fact | 范围/默认值 | 说明 |
|---|---|---|
| `enabled` | bool / `true` | 启用私有SDK云台相机后端和合并的缩放/拍照/录像控制栏；关闭后才回退原生相机控件。 |
| `sdkHost` | `192.168.144.25` | A8 Mini SDK IP。 |
| `sdkPort` | 1-65535 / `37260` | A8 Mini 私有 UDP SDK 端口。 |
| `zoomStep` | 0.1-4.5 / `1.0x` | 短按一次及长按重复一次的网格分度；中间移动严格使用完整值。若下一步会越过真实小数上限，则一次吸附到该上限；从上限缩小时先回到相邻网格点。步长1.0x时允许1080P的5.0↔5.5及2K的3.0↔3.5边界转换。 |
| `mavlinkAutoVideoStream` | bool / `false` | 是否接受 MAVLink 相机流 URI 并允许其锁定视频源。修改后重启 QGC。 |
| `forceAndroidH265HardwareDecoder` | bool / `true` | 仅 Android 生效。注册 `hvc1 -> byte-stream/au` 适配器并优先真实厂商 MediaCodec H.265 硬解；无可用硬解时保留软件回退。修改后重启 QGC。 |

Gimbal启用后，Manager即使不在Fly View也会持续运行，并每2秒向 `sdkHost:sdkPort`探测：GStreamer构建优先采用主显示sink直接报告的最终协商尺寸，仅在没有任何有效直接结果时用 `VideoManager::videoSizeChanged/decodingChanged` 启动1秒稳定兜底；非GStreamer构建直接使用这两个原生状态。拉流尺寸尚未锁存时每轮发送0x16查询设备当前支持缩放范围，避免尺寸观察链未回调时上限永久unknown；每轮还发送0x0a相机状态，取得任一有效能力后才开始查询0x18当前倍率。绝对目标和稳定确认由专用状态机管理：先留350 ms隔离窗口，再发送唯一在途的0x18；1秒未收到合法倍率才安排下一次隔离重试，10秒整体deadline不会被重试刷新。Fly View右侧纵向合并栏不等待探测结果，无论是否连接飞控或云台都立即显示。只有来源逻辑IP匹配配置endpoint、通过帧头、精确长度、CRC、control=0x02及对应payload校验的SDK响应，`sdkResponding`才变为true。

有效上限优先取QGC实际解码的拉流尺寸：1280×720/736→6.0x、1920×1080/1088→5.5x、2560×1440→3.5x、3840/4096×2160→1.0x且不可继续数字变焦。这里不是相机机内卡录分辨率。GStreamer主视频在真实首帧到达时从sink、解码器peer或ghost target的current CAPS读取宽高，同时以主 `GstGLQt6VideoItem` 根据最终 `GstVideoInfo` 设置的隐式尺寸作为第二条直接来源。两条路径任一取得受支持尺寸就直接交给Manager，并由pad路径同步校正 `VideoManager::videoSize`。若两条直接路径都没有报告任何有效尺寸，Manager仅在原生视频状态已解码、尺寸属于上述白名单且连续稳定1秒时以 `VideoManager`兜底；有效直接尺寸会立即取消该尺寸兜底。三级尺寸链尚未确认时，合法0x16回包可临时恢复设备声明的当前上限；后续受支持的真实拉流尺寸会取代0x16结果，运行期新的受支持尺寸也会更新旧上限。短暂断流不清空能力；新的不支持直接尺寸会清空旧尺寸映射并立即恢复0x16查询。每次能力确认或改变都会显式通知可用性变化并启动0x18同步；需两次串行稳定确认，并在必要时对齐到固定网格点或真实上限后才解锁。普通SDK在线探测使用1.5秒响应窗口；正在进行的倍率确认可在10秒整体deadline内重试。拍照仍可重试，录像必须先取得0x0a状态，因为0x0c是toggle。

短按从已确认的 `currentZoom` 计算唯一下一目标：在1.0x锚定网格内部按0.1x协议精度执行 `current ± zoomStep`；若放大的下一完整步会越过真实小数上限，则把目标明确设为该上限；若从不在网格上的真实上限缩小，则先返回最高合法网格点。只有这两个方向相反的边界转换允许小于配置步长的距离，其他位置不使用通用钳制制造部分步长，也不会以requested累计。步长1.0x时对应1080P的5.0↔5.5和2K的3.0↔3.5。发送后立即令显示为 `--`，0x0f ACK只表示受理；随后两个串行0x18查询都精确命中目标，才更新 `currentZoom`。任何1.8x、2.8x等运动中间值都只会清零命中计数并继续查询。长按复用完全相同的目标路径：上一目标双确认后等待180 ms再计算下一完整网格步或边界吸附；松开、取消、移出、隐藏、销毁、设置变化、SDK异常或60秒看门狗只取消后续调度，已发送目标继续确认，不发送0x05启动或停止包。启动或外部控制留下的其他非网格稳定值会先双采样，再以0x0f吸附到最近合法停点（固定网格点或真实上限）；这是独立的状态校正，不属于用户步长或小数上限转换，完成前仍显示 `--`。0x0c拍照/录像不带直接ACK：照片由0x0b确认；录像约400 ms查询0x0a并仅接受目标一致状态，2.5秒无结果标记unknown。

RTSP URL 的 `.264` 后缀只是 A8 Mini 的固定路径名，不代表当前一定为 H.264；QGC 依据 RTSP SDP 中的 `H264`/`H265` 编码声明组建管线。Android 策略只过滤 `video/x-h265` decoder，不修改 H.264 decoder rank。

开启强制硬解后，策略在 GStreamer 初始化后、`decodebin3` 创建播放管线前执行：

- 原生 Stable V5.0 在 `parsebin` 阶段把 H.265 强制为 `hvc1`；部分 Android 厂商 MediaCodec 只声明接受 Annex-B `byte-stream,alignment=au`，因此仅修改原厂 decoder rank 无法让它进入候选集合。
- custom 枚举厂商 `amcviddec-*`，排除 Google OMX、C2 Android、C2 Google、C2 Goldfish、secure、`*.sw.dec`、Qualcomm `*swvdec`、software/FFmpeg decoder；若系统同时暴露名称带 `lowlatency`、`low_latency` 或 `low-latency` 的专用组件则优先尝试，再按原 rank 逐个执行“元素可创建、静态管线可链接且 decoder 可进入 READY”预检。
- 当前项目固定的 GStreamer 1.22.12 `amcvideodec` 没有暴露可由应用设置的 low-latency 属性，因此 custom 不能通过 `g_object_set` 伪造 Android `KEY_LOW_LATENCY`；本实现使用厂商专用低延迟组件（存在时）、真实硬解、GL-compatible raw caps 和限长输出队列控制延迟。
- 找到可用候选后注册 `qgcandroidh265hwdec`，它的外部 sink 接受 `hvc1`，内部执行 `h265parse(config-interval=-1) -> byte-stream/au -> 厂商 MediaCodec`。适配器 rank 为 `GST_RANK_PRIMARY + 100`，会覆盖原生 `Force software decoder` 保存值产生的 rank 257。
- 若设备厂商解码器本身直接接受 `hvc1`，不经过适配器也可使用；该直接硬解 rank 至少提升到 `GST_RANK_PRIMARY + 2`，同样高于原生软件强制值。
- 适配器内部不创建软件解码器。所有原生软件 ranks 保留；适配器无法注册或在自动建链阶段不能使用时，`decodebin3` 仍可选择外层软件 decoder，避免无兼容硬解设备直接黑屏。MediaCodec 收到具体 profile/level 后才发生的运行期配置失败不保证自动切换，需依据首帧日志和完整 logcat 判断。
- 厂商 decoder 后的 raw queue 设为 downstream-leaky、最多 2 帧、字节/时间不设上限；显示端阻塞时丢弃旧 raw frame，不丢压缩 H.265 AU，不破坏参考帧链。
- `hvc1 -> byte-stream` 只发生在 tee 后的播放解码支路，录像支路继续使用原生 `hvc1`，不会因本次适配器改变封装格式。
- Android 首次安装默认值时，仅当 A8 Mini URL 匹配且用户从未保存 `Video/lowLatencyMode` 才将它设为 `true`；用户已有的开关选择始终保留。

只有日志出现 `Android H.265 decoder produced its first raw frame ... hardware confirmed`，才证明经过上述软件组件黑名单筛选的厂商 MediaCodec 已经实际输出画面；其中 `glMemoryOutput true` 还表示解码输出与 QGC GL 显示链协商为 GLMemory。仅看到 factory、rank 或输入 caps 不等于解码成功。这里的 `hardware confirmed` 是本 custom 基于厂商 factory 筛选后的运行证据，不等同于 Android API 29 `MediaCodecInfo.isHardwareAccelerated()` 的系统级认证。

使用流程：

1. 电脑或遥控器网口连接A8 Mini，确认可访问 `192.168.144.25`；私有相机控制无需连接飞控，也无需等待QGC出现活动Vehicle。
2. Application Settings -> Fly View -> SIYI Gimbal Camera 中确认IP、端口、缩放分度值和Enabled。页面标题说明保持简短，以免撑坏设置页布局；短按/长按、完整步长、双向边界吸附、实际拉流尺寸优先及0x16恢复后备等完整规则以本节和Fact元数据为准。
3. Application Settings -> Video -> Video Stream Integration 中选择是否使用 MAVLink 自动视频流；Android 设备确认 H.265 硬解开关开启，然后重启 QGC。
4. 返回Fly View。只要Gimbal Enabled，右侧单个纵向合并栏就应立即显示，不要求飞控或云台已连接；从上到下依次为 `+`、倍率、`-`、拍照和录像。尚未取得有效能力和倍率时倍率显示 `--`且缩放按钮禁用。视频第一帧到达后，正常直接路径应依次看到“Installed main pulled-video resolution observers”、negotiated/observed主拉流尺寸、“Latched pulled video resolution”和发出的命令24（0x18）日志；运行期切换到另一受支持尺寸时应出现“Updated pulled video resolution”。若直接观察器未回调但原生尺寸有效，则先看到“Waiting for stable VideoManager pulled-video resolution fallback”，约1秒后锁存日志的source为“stable VideoManager fallback”；若尺寸链仍无结果，则应看到发出的命令22（0x16）及“Activated SIYI zoom capability”后备日志，随后继续发出命令24。第二次一致0x18完成后应出现“zoom controls are ready”，中心显示实际倍率，并仅点亮仍有合法目标的方向按钮。720P/1080P/2K上限分别为6.0x/5.5x/3.5x；4K上限1.0x时两个按钮都灰显是相机无数字变焦能力的正确状态。录像按钮在0x0a状态未知时灰显并显示 `...`。
5. 短按 `+`/`-` 只发送一个目标：中间网格严格执行完整 `zoomStep`，仅最后一次允许吸附真实小数上限，反向时先返回相邻网格点。步长1.0x时1080P序列应为1→2→3→4→5→5.5，反向为5.5→5→4→3→2→1；2K应为1→2→3→3.5及3.5→3→2→1。一个目标确认前按钮禁用且UI显示 `--`，快速连点不会追加或累计。按住约420 ms后按相同规则串行重复目标，每个目标都需两个0x18命中；松手或移出只停止下一目标，长按不附带短按，也不应抓到任何0x05缩放包。
6. 使用纵向栏下部的相机图标拍照；录像状态尚未同步或一次切换命令仍在pending时，REC按钮灰显并显示 `...`。发送切换后约400 ms主动查询0x0a，只有与本次开始/停止目标一致的状态才允许再次操作；延迟旧状态会继续等待，2.5秒未确认则回到unknown并重新同步。照片成功有绿色闪烁，录像期间显示本次会话计时；录像最终状态以0x0a为准，0x0b只补充失败反馈。
7. 在没有飞控的纯云台场景验证缩放、拍照、录像均可用；随后连接或断开飞控，控制栏不应消失或切换后端。只有关闭Gimbal设置时，有活动飞行器才恢复原生 `PhotoVideoControl`。

推荐 PX4 TELEM2 参数：

| 参数 | 值 |
|---|---|
| `MAV_1_CONFIG` | `TELEM 2`，修改后重启飞控 |
| `SER_TEL2_BAUD` | `115200` |
| `MAV_1_MODE` | `Gimbal` |
| `MAV_1_FLOW_CTRL` | `Off` |
| `MAV_1_FORWARD` | `Enabled` |
| `MNT_MODE_IN` | `MAVLink Gimbal Protocol v2` |
| `MNT_MODE_OUT` | `MAVLink Gimbal Protocol v2` |

TELEM2参数只负责飞控与云台的MAVLink集成，是飞行任务/姿态控制场景的推荐配置，不是本合并栏的前置条件。custom的单击绝对步进、长按串行重复步进、拍照、录像和状态查询全部由电脑或遥控器直接发往 `192.168.144.25:37260/UDP`；纯云台无飞控时仍可使用，RTSP播放、私有SDK控制和飞控MAVLink是彼此独立的三条链路。

## 9. Android USB 飞控连接

该修复没有新增设置 Fact，Android 构建中默认生效，也不受 `mavlinkAutoVideoStream`、`forceAndroidH265HardwareDecoder` 或 Gimbal Enabled 影响。QGC 原生的“自动连接 Pixhawk”仍负责最终创建 MAVLink 串口链路。

连接流程：

1. 遥控器 USB 口必须工作在 OTG/Host 数据模式；飞控亮绿灯只证明 VBUS 供电，不能证明 D+/D- 数据线、Host 角色或 Android 枚举成功。
2. Application Settings -> Comm Links -> AutoConnect 中保持 Pixhawk 开启；该项默认开启。飞控 VID/PID 或描述不在 `USBBoardInfo.json` 识别范围时，可在 Comm Links 手动新建 Serial 链路并选择已枚举端口。
3. 第一次连接或重装 APK 后允许 QGC 的 USB 权限。授权请求发出后 15 秒内不会重复弹窗；若系统丢失授权结果广播，超时后允许重新请求。明确拒绝后则保持抑制，需要拔插飞控或重启 QGC 再次触发授权。
4. 关闭思翼地面站、串口终端等可能占用同一 USB endpoint 的应用；Android USB 设备连接为独占打开，其他应用未释放时 QGC 会记录 `No USB device connection` 或 open 异常。
5. 允许权限后等待 QGC 两轮串口扫描。原生 LinkManager 为避开 bootloader 重枚举会延迟自动连接，不应以飞控刚上电后一秒内没有车辆图标判断失败。

custom 管理器的状态规则：

- `drivers` 只表示“Android 当前仍能看见并已由串口 prober 匹配的物理设备”；`deviceResourcesMap` 只表示“QGC 当前实际打开的端口”。
- QGC 主动断开时停止异步 I/O、关闭端口并清除文件描述符，但保留 driver；下一轮可直接 reopen。
- 物理拔出或扫描发现设备消失时，先完整释放打开资源，再从枚举中移除 driver；即使 Android 漏发 detach 广播，空扫描也会清掉陈旧状态。custom 不从 Android 线程用裸 `QSerialPortPrivate*` 调 `nativeDeviceHasDisconnected`，而是让原生 SerialWorker 的端口可用性定时器在其所属 Qt 线程发现端口已消失并调用 `QSerialPort::close()`，避免悬空指针和跨线程关闭 Qt 对象。
- Activity 销毁时释放所有端口、注销 receiver 并把静态 manager/prober/context 复位；Activity 重建后重新初始化，不会因旧 `usbManager` 非空而跳过注册。
- `availableDevicesInfo()` 不再遍历遥控器上的所有原始 USB 设备，只返回已匹配且已授权的串口。因此内置视频、存储等非串口 USB 不会进入 `QSerialPortInfo`，无权限读取它们的 serial number 也不会让整次枚举抛异常。
- 打开失败、I/O manager 创建失败、关闭、拔出和 cleanup 共用幂等释放路径；不会留下仍占用 endpoint、携带旧 `classPtr`/I/O manager 的半打开 resource。普通 close 后只有 driver、没有 resource 是设计允许的“已发现但未打开”状态。

实际运行链路：

```text
QGCActivity.onCreate
  -> custom QGCUsbSerialManager.initialize
     -> 注册 attach / detach / permission receiver
     -> UsbSerialProber.getDefaultProber
     -> 标准 CDC communication + data interface 保守兜底
     -> 请求 Android USB 权限
  -> availableDevicesInfo（仅 matched + permission granted）
  -> AndroidSerial / QGCSerialPortInfo
  -> USBBoardInfo.json 判断 Pixhawk/SiK/RTK
  -> LinkManager 延迟 AutoConnect
  -> open：创建本次连接 resource -> port.open -> I/O manager
  -> QSerialPortPrivate 设置真实 baud/data/stop/parity 并启动异步读取
  -> MAVLink heartbeat -> Vehicle
```

日志诊断边界：`Android USB Host sees no device` 表示应用层根本没有收到设备，需检查遥控器端口模式、OTG/数据线、转接头和系统 USB Host 支持，Java 补丁无法把供电线变成数据线；`USB device visible but no serial driver matched` 表示 Android 已枚举，但接口不是默认支持的 USB 串口或不是标准 CDC-ACM，应保留日志中的 VID、PID 和 `class/subclass/protocol` 后再添加精确驱动映射。

## 10. Fuel 与默认链路

FuelStatusIndicator 依赖飞行器 `fuelStatus.telemetryAvailable`。没有 `FUEL_STATUS` 数据时控件不占用可见空间；有数据后显示百分比。液体燃料使用 ml，气体燃料使用 MPa。

母线告警读取 `vehicle.generator.busVoltage`：

- `< 20.0 V`：显示低电压告警。
- `20.0-20.4 V`：保持当前状态，形成回差。
- `> 20.4 V`：关闭告警。

默认链路只在找不到名称为 `local`（大小写不敏感）的配置时创建：

| 字段 | 默认值 |
|---|---|
| 名称 | `local` |
| 类型 | UDP |
| 本地端口 | `0` |
| 服务器 | `192.168.144.20:19856` |
| 开始时自动连接 | 关闭 |
| 高延迟 | 关闭 |

## 11. 关键运行链路

```text
PX4 HEARTBEAT
  -> CustomFirmwarePluginFactory
     -> 能力列表声明 PX4 + MultiRotor
     -> 当前运行选择只检查 MAV_AUTOPILOT_PX4，未检查 MAV_TYPE
  -> CustomFirmwarePlugin
     -> CustomAutoPilotPlugin 控制车辆设置页
     -> toolIndicators 移除 RC RSSI、插入 Fuel
     -> updateAvailableFlightModes 限制可设置模式
     -> hasGimbal 声明 pitch/yaw 能力
```

```text
SettingsManager 创建 AppSettings::appFontPointSize
  -> SettingsFact 调用 CustomPlugin::adjustSettingMetaData
     -> 先保留 QGCCorePlugin 原生 metadata 调整
     -> Android：rawDefaultValue = 12 pt
     -> 非 Android：不覆盖，保留原生 0
  -> QSettings 根级 appFontPointSize
     -> 已存在：使用用户保存的整数点数
     -> Android 且不存在：使用 12 pt 缺省值
     -> 非 Android 且不存在：ScreenTools 使用平台字号
  -> General / UI Scaling
     -> 目标遥控器：12 / 14，显示 86%
     -> 非 Android 原生缺省：平台字号 / 平台字号，显示 100%
```

```text
QGCApplication
  -> VideoManager 构造
     -> GStreamer::initialize() 注册解码插件
  -> CustomPlugin::init()
     -> DefaultCommunicationLinkInstaller
     -> Viewer3DSettings / External3DMapManager / CustomViewer3DManager
     -> FlyViewCustomSettings（FlyView/showHeadingCompassBar）
      -> GimbalControlSettings / GimbalControlManager
         -> enabled时立即启动后台2秒探测，不依赖activeVehicle或QML可见
         -> GStreamer优先接收主显示sink最终协商尺寸；无直接结果时稳定1秒的VideoManager尺寸兜底
         -> 非GStreamer直接使用VideoManager解码状态和尺寸
         -> 尺寸仍未锁存时每2秒查询0x16当前支持范围，避免能力永久unknown
         -> 拉流映射：720P→6.0x、1080P→5.5x、2K→3.5x、4K→1.0x
         -> 支持尺寸确认或变化后取代0x16后备，并立即启动两次0x18稳定确认
         -> 每轮发送0x0a状态；取得能力后，缩放空闲时发送0x18
         -> 卡录配置不参与上限；新拉流尺寸可安全更新，短暂断流保留旧能力
         -> 来源逻辑IP、精确帧长、CRC、control=0x02及payload均合法才置sdkResponding
         -> 一个0x0f目标在途；两个串行0x18都命中才发布current，确认前UI显示--
         -> 短按和长按均串行目标；中间完整zoomStep，末端双向吸附真实小数上限
     -> AndroidVideoDecoderPolicy::apply()
        -> 枚举并预检 byte-stream/au 厂商 amcviddec-*
        -> 注册高 rank qgcandroidh265hwdec
     -> GimbalVideoStreamSupport 安装 A8 Mini 默认值
  -> VideoManager::init()
     -> 创建 VideoReceiver / decodebin3
        -> CustomPlugin::createVideoSink()复用原生qgcvideosinkbin
           -> PulledVideoResolutionProbe在真实首帧读取sink/peer/ghost-target current CAPS
           -> 同时观察GstGLQt6VideoItem由最终GstVideoInfo写入的隐式尺寸
           -> 两路尺寸直达Manager；pad路径另发布到VideoManager，thermal流不参与
        -> 原生 parsebin 输出 hvc1
        -> qgcandroidh265hwdec
           -> h265parse -> byte-stream/au
           -> 厂商 amcviddec-* -> raw leaky queue（2 帧）
        -> qgcvideosinkbin / qml6glsink
  -> CustomPlugin::createQmlApplicationEngine()
     -> CustomOverrideInterceptor
        -> /Custom/qml 中存在才覆盖
        -> 其他 QML 使用 src 原生模块
```

```text
Application Settings / General
  -> 原生 AppSettings.qml 请求 GeneralSettings.qml
  -> CustomOverrideInterceptor 映射到 custom GeneralSettings.qml
  -> 页面读取原生 AppSettings::appFontPointSize
     -> Android 新缺省 12 pt，在目标遥控器显示 86%
     -> 非 Android 原生缺省显示 100%
  -> -/+ 每次修改 1 pt，并由原生 SettingsFact 持久化
```

```text
Application Settings / Fly View
  -> 原生 AppSettings.qml 请求 FlyViewSettings.qml
  -> CustomOverrideInterceptor 映射到 custom FlyViewSettings.qml
  -> Instrument Panel
     -> showHeadingCompassBar（即时生效）
  -> Viewer3DSettingsGroup.qml
  -> GimbalControlSettingsGroup.qml
```

```text
Application Settings / Video
  -> 原生 AppSettings.qml 请求 VideoSettings.qml
  -> CustomOverrideInterceptor 映射到 custom VideoSettings.qml
  -> 保留原生 Video Source / Connection / Settings / Local Video Storage
  -> Video Stream Integration
     -> mavlinkAutoVideoStream
     -> forceAndroidH265HardwareDecoder（所有平台显示，仅 Android 生效）
```

```text
Fly View
  -> 原生 FlyView.qml / FlyViewWidgetLayer.qml / FlyViewToolStrip.qml
  -> custom FlyViewToolStripActionList.qml 增加 3D 入口
  -> custom FlyViewTopRightColumnLayout.qml
     -> Gimbal enabled
        -> 不检查activeVehicle，显式Loader常驻 GimbalCameraControl.qml
        -> 合并栏始终显示，sdkResponding不决定可见性；SDK超时会撤销倍率/录像已知状态并禁用相应操作
         -> 顶部 GimbalZoomControl.qml（+ / 倍率 / -）
            -> 短按：中间完整zoomStep或唯一边界吸附 -> 0x0f -> 两次0x18目标命中
           -> 长按420 ms：重复同一目标规则，前一目标双确认后再继续
           -> 松开/取消/移出/离线/销毁：取消后续步骤，已发目标继续确认，不发0x05
        -> 下部思翼机内相机控制
           -> QGC拍照/录像图标，无原生缩放滑块
           -> 0x0c拍照可离线重试；切换录像需先取得0x0a状态
           -> 0x0b功能反馈 + 0x0a录像状态校正
        -> 不依赖Vehicle、飞控或MAVLink相机管理器
     -> Gimbal disabled && activeVehicle存在
        -> 回退原生 PhotoVideoControl
  -> custom FlyViewCustomLayer.qml
     -> showHeadingCompassBar && Vehicle.heading 有效
        -> 显式 Loader 加载 FlyViewCompassBar.qml
        -> 11 个相对方位 Label + 当前航向数值 + 固定指针
        -> 示例首选宽度 + Fly View 屏幕边界钳制，不受角落 inset 挤压
        -> 合并 bottomEdgeCenterInset
     -> 燃料电池母线低电压告警
```

## 12. 构建与验证

Ubuntu 24.04 推荐使用项目要求的 CMake 3.25+ 和 Qt 6.8.x，切换分支或改动 QRC/CMake 后执行干净配置和构建。

Android arm64 Release 建议与当前 CI 环境保持一致：Qt 6.8.3 Android kit、JDK 17、Android SDK 35、NDK r26b 和 `arm64-v8a`。若遥控器安装的是 32 位 APK，还需单独构建并验证 `armeabi-v7a`。

Ubuntu 上建议使用独立构建目录执行 Android arm64 干净配置：

```bash
/opt/Qt/6.8.3/android_arm64_v8a/bin/qt-cmake \
  -S . \
  -B ../build-qgc-android-arm64 \
  -G Ninja \
  -DCMAKE_BUILD_TYPE=Release \
  -DQT_HOST_PATH=/opt/Qt/6.8.3/gcc_64 \
  -DQT_ANDROID_ABIS=arm64-v8a \
  -DQT_ANDROID_BUILD_ALL_ABIS=OFF \
  -DQGC_STABLE_BUILD=OFF

cmake --build ../build-qgc-android-arm64 --parallel
```

第一次加入 Android overlay 后必须新建 Android 构建目录，或只删除旧 Android 构建产物后重新 configure，不能只执行增量 APK 打包。配置日志应出现 `QGC: Overlaying custom Android package files`；并检查：

```bash
grep '^QGC_ANDROID_PACKAGE_SOURCE_DIR' ../build-qgc-android-arm64/CMakeCache.txt
grep 'QGC_CUSTOM_ANDROID_USB_SERIAL_MANAGER_V1' \
  ../build-qgc-android-arm64/custom/android/src/org/mavlink/qgroundcontrol/QGCUsbSerialManager.java
```

第一条必须指向当前 build 下的 `custom/android`，第二条必须命中 custom 标记；否则安装的 APK 仍可能使用原生 Java 类。

重点验证：

1. Application Settings -> Fly View -> Instrument Panel 显示 `Show Heading Compass Bar`，页面同时保留Viewer3D和 `SIYI Gimbal Camera`，后者描述私有UDP缩放/拍照/录像与短按/长按行为，且该页不再显示两个视频流开关。
2. Application Settings -> Video 保留全部原生设置组，并在所有平台显示 Video Stream Integration 及两个开关；H.265 强制硬解设置仅在 Android 生效。
3. Viewer3D Enabled 持久化，重启后图标状态正确。
4. 3D 图标白色，2D/3D 可往返切换。
5. 本地 OSM、外部 OBJ/glTF/GLB 和可选 Google 3D 正常加载。
6. 验证云台在线发现与飞控解耦：
   - Gimbal Enabled但飞控和云台都未接入时，合并栏仍必须立即显示并占用完整布局尺寸；Manager继续在后台探测，状态点灰显、倍率显示 `--`，两个缩放按钮禁用且短按/长按均不得发送0x0f或0x05。拍照仍可尝试发送，录像保持 `...` 和禁用。错误来源逻辑IP、短包、错误帧头/长度/CRC、control不是0x02或业务payload非法都不能把 `sdkResponding`置true。
   - 用测试socket分别回送原生IPv4来源和IPv4-mapped IPv6来源，二者表示同一配置IP时都必须通过来源检查；同IP但回包源端口不同且CRC合法时也必须接受。真正不同的来源IP仍须丢弃，并可在 `gcs.custom.gimbal.sdk` debug日志中看到原因。
   - 只连接A8 Mini网络、正常拉流并接通私有SDK、完全不连接飞控时，合法回包必须把状态点切为绿色；受支持的实际拉流尺寸或合法0x16能力，加两份一致0x18即可恢复实时倍率，0x0a恢复机内录像状态。连接或断开飞控不得影响控制栏可见性和后端选择。
   - 空闲探测时云台断线超过1.5秒，合并栏不得消失，只将倍率/录像状态标记unknown、状态点变灰且倍率显示 `--`；正在进行的倍率操作允许在10秒整体deadline内串行重试。缩放按钮禁用，拍照仍允许重试，录像保持禁用。若此前正在长按，只取消尚未发送的后续绝对步骤，不发送0x05停止包。重新接入后无需进入设置页即可恢复状态同步。
7. 验证合并控制栏的输入、安全、相机状态和布局：
   - 验证实际拉流尺寸来源：1280×720/736必须确认6.0x，1920×1080/1088必须确认5.5x，2560×1440必须确认3.5x，3840/4096×2160必须确认1.0x且两个方向都无下一目标。先验证任一直接观察器报告尺寸时立即取消 `VideoManager`兜底；再模拟两个直接观察器均不回调，要求 `decoding=true`且同一受支持 `VideoManager::videoSize`连续稳定1秒后才采用，尺寸在1秒内变化时必须重新计时。短暂停止解码应保留已确认能力但清除旧直接尺寸标记；重连后即使新管线的直接观察器失效，稳定1秒的VideoManager新尺寸也必须能替换旧上限。同一QGC进程内依次执行4K→1080P和1080P→4K，必须更新为5.5x和1.0x并重新双确认，不得要求重启。新的640×480或1366×768直接协商尺寸应释放旧映射并转入0x16后备，空尺寸不应误清空能力。
   - 抓包确认尚无受支持拉流尺寸时Manager每2秒发送0x16；合法范围应激活临时能力并继续发出0x18，两个一致实际倍率回包后解锁。0x16 `00 00`、超过6.0x、错误小数、错误长度/CRC/control必须拒绝。随后报告受支持的实际拉流尺寸时，尺寸映射必须取代0x16临时值、安全清理旧目标并重新双确认；保持受支持尺寸期间的迟到0x16不得改变上限，新的不支持直接尺寸释放旧映射后则重新允许0x16恢复。只改变机内卡录分辨率不得改变结果。
   - 验证0x16/0x18双格式解析。新版：`01 00`→1.0x、`01 08`→1.8x、`02 08`→2.8x、`03 05`→3.5x、`05 05`→5.5x；真机旧版：`0A 00`→1.0x、`12 00`→1.8x、`1C 00`→2.8x、`23 00`→3.5x、`37 00`→5.5x。必须逐包先尝试新版，只有新版候选越过当前A8范围时才计算完整小端uint16/10旧版候选并再次校验；新版和旧版成功日志应分别带 `encoding integer plus decimal`和 `encoding little-endian tenths compatibility`。`00 00`、0.9x、超过当前上限、`FF FF`、非法小数字节、短/长payload及带尾随字节的完整帧均不得改变倍率或编码输出参数。
   - `-`/`+`短按只发送一次目标0x0f，释放不得重复触发。步长1.0x时1080P严格覆盖1→2→3→4→5→5.5及5.5→5→4→3→2→1，2K严格覆盖1→2→3→3.5及3.5→3→2→1；其中5.0↔5.5和3.0↔3.5是唯一允许的小于步长边界转换。1.8→2.8、2.8→1.8等其他非网格目标不得作为用户步进直接发送。快速点击 `+++`在首个目标确认前不得追加第二个0x0f；确认前倍率显示 `--`。
   - 按住420 ms后抓包应看到当前序列的一个0x0f目标；只有两个串行0x18查询都命中该目标后，才允许在180 ms后发送下一完整网格步或唯一边界吸附。松开、取消、移出、控件隐藏/销毁、SDK异常、endpoint/Enabled变化和应用退出只取消后续目标，不发送任何0x05方向或停止包，也不得补发短按。60秒看门狗必须兜底。
   - 验证查询串行化：0x0f后先有约350 ms旧包隔离，同一时刻最多一个0x18查询在途；单次查询1秒超时后重新隔离再查，10秒整体deadline不被重试刷新。构造“1.8、1.8、2.0、2.0”回包，目标2.0时前两包不得显示或完成，只有后两个独立目标命中才显示2.0；丢失一个查询回复、negative 0x0f ACK、迟到回包和快速反向操作均不得产生1.8/2.8显示或并发目标。
   - 拍照按钮发送0x0c功能0，只有存在pending拍照请求时成功0x0b反馈才触发绿色提示并递增照片数；录像状态未同步或命令pending时按钮必须灰显且不能再次发送toggle。状态已知后发送0x0c功能2，必须先建立target/pending门控再发出乐观状态变化；约400 ms后发送0x0a查询，命令前在途旧状态及与target不一致的0/1不得完成pending，2/3立即报错，2.5秒未确认回到unknown。分别覆盖正常开始/停止、同步信号重入、快速连点、无存储卡状态2、录像数据丢失状态3及0x0b功能4失败；0x0b大于4的未知值必须在Protocol层拒绝，不能上报为业务反馈或当成录像成功。
   - 合并栏必须保持“上部缩放、下部拍照/录像”的纵向单栏结构，严格按 `+ -> 当前倍率 -> - -> 横向分隔线 -> 拍照 -> 录像` 排列，使用QGC相机图标且不出现原生缩放滑块。Gimbal启用时右侧Column宽度和Loader高度必须始终随控制栏完整隐式尺寸扩展，而不是在离线时缩为0、把整栏压成一个按钮高度或裁掉底部录像按钮；在Android 86%/100%、桌面、横竖屏、地图/视频主窗口、PIP、Viewer3D和小屏触摸场景检查不裁切、不重叠、按钮达到移动端最小触控尺寸。
   - Gimbal Disabled且存在活动飞行器时恢复原生 `PhotoVideoControl`；关闭时没有活动飞行器则不加载原生控件。关闭后回送关闭前轮询产生的迟到0x18/0x0a/0x0b或任意0x16，不能重新把私有SDK标记在线、改变已清空的状态或发送0x0f。Gimbal Enabled但离线时仍显示私有合并栏，以灰色状态明确离线，不得用原生控件替换或把整栏隐藏。
8. Ubuntu 24.04 播放同一路 H.265 RTSP 保持正常；Ubuntu/虚拟机代理需将 `192.168.144.25` 加入忽略列表。
9. Android 使用云台 H.264 编码回归测试，画面、延迟和断流重连均不退化。
10. Android 使用云台 H.265 编码连续播放至少 10 分钟，分别记录开始、5 分钟和 10 分钟的端到端延迟，确认延迟不持续增长；同时测试应用前后台切换和断流重连。
11. 真机日志中确认 `qgcandroidh265hwdec` 使用厂商 `amcviddec-*`，其输入 caps 为 `stream-format=byte-stream,alignment=au`，并出现首个 raw frame 的 `hardware confirmed` 日志；不能再用 rank 单独判断硬解是否成功。
12. H.265 播放期间开始和停止录像，确认录像文件仍可正常回放；这验证播放支路转换没有影响原生 `hvc1` 录像支路。
13. 在没有兼容 H.265 硬解的 Android 设备上，适配器不应注册，日志应告警未找到厂商 MediaCodec，原生软件 ranks 保持原值且不应直接黑屏。
14. Fuel 遥测存在时顶部显示 Fuel，无数据时隐藏。
15. 缺少 `local` 链路时下次启动自动补建，已有同名链路不会重复或被覆盖。
16. Factory能力列表应声明PX4 + MultiRotor，APM不出现在支持列表中；同时用一个非多旋翼PX4 heartbeat确认当前边界：由于 `firmwarePluginForAutopilot()` 尚未检查 `vehicleType`，它仍会取得CustomFirmwarePlugin，不能把“支持列表只声明多旋翼”误当成运行时硬拒绝。
17. 普通模式只显示 Safety 设置页，高级模式显示完整定制 PX4 设置页。
18. 飞行模式仅 Loiter、RTL、Mission 可由该列表设置，RC RSSI 不显示，Fuel 紧随 Battery。
19. Android 冷启动前已插入飞控，以及 QGC 启动后再插入飞控，两种顺序均可自动连接；无权限时只请求一次，当前 attach 会话已有权限时不重复弹窗。
20. 同一根 USB 线不拔，QGC 主动断开/重新连接至少 20 次；不得出现 `Attempt to open unknown device` 或重复端口，每次 close 日志回到 `openResources=0`，下一次 open 为 `openResources=1`，driver 和 pending permission 数量不持续增长。
21. 保持 MAVLink 已连接时拔出/插回至少 20 次，并覆盖飞控 bootloader 到 application 的重枚举；每轮都先释放旧端口再创建新端口。
22. 拔出最后一个串口设备后再插入，旧 driver 不得残留；拒绝权限后拔插并改为允许，应能恢复枚举和连接。
23. QGC 前后台切换和 Activity 重建后 receiver 仍能收到新拔插事件；思翼内置视频 USB 与飞控同时存在时，只有串口设备进入 QGC 端口列表。
24. 先由思翼地面站或串口工具独占飞控端口，确认 QGC 明确记录 open 失败；关闭占用方后重新连接，QGC 无需杀进程即可成功。
25. `Show Heading Compass Bar` 开关无需重启即可立即显示/隐藏，重启 QGC 后保持用户选择；没有活动飞行器或 `heading` 为 NaN 时不显示伪造的 0°/N。
26. 使用模拟或真机航向覆盖 N、NE、E、SE、S、SW、W、NW，并重点检查 359° -> 0° -> 1° 连续过渡，中央数值、固定指针和移动方位必须一致。
27. 在地图主窗口、视频主窗口、地图/视频 PIP 互换、虚拟摇杆、右下仪表盘、Viewer3D、横竖屏和小屏布局下检查罗盘条底边位置、宽度及 `bottomEdgeCenterInset`；重点在目标遥控器 86% 缩放下把左下 PIP 从 10% 连续拖到 75%，以及改变右下仪表宽度，罗盘条必须保持示例的首选宽度，不能缩成点、短条或消失。常规尺寸下检查无不必要遮挡；极端放大的角落控件允许与罗盘条视觉层叠，但 PIP 调整和罗盘条区域的地图拖动、缩放必须仍然有效；全屏视频模式按原生语义隐藏。
28. Android H.265 连续播放测试期间同时保持罗盘条开启并改变航向，确认 11 个方位 Label 的更新不造成新增卡顿或持续帧率下降。
29. 在采用 14 pt 平台基准的目标 Android 遥控器上清除应用数据或净安装 APK，首次进入 Application Settings -> General 时 UI Scaling 应显示 86%，运行中的 `appFontPointSize` Fact 应为整数 12 pt。
30. 在 Android 上使用原生 `-`/`+` 修改整数点数并重启 QGC、覆盖安装保留数据的新 APK，必须保持用户值而不是恢复 86%；执行“清除全部设置”后才恢复 12 pt 缺省值。分别净安装 Ubuntu、Windows、macOS/iOS 构建，默认应保持原生 100%。
31. 物理宽度小于 120 mm 的极小 Android 设备单独确认平台基准和页面显示值；其 11 pt 基准无法用整数点数精确表示 86%，不得把固定 12 pt 一概描述为所有 Android 屏幕的 86%。

Android 调试时同时关注 `gcs.custom.video.androidvideodecoderpolicy` 和 `gcs.custom.video.androidh265hardwaredecoderadapter`。可用 QGC Application Messages 或 `adb logcat` 查看：

```bash
adb logcat -v threadtime | grep -Ei \
  "androidvideodecoderpolicy|androidh265hardwaredecoderadapter|qgcandroidh265hwdec|amcviddec|avdec_h265|byte-stream|hvc1|not-negotiated|configure codec"
```

关键日志分三层判断：

1. `Registered qgcandroidh265hwdec ... hvc1 -> byte-stream/au conversion`：适配器及厂商 factory 已找到并注册；候选预检日志中的 `lowLatencyVariant true` 表示选中了厂商专用低延迟组件。
2. `selected actual decoder ... negotiated sink caps ... byte-stream ... alignment=au`：播放管线已经把 Annex-B AU 送入该 MediaCodec。
3. `produced its first raw frame ... hardware confirmed`：经过软件黑名单筛选的厂商 MediaCodec 已经真实输出首帧，这是 QGC 管线侧最强的运行证据；`glMemoryOutput true` 表示同时走通 GLMemory 显示路径。系统级硬件属性仍以 Android API 29 `isHardwareAccelerated()` 为准。

若只有前两层而没有第三层，或出现 `not-negotiated`、`Failed to configure codec`、profile/level 不支持等错误，说明厂商 MediaCodec 在收到真机流参数后配置失败；应保留完整 logcat，不能把它误判成“rank 已正确所以硬解成功”。

Android USB 调试使用独立的 logcat 过滤：

```bash
adb logcat -v threadtime | grep -Ei \
  "QGCUsbSerial-Custom|qgc.android.androidserial|qserialport_android|SerialLink|UsbHostManager|USB_PERMISSION|USB_DEVICE_(ATTACHED|DETACHED)|Attempt to open unknown|No USB device connection"
```

看到 `Initialized custom-usb-v1` 可确认 APK 已使用 custom overlay。随后按顺序判断：

1. `USB topology changed: Android Host sees 0 device(s)`：Android 未枚举，先处理 OTG Host、USB 口角色或数据线。
2. `USB device visible but no serial driver matched`：设备已枚举但驱动未匹配，保存同一行 VID/PID 和 interfaces。
3. `Requesting permission` 后必须有 `Permission granted`；若 denied，拔插或重启后重新授权。
4. `Discovered ...`、`Reporting N authorized USB serial device(s) to QGC`（`N >= 1`）和 `USB serial port opened` 依次出现，表示 Java 枚举、Qt 端口发现和实际独占打开均成功；此后仍无飞行器再检查 AutoConnect Pixhawk、USBBoardInfo 识别和 MAVLink heartbeat。

运行日志出现 `GimbalCameraControl is not a type`、`GimbalZoomControl is not a type` 或 `Gimbal camera control failed to load`，首先检查 `custom.qrc` 是否同时注册 `QGroundControl/FlightDisplay/GimbalCameraControl.qml` 和 `GimbalZoomControl.qml`。顶层由 `FlyViewTopRightColumnLayout.qml`使用完整 `qrc:/Custom/qml/QGroundControl/FlightDisplay/GimbalCameraControl.qml`地址显式加载，缩放子控件再从同一资源目录解析；它们不加入原生FlightDisplay qmldir。新增QRC文件后必须重新构建资源，若仍命中旧缓存，应新建构建目录后重新configure，而不是修改 `src` qmldir。

Gimbal Enabled但合并栏不显示时，不要检查飞控、云台回包或 `activeVehicle`：当前可见性已完全与连接状态解耦，只要Enabled为true就必须显示。优先检查设置值、custom QRC命中、Loader错误及资源是否重新构建。若控制栏显示但状态点持续灰色，再确认A8 Mini供电和网络、`sdkHost/sdkPort`、本机路由及2秒轮询；RTSP视频与私有UDP SDK是独立链路。接收端接受逻辑等价的IPv4/IPv4-mapped IPv6来源，也不强制回包源端口为37260，但要求来源逻辑IP、帧头、精确长度、CRC、control=0x02及业务payload全部合法。

视频有画面且SDK已有0x0a回包、但缩放按钮仍灰色时，按三级能力链检查日志。第一级是 `gcs.custom.customplugin` 的 `Installed main pulled-video resolution observers`，随后应出现 `gcs.custom.gimbal.pulledvideoresolution` 的 `Negotiated main pulled-video resolution` 或 `gcs.custom.gimbal.control` 的 `Observed negotiated main pulled-video resolution`；第二级是在两路直接观察器都未报告时出现 `Waiting for stable VideoManager pulled-video resolution fallback`，约1秒后以 `stable VideoManager fallback`为source确认；第三级是在尺寸仍未确认时，`gcs.custom.gimbal.sdk` 每2秒发出命令22（0x16），合法响应后出现 `Activated SIYI zoom capability`。首次尺寸确认记录 `Latched pulled video resolution`，运行期受支持尺寸变化记录 `Updated pulled video resolution`；不支持的直接尺寸会记录 `Released stale pulled-video zoom capability`并回到0x16。任一级激活能力后都应在350 ms隔离后发出命令24（0x18），收到两次一致合法0x18后出现 `Confirmed stable SIYI zoom ... zoom controls are ready`。若0x16有发送却无合法响应，检查设备固件是否支持当前范围查询和原始回包；若命令24有发送却不能双确认，检查0x18 payload与UDP丢包，而不再把问题误判为QML按钮锁定。

拉流尺寸白名单为1280×720/736、1920×1080/1088、2560×1440及3840/4096×2160，对应6.0x、5.5x、3.5x及1.0x。未到边界时至少一个方向应可用；1.0x时减号灰显、最大倍率时加号灰显是正确边界。4K模式的真实上限就是1.0x，因此两个方向都灰显是相机不支持该模式数字变焦，不属于状态机锁死。若设置页出现分组拉伸、顶部裁切或无法滚到底，检查 `FlyViewSettings.qml` 外层Loader的 `Layout.preferredHeight/minimumHeight`、两个设置组根Loader的 `implicitHeight` 透传，以及是否重新引入了用长说明撑大隐式宽度或手工写 `item.width` 的逻辑。

初始镜头1.0x却显示错误、实际与显示不一致或缩放按钮始终灰色时，先查看0x18原始payload。新版 `01 00`表示1.0x、`01 08`表示1.8x；A8真机旧版会返回 `0A 00`表示1.0x、`12 00`表示1.8x。日志若持续出现 `Rejected invalid SIYI current zoom payload "0a 00"`，说明运行的仍是未加入旧版后备的旧构建。新实现对此应记录 `Decoded SIYI current zoom 1 encoding little-endian tenths compatibility`，并在第二份一致回包后记录 `Confirmed stable SIYI zoom 1 - zoom controls are ready`。启动、重连、外部变化及0x0f目标都必须在两个0x18查询得到一致实际值后才更新显示；0x0f ACK与requested目标都不能直接成为显示值。上限首先查看已确认拉流尺寸：720P、1080P、2K、4K分别应为6.0x、5.5x、3.5x、1.0x；尺寸尚未确认时0x16可以临时提供设备当前上限，受支持尺寸会覆盖该后备，卡录分辨率始终不参与。运行期更换为另一受支持拉流规格会更新上限并重新双确认，不需要重启QGC。步长1.0时，5.0↔5.5和3.0↔3.5产生0.5距离是预期的真实上限双向吸附；其他中间位置保持1.0完整步长。倍率网格以1.0x为锚点，并非要求显示值是 `zoomStep` 相对0的整数倍；真实小数上限也是明确允许的边界例外。启动或外部控制留下其他非网格倍率时会自动归整到最近合法停点，该过程显示 `--`且完成后才解锁按钮。长按日志应只包含串行的“0x0f目标、0x18查询、0x18查询”，释放不应出现0x05。紧邻应用退出出现的 `PhotoVideoControl.qml`中 `cameraManager/currentCameraInstance`空对象警告来自QGC原生相机面板销毁时序，不参与custom缩放状态机，也不是本次锁定原因。

`FlyViewCompassBar.qml` 同样不加入原生 FlightDisplay qmldir，而由 `FlyViewCustomLayer.qml` 使用 `qrc:/Custom/qml/QGroundControl/FlightDisplay/FlyViewCompassBar.qml` 显式加载。若设置开关存在但界面不显示，先检查 Application Messages 中的 `Fly View compass bar failed to load`，再确认 `custom.qrc` 已重新编译、存在活动飞行器且 `Vehicle.heading` 不是 NaN。

常用静态检查：

```powershell
rg --files custom
rg -n "adjustSettingMetaData|appFontPointSize|FlyViewCompassBar|FlyViewCustomSettings|showHeadingCompassBar|GimbalCameraControl|A8MiniZoomPolicy|startZoom|takePhoto|toggleVideoRecording|CommandManualZoom|CommandMaximumZoomValue|CommandCurrentZoomValue|CommandCameraSystemInfo|CommandFunctionFeedback|CommandPhotoAndRecord" custom
rg -n "CustomIconButton|CustomOnOffSwitch|CustomVehicleButton|CustomAttitudeWidget" custom
git diff --check
```

桌面测试构建启用 `QGC_BUILD_TESTING` 后，至少运行：

```powershell
cmake --build <desktop-build> --target check_siyi_protocol
ctest --test-dir <desktop-build>/custom -R '^SiyiProtocolTest$' --output-on-failure
```
