# QGC 二次开发说明

> **文档定位**：说明当前版本的功能、操作方式、实现链路与维护入口。
>
> **阅读顺序**：先看进度与架构，再按模块查阅；新增功能沿用第 5 节模板。

| 项目 | 内容 |
|---|---|
| 工程 | `qgroundcontrol_viewer3d` |
| 分支 | `SecDev/ft/control` |
| 应用名 | `Custom-QGroundControl` |
| 业务代码入口 | `custom/` |
| 本次代码核对基线 | `71a84cb4d`，包含工作区现有的 A8 采集工具 |
| 文档更新 | 2026-09-16 |

## 阅读导航

| 章节 | 主要内容 |
|:---|:---|
| [01 · 当前开发进度](#progress) | 本版能力、已验证范围与待办 |
| [02 · custom 架构](#architecture) | 目录组织、启动链路、对象接口与原生边界 |
| [03 · 功能模块](#modules) | 功能、配置、状态、实现流程与源码文件 |
| [04 · 构建与验证](#verification) | 构建条件、自动检查、真机矩阵与采集工具 |
| [05 · 后续维护](#maintenance) | 扩展步骤、文档模板与验收记录 |

| 按功能查阅 | 模块入口 |
|:---|:---|
| 场景与画面 | [3.1 三维视图](#viewer3d) · [3.2 双视频与解码](#video) · [3.8 双罗盘](#compass) |
| 相机与操控 | [3.3 A8 Mini](#a8) · [3.4 MT11](#mt11) · [3.5 本地媒体](#media) · [3.6 云台姿态](#gimbal) · [3.7 UniRC](#unirc) |
| 飞控与遥测 | [3.9 电源与燃料](#power) · [3.10 距离提示](#radar) · [3.11 通信](#comms) · [3.12 PX4 定制](#px4) |
| 产品配置 | [3.13 设置与翻译](#settings) |

**首次接手项目**：阅读第 1、2 节，随后选择一个模块沿“界面 → Manager → 协议/原生接口”查看代码。

**部署和使用**：先核对 3.11 的通信，再配置 3.2 的视频及对应相机，最后按 4.2 验收。

**继续开发**：先沿 [2.1 custom 文件树](#custom-file-tree) 查阅逐文件职责，再看第 3 节对应功能的文件/资源组合和实现流程，按 5.3 同步代码、设置、资源与验证。

---

<a id="progress"></a>

## 1. 当前开发进度

本表区分源码已实现的能力与设备验证状态。本次按当前工作区核对文件、设置和调用流程，并检查文档结构；未重新编译产品或执行真机验收。表中已有实测反馈保留原有证据范围，不能据此推定本版全部场景已通过。

> **状态口径**：“已集成”表示源码已有构建接入和运行调用链；“主机通过”仅覆盖注明的测试范围；“真机通过”需要对应版本、设备和场景的证据。未完成的项目保留在“验证状态与下一步”列。

| 模块 | 当前已实现 | 验证状态与下一步 |
|---|---|---|
| Viewer3D | OSM、外部模型、可选 Google 3D；本地场景显示飞行器与任务 | 已集成；按目标平台验证导入、坐标配准和可选 WebEngine 能力 |
| 双视频与 Android 解码 | 独立 Video 1/2、三视图切换、双辅窗连续缩放、厂商硬解、逐路恢复 | PIP 缩放 Qt 6 主机事件回归通过；桌面 MT11 播放已有实测；双路实播缩放及 Android 双路、交换源和持续播放仍需完整验收 |
| A8 Mini 相机 | 缩放、拍照、录像、能力查询与播放后停滞恢复 | 已有真机使用及新版目视播放正常反馈；当前缩放与长期稳定性需按矩阵回归 |
| MT11 相机 | 独立 SDK、短按/长按变倍、三种视频模式、媒体控制 | 协议和策略已有主机测试；手势、模式画面及 Android 链路待真机验收 |
| 本地照片与录像 | 两路独立保存及 Android 图库发布；A8 支持断流分段续录，MT11 断流停止本地录像 | 已集成；两路恢复差异、存储容量、退出收尾及卸载保留待完整验收 |
| 云台姿态与模式 | 自动申请控制权、共享回中、实际模式回读及切换闭环 | 重连模式显示已有确认；最新模式切换和会话隔离待 Android 回归 |
| UniRC 10 Pro | 蓝牙 SDK、16 通道显示、CH9 变倍、CH10 回中/俯视交替 | 蓝牙通道、CH9 和基础回中已有实测；动态交替、手动复位及顶部联动待验收 |
| 双罗盘 | 飞行器航向、活动 MAVLink 云台世界方位角 | 已集成；当前反馈换算有实测依据，仍需锁定/跟随、转动基座和失联回归 |
| 电源、Fuel 与母线告警 | 电压/功率、多级低压状态、燃料详情、参数化母线告警 | 已集成；需结合当前飞控参数和遥测验收 |
| Proximity Radar | 十方向距离、低于 5 m 的红色闪烁提示 | 已集成；需验证目标传感器方向与数据 |
| 通信与 Android USB | 默认 UDP 配置、USB 串口授权/枚举/热插拔 | 已集成；目标遥控器 USB Host 与飞控重连待真机验收 |
| PX4 定制 | 产品插件、常规模式列表、普通/高级设备设置页 | 已集成；跟随飞控固件和参数版本回归 |
| 设置、翻译与布局 | Fact 持久化、Fly View 自适应布局、Android 默认字号、中文资源 | 已集成并有布局检查工具；净安装、升级保留和目标屏幕待回归 |

待办只在本表保留当前结论；具体验收范围见 [4.2](#acceptance)。测试通过时，应记录平台、构建版本和结论，再更新对应行。

---

<a id="architecture"></a>

## 2. custom 架构与集成边界

<a id="custom-file-tree"></a>

### 2.1 custom 文件树与逐文件职责

下面按 `custom/` 的实际目录层级逐文件展开。**文件名单独占行，其下两个完整说明点分别为：①负责的内容与接口；②实现方式、输入输出或协作关系。** 同名 `.h` 和 `.cc` 分别说明接口契约与具体实现，资源和测试文件则说明内容、引用方及使用范围。

- 沿文件树的缩进拼接路径即可定位文件；说明统一从第 61 列的 `#` 开始，其前仅使用空格，不放置树枝符号。
- VS Code 工作区已为 Markdown 开启自动换行和 `editor.wrappingIndent: "same"`。窗口缩窄时，说明的续行沿用行首缩进，与 `#` 起始位置对齐；①、②内部不手动断行。
- 本树涵盖业务源码、QML、设置、资源、测试与工具；构建缓存和生成文件不作为维护入口。第 3 节再按功能组合相关文件，说明完整执行流程；2.2～2.4 解释构建接入与原生 QGC 边界。

~~~text
custom/
                                                            # 产品定制代码、资源、平台适配与开发验证
├── CMakeLists.txt
                                                            # ① 构建入口：将 custom C++、复用的原生 Viewer3D 实现和 custom.qrc 纳入应用；查找 Bluetooth、Quick3D、Quick3DAssetUtils，按可用性接入 WebEngineQuick。
                                                            # ② 装配方式：声明 Custom.Widgets/Custom.FlightDisplay QML 模块，生成 Android 模板覆盖目录、编译翻译，并在桌面 QGC_BUILD_TESTING 开启时注册 13 个 C++ 测试目标。
├── custom.qrc
                                                            # ① 资源清单：通过 prefix/alias 定义 QML 页面、设置 JSON、图标、F450 网格和三维 shader 的运行时路径；/Custom/qml 下的同名别名供插件拦截后覆盖原生页面。
                                                            # ② 关联关系：同时引用 custom 文件和保留复用的原生 QML/材质；新增界面或移动资源后需同步路径，CMake 负责打包，CustomPlugin 的 URL 拦截器负责将页面请求导向对应资源。
├── cmake/
                                                            # 构建选项覆盖
│   └── CustomOverrides.cmake
                                                            # ① 产品构建配置：设置 Custom-QGroundControl 应用名称，集中指定本产品采用的 QGC 编译开关，作为主工程配置阶段读取的覆盖文件。
                                                            # ② 替换边界：关闭原生 Viewer3D、APM 相关目标/方言和原生 PX4 Factory，由 custom/CMakeLists.txt 接入定制实现；新增替换项时需同步核对源码列表及工厂注册，避免两套实现同时构建。
├── android/
                                                            # Android Java 覆盖文件，经 CMake 合并到构建模板
│   └── src/
                                                            # Java 源码，目录层级对应包名
│       └── org/
│           └── mavlink/
│               └── qgroundcontrol/
│                   ├── QGCCustomMediaLibrary.java
                                                            # ① Android 媒体落盘实现：选择存储卷和应用暂存目录；照片/录像先在暂存区生成，再经单线程任务复制到公共媒体目录，Android 10 及以上使用 MediaStore，旧版本写公共目录后通知媒体扫描。
                                                            # ② 完成与恢复：记录发布任务及本次安装的媒体登记，提交成功后清理源文件，启动时恢复未完成发布；提供等待发布、删除媒体和容量清理接口，由 AndroidMediaLibrary 的 JNI 桥调用。
│                   └── QGCUsbSerialManager.java
                                                            # ① Android USB 串口桥：枚举 USB Host 设备，以串口驱动探测和 CDC 回退匹配设备；管理广播接收、访问授权和可用端口信息，将设备列表提供给 Qt 串口层。
                                                            # ② 数据与生命周期：打开设备端口并建立 SerialInputOutputManager，将接收/错误回调转交 native；实现同步/异步写入、波特率及控制线设置，设备拔出或关闭时停止 I/O 并释放连接。
├── src/
                                                            # 运行时 C++、QML、设置元数据与产品资源
│   ├── CustomPlugin.h
                                                            # ① 产品插件接口：声明向 QML 暴露的相机、双路视频、UniRC、方位角、云台动作和各设置对象属性，以及产品启动、MAVLink 处理和视频 sink 创建的重载接口。
                                                            # ② 对象归属：保存各 Settings/Manager 的实例及退出状态，声明默认配置、资源重定向、三维注册和媒体收尾相关入口；新增全局业务服务需在此声明，再由 CustomPlugin.cc 创建和接线。
│   ├── CustomPlugin.cc
                                                            # ① 产品启动总装：创建设置与业务 Manager，安装默认链路/视频设置、中文翻译和 QML URL 拦截器，注册 Viewer3D 类型；将这些对象接入原生 QGCCorePlugin 生命周期并提供给界面。
                                                            # ② 运行接线：mavlinkMessage 将消息交给方位角 Provider 并过滤自动视频信息，模式控制器自行订阅 Vehicle 消息；主/次视频项和接收器分别接 A8/MT11，安装尺寸探针与恢复逻辑，退出时收尾媒体、第二路视频和后台发布。
│   ├── Android/
                                                            # Android 媒体桥与 UniRC 蓝牙通道控制
│   │   ├── AndroidMediaLibrary.cc
                                                            # ① JNI 适配实现：调用 QGCCustomMediaLibrary 的静态方法，转换 QString、Java 返回对象及异常结果，为暂存、发布、配额清理和等待提供统一 C++ 入口。
                                                            # ② 媒体链路：相机 Manager 完成截图或关闭录像文件后调用发布接口，Java 负责公共目录提交与恢复；新增存储能力时需同步本文件、AndroidMediaLibrary.h 和 Java 方法签名。
│   │   ├── AndroidMediaLibrary.h
                                                            # ① 平台媒体接口声明：提供暂存目录、旧媒体源目录、文件发布、已发布录像清理、等待发布和媒体删除方法，供 A8/MT11 本地照片与录像逻辑共用。
                                                            # ② 调用约定：以 C++ 路径、文件类型和执行结果隔离 Android Java API；Manager 负责生成文件与控制录制，本接口只承接平台存储操作，具体 JNI 签名和结果转换在同名 .cc。
│   │   ├── UniRcChannelController.cc
                                                            # ① 接收链路：依据启用开关、前后台状态、蓝牙权限和配置 MAC 建立 RFCOMM 连接，发送 20 Hz 通道请求；区分写入本地队列与实际发送，经 StreamParser 组帧后刷新 16 路数值和诊断状态。
                                                            # ② 动作链路：用 UniRcChannelPolicy 将 CH9 转为 A8 连续变倍、CH10 转为共享回中/俯视，CH7/8 手动输入复位动作序列；首包/持续输入超时、失联或切后台时停止动作、清空输入并按条件重连。
│   │   ├── UniRcChannelController.h
                                                            # ① UniRC 控制器契约：声明 bluetoothConnected、sdkRouteActive、channelInputActive、channelValues、channel9/channel10 和诊断信息等 QML 属性，以及 shutdown 退出接口。
                                                            # ② 异步状态：保存 Bluetooth socket、权限/应用状态处理、请求发送阶段、首包及输入 watchdog、重连定时器和通道动作状态；通过设置、A8 Manager 与回中协调器连接输入和执行端。
│   │   ├── UniRcChannelPolicy.cc
                                                            # ① 数值判定：检查 900～2100 的有效输入；CH9 在 1475～1525 回中后按方向输出并应用反向开关，CH10 用 ≤1250 释放、≥1750 按下判断有效边沿。
                                                            # ② 动作保护：CH7/8 超出 1400～1600 判为手动控制；CH9/10 非法输入清除已就绪状态，恢复后仍须重新回中/释放，避免控制器把失效数据或持续按住状态当成新操作。
│   │   ├── UniRcChannelPolicy.h
                                                            # ① 纯通道策略接口：定义 CH7/8/9/10 输入的有效范围、方向与边沿状态，以及处理结果结构，向控制器返回需要执行的动作而不直接访问蓝牙或云台。
                                                            # ② 状态约定：CH9 必须先回中才能输出方向，CH10 必须先释放才能识别按下；保留反向配置和输入保护所需状态，规则实现位于同名 .cc，可由独立测试直接调用。
│   │   ├── UniRcProtocol.cc
                                                            # ① 编码与校验：封装 55 66 帧头、控制字、长度、序号、命令和 CRC16-XMODEM，生成开启 20 Hz 上报及关闭请求，检查通道包命令、长度与 CRC。
                                                            # ② 流式解析：累积 Bluetooth 分段数据，处理半帧、连续多帧和无效帧重同步；将合法 0x42/32 字节载荷解码为 16 路数值，供 UniRcChannelController 更新界面和执行动作。
│   │   └── UniRcProtocol.h
                                                            # ① UniRC 字节协议声明：定义命令、通道数据包、CRC 与启停通道请求接口；StreamParser 保存跨次接收的字节缓存，并向控制器输出完整解析结果。
                                                            # ② 数据约定：通道上报包含 16 个 int16 数值，帧长、序号及数值采用协议规定的字节序；本文件负责通信数据契约，通道阈值和动作含义由 UniRcChannelPolicy 解释。
│   ├── AutoPilotPlugin/
                                                            # 设备设置页面定制
│   │   ├── CustomAutoPilotPlugin.cc
                                                            # ① 页面组合实现：参数就绪并通过版本条件后生成设备组件；普通模式提供 Safety，高级模式加入 Airframe、Sensors、Radio、Flight Modes、Power、Actuators/Motors 和 Tuning 等页面。
                                                            # ② 更新流程：复用原生组件的检查及配置能力，根据固件条件选择执行器或电机页面；高级模式改变时清理缓存并发出列表变化通知，让界面重新取得当前模式的组件集合。
│   │   └── CustomAutoPilotPlugin.h
                                                            # ① 设备设置入口声明：扩展 PX4 AutoPilotPlugin，声明 vehicleComponents 组件列表及高级模式更新槽，保存安全、传感器、遥控、电源、执行器等设置页对象。
                                                            # ② 组合关系：由 CustomFirmwarePlugin 为车辆创建；各组件复用原生 PX4 页面，本类决定它们在普通/高级模式下是否进入设备设置导航，实际列表生成在同名 .cc。
│   ├── Comms/
                                                            # 默认通信配置安装
│   │   ├── DefaultCommunicationLinkInstaller.cc
                                                            # ① 安装条件：读取 QSettings 中保存的链路数量，仅在可确认列表为空时写入 local UDP；已有链路或无效数量值不覆盖，保存后同步设置。
                                                            # ② 默认内容：本地监听 UDP 14550，目标 192.168.144.20:19856，自动连接和高延迟均关闭；CustomPlugin 在 LinkManager 加载前调用，后续用户在原生通信连接页面管理该配置。
│   │   └── DefaultCommunicationLinkInstaller.h
                                                            # ① 默认链路安装入口：声明静态 ensureInstalled，供产品初始化阶段在原生 LinkManager 读取持久化连接列表之前调用。
                                                            # ② 责任边界：只负责首次空列表的保存配置，不创建运行中的通信连接；是否自动连接、UDP 监听端口与目标地址由同名 .cc 写入并交给原生链路系统执行。
│   ├── FirmwarePlugin/
                                                            # PX4 固件插件、模式、参数和工具栏定制
│   │   ├── CustomFirmwarePlugin.cc
                                                            # ① 飞控能力实现：设置 Pause/Return/Mission 等模式的可选状态，声明 pitch/yaw 云台轴能力并创建 CustomAutoPilotPlugin；为 UAVCAN 电压阈值提供元数据默认值，不强制写入飞控参数。
                                                            # ② 界面接入：调整 toolIndicators，移除遥控 RSSI 项，在电池后加入燃油、GPS 后加入避障雷达；对应外观在 toolbar/QmlControls，SYSTEM_TIME 处理用于诊断而不修改发送时间。
│   │   ├── CustomFirmwarePlugin.h
                                                            # ① PX4 产品固件接口：声明飞行模式、工具栏指示器、云台轴能力、参数元数据及 AutoPilotPlugin 创建等重载方法，集中规定产品对原生 PX4 行为的定制范围。
                                                            # ② 使用关系：CustomFirmwarePluginFactory 返回本类实例，Vehicle 通过原生 FirmwarePlugin 接口使用它；工具栏 QML 路径、模式可选性和参数显示规则在同名 .cc 实现。
│   │   ├── CustomFirmwarePluginFactory.cc
                                                            # ① 工厂选择实现：向 QGC 声明 PX4/多旋翼支持类别，firmwarePluginForAutopilot 根据 autopilotType 选择并缓存 CustomFirmwarePlugin 实例。
                                                            # ② 构建关联：配合 CustomOverrides 关闭原生 PX4 Factory，避免产品入口冲突；支持机型列表是能力声明，此处创建分支主要判断 PX4 类型，不另外按每种 vehicleType 分流。
│   │   └── CustomFirmwarePluginFactory.h
                                                            # ① 固件插件工厂契约：声明支持的固件类型、机型类别及 firmwarePluginForAutopilot 创建接口，保存产品 FirmwarePlugin 实例供原生工厂机制调用。
                                                            # ② 职责边界：负责选择使用哪个固件插件，不处理遥测或绘制界面；实际 PX4 匹配、实例复用和不支持类型的返回行为在同名 .cc。
│   ├── FlightDisplay/
                                                            # 飞行页、相机栏、PIP、罗盘与告警 UI
│   │   ├── DualPipView.qml
                                                            # ① 双辅窗布局：将地图、Video 1、Video 2 作为 item1/item2/item3，绘制两个 PipPane 及窗口边框、展开/收起和弹出操作；缩放按父容器坐标计算位移，联动更新两个 16:9 辅窗并钳制尺寸边界。
                                                            # ② 切换实现：_initializeLayout/_reconcileLayout/_applyLayout/_activateSlot 用 map/video1/video2 稳定键交换位置，并保存 MainFlyWindowView、IsPIPVisible 等布局设置；由 FlyView 传入实际显示项。
│   │   ├── FlightDisplayViewSecondaryVideo.qml
                                                            # ① 第二路画面本体：secondaryVideoContent 中的 QGCVideoBackground 承载视频纹理；本文件定义等待/禁用时的 noVideo 图文、背景和九宫格参考线，负责画面内部显示。
                                                            # ② 渲染接线：通过 initVideoItem 将窗口与显示项交给 DualVideoManager，getWidth/getHeight 按宽高比和适配模式计算画面尺寸；PIP 归属、全屏和外层提示由 FlyViewSecondaryVideo 处理。
│   │   ├── FlyView.qml
                                                            # ① 飞行页总装与布局：创建地图、主视频、第二路视频、DualPipView、原生 widgetLayer、自定义覆盖层和三维窗口；设置区域尺寸、PIP 左下锚点、层叠关系及相互依赖。
                                                            # ② 模块连接：将三个内容项送入 DualPipView，向 FlyViewCustomLayer 提供页面可用空间/右上保留区，并连接工具条与 Viewer3D 显隐；整页布局改这里，相机栏内部按钮在 GimbalCameraControl。
│   │   ├── FlyViewCompassBar.qml
                                                            # ① 两条罗盘共用的 UI：compassBar 绘制条带背景和循环方位刻度，headingIndicator/headingLabel 显示中心角度，compassArrowIndicator 使用 compassPointer.svg；字体、颜色、宽高及指针尺寸均在此定义。
                                                            # ② 显示输入：通过 directionDegrees、indicatorPrefix 接收角度和标识，将角度归一到 0～360°并排列刻度；默认 directionDegrees 读取 vehicle.heading.rawValue，顶部云台实例由 FlyViewCustomLayer 改绑 Provider.absoluteYaw。
│   │   ├── FlyViewCustomLayer.qml
                                                            # ① 覆盖层装配：compassBarLoader 放置底部飞控航向条，gimbalCompassBarLoader 放置顶部云台方位角条；分别绑定开关、显隐、上下锚点和 QGCToolInsets，同时加载母线电压告警。
                                                            # ② 数据与可用性：底部沿用活动车辆 heading；顶部绑定 GimbalAzimuthProvider.absoluteYaw，并结合车辆/云台/Provider 有效性与失联状态显示；Provider 执行 2 s 过期检查，底部没有相同的独立超时逻辑。
│   │   ├── FlyViewSecondaryVideo.qml
                                                            # ① 第二路飞行页包装：将 FlightDisplayViewSecondaryVideo 放入 PipState 内容容器，定义第二路标签、双击全屏行为和原生距离/雷达叠加，是 DualPipView 接收的第二路显示项。
                                                            # ② 窗口迁移：弹出或返回飞行页时协调停止视频与延迟重启，待窗口/渲染对象稳定后恢复播放；实际 RTSP 接收由 DualVideoManager 管理，纹理绘制和画面适配在内部显示组件。
│   │   ├── FlyViewToolStripActionList.qml
                                                            # ① 飞行页工具条动作定义：组合原生 GuidedActions 和定制三维切换按钮，设置按钮名称、图标、可见/可用条件及当前页面状态。
                                                            # ② 三维入口：根据 Viewer3D 开关和当前显示状态，在 3D View/Fly 间切换并调用三维容器；入口图形来自 city_3d_map_icon.svg，场景加载与模型交互在 Viewer3D 目录。
│   │   ├── FlyViewTopRightColumnLayout.qml
                                                            # ① 右上列外观与相机选择：组合地形下载进度、cameraSelector 胶囊、cameraTab 标签、A8/MT11 在线状态点和相机面板；标签颜色、尺寸、间距及选中状态在此设置。
                                                            # ② 面板切换：selectCamera/normalizeSelectedCamera 维护 _selectedCamera，cameraControlLoader 加载相应控制栏；两种私有相机均关闭时回退到原生拍照录像控件，整列页面锚点仍在原生 FlyViewWidgetLayer.qml。
│   │   ├── GeneratorBusVoltageAlert.qml
                                                            # ① 母线告警条：required vehicle 指定所属车辆，读取 COM_GEN_V_LOW、COM_GEN_V_LOW_T 与母线电压，绘制告警文本/底色；车辆、参数、遥测或通信不可用时隐藏提示。
                                                            # ② 延时确认：_updateWarningState/_completePendingTransition 记录连续低于/高于阈值的时间，到期才进入/解除；等于阈值取消当前计时，无效数据保留已确认状态，切车重新建立状态，由覆盖层负责位置。
│   │   ├── GimbalCameraControl.qml
                                                            # ① 相机控制栏主要 UI 文件：controlColumn 纵向排列变倍、模式、拍照、录像、计时及 SD/LOCAL 标记；panelColor/panelBorderColor/panelPadding/actionSize/itemSpacing 定义底色、边框、留白、按钮尺寸与间距，zoomControl 嵌入变倍子组件。
                                                            # ② 交互与状态：photoButton 调 takePhoto、videoButton 调 toggleVideoRecording，按 Manager 的可用/pending/会话属性更新按钮、闪光和 recordingTimeText；modeButton/videoModeMenu 提供 MT11 三种画面模式，A8 直接使用本组件，MT11 由包装文件注入 Manager。
│   │   ├── GimbalZoomControl.qml
                                                            # ① 变倍子组件 UI：zoomColumn 排列 zoomInButton、targetZoomLabel、actualZoomLabel 和 zoomOutButton，定义 +/− 按钮、倍率文字、颜色及尺寸属性；外层 GimbalCameraControl 会传入尺寸/配色覆盖默认值。
                                                            # ② 手势实现：MouseArea 保存按下时的 Manager，holdThresholdMs=420 识别长按、holdStartRetryMs=100 重试启动；短按调用步进，长按调用连续变倍，释放/隐藏/切相机/应用失活时结束或取消，倍率边界由 Manager/Policy 决定。
│   │   └── MT11CameraControl.qml
                                                            # ① MT11 相机面板包装：通过 controlLoader 加载共享 GimbalCameraControl，加载后绑定 mt11ControlManager，开启 thermalControlsVisible 并关闭额外实测倍率显示。
                                                            # ② 接口转接：向父层提供加载项的隐式尺寸和 closeTransientUi，用于相机切换时关闭菜单；面板布局、拍照/录像按钮在 GimbalCameraControl，变倍按钮外观和手势在 GimbalZoomControl。
│   ├── FlightMap/
                                                            # 飞行页使用的图形资源
│   │   └── Images/
                                                            # 罗盘指针资源
│   │       └── compassPointer.svg
                                                            # ① 罗盘指针资源：定义中心方向指针的矢量路径，供飞控航向和云台方位角两条罗盘共享；需要改变箭头轮廓时编辑本文件。
                                                            # ② 显示关联：由 custom.qrc 打包并在 FlyViewCompassBar 的 compassArrowIndicator 中加载，显示大小与颜色由 QML 控制；角度、刻度文字和页面位置分别由罗盘组件与覆盖层处理。
│   ├── Gimbal/
                                                            # 相机协议、变倍/媒体状态与 MAVLink 云台协调
│   │   ├── A8MiniZoomPolicy.cc
                                                            # ① 能力与目标计算：将卡录 4K/2K/1080p/720p 分别映射到 1/3.5/5.5/6 倍上限，检查支持的拉流尺寸；短按复用 ZoomStepPolicy，长按按持续时间和默认 600 ms 档位周期推进显示目标。
                                                            # ② 反馈处理：计算档位对齐、方向到位、精确端点与端点交接，TargetTracker 判断实测是否匹配目标；Manager 负责提供有效 0x18 查询反馈并决定后续发命令或收尾。
│   │   ├── A8MiniZoomPolicy.h
                                                            # ① A8 倍率规则契约：声明卡录分辨率能力、允许的拉流尺寸、档位/长按目标、到位与端点判定接口；TargetTracker 保存目标与匹配观察状态。
                                                            # ② 输入为分辨率、当前/实测倍率、步长、方向和按住时间，输出能力上限或目标倍率；不包含 UDP、界面和定时器，供 GimbalControlManager 调用并由独立策略测试覆盖。
│   │   ├── Ch10GimbalActionState.h
                                                            # ① 共享动作状态：仅头文件实现 CH10 的“下一次回中/下一次俯视”顺序及 revision，用于顶部 Center、遥控 CH10 和手动操作之间同步动作含义。
                                                            # ② 更新规则：动作请求携带当前 revision，只有匹配版本的 commandAccepted 才推进序列；新手动操作或复位更新版本，使旧 ACK 不能改变新序列，由 GimbalCenterCoordinator 和 UniRC 调用。
│   │   ├── GimbalAzimuthPolicy.cc
                                                            # ① 参考系换算实现：检查四元数和冲突标志；Earth 参考系直接提取世界方位角，Vehicle 参考系通过有效 delta_yaw 或飞控航向转到世界参考系，再归一化角度。
                                                            # ② 兼容规则：无显式参考系时按 Provider 给定的 legacy 约定计算，可处理车辆航向减反馈 yaw 的安装方向；返回来源/错误而不伪造缺失基准，供顶部云台罗盘判断是否可显示。
│   │   ├── GimbalAzimuthPolicy.h
                                                            # ① 世界方位角计算契约：Input 包含 [w,x,y,z] 四元数、Earth/Vehicle 参考系标志、delta_yaw 可用性、飞控航向和 legacy 方向约定；Result 返回有效性、角度、来源与错误。
                                                            # ② 对外提供 calculate、isValidQuaternion 和 wrap180；显式参考系优先于 legacy 配置，计算只依赖输入值，车辆选择、消息解析和 2 s 时效由 GimbalAzimuthProvider 负责。
│   │   ├── GimbalAzimuthProvider.cc
                                                            # ① 消息到角度：接收飞控姿态及 GIMBAL_DEVICE_ATTITUDE_STATUS，按车辆和云台标识保存样本，选择活动云台后构造 GimbalAzimuthPolicy::Input，计算并发布 absoluteYaw 与来源。
                                                            # ② 有效性控制：处理独立云台的设备匹配、切车/切云台、通信丢失和 2 s 样本过期；飞控基准使用未取整 GimbalHeadingTelemetry，结果绑定顶部 gimbalCompassBarLoader，不接管底部飞控航向显示。
│   │   ├── GimbalAzimuthProvider.h
                                                            # ① 云台罗盘数据接口：声明 valid、absoluteYaw、usingDeltaYaw、referenceSource 等属性及 MAVLink 输入入口，向 QML 提供可显示的世界方位角。
                                                            # ② 缓存与关联：保存按车辆、component/device 区分的云台姿态和 GimbalHeadingTelemetry，跟踪活动车辆/云台、链路状态与采样时间；具体匹配、过期和计算逻辑在同名 .cc。
│   │   ├── GimbalCenterCoordinator.cc
                                                            # ① 执行流程：每次请求冻结车辆/云台上下文，显式发送接管配置并等待 ACK 与所有权成立，再发送 1° 预激活；间隔 400 ms 后发送最终回中或俯视命令。
                                                            # ② 收尾与隔离：处理最终 ACK、4 s 最终确认和 10 s 请求超时，切车/切云台或新请求时取消旧上下文；只有匹配版本的成功结果推进 CH10 状态，顶部 Center 与遥控入口复用同一流程。
│   │   ├── GimbalCenterCoordinator.h
                                                            # ① 回中/俯视事务接口：声明 requestCenter、requestNextCh10Action 等入口及请求状态，供顶部工具栏与 UniRC 共用；保存本次车辆、云台、管理组件和动作版本。
                                                            # ② 事务成员：声明控制权检查、配置 ACK、预激活、最终角度命令、定时器与取消处理；关联 Ch10GimbalActionState，使命令完成只更新对应请求的动作顺序。
│   │   ├── GimbalControl.SettingsGroup.json
                                                            # ① 设置元数据：定义 A8/MT11 启用、SDK 地址/端口、各自变倍步长、本地媒体开关、UniRC 蓝牙/反向配置、MAVLink 自动视频和 Android 硬解策略的类型、默认值、范围及说明。
                                                            # ② 加载关系：GimbalControlSettings 按设置键创建 Fact，GimbalControlSettingsGroup/VideoSettings 绑定并编辑，Manager 订阅变化；新增设置要同步 JSON、C++ getter 和界面，已有用户值的兼容处理在 Settings.cc。
│   │   ├── GimbalControlManager.cc
                                                            # ① 变倍与设备控制：通过 SiyiSdk 查询卡录分辨率/最大倍率/实测倍率，结合拉流尺寸与 A8MiniZoomPolicy 解锁能力；短按发绝对目标，长按发方向并推进显示档位，停止后查询、对齐和确认，同时协调触摸与 UniRC 的动作所有权。
                                                            # ② 媒体会话：拍照同时请求相机与本地解码帧，录像协调 SD 和 Video 1 本地接收器；A8 断流保留本地续录意图，恢复后另起片段，段开始前触发容量清理，Android 文件经媒体库发布，停止/退出等待文件和任务收尾。
│   │   ├── GimbalControlManager.h
                                                            # ① A8 业务接口：声明 zoomIn/zoomOut/setZoom、startZoom/stopZoom/cancelZoom、takePhoto/toggleVideoRecording、模式查询及 UniRC 专用变倍入口；QML 属性提供倍率、能力、在线、命令 pending、SD 与本地会话状态。
                                                            # ② 接线与状态：setMainVideoItem/setMainVideoReceiver 接入 Video 1，setNegotiatedPulledVideoResolution 接收真实视频尺寸；成员保存 SDK、能力查询、触摸/遥控动作持有者、录像所有权与媒体任务，shutdownLocalMedia 负责退出收尾。
│   │   ├── GimbalControlSettings.cc
                                                            # ① Fact 创建与保存：注册 GimbalControl 设置组和全部配置项，使 SDK 端点、相机开关、步长及 Android/UniRC 策略可被界面编辑并由 Manager 响应。
                                                            # ② 兼容规则：按版本处理 MT11 默认 SDK 主机从 .25 到 .24 的配置迁移，以及缺失/空 UniRC 蓝牙地址的默认补全；保留不属于迁移范围的用户值，默认元数据仍来自同名 JSON。
│   │   ├── GimbalControlSettings.h
                                                            # ① 设置访问声明：按 GimbalControl 组提供 A8、MT11、UniRC、本地媒体和视频策略的 Fact getter 与键名，供 QML 及业务 Manager 使用同一份持久化配置。
                                                            # ② 关联文件：字段类型、默认值和范围由 GimbalControl.SettingsGroup.json 提供，创建与迁移在 .cc；A8 使用 zoomStep，MT11 使用独立 mt11ZoomStep，避免两个控制器共用错误配置。
│   │   ├── GimbalHeadingTelemetry.cc
                                                            # ① 样本接收：拒绝非有限角度、无效或乱序时间；处理设备启动时间重复/回退，避免旧报文延长缓存寿命，并在确认设备重启时清理旧时序状态。
                                                            # ② 来源选择：优先取时间最新的 ATTITUDE/ATTITUDE_QUATERNION，时间相同时选四元数，高延迟航向仅作后备；独立检查各样本寿命，供 GimbalAzimuthProvider 获取准确航向或无效结果。
│   │   ├── GimbalHeadingTelemetry.h
                                                            # ① 未取整航向缓存接口：定义 Quaternion、Attitude、HighLatency 来源及 Sample，update 同时接收角度、本地接收时间和可选设备启动时间，heading 返回当前可用样本。
                                                            # ② 时间约定：每个来源独立 2 s 过期，物理飞控航向不叠加显示偏移；仅为云台参考系转换提供基准，底部罗盘直接读取原生 Vehicle.heading，不经过此缓存。
│   │   ├── GimbalMediaSessionPolicy.cc
                                                            # ① 状态决策：根据意图和接收器状态返回 StartOwned、StopOwned、ConfirmOwned、AdoptExternal、ReleaseExternal 或 None，区分本模块启动的录像与外部已在录制的录像。
                                                            # ② 会话归并：将 SD 是否录制、SD 命令 pending 和本地实际状态组合成会话录制/可用结果；GimbalControlManager 执行决策并处理异步结果，避免将临时 pending 当成已经取得录像所有权。
│   │   ├── GimbalMediaSessionPolicy.h
                                                            # ① A8 本地录像状态模型：LocalState 定义录制意图、设置/拉流状态、实际录制、所有权、外部会话、启动/停止 pending 及启动阻塞；LocalAction 声明需要执行的动作。
                                                            # ② 纯接口：localAction 计算下一步，recordingSessionCapturing/recordingAvailable 计算 UI 会话状态与按钮可用性；不直接控制接收器，MT11 对应逻辑由其 Manager 自行维护。
│   │   ├── GimbalModeController.cc
                                                            # ① 模式采样：按活动车辆/云台选择路由，产品 A8 路由用 SDK 查询确认实际模式，其余结合 MAVLink 状态；约 2 s 查询、3.5 s 样本有效期，失效发布 Unknown，切换上下文更新 sessionRevision。
                                                            # ② 切换闭环：冻结点击目标，确认控制权后发零角速度的模式命令并等待 ACK，A8 再发明确 SDK 模式设置，延迟查询验证反馈；5 s 超时或会话变化取消，只有实际反馈确认才完成按钮状态切换。
│   │   ├── GimbalModeController.h
                                                            # ① 实际模式与命令接口：暴露 known、mode、yawLocked、commandPending、sessionRevision 及当前 gimbal；requestYawLock 接收点击时确定的目标和会话版本，cancelModeCommand 取消未完成切换。
                                                            # ② 内部契约：Mode 区分 Unknown/Follow/Locked/Fpv，保存车辆/云台绑定、采样寿命、SDK 请求号和 AwaitingAck/AwaitingFeedback 阶段；顶部 GimbalIndicator 以这些属性显示实际状态及等待状态。
│   │   ├── GimbalPhotoCapturePolicy.cc
                                                            # ① 尺寸计算：优先采用协商得到的源分辨率，其次视频项隐式尺寸，最后才使用当前显示尺寸；检查长边、短边和总像素上限，避免照片尺寸随 PIP 大小随意变化。
                                                            # ② 输出整理：captureGeometry 将目标物理像素换算为逻辑抓图尺寸，保持完整源画面等比缩放；prepareImageForSaving 修正小数 DPR 舍入，源/目标比例不一致时居中补黑边，交由 Manager 保存。
│   │   ├── GimbalPhotoCapturePolicy.h
                                                            # ① 本地照片几何接口：CaptureGeometry 记录输出像素、有效画面像素和 Qt Quick 逻辑抓图尺寸；提供源尺寸选择、像素上限检查、DPR 换算与保存前图像整理方法。
                                                            # ② 职责边界：输入协商视频尺寸、目标尺寸和设备像素比，输出稳定截图几何；A8/MT11 Manager 负责是否有解码帧、调用抓图和保存线程，本策略不读取相机或启动录制。
│   │   ├── GimbalVideoStreamSupport.cc
                                                            # ① 默认流安装：通过版本标记处理空值/已知旧默认值，将主路设为 rtsp://192.168.144.25:8554/main.264，并保证合适的接收超时；Android 未保存低延迟配置时安装产品默认值。
                                                            # ② 自动发现控制：mavlinkAutoVideoStream 关闭时过滤 VIDEO_STREAM_INFORMATION，防止飞控自动视频消息替换手工主路配置；用户已有自定义地址按迁移条件保留，修改默认规则需同步版本与条件。
│   │   ├── GimbalVideoStreamSupport.h
                                                            # ① 主视频配置接口：installA8MiniDefaults 安装产品视频默认值，shouldFilterMavlinkMessage 根据设置判断是否屏蔽自动视频信息，供 CustomPlugin 初始化和 MAVLink 分发调用。
                                                            # ② 作用范围：复用原生 VideoManager 的设置与消息入口，不在此创建接收器；A8 默认流迁移与自动发现开关实现在 .cc，第二路 URL 由 VideoCustomSettings 独立维护。
│   │   ├── Mt11ControlManager.cc
                                                            # ① 设备动作实现：短按使用 0x0F 绝对目标且受 30 倍协议边界约束，长按使用 0x05 方向控制并处理更高混合倍率；结合反馈、档位对齐、保活/超时控制停止，另管理三种视频模式的请求与确认。
                                                            # ② 第二路媒体：从 Video 2 解码项截图，协调 SD 与本地录像并发布文件；断流清除本地录像意图，恢复后不自动续录，停止超时可继续重试；本类开始录像未独立调用容量清理，不能据此声称 MT11 单独持续执行配额。
│   │   ├── Mt11ControlManager.h
                                                            # ① MT11 业务接口：声明独立变倍、拍照、录像、VideoMode 及兼容 thermal 属性，提供 target/actual 倍率、模式 known/pending、SD 与本地录像会话和错误等 QML 状态。
                                                            # ② 依赖与异步成员：保存 Mt11Sdk、能力/倍率轮询、长按保活/停止状态和 Video 2 媒体引用；共享面板通过统一动作接口调用本类，接收器切换与退出通过媒体清理接口收尾。
│   │   ├── Mt11Protocol.cc
                                                            # ① 字节编解码：构造并校验 MT11 帧及 CRC；连续变倍 ACK 按小端 16 位数除以 10 解码，最大/当前倍率按整数与小数两个字节解码，避免混合倍率读错。
                                                            # ② 模式与反馈：用 0x11 命令编码 [00 02]、[02 00]、[03 02] 三种画面模式，解析 0x10/0x11 及相机功能反馈；Mt11Sdk 派发解析结果，Manager 更新模式/倍率/录像状态。
│   │   ├── Mt11Protocol.h
                                                            # ① MT11 协议声明：定义命令、视频模式、倍率与功能反馈结构，以及帧封装、CRC 和各载荷解析入口，作为 Mt11Sdk 与控制器之间的设备数据契约。
                                                            # ② 编码区分：连续变倍反馈和最大/当前倍率反馈采用不同编码，视频模式有三种固定载荷；接口只返回协议值及校验结果，命令是否可发、何时重试由 SDK/Manager 判断。
│   │   ├── Mt11Sdk.cc
                                                            # ① 请求发送与验证：向配置的 MT11 IP/端口发送 Mt11Protocol 帧，接收时校验源地址/端口和帧内容，并用约 1.5 s 的近期命令窗口关联普通 ACK。
                                                            # ② 反馈派发：将倍率、状态、视频模式等结果发为业务信号；异步 0x0B 功能反馈不依赖普通请求窗口，错误交给 Manager 展示和处理，socket 的存在不等于设备已经响应。
│   │   ├── Mt11Sdk.h
                                                            # ① MT11 UDP 服务接口：声明端点设置、变倍/状态/视频模式/拍照录像命令及反馈信号，保存 socket、帧序号和近期请求关联状态。
                                                            # ② 分层关系：Protocol 提供字节编码，Sdk 负责发送和分发设备回复，Mt11ControlManager 决定业务时序；该端点独立于 A8 SiyiSdk，防止两台相机反馈互相影响。
│   │   ├── Mt11ZoomPolicy.cc
                                                            # ① 档位计算：按最小倍率锚定步长，将原始实测值对齐为合法显示目标，方向参数决定恰好位于两档中点时的取舍；与共用 ZoomStepPolicy 保持档位定义一致。
                                                            # ② 短按边界：独立检查实际倍率与 30 倍绝对命令范围，保留精确 30 倍终点；设备更高的混合倍率反馈不直接变成可发送的短按绝对目标，由连续变倍路径处理。
│   │   ├── Mt11ZoomPolicy.h
                                                            # ① MT11 档位契约：定义 MinimumZoom=1 与 AbsoluteCommandMaximumZoom=30，声明 isDisplayTarget、tapTarget、alignedDisplayTarget 纯函数。
                                                            # ② 输入职责：measuredZoom 决定是否越过绝对命令边界，displayZoom 用于合法档位规划；输出目标供 Manager 发送，设备最大混合倍率和长按通信仍由 Manager 维护。
│   │   ├── SiyiProtocol.cc
                                                            # ① 帧与载荷实现：生成协议帧、计算 CRC 并校验 ACK，解析 0x05 连续倍率、0x0F 绝对变倍、0x18 当前倍率、0x16 最大倍率、0x20 卡录分辨率等字段。
                                                            # ② 相机功能：编解码 0x0A 状态/模式、0x0C 操作及 0x0B 功能反馈等载荷；本文件确定字段长度、比例与字节序，SiyiSdk 负责传输，GimbalControlManager 根据结果推进业务状态。
│   │   ├── SiyiProtocol.h
                                                            # ① A8/SIYI 协议契约：声明帧结构、命令编号、相机/倍率/分辨率反馈数据与 CRC、封装、解析接口，为 SiyiSdk 提供不依赖 socket 的字节处理。
                                                            # ② 功能映射：覆盖连续/绝对变倍、当前/最大倍率、卡录参数、相机状态/模式及拍照录像功能命令；扩展协议时先补数据结构与编解码，再在 SDK 信号和 Manager 动作中接入。
│   │   ├── SiyiSdk.cc
                                                            # ① 普通通信：使用 SiyiProtocol 生成报文并发送到 A8 端点，读取 UDP 后校验来源 IP 和协议内容，将倍率、分辨率、相机状态及功能反馈转换为信号。
                                                            # ② 专用查询：为模式查询维护独立 socket/requestId、超时和取消逻辑，只把属于当前查询的有效回复送回调用者；端点变化时更新连接状态，具体重试和业务动作由 Manager/ModeController 决定。
│   │   ├── SiyiSdk.h
                                                            # ① A8 UDP 服务接口：声明变倍、相机状态/模式、拍照录像等请求和业务反馈信号，提供端点配置，并保存普通命令 socket 与查询定时器。
                                                            # ② 模式查询隔离：另外声明带 requestId 的模式查询与取消入口，维护专用 socket 和请求状态；GimbalModeController 经 Manager 使用该通道，防止普通状态回复被当成当前模式查询结果。
│   │   ├── ZoomStepPolicy.cc
                                                            # ① 数值实现：以最小倍率锚定等步长档位，处理浮点/十分位精度、方向选择和范围限制；上限不落在完整步长上时保留最后一个较短区间及精确终点。
                                                            # ② 复用关系：A8MiniZoomPolicy 与 Mt11ZoomPolicy 在各自协议边界内调用本策略，避免两个相机各自取整产生不同显示规则；相关倍率协议/策略测试覆盖步进和边界结果。
│   │   └── ZoomStepPolicy.h
                                                            # ① 共用倍率档位接口：声明 isAlignedZoom、alignmentTarget 和 stepTarget，统一 A8/MT11 的档位合法性、实测对齐与单步前进规则。
                                                            # ② 输入输出：使用当前倍率、步长、最小/最大倍率及方向计算目标；档位以最小倍率为基准，精确最大值始终可作为终点，设备能力和通信状态不属于本接口。
│   ├── QmlControls/
                                                            # 通用遥测控件、详情页和三维模块声明
│   │   ├── BatteryIndicator.qml
                                                            # ① 电池指示器及详情 UI：按活动车辆 batteries 列表显示图标、状态、电压/电流与电量，_formatPower 用电压×电流计算功率；点击打开电池详情和有关参数编辑。
                                                            # ② 状态规则：保留 FAILED/UNHEALTHY/CHARGING 等原生状态，其余按阈值判断低电压等级；_parameterFact 读取 UAVCAN 相关参数，元数据默认值在 CustomFirmwarePlugin，界面不通过显示逻辑强制改写飞控值。
│   │   ├── FuelStatusIndicatorPage.qml
                                                            # ① 燃油详情抽屉：布局剩余百分比、剩余量、最大容量、已消耗量、流量和温度等字段；用 valueString 显示数值，getFuelUnit 按燃油类型提供 ml/MPa，温度使用 Fact.units，并隐藏无效项目。
                                                            # ② 数据关联：由工具栏 FuelStatusIndicator 打开，_hasFuel 检查 fuelStatus.telemetryAvailable，各行检查 rawValue 是否有效；顶部图标颜色/百分比在 toolbar 文件，消息接收和 Fact 更新复用原生车辆系统。
│   │   ├── ProximityRadarIndicatorPage.qml
                                                            # ① 避障雷达详情：将上、下及水平方向的距离条目排成详情列表，显示方向名称、测距数值、单位和接近告警样式，便于查看顶部汇总图标所代表的各方向状态。
                                                            # ② 模型复用：接收 ProximityRadarIndicator 构建的方向条目和告警阈值，仅展示有效测距并采用相同的阈值判断；本文件不解析 MAVLink，数据来自原生 distanceSensors Fact。
│   │   └── Viewer3D/
                                                            # 三维 QML 模块描述文件
│   │       └── Models3D/
                                                            # 本地三维模型模块声明
│   │           └── qmldir
                                                            # ① QML 类型清单：列出 Models3D 的 CameraLightModel、Line3D、External3DMap、Viewer3DModel、Viewer3DVehicleItems、Waypoint3DModel 及对应版本/文件，配合资源目录解析模型组件。
                                                            # ② 资源关联：与 custom.qrc 的模型 QML 别名配套使用，既包含定制总装也引用复用的原生部件；新增/重命名可导入类型时同时核对 qmldir、QRC 和组件实际文件路径。
│   ├── Settings/
                                                            # 飞行视图与第二路视频设置
│   │   ├── FlyViewCustom.SettingsGroup.json
                                                            # ① 罗盘显示设置元数据：定义 showHeadingCompassBar 和 showGimbalHeadingCompassBar 两个布尔 Fact，分别控制底部飞控航向条与顶部云台方位角条，默认关闭。
                                                            # ② 界面/逻辑关联：FlyViewCustomSettings 加载并保存配置，FlyViewSettings 显示两个独立开关，FlyViewCustomLayer 订阅决定加载/显隐；此文件不定义罗盘绘图或角度计算。
│   │   ├── FlyViewCustomSettings.cc
                                                            # ① Fact 注册实现：将 FlyViewCustom 设置类注册给 QML，在 FlyView 持久化组创建 showHeadingCompassBar、showGimbalHeadingCompassBar。
                                                            # ② 使用流程：设置页修改 Fact 后自动保存并通知覆盖层更新两条罗盘；默认值/说明由 JSON 提供，实际 UI 尺寸和角度来源分别留在 FlyViewCompassBar 与 FlyViewCustomLayer。
│   │   ├── FlyViewCustomSettings.h
                                                            # ① 飞行页附加设置声明：在独立 C++ 设置类中提供两条罗盘显示开关的键名和 Fact getter，向 CustomPlugin/QML 暴露配置访问接口。
                                                            # ② 存储关系：设置归入原生 FlyView 组，元数据来自 FlyViewCustom.SettingsGroup.json，具体组注册和 Fact 创建位于 .cc；扩展飞行页附加开关时同步这三处。
│   │   ├── VideoCustom.SettingsGroup.json
                                                            # ① 第二路视频元数据：定义 secondaryRtspUrl 字符串 Fact，默认 rtsp://192.168.144.24:8554/video1，作为 Video 设置组的定制补充。
                                                            # ② 消费关系：VideoSettings 提供输入框，VideoCustomSettings 管理保存/兼容，DualVideoManager 读取地址启停第二路；该 URL 不等同于 MT11 SDK 地址，控制协议和视频连接可独立配置。
│   │   ├── VideoCustomSettings.cc
                                                            # ① 配置兼容实现：仅当 Video/secondaryRtspUrl 不存在时读取旧 GimbalControl/mt11RtspUrl，将精确匹配旧默认 .25/video1 的值迁为 .24/video1。
                                                            # ② 保留规则：用户自定义值包括空字符串均保留，旧键也保留；随后注册 QML 类型并创建 secondaryRtspUrl Fact，供 VideoSettings 编辑与 DualVideoManager 订阅刷新。
│   │   └── VideoCustomSettings.h
                                                            # ① 第二路设置接口：声明 VideoCustomSettings 与 secondaryRtspUrl 键名/Fact getter，使界面和 DualVideoManager 使用统一的第二路地址。
                                                            # ② 模块边界：扩展原生 Video 设置组而不修改原生 VideoSettings 类；默认值来自 VideoCustom.SettingsGroup.json，旧 MT11 视频设置迁移在同名 .cc，接收器创建不在本类。
│   ├── UI/
                                                            # 设置页及顶部工具栏
│   │   ├── AppSettings/
                                                            # 设置总页、功能组与自适应编辑控件
│   │   │   ├── FlyViewComboBox.qml
                                                            # ① 普通下拉行组件：封装 QGCComboBox，提供 model、currentIndex、currentText、comboBox 别名并转发 activated，用于非 Fact 数据的模式或选项选择。
                                                            # ② 统一外观：依托 FlyViewSettingsRow 管理标签与控件布局，使用 FlyViewComboBoxDelegate 绘制选项，并处理文本省略；调用页负责把所选索引转换为业务设置。
│   │   │   ├── FlyViewComboBoxDelegate.qml
                                                            # ① 下拉选项外观：定义每条选项的文字、背景、选中/高亮状态、内边距和最小显示尺寸，comboBoxWidth 约束选项宽度。
                                                            # ② 复用入口：被 FlyViewComboBox 和 FlyViewFactComboBox 共用，颜色跟随 QGCPalette；只决定弹出选项如何绘制，模型、当前值和 Fact 写回仍由相应下拉控件管理。
│   │   │   ├── FlyViewFactComboBox.qml
                                                            # ① Fact 枚举下拉行：封装原生 FactComboBox 并暴露 fact、indexModel、comboBox，利用 Fact 元数据提供枚举及值转换，保留 activated 通知。
                                                            # ② 布局与写回：继承共用设置行并使用 FlyViewComboBoxDelegate，统一宽度、文本省略与主题；选择后由原生 FactComboBox 更新 Fact，调用页面无需自行拼装枚举索引映射。
│   │   │   ├── FlyViewFactSwitch.qml
                                                            # ① Fact 开关 UI：在原生 FactCheckBoxSlider 基础上定制标签、胶囊开关、圆点和配色，使用 ScreenTools 字号/触控尺寸适配窄屏及大字号。
                                                            # ② 交互继承：保留 Fact 双向绑定和整行鼠标/触摸点击行为，修改布尔值后触发设置保存；调用页传入 fact 与文字，本文件统一开关外观，不另维护一份业务状态。
│   │   │   ├── FlyViewFactTextField.qml
                                                            # ① Fact 输入行：在 FlyViewSettingsRow 中放置原生 FactTextField，暴露 fact 和 textField，默认用 Fact.shortDescription 作为标签。
                                                            # ② 输入处理：数值类型、单位、范围校验及写回沿用 FactTextField/元数据；本文件负责标签与输入框组合，字段默认值/上下限需在所属 SettingsGroup.json 调整。
│   │   │   ├── FlyViewSettings.qml
                                                            # ① 飞行设置页面总装：组合常规飞行、引导动作、MAVLink 动作、虚拟摇杆、仪表等原生设置，并放入两条罗盘开关、GimbalControlSettingsGroup 和 Viewer3DSettingsGroup。
                                                            # ② 按需接入：通过 corePlugin 获取附加 Fact，用 Loader 加载云台与三维分组并处理可见条件；设置行/分组外观复用 FlyViewSettings 系列组件，业务行为由相应 Manager 订阅配置变化执行。
│   │   │   ├── FlyViewSettingsPage.qml
                                                            # ① 设置页容器：提供可滚动区域、统一页边距和居中主 ColumnLayout，contentMaximumWidth 为默认字符宽度的 100 倍，避免宽屏页面过度拉伸。
                                                            # ② 内容接口：default contentItem 指向 mainLayout.data，业务页直接声明分组作为子内容；容器管理滚动、可用宽度和背景，单行的宽窄屏重排交由 FlyViewSettingsRow。
│   │   │   ├── FlyViewSettingsRow.qml
                                                            # ① 标签/控件行布局：以 GridLayout 组织 label 和 controlLayout，宽屏两列并排，宽度小于默认字符宽度×66 时 stacked 切换为上下排列。
                                                            # ② 尺寸约束：暴露 controlPreferredWidth 并结合可用空间设置控件最小/首选/最大宽度，标签支持换行；default contentItem 接收输入框、下拉框等，统一解决分组中的控件对齐。
│   │   │   ├── FlyViewSettingsSection.qml
                                                            # ① 设置分组外观：绘制分组标题 heading、说明 headingDescription、背景/边框与统一 padding，形成飞行设置中清晰的功能分区。
                                                            # ② 内容布局：default contentItem 指向 contentLayout，contentSpacing 控制组内行间距，颜色跟随主题/启用状态；各业务分组只提供字段，分组通用间距与样式在这里维护。
│   │   │   ├── GeneralSettings.qml
                                                            # ① 通用设置页面：保留语言、单位、音频、保存路径与品牌图等原生选项，定制基础字体大小输入及增减按钮，便于桌面和 Android 调整整体可读性。
                                                            # ② 字号链路：控件修改 appSettings.appFontPointSize Fact，ScreenTools 和全局控件随之刷新；Android 首次字体默认值由 CustomPlugin 的元数据调整安装，当前用户字号由设置系统持久化。
│   │   │   ├── GimbalControlSettingsGroup.qml
                                                            # ① 云台配置 UI：显示 A8/MT11 启用、各自 SDK 主机/端口与变倍步长；Android 区域提供 UniRC 通道控制、SDK 接口、蓝牙 MAC、CH9 反向及连接/诊断信息。
                                                            # ② 数据绑定：编辑 GimbalControlSettings Fact，读取 UniRcChannelController.channelValues 显示 16 路实时通道网格，按开关调整字段可用性；这是配置页，相机控制栏按钮与排版位于 FlightDisplay/GimbalCameraControl.qml。
│   │   │   ├── VideoSettings.qml
                                                            # ① 视频配置 UI：组合主路源类型/URL、第二路 RTSP URL、低延迟、超时、画面适配、录制格式及存储限额，加入 MAVLink 自动发现、Android 硬解和本地媒体开关。
                                                            # ② 设置作用：分别绑定原生 VideoSettings、VideoCustomSettings 和 GimbalControlSettings，按平台/源类型显示字段；本地媒体开关控制解码帧照片/本地录像，SD 相机录制独立，配额实际触发点见 A8 Manager。
│   │   │   └── Viewer3DSettingsGroup.qml
                                                            # ① 三维设置 UI：提供总开关、OSM/外部模型/Google 地图选择、API Key、文件选择与导入状态，以及外部原点经纬高、单位、比例、yaw、默认层高和高度偏置。
                                                            # ② 加载与写回：viewer3DRequiredFactsReady 检查插件/Fact 就绪后装入分组；模型按钮调用 External3DMapManager.importModelFile，其他字段写入 Viewer3DSettings，模式变化控制相关字段显隐并由三维后端重建/更新显示。
│   │   └── toolbar/
                                                            # 云台、Fuel、距离提示的顶部入口
│   │       ├── FuelStatusIndicator.qml
                                                            # ① 顶部燃油图标：读取活动车辆燃油遥测及 telemetryAvailable，绘制 FuelIcon.svg 和百分比文字；getFuelColor 按 >50%、>25% 和其余区间使用绿/橙/红色。
                                                            # ② 入口逻辑：getFuelText 格式化顶部摘要，仅有可用燃油数据时显示；点击通过 showIndicatorDrawer 打开 FuelStatusIndicatorPage，容量、流量、温度等详细字段由详情页组织。
│   │       ├── GimbalIndicator.qml
                                                            # ① 顶部 MAVLink 云台操作 UI：显示云台、控制权和实际 Follow/Locked/Fpv/Unknown 模式，提供接管、回中、俯视、收回及 Lock/Follow 操作；按钮等待/可用状态绑定控制权和模式事务。
                                                            # ② 动作分发：_requestCenter 调共享 GimbalCenterCoordinator；其他动作经 _dispatchOwnershipAction/_reviewPendingOwnership 等冻结点击目标并等待控制权，模式切换交给 GimbalModeController，避免等待期间状态变化把原目标反转。
│   │       ├── Images/
                                                            # 工具栏专用图标
│   │       │   └── FuelIcon.svg
                                                            # ① 燃油图标矢量资源：定义顶部燃油指示器的图形轮廓，修改此文件可改变图标造型而不影响遥测数值或告警阈值。
                                                            # ② 加载关系：由 custom.qrc 打包、FuelStatusIndicator.qml 引用，缩放和动态着色由 QML 控制；点击展开、百分比文字与颜色区间均在指示器 QML 中定义。
│   │       └── ProximityRadarIndicator.qml
                                                            # ① 顶部避障汇总 UI：从 distanceSensors 的上/下及八个水平方向 Fact 构建 radarModel.entries，筛选有效距离，绘制雷达标识和近距离告警状态。
                                                            # ② 告警/详情：距离严格小于 alertDistanceMeters=5 m 时告警，并以 400 ms 节奏闪烁；点击把相同方向模型和阈值交给 ProximityRadarIndicatorPage，通信数据解析复用原生车辆遥测系统。
│   ├── VideoManager/
                                                            # 第二路视频管理与运行期解码/恢复策略
│   │   ├── DualVideoManager.cc
                                                            # ① 接收与渲染：订阅 secondaryRtspUrl，创建独立接收器，将 initVideoItem 提交的窗口/视频项在渲染初始化完成后接线；_applyDesiredState 串行推进停止、启动与重建，并处理主路在用/释放中 URI 的重复源保护。
                                                            # ② 恢复与退出：用 URI+generation 排除旧回调，首帧失败结合硬件解码候选重试，重连采用 1/2/4/8/15 s 退避；cleanup 先通知媒体使用者再释放对象，QML 用其状态显示 Video 2 和画面尺寸。
│   │   ├── DualVideoManager.h
                                                            # ① 独立第二路视频接口：暴露 enabled/hasVideo/duplicateSource/streaming/decoding、videoSize/aspectRatio、fullScreen 及 videoReceiver/videoItem，声明 initVideoItem、startVideo、stopVideo、cleanup。
                                                            # ② 生命周期成员：保存第二路 receiver/sink 对应显示项、窗口、当前 URI/管线 generation、主路占用 URI 和重连/首帧定时器；释放前后信号供 MT11 媒体逻辑收尾，不复用主路接收器。
│   │   └── VideoReceiver/
                                                            # 接收器扩展
│   │       └── GStreamer/
                                                            # GStreamer 解码、格式、恢复与分辨率探针
│   │           ├── A8RtspRecoveryPolicy.cc
                                                            # ① 恢复判定：匹配配置的 A8 RTSP 主机并排除 MT11，解析 RTCP Sender Report，比较 NTP/RTP 与本地时间判断跳变；结合 RTP/媒体进度区别媒体停滞和时钟异常后的停滞。
                                                            # ② 触发边界：只在前台、已显示过画面且未录像等条件满足时评估，媒体停滞约 6 s、时钟证据后的停滞约 2 s；每代最多一次、同 URI 60 s 内最多两次，由 StreamRecovery 执行恢复。
│   │           ├── A8RtspRecoveryPolicy.h
                                                            # ① A8 拉流健康纯策略：Progress 记录 RTP/媒体数量和最近进度/时钟跳变时间，SenderReport 表示 RTCP 时间映射，Action 区分无需操作、时钟停滞、媒体停滞和限流。
                                                            # ② 接口约定：matches/senderReport/clockJump 负责来源与证据计算，begin/displayed/evaluate/stop 管理一代管线的恢复资格；调用者传入单调时钟、前台及录制状态，策略自身不改时钟或启停视频。
│   │           ├── A8RtspStreamRecovery.cc
                                                            # ① 证据采集：在合适的 A8 管线上安装 RTP、RTCP、媒体及显示进度观察，关联 URI/generation；定时汇总进度并交给 A8RtspRecoveryPolicy，避免把另一条流或旧代次计入当前判断。
                                                            # ② 动作执行：策略允许时请求受限的流会话恢复，录像中不自动重启；管线停止/切换或对象销毁时 detachProbes 解除探针及回调，日志记录触发依据和预算情况。
│   │           ├── A8RtspStreamRecovery.h
                                                            # ① 接收器恢复安装入口：install(receiver, sink, settings) 将 A8 RTSP 健康观察器安装到指定视频接收器和显示 sink，供 CustomPlugin 的视频初始化调用。
                                                            # ② 依赖边界：设置提供 A8/MT11 主机识别，接收器提供流状态及代次；探针、定时观察和恢复动作封装于 .cc，纯阈值/预算规则由 A8RtspRecoveryPolicy 承担。
│   │           ├── AndroidH265DecoderCapsPolicy.cc
                                                            # ① CAPS 内容实现：集中给出 video/x-h265 的 byte-stream、alignment=au、parsed=true 以及允许的帧率范围，描述 h265parse 到 Android MediaCodec 的协商约束。
                                                            # ② 协商目的：保留已知 A8 帧率，同时允许无公布帧率的流使用 0/1 哨兵值；AndroidH265HardwareDecoderAdapter 引用此契约，相关测试核对字符串要求，不在此执行解码。
│   │           ├── AndroidH265DecoderCapsPolicy.h
                                                            # ① H.265 解码输入契约声明：byteStreamAccessUnitCaps 返回归一化 Annex-B、按访问单元对齐的 caps 字符串，供适配器构建解码器前的过滤条件。
                                                            # ② 接口边界：本类统一格式字符串而不校验任意输入 caps；适配器负责把返回内容应用到管线，具体编码格式、parsed 标志及帧率范围在 .cc。
│   │           ├── AndroidH265DecoderFallback.cc
                                                            # ① 候选状态管理：按 receiver、URI、generation 和当前 H.265 输入格式保存重试进度，仅在符合解码失败条件时选择下一条兼容 MediaCodec 路由，避免网络断流误触发换解码器。
                                                            # ② 切换规则：利用 RoutePolicy 去重排序并记录已尝试候选，格式变化时重置对应尝试空间；候选耗尽返回首选适配器稳定路由，不启用软件解码，结果交由调用者停止/重启管线。
│   │           ├── AndroidH265DecoderFallback.h
                                                            # ① 接收器级硬解重试接口：install 跟踪 URI，resetForCurrentInputFormat 同步分包格式变化，activeAdapterFactoryName 查询当前路由，prepareHardwareRetry 依据失败证据推进候选。
                                                            # ② 输入边界：接收 URI/generation、编码、源帧/解码帧/显示帧证据和解码分支错误标志；每条接收器独立保存选择，供主路 Recovery 与第二路 DualVideoManager 共用。
│   │           ├── AndroidH265DecoderRoutePolicy.cc
                                                            # ① 确定性排序：先放备用 Annex-B 归一化适配器，再放与输入分包兼容的直接厂商解码器，剔除空名称和重复项但保持原相对顺序。
                                                            # ② 推进与耗尽：根据上次工厂及位置，每个候选最多尝试一次，全部失败时返回 exhausted；Fallback 据此恢复首选路线，避免在有限几个解码器之间无限循环。
│   │           ├── AndroidH265DecoderRoutePolicy.h
                                                            # ① 硬解候选纯接口：RouteSelection 返回 factoryName、candidateIndex 与 exhausted；orderedRetryFactories 组合候选，nextRoute 计算下一次选择。
                                                            # ② 调用关系：输入候选适配器、兼容的直接 MediaCodec 工厂及已尝试位置，输出一次决策；不修改全局 rank 或接收器，由 AndroidH265DecoderFallback 应用选择，独立测试可直接构造候选列表。
│   │           ├── AndroidH265HardwareDecoderAdapter.cc
                                                            # ① 管线实现：创建 hvc1→h265parse→Annex-B/AU caps→厂商 MediaCodec 的解码适配 bin，连接 pad 与协商条件，使主路既有输入封装能够进入可用的 Android 硬件解码器。
                                                            # ② 工厂注册：按兼容性/优先级挑选厂商解码器，首选适配器获得自动选择 rank，其余注册为备用；配合 CapsPolicy 保持输入契约，向 DecoderPolicy/Fallback 暴露真实工厂映射和候选顺序。
│   │           ├── AndroidH265HardwareDecoderAdapter.h
                                                            # ① Android H.265 适配器注册接口：registerElement 在 GStreamer 初始化后、decodebin 创建前注册解码 bin，并提供首选/备用工厂名称及适配器到真实硬件工厂的查询方法。
                                                            # ② 识别契约：isVendorHardwareDecoderFactoryName 过滤已知软件包装器，adapterRouteContainsFactory 等方法帮助恢复逻辑识别实际失败分支；备用适配器保持 rank NONE，仅供指定接收器显式重试。
│   │           ├── AndroidH265StreamFormatPolicy.cc
                                                            # ① 主机匹配实现：解析并规范比较 URI 与 MT11 配置主机，选择适合该设备的 H.265 parser 输出；无匹配或无有效主机时保留原有格式路径。
                                                            # ② 管线关联：动态属性由接收器在创建一代管线时读取并冻结，避免运行中随配置摇摆；首帧恢复路径也可在满足条件时执行受限格式切换，切换状态管理在 Fallback/Recovery。
│   │           ├── AndroidH265StreamFormatPolicy.h
                                                            # ① 按流选择分包格式：声明 parserOutputFormatForUri(uri, mt11Host) 和 receiverPropertyName，为管线建立前设置接收器动态属性提供统一入口。
                                                            # ② 输入/输出：根据 RTSP 主机与配置 MT11 主机匹配返回 byte-stream，其余返回空值沿用原生 hvc1；主路/第二路均可使用，识别依据是流地址而非固定 Video 1/2 槽位。
│   │           ├── AndroidVideoDecoderPolicy.cc
                                                            # ① 工厂优先级配置：识别可用厂商 MediaCodec，按 forceHardwareDecoding 设置硬解相关 rank，注册 H.265 归一化适配器并避免将已知 Android 软件包装器当作厂商硬件。
                                                            # ② 候选供应：按当前输入是否原生 byte-stream 组合可兼容的备用适配器和直接解码器，提供给接收器级 Fallback；主/次流的自动选择共享初始策略，具体重试选择互不串用。
│   │           ├── AndroidVideoDecoderPolicy.h
                                                            # ① 全局 Android 解码策略接口：apply(forceHardwareDecoding) 安装产品 H.264/H.265 厂商硬解选择规则，hardwareRetryFactoryNames 按当前分包格式返回候选工厂。
                                                            # ② 时序要求：GStreamer 已初始化而两路 decodebin 尚未创建时调用；本接口决定可自动选择的 rank 和候选集合，单条流的失败证据、重试次数与代次由 Recovery/Fallback 管理。
│   │           ├── AndroidVideoDecoderRecovery.cc
                                                            # ① 失败诊断：订阅主路启动和逐阶段出帧信号，用 _armFirstFrameWatchdog 检查首次显示，_handlePipelineError 区分源错误与解码分支错误，旧 URI/代次事件不影响当前管线。
                                                            # ② 恢复执行：_restartAfterDecoderFailure 将有效证据交给 AndroidH265DecoderFallback，必要时切换兼容格式/硬解候选，等待停止完成后重启；正常 sink 出帧解除首帧等待。
│   │           ├── AndroidVideoDecoderRecovery.h
                                                            # ① 主路首帧恢复观察器：install 将 QObject 观察器挂到主 VideoReceiver，声明管线代次、源帧、解码器选择/出帧、sink 出帧、错误及停止完成处理接口。
                                                            # ② 状态组成：保存当前 URI/generation、编码/工厂、已接收帧证据和首帧 watchdog；这是主路的恢复驱动，第二路等价时序由 DualVideoManager 自身实现。
│   │           ├── PulledVideoResolutionProbe.cc
                                                            # ① 尺寸采集：观察 sink pad 的协商 caps，读取宽高并等待对应协商下的真实解码 buffer 到达后再发布，避免只有 CAPS 尚未出帧就确认能力；随对象生命周期清理探针，排除 thermal 接收器。
                                                            # ② 能力链路：发布接收器尺寸通知及可选回调；CustomPlugin 将主路尺寸排队交给 GimbalControlManager，MT11 读取第二路 receiver.videoSizeChanged；本文件只报告真实协商结果，倍率上限及照片条件由各 Manager/Policy 判断。
│   │           └── PulledVideoResolutionProbe.h
                                                            # ① 协商尺寸探针接口：install 接收 GStreamer sink、QObject 生命周期对象及可选 ResolutionHandler 回调，返回是否成功安装非 thermal 视频探针。
                                                            # ② 数据契约：回调输出解码/显示链路协商得到的 QSize，供产品相机判断真实拉流尺寸；不使用 PIP 控件宽高代替视频分辨率，也不改变接收器画面布局。
│   └── Viewer3D/
                                                            # 三维后端、场景、模型和手动导入样例
│       ├── CityMapGeometry.cc
                                                            # ① OSM 建筑几何桥：setOsmFilePath/loadOsmMap 将设置中的地图路径交给 OsmParser，监听解析完成/地图变化，updateViewer 把建筑顶点数据装入 QQuick3DGeometry。
                                                            # ② 渲染输出：设置位置属性、步长和三角形绘制方式，供 Viewer3DModel 中建筑 Model 使用；本目录只替换实现 .cc，类声明复用原生 CityMapGeometry.h，建筑三角化算法在 OsmParser。
│       ├── CustomViewer3DManager.cc
                                                            # ① 对象创建：构造 OsmParser 与 Viewer3DQmlBackend 并调用后端 init 建立关联，析构时释放对象，使地图解析和 GPS 基准随三维管理器生命周期管理。
                                                            # ② 类型注册：在 QGroundControl.Viewer3D 中注册 Viewer3DManager、GeoCoordinateType、CityMapGeometry、地表几何/纹理及只读后端类型；CustomPlugin 调用后，Viewer3D.qml 的 Loader 才能实例化这些 C++ 类型。
│       ├── CustomViewer3DManager.h
                                                            # ① 三维对象容器声明：向 QML 提供只读 osmParser 和 qmlBackend 属性，声明构造/析构以及静态 registerQmlTypes，是本地三维视图的数据入口。
                                                            # ② 归属关系：管理 OsmParser 与 Viewer3DQmlBackend 的生命周期，QML 以 Viewer3DManager 类型创建；导入外部模型的 External3DMapManager 则由 CustomPlugin 提供，二者职责分开。
│       ├── External3DMapManager.cc
                                                            # ① 导入路径选择：OBJ/glTF/GLB/QML 直接校验并写入模型设置；FBX/DAE/STL/PLY 查找 Qt Balsam，通过 QProcess 异步转换为可加载资源，查找来源包括配置环境、应用/Qt 路径和 PATH。
                                                            # ② 结果提交：在应用数据目录 Viewer3DExternalMaps 下管理转换输出，选择生成的 QML，更新文件路径与导入状态并报告失败；Viewer3DSettingsGroup 展示状态，External3DMap.qml 根据最终路径加载。
│       ├── External3DMapManager.h
                                                            # ① 外部模型导入接口：提供 importModelFile、clearStatus、格式判断、balsamExecutable 与 supportedFormatsText；importing/lastImportStatus 向设置页报告任务执行和结果。
                                                            # ② 任务状态：保存 Viewer3DSettings、转换进程与状态处理方法，声明直接格式识别、Balsam 查找、输出目录及转换后 QML 选择；负责准备可加载文件，场景展示在 External3DMap.qml。
│       ├── ExternalWGS84_UE5_MapSample/
                                                            # 手动导入/配准样例，不进入产品 QRC/APK
│       │   ├── osm_overpass_source.json
                                                            # ① 地图来源记录：保存生成样例时取得的 Overpass/OSM 地物数据，供建筑、道路等来源溯源、重新处理或重新生成外部模型。
                                                            # ② 运行边界：Viewer3D 导入时读取已生成的 OBJ/FBX 及材质，而不把此 JSON 当场景直接加载；需要更换样例区域时同时重新生成几何并核对原点和来源说明。
│       │   ├── qgc_viewer3d_import_settings.json
                                                            # ① 样例配准数据：保存建议的 WGS84 原点、单位到米、比例、朝向及高度等参数，为手工填写 Viewer3D 设置和比较场景位置提供依据。
                                                            # ② 读取方式：当前 importModelFile 不会自动读取/应用此 JSON，开发者需对照 README 在三维设置页填写；若调整样例几何的坐标或单位，应同步维护参数与说明。
│       │   ├── README.md
                                                            # ① 外部地图样例说明：列出推荐 OBJ、场景组成、OSM 来源/许可、资源数量、使用范围与导入步骤，用于开发者理解样例及进行外部模型验收。
                                                            # ② 配准参数：说明原点 37.4456/-122.1616/9 m、单位/比例 1、yaw 0 等建议值，并要求 OBJ、MTL、textures 保持相对路径；样例用于外部加载，不是随 APK 打包的默认运行场景。
│       │   ├── realistic_town_wgs84_map.fbx
                                                            # ① 需转换的城镇样例：提供同类场景的 FBX 版本，用于验证 External3DMapManager 的 Balsam 转换路径以及转换后 QML/网格/材质的加载。
                                                            # ② 使用流程：设置页选择 FBX 后等待转换完成，再由 External3DMap 加载输出；运行环境需要可用的 Balsam，直接加载验证可使用同目录 OBJ，地理配准参数仍需按 README 设置。
│       │   ├── realistic_town_wgs84_map.mtl
                                                            # ① OBJ 材质表：定义地面、道路、外墙、屋顶、车辆和树木等材质的颜色/反光参数，并通过 map_Kd 将 11 张纹理映射到对应材质。
                                                            # ② 关联方式：realistic_town_wgs84_map.obj 按材质名引用，贴图采用 textures/ 相对路径；修改材质名或贴图文件名需同时核对 OBJ/MTL，几何位置与 QGC 原点配置不在此文件。
│       │   ├── realistic_town_wgs84_map.obj
                                                            # ① 直接加载的城镇几何：包含建筑、道路、车辆、树木和路灯等模型及 UV，使用以样例原点为基准的米制 ENU 坐标，适合验证 WGS84 配准和飞机/航线叠加。
                                                            # ② 资源依赖：通过同名 MTL 引用材质及 textures；由 External3DMap 的 RuntimeLoader 加载，移动模型时要一起保留材质/贴图目录，原点、比例、yaw 在设置页配置。
│       │   └── textures/
                                                            # 城镇材质引用的贴图
│       │       ├── asphalt_worn.png
                                                            # ① 道路磨损沥青纹理：作为样例 MTL 中 mat_asphalt 的 map_Kd 颜色贴图，由模型 UV 决定在对应表面的铺设位置与重复方式。
                                                            # ② 资源关联：随 realistic_town_wgs84_map.obj/.mtl 一起提供，保持 textures/asphalt_worn.png 相对路径；替换可改变表面观感，不改变模型几何、原点或比例，也不单独加入应用 QRC。
│       │       ├── facade_brick_windows.png
                                                            # ① 砖墙与重复窗户纹理：作为样例 MTL 中 mat_facade_brick 的 map_Kd 颜色贴图，由模型 UV 决定在对应表面的铺设位置与重复方式。
                                                            # ② 资源关联：随 realistic_town_wgs84_map.obj/.mtl 一起提供，保持 textures/facade_brick_windows.png 相对路径；替换可改变表面观感，不改变模型几何、原点或比例，也不单独加入应用 QRC。
│       │       ├── facade_light_windows.png
                                                            # ① 浅色建筑外墙与窗户纹理：作为样例 MTL 中 mat_facade_light 的 map_Kd 颜色贴图，由模型 UV 决定在对应表面的铺设位置与重复方式。
                                                            # ② 资源关联：随 realistic_town_wgs84_map.obj/.mtl 一起提供，保持 textures/facade_light_windows.png 相对路径；替换可改变表面观感，不改变模型几何、原点或比例，也不单独加入应用 QRC。
│       │       ├── facade_modern_windows.png
                                                            # ① 现代建筑立面与窗户纹理：作为样例 MTL 中 mat_facade_modern 的 map_Kd 颜色贴图，由模型 UV 决定在对应表面的铺设位置与重复方式。
                                                            # ② 资源关联：随 realistic_town_wgs84_map.obj/.mtl 一起提供，保持 textures/facade_modern_windows.png 相对路径；替换可改变表面观感，不改变模型几何、原点或比例，也不单独加入应用 QRC。
│       │       ├── facade_tan_windows.png
                                                            # ① 棕褐色建筑立面与窗户纹理：作为样例 MTL 中 mat_facade_tan 的 map_Kd 颜色贴图，由模型 UV 决定在对应表面的铺设位置与重复方式。
                                                            # ② 资源关联：随 realistic_town_wgs84_map.obj/.mtl 一起提供，保持 textures/facade_tan_windows.png 相对路径；替换可改变表面观感，不改变模型几何、原点或比例，也不单独加入应用 QRC。
│       │       ├── grass_mixed.png
                                                            # ① 地面草地纹理：作为样例 MTL 中 mat_grass 的 map_Kd 颜色贴图，由模型 UV 决定在对应表面的铺设位置与重复方式。
                                                            # ② 资源关联：随 realistic_town_wgs84_map.obj/.mtl 一起提供，保持 textures/grass_mixed.png 相对路径；替换可改变表面观感，不改变模型几何、原点或比例，也不单独加入应用 QRC。
│       │       ├── roof_flat_gray.png
                                                            # ① 灰色屋顶纹理：作为样例 MTL 中 mat_roof_gray / mat_roof_flat 的 map_Kd 颜色贴图，由模型 UV 决定在对应表面的铺设位置与重复方式。
                                                            # ② 资源关联：随 realistic_town_wgs84_map.obj/.mtl 一起提供，保持 textures/roof_flat_gray.png 相对路径；替换可改变表面观感，不改变模型几何、原点或比例，也不单独加入应用 QRC。
│       │       ├── roof_tile_red.png
                                                            # ① 红色瓦屋顶纹理：作为样例 MTL 中 mat_roof_red 的 map_Kd 颜色贴图，由模型 UV 决定在对应表面的铺设位置与重复方式。
                                                            # ② 资源关联：随 realistic_town_wgs84_map.obj/.mtl 一起提供，保持 textures/roof_tile_red.png 相对路径；替换可改变表面观感，不改变模型几何、原点或比例，也不单独加入应用 QRC。
│       │       ├── shopfront_facade.png
                                                            # ① 沿街店铺门面纹理：作为样例 MTL 中 mat_shopfront 的 map_Kd 颜色贴图，由模型 UV 决定在对应表面的铺设位置与重复方式。
                                                            # ② 资源关联：随 realistic_town_wgs84_map.obj/.mtl 一起提供，保持 textures/shopfront_facade.png 相对路径；替换可改变表面观感，不改变模型几何、原点或比例，也不单独加入应用 QRC。
│       │       ├── sidewalk_concrete.png
                                                            # ① 人行道混凝土纹理：作为样例 MTL 中 mat_sidewalk 的 map_Kd 颜色贴图，由模型 UV 决定在对应表面的铺设位置与重复方式。
                                                            # ② 资源关联：随 realistic_town_wgs84_map.obj/.mtl 一起提供，保持 textures/sidewalk_concrete.png 相对路径；替换可改变表面观感，不改变模型几何、原点或比例，也不单独加入应用 QRC。
│       │       └── tree_leaf.png
                                                            # ① 树木叶片/树冠纹理：作为样例 MTL 中 mat_tree_leaf 的 map_Kd 颜色贴图，由模型 UV 决定在对应表面的铺设位置与重复方式。
                                                            # ② 资源关联：随 realistic_town_wgs84_map.obj/.mtl 一起提供，保持 textures/tree_leaf.png 相对路径；替换可改变表面观感，不改变模型几何、原点或比例，也不单独加入应用 QRC。
│       ├── Images/
                                                            # 三维入口图标
│       │   └── city_3d_map_icon.svg
                                                            # ① 三维入口图标：提供城市场景的矢量轮廓，由飞行工具条的三维按钮显示；修改图标形状时编辑本文件。
                                                            # ② 加载与交互：custom.qrc 将资源打包，FlyViewToolStripActionList 控制图标尺寸、按钮可见性和 3D View/Fly 点击切换；本资源不决定三维地图来源或模型加载方式。
│       ├── OsmParser.cc
                                                            # ① OSM 数据处理：parseOsmFile 启动复用的原生后台解析线程，osmParserFinished 保存有效建筑/区域数据、GPS 参考点并发出地图变化；建筑高度优先取 height，其次层数×默认层高。
                                                            # ② 建筑网格：buildingToMesh 对外轮廓及内孔进行 earcut 三角化，生成屋顶、底面和外/内墙面顶点，交给 CityMapGeometry；类声明和底层 OSM 线程仍复用原生文件，算法定制集中在本实现。
│       ├── Viewer3D.SettingsGroup.json
                                                            # ① 三维配置元数据：定义 enabled、地图源/API Key、OSM 路径/层高、外部模型路径/原点经纬高/单位/比例/yaw 及 altitudeBias 等 14 个 Fact 的类型、默认值、范围和说明。
                                                            # ② 使用链路：Viewer3DSettings 加载并持久化，Viewer3DSettingsGroup 提供编辑，后端与 QML 场景订阅变化；修改默认值不等同覆盖已有用户值，原点和高度偏置共同影响外部地图与车辆/任务配准。
│       ├── Viewer3DQml/
                                                            # 三维窗口与本地/Google 页面
│       │   ├── Drones/
                                                            # 飞行器模型总装与部件 mesh
│       │   │   ├── Djif450/
                                                            # F450 部件几何；部件 QML 通过 QRC 复用原生文件
│       │   │   │   ├── DroneModel_arm_1/
│       │   │   │   │   └── node.mesh
                                                            # ① F450 第 1 根机臂的网格几何，保存该部件的顶点/表面几何；它是 F450 组合模型中的独立部件数据，不包含车辆遥测或姿态更新逻辑。
                                                            # ② 资源链路：custom.qrc 按部件路径打包，由复用的同名原生部件 QML 加载，再由 DroneModelDjiF450.qml 总装和驱动；替换网格时保持该部件的坐标、比例及资源引用一致。
│       │   │   │   ├── DroneModel_arm_2/
│       │   │   │   │   └── node.mesh
                                                            # ① F450 第 2 根机臂的网格几何，保存该部件的顶点/表面几何；它是 F450 组合模型中的独立部件数据，不包含车辆遥测或姿态更新逻辑。
                                                            # ② 资源链路：custom.qrc 按部件路径打包，由复用的同名原生部件 QML 加载，再由 DroneModelDjiF450.qml 总装和驱动；替换网格时保持该部件的坐标、比例及资源引用一致。
│       │   │   │   ├── DroneModel_arm_3/
│       │   │   │   │   └── node.mesh
                                                            # ① F450 第 3 根机臂的网格几何，保存该部件的顶点/表面几何；它是 F450 组合模型中的独立部件数据，不包含车辆遥测或姿态更新逻辑。
                                                            # ② 资源链路：custom.qrc 按部件路径打包，由复用的同名原生部件 QML 加载，再由 DroneModelDjiF450.qml 总装和驱动；替换网格时保持该部件的坐标、比例及资源引用一致。
│       │   │   │   ├── DroneModel_arm_4/
│       │   │   │   │   └── node.mesh
                                                            # ① F450 第 4 根机臂的网格几何，保存该部件的顶点/表面几何；它是 F450 组合模型中的独立部件数据，不包含车辆遥测或姿态更新逻辑。
                                                            # ② 资源链路：custom.qrc 按部件路径打包，由复用的同名原生部件 QML 加载，再由 DroneModelDjiF450.qml 总装和驱动；替换网格时保持该部件的坐标、比例及资源引用一致。
│       │   │   │   ├── DroneModel_Base_bottom_1/
│       │   │   │   │   └── node.mesh
                                                            # ① F450 机架下板的网格几何，保存该部件的顶点/表面几何；它是 F450 组合模型中的独立部件数据，不包含车辆遥测或姿态更新逻辑。
                                                            # ② 资源链路：custom.qrc 按部件路径打包，由复用的同名原生部件 QML 加载，再由 DroneModelDjiF450.qml 总装和驱动；替换网格时保持该部件的坐标、比例及资源引用一致。
│       │   │   │   ├── DroneModel_Base_Top_1/
│       │   │   │   │   └── node.mesh
                                                            # ① F450 机架上板的网格几何，保存该部件的顶点/表面几何；它是 F450 组合模型中的独立部件数据，不包含车辆遥测或姿态更新逻辑。
                                                            # ② 资源链路：custom.qrc 按部件路径打包，由复用的同名原生部件 QML 加载，再由 DroneModelDjiF450.qml 总装和驱动；替换网格时保持该部件的坐标、比例及资源引用一致。
│       │   │   │   ├── DroneModel_BLDC_1/
│       │   │   │   │   └── node.mesh
                                                            # ① F450 第 1 个电机的网格几何，保存该部件的顶点/表面几何；它是 F450 组合模型中的独立部件数据，不包含车辆遥测或姿态更新逻辑。
                                                            # ② 资源链路：custom.qrc 按部件路径打包，由复用的同名原生部件 QML 加载，再由 DroneModelDjiF450.qml 总装和驱动；替换网格时保持该部件的坐标、比例及资源引用一致。
│       │   │   │   ├── DroneModel_BLDC_2/
│       │   │   │   │   └── node.mesh
                                                            # ① F450 第 2 个电机的网格几何，保存该部件的顶点/表面几何；它是 F450 组合模型中的独立部件数据，不包含车辆遥测或姿态更新逻辑。
                                                            # ② 资源链路：custom.qrc 按部件路径打包，由复用的同名原生部件 QML 加载，再由 DroneModelDjiF450.qml 总装和驱动；替换网格时保持该部件的坐标、比例及资源引用一致。
│       │   │   │   ├── DroneModel_BLDC_3/
│       │   │   │   │   └── node.mesh
                                                            # ① F450 第 3 个电机的网格几何，保存该部件的顶点/表面几何；它是 F450 组合模型中的独立部件数据，不包含车辆遥测或姿态更新逻辑。
                                                            # ② 资源链路：custom.qrc 按部件路径打包，由复用的同名原生部件 QML 加载，再由 DroneModelDjiF450.qml 总装和驱动；替换网格时保持该部件的坐标、比例及资源引用一致。
│       │   │   │   ├── DroneModel_BLDC_4/
│       │   │   │   │   └── node.mesh
                                                            # ① F450 第 4 个电机的网格几何，保存该部件的顶点/表面几何；它是 F450 组合模型中的独立部件数据，不包含车辆遥测或姿态更新逻辑。
                                                            # ② 资源链路：custom.qrc 按部件路径打包，由复用的同名原生部件 QML 加载，再由 DroneModelDjiF450.qml 总装和驱动；替换网格时保持该部件的坐标、比例及资源引用一致。
│       │   │   │   ├── DroneModel_propeller2_2/
│       │   │   │   │   └── node.mesh
                                                            # ① F450 螺旋桨 propeller2_2 的网格几何，保存该部件的顶点/表面几何；它是 F450 组合模型中的独立部件数据，不包含车辆遥测或姿态更新逻辑。
                                                            # ② 资源链路：custom.qrc 按部件路径打包，由复用的同名原生部件 QML 加载，再由 DroneModelDjiF450.qml 总装和驱动；替换网格时保持该部件的坐标、比例及资源引用一致。
│       │   │   │   ├── DroneModel_propeller2_7/
│       │   │   │   │   └── node.mesh
                                                            # ① F450 螺旋桨 propeller2_7 的网格几何，保存该部件的顶点/表面几何；它是 F450 组合模型中的独立部件数据，不包含车辆遥测或姿态更新逻辑。
                                                            # ② 资源链路：custom.qrc 按部件路径打包，由复用的同名原生部件 QML 加载，再由 DroneModelDjiF450.qml 总装和驱动；替换网格时保持该部件的坐标、比例及资源引用一致。
│       │   │   │   ├── DroneModel_propeller22_1/
│       │   │   │   │   └── node.mesh
                                                            # ① F450 螺旋桨 propeller22_1 的网格几何，保存该部件的顶点/表面几何；它是 F450 组合模型中的独立部件数据，不包含车辆遥测或姿态更新逻辑。
                                                            # ② 资源链路：custom.qrc 按部件路径打包，由复用的同名原生部件 QML 加载，再由 DroneModelDjiF450.qml 总装和驱动；替换网格时保持该部件的坐标、比例及资源引用一致。
│       │   │   │   └── DroneModel_propeller22_2/
│       │   │   │       └── node.mesh
                                                            # ① F450 螺旋桨 propeller22_2 的网格几何，保存该部件的顶点/表面几何；它是 F450 组合模型中的独立部件数据，不包含车辆遥测或姿态更新逻辑。
                                                            # ② 资源链路：custom.qrc 按部件路径打包，由复用的同名原生部件 QML 加载，再由 DroneModelDjiF450.qml 总装和驱动；替换网格时保持该部件的坐标、比例及资源引用一致。
│       │   │   └── DroneModelDjiF450.qml
                                                            # ① F450 飞机模型总装：实例化机臂、上下机架、电机和螺旋桨等部件，绑定车辆 roll/pitch/heading、机号标识及模型比例；部件 QML 经 QRC 复用原生文件，几何使用同目录 Djif450 网格。
                                                            # ② 位置驱动：以 gpsRef 转换车辆经纬度，displayAltitudeMeters 选择外部地图 AMSL 相对原点或对应高度，再加 altitudeBias 并乘 10；位置约 200 ms、姿态约 100 ms 动画平滑，具体网格轮廓由 node.mesh 提供。
│       │   ├── Google3DMapUnavailable.qml
                                                            # ① 无 WebEngine 平台提示：保留 viewer3DManager/isViewer3DOpen 接口，与其他三维页面采用相同加载约定，显示当前构建无法提供 Google 三维页面的说明。
                                                            # ② 选择关系：Viewer3D.qml 根据 corePlugin.google3DMapsAvailable 决定加载本页；不可用提示文字和排版在这里维护，WebEngine 是否编入由 CMake 决定，OSM/外部本地场景另行加载。
│       │   ├── Google3DMapView.qml
                                                            # ① Google 三维页面：通过 WebEngineView 加载 _buildGoogle3DHtml 生成的 HTML，使用配置 API Key 和有效中心经纬高建立地图，提供缺少 Key/坐标和加载状态提示。
                                                            # ② 重载机制：中心优先活动车辆坐标、否则用飞行地图位置；reloadGoogle3DMap 结合约 150 ms 定时合并和加载签名避免重复生成，车辆切换/配置触发重载，当前实现不是逐帧追踪车辆的本地三维叠加。
│       │   ├── Models3D/
                                                            # 场景、外部地图与车辆/任务叠加
│       │   │   ├── External3DMap.qml
                                                            # ① 外部地图显示：识别路径扩展名，OBJ/glTF/GLB 用 RuntimeLoader，QML 用 Loader；输出 statusText/hasBlockingIssue 说明空路径、未转换格式或加载异常。
                                                            # ② 配准变换：模型缩放为 unitToMeters×userScale×10，按 yawDegrees 绕竖直轴旋转；世界坐标原点由后端选择，车辆/航点高度在叠加组件计算，FBX 等转换由 External3DMapManager 完成。
│       │   │   ├── Viewer3DModel.qml
                                                            # ① 本地三维场景总装：创建 View3D、相机/灯光、建筑与地表材质，通过 mapGeometryLoader 在 OSM 几何和 External3DMap 间切换；为车辆创建 PlanMasterController 和 Viewer3DVehicleItems。
                                                            # ② 交互与提示：rotateCamera/moveCamera/zoomCamera 响应鼠标和触控，按缩放调整移动速度，并显示地表下载/外部地图加载状态；gpsRef 来自 Manager 后端，模型/任务数据由车辆与计划控制器提供。
│       │   │   └── Viewer3DVehicleItems.qml
                                                            # ① 单车叠加总装：绑定车辆、任务控制器和三维设置，isItemAcceptable/getItemName 筛选并命名可显示任务项，addMissionItemsToListModel 与 addSegmentToMissionPathModel 构建航点及相邻航段。
                                                            # ② 位置与高度：创建 DroneModelDjiF450、Waypoint3DModel 和 Line3D；外部地图下使用 AMSL 减原点海拔，并把 altitudeBias 同时用于飞机、任务点和航段端点，统一换算到每米 10 场景单位。
│       │   └── Viewer3D.qml
                                                            # ① 三维窗口外层：open/close 管理显示，管理器 Loader 按三维总开关创建 Viewer3DManager，内容 Loader 在本地场景、Google 页面及无 WebEngine 提示之间选择。
                                                            # ② 绑定与生命周期：_viewer3DSource 确定资源，_bindLoadedView 将管理器和开窗状态绑定给载入项；普通关闭只隐藏窗口，关闭三维总开关才停用管理器 Loader，场景细节在对应页面实现。
│       ├── Viewer3DQmlBackend.cc
                                                            # ① 参考点选择：外部地图模式优先使用配置的原点经纬高；否则采用有效 OSM 参考点，再以活动车辆坐标作为后备，通过 _restoreBestGpsRef 更新统一基准。
                                                            # ② 事件连接：init 接入地图解析、设置变化和车辆切换/坐标更新，_trySetExternalMapGpsRef 校验并应用外部原点；发出 gpsRefChanged 后场景中的坐标转换、地表和车辆显示重新绑定。
│       ├── Viewer3DQmlBackend.h
                                                            # ① 三维 GPS 基准接口：暴露 gpsRef 和变化通知，声明 init、活动车辆/坐标回调、外部地图设置回调及最佳参考点恢复方法。
                                                            # ② 数据依赖：持有 OsmParser、Viewer3DSettings 和活动车辆关联，供 Viewer3DModel/车辆叠加把 WGS84 坐标转到本地场景；此类选择基准，不承担 OBJ 导入或相机视角操作。
│       ├── Viewer3DSettings.cc
                                                            # ① 设置注册实现：在 Viewer3D 持久化组创建 14 个 Fact，并将设置类注册为 QML 可引用类型，确保界面和 C++ 读取相同配置。
                                                            # ② 扩展入口：新增三维参数时在此增加 Fact 声明，并同步 .h getter、JSON 元数据和设置页绑定；场景通过值变化更新地图模式、模型变换或高度，参数的具体使用不在本类执行。
│       ├── Viewer3DSettings.h
                                                            # ① 三维设置访问声明：提供地图总开关、Google/外部模式、OSM/模型路径、外部原点、单位、比例、yaw、层高及 altitudeBias 的键名和 Fact getter。
                                                            # ② 依赖关系：由 CustomPlugin 创建供设置页、导入器、GPS 后端和场景共用；元数据在 Viewer3D.SettingsGroup.json，实际设置组与 QML 类型注册在 .cc。
│       └── Viewer3DTerrainGeometry.cc
                                                            # ① 地表几何生成：updateEarthData/buildTerrain_2 根据 GPS 参考点、ROI 范围和瓦片网格生成地表顶点，计算法线与 UV，设置 Quick3D 所需的几何属性。
                                                            # ② 贴图关系：与复用的 Viewer3DTerrainTexture 和三维材质/shader 配合把地图瓦片铺到地表；类声明仍来自原生头文件，本文件维护地表坐标、分段和网格更新，不生成建筑或导入外部模型。
├── test/
                                                            # 开发验证；C++ 按桌面测试开关构建，Python 检查单独运行
│   ├── Android/
                                                            # UniRC 协议与通道策略测试
│   │   └── UniRcProtocolTest.cc
                                                            # ① UniRC 协议/通道回归：构造 20 Hz 请求及通道响应，验证 CRC、帧长、半帧/多帧、重同步和非法输入，同时检查 CH9 回中/反向、CH10 释放到按下边沿和 CH7/8 死区。
                                                            # ② 动作状态验证：直接调用 UniRcProtocol、UniRcChannelPolicy 和 Ch10GimbalActionState，确认手动操作顺序、迟到 ACK 与动作轮换规则；作为桌面 QtTest 目标运行，不需要实际 Bluetooth 或遥控器。
│   ├── FlightDisplay/
                                                            # 飞行页交互测试
│   │   └── DualPipResizeTest.py
                                                            # ① PIP 缩放回归：PySide6 离屏加载实际 DualPipView 和原生 PipState，用 Qt 鼠标事件验证上下手柄连续/反向拖动、越界限幅、单辅窗、取消后重拖及切换主辅后重新进入辅窗缩放。
                                                            # ② 验证边界：设置、字体及地图/视频内容使用替身，同时检查实际内容项宽高、16:9 比例和父容器尺寸变化时的限制；单独运行，不属于 CTest，也不测量双路解码时的渲染帧率。
│   ├── Gimbal/
                                                            # 相机协议、媒体、云台和方位角测试
│   │   ├── AzimuthStubs/
                                                            # 方位角 Provider 测试依赖替身；模拟类集中在 TestDoubles.h
│   │   │   ├── Fact.h
                                                            # ① 测试头入口：模拟 rawValue 的读写和变更信号，用于区分原生显示航向与消息提供的原始航向。
                                                            # ② 包含同目录 TestDoubles.h 中的集中模拟类；GimbalAzimuthProviderTest 通过测试 include 路径解析到本头，仅满足方位角 Provider所需接口，不替换产品构建中的原生实现。
│   │   │   ├── Gimbal.h
                                                            # ① 测试头入口：模拟 deviceId、managerCompid 等云台标识，使 Provider 可以按活动云台选择消息来源。
                                                            # ② 包含同目录 TestDoubles.h 中的集中模拟类；GimbalAzimuthProviderTest 通过测试 include 路径解析到本头，仅满足方位角 Provider所需接口，不替换产品构建中的原生实现。
│   │   │   ├── GimbalController.h
                                                            # ① 测试头入口：模拟 activeGimbal 与切换通知，触发 Provider 重新匹配云台缓存。
                                                            # ② 包含同目录 TestDoubles.h 中的集中模拟类；GimbalAzimuthProviderTest 通过测试 include 路径解析到本头，仅满足方位角 Provider所需接口，不替换产品构建中的原生实现。
│   │   │   ├── MAVLinkLib.h
                                                            # ① 测试协议头适配：直接包含 <common/mavlink.h>，为独立测试提供真实 MAVLink 消息、字段和编码函数，而不引入整个 QGC MAVLink 包装依赖。
                                                            # ② 编译关联：在对应测试目标的优先 include 路径下满足生产源码对 MAVLinkLib.h 的引用；数据包仍按实际协议构造，车辆/链路等运行环境由同目录 TestDoubles.h 模拟。
│   │   │   ├── MultiVehicleManager.h
                                                            # ① 测试头入口：模拟活动车辆获取、设置与切车信号，检查多车辆隔离和重新绑定。
                                                            # ② 包含同目录 TestDoubles.h 中的集中模拟类；GimbalAzimuthProviderTest 通过测试 include 路径解析到本头，仅满足方位角 Provider所需接口，不替换产品构建中的原生实现。
│   │   │   ├── TestDoubles.h
                                                            # ① 方位角 Provider依赖模拟集合：集中定义本目录各入口头对应的 QObject/Fact/车辆/云台等替身，实现测试可设置的属性、通知信号和调用记录。
                                                            # ② 由 GimbalAzimuthProviderTest 创建并操纵，驱动生产类进入正常、切换、失败和迟到结果等场景；新增生产依赖时优先补充最小所需接口，保持与其他 Stubs 套件的状态和编译目标隔离。
│   │   │   ├── Vehicle.h
                                                            # ① 测试头入口：模拟车辆/组件 ID、heading Fact、云台控制器和链路对象，组成 Provider 的最小车辆上下文。
                                                            # ② 包含同目录 TestDoubles.h 中的集中模拟类；GimbalAzimuthProviderTest 通过测试 include 路径解析到本头，仅满足方位角 Provider所需接口，不替换产品构建中的原生实现。
│   │   │   └── VehicleLinkManager.h
                                                            # ① 测试头入口：模拟 communicationLost 状态与通知，检查失联清空和重连必须有新样本的规则。
                                                            # ② 包含同目录 TestDoubles.h 中的集中模拟类；GimbalAzimuthProviderTest 通过测试 include 路径解析到本头，仅满足方位角 Provider所需接口，不替换产品构建中的原生实现。
│   │   ├── CoordinatorStubs/
                                                            # 回中协调器测试依赖替身；模拟类集中在 TestDoubles.h
│   │   │   ├── Fact.h
                                                            # ① 测试头入口：为云台标识、俯仰角等提供最小 rawValue 读取，满足协调器判断当前姿态的依赖。
                                                            # ② 包含同目录 TestDoubles.h 中的集中模拟类；GimbalCenterCoordinatorTest 通过测试 include 路径解析到本头，仅满足回中协调器所需接口，不替换产品构建中的原生实现。
│   │   │   ├── Gimbal.h
                                                            # ① 测试头入口：模拟标识、俯仰角和控制权属性/通知，供测试推进显式接管与姿态请求。
                                                            # ② 包含同目录 TestDoubles.h 中的集中模拟类；GimbalCenterCoordinatorTest 通过测试 include 路径解析到本头，仅满足回中协调器所需接口，不替换产品构建中的原生实现。
│   │   │   ├── GimbalController.h
                                                            # ① 测试头入口：模拟申请控制权、回中和角度发送，记录调用参数以断言预激活及最终命令顺序。
                                                            # ② 包含同目录 TestDoubles.h 中的集中模拟类；GimbalCenterCoordinatorTest 通过测试 include 路径解析到本头，仅满足回中协调器所需接口，不替换产品构建中的原生实现。
│   │   │   ├── MultiVehicleManager.h
                                                            # ① 测试头入口：模拟活动车辆与切车通知，检查请求上下文取消和动作序列复位。
                                                            # ② 包含同目录 TestDoubles.h 中的集中模拟类；GimbalCenterCoordinatorTest 通过测试 include 路径解析到本头，仅满足回中协调器所需接口，不替换产品构建中的原生实现。
│   │   │   ├── TestDoubles.h
                                                            # ① 回中协调器依赖模拟集合：集中定义本目录各入口头对应的 QObject/Fact/车辆/云台等替身，实现测试可设置的属性、通知信号和调用记录。
                                                            # ② 由 GimbalCenterCoordinatorTest 创建并操纵，驱动生产类进入正常、切换、失败和迟到结果等场景；新增生产依赖时优先补充最小所需接口，保持与其他 Stubs 套件的状态和编译目标隔离。
│   │   │   └── Vehicle.h
                                                            # ① 测试头入口：记录发送命令并注入 ACK，检查配置确认、最终确认、失败和迟到回调的处理。
                                                            # ② 包含同目录 TestDoubles.h 中的集中模拟类；GimbalCenterCoordinatorTest 通过测试 include 路径解析到本头，仅满足回中协调器所需接口，不替换产品构建中的原生实现。
│   │   ├── GimbalAzimuthPolicyTest.cc
                                                            # ① 方位角纯计算测试：覆盖 Earth/Vehicle/legacy 参考系、delta_yaw、安装方向反转、四元数正负/非单位输入、跨北角度及缺少世界参考的情况。
                                                            # ② 验证方式：构造 Input 调用 GimbalAzimuthPolicy 并比较有效性、角度与来源，包含姿态/飞控航向变化的连续样本；保证坐标换算语义，实际消息路由及时效另由 Provider 测试检查。
│   │   ├── GimbalAzimuthProviderTest.cc
                                                            # ① 方位角数据链测试：构造实际 MAVLink 消息并驱动生产 Provider，检查车辆/component/device 路由、未取整航向、显式参考系覆盖、消息过期和断线重连后的有效性。
                                                            # ② 隔离依赖：使用 AzimuthStubs 模拟车辆/云台/链路，以 Qt 信号和受控样本触发变化，确认错误来源、旧帧及无效遥测不会续命；不需要启动完整 QGC 或连接云台。
│   │   ├── GimbalCenterCoordinatorTest.cc
                                                            # ① 回中事务测试：覆盖重新显式接管、配置 ACK 匹配、控制权一致性、1° 预激活、最终命令/ACK、超时、取消和切车，检查失败时不发后续姿态命令。
                                                            # ② 状态断言：用 CoordinatorStubs 记录调用与参数，注入迟到/重复 ACK 和手动新操作，确认旧结果不会执行取消动作或推进新的 CH10 序列；直接编译生产 Coordinator 进行桌面验证。
│   │   ├── GimbalHeadingTelemetryTest.cc
                                                            # ① 原始航向缓存测试：验证小数精度、跨北归一、来源独立过期、最新测量选择、同时间四元数优先和高延迟后备，避免把显示取整值用于方位角。
                                                            # ② 时序断言：向 update 注入无效角度、接收时间回退、重复/小幅回退的启动时间、重启及时间绕回，检查 heading/clear 的结果；使用受控时间直接测试，无车辆或网络依赖。
│   │   ├── GimbalMediaSessionPolicyTest.cc
                                                            # ① A8 媒体状态决策测试：组合本地意图、SD 状态、流/开关、所有权、外部录制和 pending，验证启动、确认、停止、观察/释放及本地独立录像的可用性。
                                                            # ② 边界断言：检查在途启动取消后等待结果、迟到确认后的补偿停止、停止 pending 防重启和重复协调幂等；直接调用纯 Policy，不验证编码器文件输出、Android 图库或 MT11 Manager 全部行为。
│   │   ├── GimbalModeControllerTest.cc
                                                            # ① 实际模式与命令闭环测试：覆盖 A8 SDK/标准 MAVLink 路由、Follow/Locked/Fpv/Unknown、采样过期、显式目标、ACK 和实测回读，以及端点/车辆变化。
                                                            # ② 异步隔离：使用 ModeStubs 注入查询反馈、控制权、发送失败/重复拒绝和超时，确认 sessionRevision 拒绝旧操作、未变反馈不算成功；检查生产 ModeController 的行为，不依赖真实设备。
│   │   ├── GimbalModeUiTest.py
                                                            # ① 顶部模式 QML 检查：通过 PySide6 提取实际 GimbalIndicator 的绑定、回调及控制权辅助函数，构建最小测试场景，验证模式文字、按钮等待、点击时目标和接管后执行。
                                                            # ② 运行范围：模拟服务、控件和定时事件，检查会话变化/取消不会执行旧目标；此 Python 脚本需单独运行，不属于 CTest 自动目标，也不替代完整应用或实机控制权验收。
│   │   ├── GimbalPhotoCapturePolicyTest.cc
                                                            # ① 照片尺寸测试：覆盖协商源尺寸优先级、PIP 尺寸独立性、长短边/总像素限制、多种输出分辨率及设备像素比，验证抓图几何是否合法。
                                                            # ② 图像断言：调用 captureGeometry/prepareImageForSaving，检查小数 DPR 修正、完整画面等比缩放、精确输出和补黑边；使用测试图像直接验证算法，不需要连接视频源。
│   │   ├── ModeStubs/
                                                            # 模式控制器测试依赖替身；模拟类集中在 TestDoubles.h
│   │   │   ├── Fact.h
                                                            # ① 测试头入口：模拟 rawValue 读写和 rawValueChanged，使云台状态变化能沿生产绑定通知控制器。
                                                            # ② 包含同目录 TestDoubles.h 中的集中模拟类；GimbalModeControllerTest 通过测试 include 路径解析到本头，仅满足模式控制器所需接口，不替换产品构建中的原生实现。
│   │   │   ├── Gimbal.h
                                                            # ① 测试头入口：模拟设备/组件标识、yawLock、控制权和角速度设置，供实际模式同步及命令验证。
                                                            # ② 包含同目录 TestDoubles.h 中的集中模拟类；GimbalModeControllerTest 通过测试 include 路径解析到本头，仅满足模式控制器所需接口，不替换产品构建中的原生实现。
│   │   │   ├── GimbalController.h
                                                            # ① 测试头入口：模拟活动云台、云台列表和 sendRate 调用，检查路由选择及实际命令发送。
                                                            # ② 包含同目录 TestDoubles.h 中的集中模拟类；GimbalModeControllerTest 通过测试 include 路径解析到本头，仅满足模式控制器所需接口，不替换产品构建中的原生实现。
│   │   │   ├── GimbalControlManager.h
                                                            # ① 测试头入口：模拟 A8 模式查询、取消、显式写入和结果信号，代替真实 SiyiSdk 网络服务。
                                                            # ② 包含同目录 TestDoubles.h 中的集中模拟类；GimbalModeControllerTest 通过测试 include 路径解析到本头，仅满足模式控制器所需接口，不替换产品构建中的原生实现。
│   │   │   ├── MAVLinkLib.h
                                                            # ① 测试协议头适配：直接包含 <common/mavlink.h>，为独立测试提供真实 MAVLink 消息、字段和编码函数，而不引入整个 QGC MAVLink 包装依赖。
                                                            # ② 编译关联：在对应测试目标的优先 include 路径下满足生产源码对 MAVLinkLib.h 的引用；数据包仍按实际协议构造，车辆/链路等运行环境由同目录 TestDoubles.h 模拟。
│   │   │   ├── MultiVehicleManager.h
                                                            # ① 测试头入口：模拟活动车辆、车辆列表数量和切换通知，检查单车辆 A8 路由条件。
                                                            # ② 包含同目录 TestDoubles.h 中的集中模拟类；GimbalModeControllerTest 通过测试 include 路径解析到本头，仅满足模式控制器所需接口，不替换产品构建中的原生实现。
│   │   │   ├── QGCLoggingCategory.h
                                                            # ① 测试日志适配：包含 Qt QLoggingCategory，并把生产 QGC_LOGGING_CATEGORY 宏映射为 Q_LOGGING_CATEGORY，满足独立编译 GimbalModeController 的日志声明。
                                                            # ② 作用范围：仅由模式测试的 include 路径选用，保留日志分类名称而免于链接完整 QGC 日志管理；不修改产品日志开关、输出路径或运行时业务状态。
│   │   │   ├── QmlObjectListModel.h
                                                            # ① 测试头入口：提供列表 count 及变化信号，用于模拟车辆/云台数量而不引入完整模型系统。
                                                            # ② 包含同目录 TestDoubles.h 中的集中模拟类；GimbalModeControllerTest 通过测试 include 路径解析到本头，仅满足模式控制器所需接口，不替换产品构建中的原生实现。
│   │   │   ├── TestDoubles.h
                                                            # ① 模式控制器依赖模拟集合：集中定义本目录各入口头对应的 QObject/Fact/车辆/云台等替身，实现测试可设置的属性、通知信号和调用记录。
                                                            # ② 由 GimbalModeControllerTest 创建并操纵，驱动生产类进入正常、切换、失败和迟到结果等场景；新增生产依赖时优先补充最小所需接口，保持与其他 Stubs 套件的状态和编译目标隔离。
│   │   │   ├── Vehicle.h
                                                            # ① 测试头入口：模拟车辆标识、命令 pending、查询/发送次数和 ACK，检查模式命令确认及失败分支。
                                                            # ② 包含同目录 TestDoubles.h 中的集中模拟类；GimbalModeControllerTest 通过测试 include 路径解析到本头，仅满足模式控制器所需接口，不替换产品构建中的原生实现。
│   │   │   └── VehicleLinkManager.h
                                                            # ① 测试头入口：模拟通信丢失与 allLinksRemoved 通知，使断线和切换连接可取消旧模式请求。
                                                            # ② 包含同目录 TestDoubles.h 中的集中模拟类；GimbalModeControllerTest 通过测试 include 路径解析到本头，仅满足模式控制器所需接口，不替换产品构建中的原生实现。
│   │   ├── Mt11ProtocolTest.cc
                                                            # ① MT11 协议测试：核对连续/绝对变倍、相机编码参数和三种视频模式的帧字节、CRC、严格解码、多帧完整性，以及倍率/功能反馈的不同载荷格式。
                                                            # ② 策略联测：验证 Mt11ZoomPolicy 的显示档位、30 倍绝对命令终点及实测值边界；测试不依赖 MT11 网络设备，修改命令或倍率规则后可先执行该桌面目标。
│   │   ├── SiyiModeQueryTest.cc
                                                            # ① A8 模式查询通信测试：使用本地 UDP 端点核对明确模式命令的线上字节、当前查询端口和配置来源匹配，并检查普通相机请求报文保持有效。
                                                            # ② 请求隔离：模拟取消、重连、查询过期与零序号回复，确认旧 socket/旧请求反馈不会被接受或干扰普通轮询；测试生产 SiyiSdk 和 Protocol 的模式通道，不要求连接实际相机。
│   │   └── SiyiProtocolTest.cc
                                                            # ① A8 协议/能力测试：检查连续与绝对变倍帧、相机编码参数、严格帧/CRC、多帧报文、ACK 和倍率载荷，覆盖非法字段及不同卡录/拉流分辨率能力。
                                                            # ② 档位联测：验证 A8MiniZoomPolicy/ZoomStepPolicy 的上限、短按序列、长按推进、方向到位、精确端点和 TargetTracker 反馈确认；为 Manager 上层状态提供底层规则保证，不直接驱动设备。
│   ├── UI/
                                                            # 实际 QML 的布局与交互验证
│   │   └── FlyViewSettingsLayout/
                                                            # 离屏布局检查脚本和必要的 QGC 类型替身
│   │       ├── ParameterEditorDialog.qml
                                                            # ① 参数编辑对话框替身：提供 title、fact 和空 open 方法，使实际 Fact 控件在离屏环境可解析其参数编辑入口。
                                                            # ② 测试边界：由 run.py 注册给测试 QML 引擎，不弹出真正参数窗口、不向飞控写入；检查重点是设置页加载/布局，原生参数编辑完整行为不由这个替身验证。
│   │       ├── QGCButton.qml
                                                            # ① 测试按钮外壳：继承 Qt Quick Controls Button，按测试 ScreenTools 设置字体和字号，保留点击、启用状态及隐式尺寸等标准接口。
                                                            # ② 布局关联：供实际设置页面/文件选择按钮在离屏引擎中使用，随字号缩放参与宽窄屏检查；它不复制产品按钮的全部主题实现或接入真实应用操作。
│   │       ├── QGCFileDialog.qml
                                                            # ① 文件选择替身：提供 nameFilters、title、folder、acceptedForLoad 信号和空 openForLoad 方法，满足 OSM/外部模型等设置页的文件对话框接口。
                                                            # ② 测试使用：run.py 可加载和驱动选择回调而无需打开系统窗口；此文件不读取目录、不实现 Android 文件权限，也不执行真实模型导入，仅隔离布局检查的外部依赖。
│   │       ├── QGCFileDialogController.qml
                                                            # ① 文件列表服务替身：getFiles 返回预设的较长飞行动作文件名，使实际设置页能形成下拉内容并测试窄屏下的文字宽度/省略。
                                                            # ② 数据边界：不扫描磁盘或加载真实 MAVLink 动作文件；由 run.py 注册为所需 QML 服务类型，实际产品目录访问仍使用原生 QGCFileDialogController。
│   │       ├── QGCPalette.qml
                                                            # ① 测试主题颜色对象：模拟 Light/Dark、globalTheme、colorGroupEnabled 及窗口、边框、文字和按钮颜色，颜色值对照原生 QGCPalette。
                                                            # ② 驱动方式：读取 layoutTestStyle.light 切换明暗主题，供页面、分组及 16 路通道格截图检查；是测试引擎的服务替身，不覆盖应用实际调色板。
│   │       ├── QGCTextField.qml
                                                            # ① 输入框布局替身：基于 TextField 提供 unitsLabel/showUnits、帮助/校验接口、字号、背景边框及单位标签，形成与实际设置行交互兼容的输入外壳。
                                                            # ② 验证分工：生产 FactTextField 仍加载在此基础上，测试其 Fact 写回及宽度适配；替身的校验提示方法为空，不能把测试结果扩展为原生输入框所有错误提示行为已验证。
│   │       ├── README.md
                                                            # ① 布局验证使用说明：列出测试范围、PySide6/Qt 6 rcc 依赖、资源编译和 run.py 的 --resource/--output 命令，说明截图输出及准备目录。
                                                            # ② 边界说明：区分实际生产 QML 与服务/控件替身，解释离屏软件渲染、预览中文翻译和 Android 分组在测试中的开启方式，避免把布局通过当成完整 QGC/Android 功能验收。
│   │       ├── run.py
                                                            # ① 离屏布局入口：加载实际 custom.qrc 编译资源和生产 Fact/设置组件，注册应用服务/Fact/主题替身，检查 320/480/800/1200/1920 宽度、150% 字号及明暗主题并输出截图。
                                                            # ② 交互断言：核对居中与最大宽度、窄屏收缩、16 通道格不重叠、开关/输入/下拉的 Fact 写回及三维源切换；依赖 PySide6 和 Qt 6 rcc 资源，单独运行，不打开真实应用或连接设备。
│   │       └── ScreenTools.qml
                                                            # ① 测试屏幕/字号单例：提供默认字符宽高、字号、字体、像素密度与控件边距，默认使用 Microsoft YaHei/Consolas，供实际设置组件计算尺寸。
                                                            # ② 缩放驱动：以 layoutTestStyle.scale 调整字符度量，支持 100%/150% 字号场景；测试视口宽度由 run.py 控制，不读取真实 Android 屏幕或触摸设备。
│   └── VideoManager/
                                                            # 视频策略测试
│       └── VideoReceiver/
                                                            # 接收器相关策略测试
│           └── GStreamer/
                                                            # 解码路由和 A8 恢复策略测试
│               ├── A8RtspRecoveryPolicyTest.cc
                                                            # ① A8 健康策略测试：构造 URI、RTCP Sender Report、NTP/RTP 时间和进度，验证主机隔离、畸形报文、时钟跳变证据及媒体停滞阈值。
                                                            # ② 恢复边界：检查连续出帧不重启、时钟报文单独不足以触发、每代一次/跨重连预算、URI 切换和前台/录像门控；直接使用纯策略，测试不启动实际 GStreamer RTSP 会话。
│               └── AndroidH265DecoderRoutePolicyTest.cc
                                                            # ① 硬解路线测试：覆盖备用适配器优先、去重排序、每个候选只选一次、空列表/耗尽及旧索引修复，验证不同接收器的候选状态不混用。
                                                            # ② 格式契约联测：检查 MT11 主机选 byte-stream、其他 URI 保留既有路径，以及 H.265 caps 允许未知帧率；仅验证 Route/Caps/StreamFormat 纯规则，不等同 Android MediaCodec 实机成功解码。
├── tools/
                                                            # 实机采集等开发工具
│   └── a8-video-capture.sh
                                                            # ① Ubuntu/ADB 实机采集工具：start 以诊断日志参数重启 QGC 并在设备上持续采集，支持拔掉 USB 后使用双路视频，再重连执行 finish 拉取日志和归档。
                                                            # ② 使用条件：检查设备约 1 GiB 可用空间，采集设置约 15 min/512 MiB 上限，可用 ANDROID_SERIAL 选设备、A8_CAPTURE_OUTPUT_DIR 指定输出；仅供开发取证，不参与 APK 运行功能，完整步骤见 4.3。
└── translations/
                                                            # 源文本模板、中文翻译与提取工具
    ├── custom_zh_CN.ts
                                                            # ① 简体中文翻译目录：按 context/source 保存 custom 用户可见字符串的译文和完成状态，涉及相机、设置、三维及提示等定制界面。
                                                            # ② 构建链路：custom-lupdate.sh 从源码更新条目，开发者复核 unfinished/上下文变化，CMake 编译为 custom_zh_CN.qm 并打包到 :/i18n，CustomPlugin 按语言加载；不直接编辑生成的 QM。
    ├── custom-lupdate.sh
                                                            # ① 翻译提取脚本：查找 PATH 中的 lupdate 或使用 LUPDATE 指定工具，对 custom/src 执行提取，更新 custom.ts 源模板和已有 custom_*.ts 语言目录，并去除过时条目。
                                                            # ② 维护方式：新增/移动/修改 QML 的 qsTr 或 C++ 可翻译文本后运行，再审阅语言文件中的 source/context 与 unfinished；脚本更新目录，不自动完成中文翻译或编译应用资源。
    ├── custom.ts
                                                            # ① 翻译源模板：记录 custom 源码中被提取的 context、source 和位置，便于追踪新增、移动或删除的可翻译文本。
                                                            # ② 使用关系：由 custom-lupdate.sh 更新，供语言目录维护参考；模板本身不编入应用，运行时加载的是 custom_zh_CN.ts 等语言文件经 CMake 生成的 QM。
    └── README.md
                                                            # ① 翻译维护文档：说明源模板、各语言 TS、生成 QM 的角色，给出提取命令和 LUPDATE 工具路径覆盖方式。
                                                            # ② 维护流程：指导用 Qt Linguist 复核新字符串、文件移动后的上下文及 unfinished，再由 CMake 编译打包、CustomPlugin 按当前语言加载；生成 QM 属于构建产物，不作为源文件提交。
~~~

**原生复用边界**：树中只列 `custom/` 内实际存在的文件。例如，相机栏内部外观在 `FlightDisplay/GimbalCameraControl.qml`，整列的页面锚点在原生 [FlyViewWidgetLayer.qml](src/FlightDisplay/FlyViewWidgetLayer.qml)；Viewer3D 的公共头文件、部件 QML 和 shader 也有原生复用。对应关系见第 3 节各模块及 2.4。

**维护方式**：新增、移动或删除文件时同步文件树；文件名与职责分别占行，职责行的 `#` 前保留 60 个空格，①、②各写成一条完整源码行，由 VS Code 自动换行。功能流程变化时更新第 3 节所属模块的实现步骤和文件/资源协作关系。

### 2.2 构建与运行入口

| 文件 | 主要职责 |
|---|---|
| `custom/cmake/CustomOverrides.cmake` | 固定应用名；关闭原生 Viewer3D 构建和 APM；关闭原生 PX4 Factory，由 custom 接管 |
| `custom/CMakeLists.txt` | 收集 custom C++；复用原生 Viewer3D 公共实现；接入 Bluetooth、Quick3D、AssetUtils 和可选 WebEngineQuick；生成 Android 模板副本 |
| `custom/custom.qrc` | 定义资源路径与 alias；打包同路径 QML 覆盖、Fact JSON、图标和模型 |
| `CustomPlugin.h/.cc` | 创建并向 QML 暴露管理器；安装默认值与翻译；连接视频接收器、MAVLink 消息及退出清理 |

从构建到运行，按以下入口追踪：

| 顺序 | 入口 | 具体执行内容 |
|:---|:---|:---|
| 1. 配置工程 | 根 CMake → CustomOverrides → custom/CMakeLists | 启用 CustomPlugin，收集 custom 与复用的原生源文件；QML 模块和 QRC 分别接入 |
| 2. 插件创建 | `CustomPlugin::instance()` / `customInstance()` | 返回应用级插件实例；构造时先设置 GStreamer/GIO 网络策略 |
| 3. 产品初始化 | `CustomPlugin::init()` | 安装空列表默认链路、加载翻译，依次创建 Settings 与业务对象，注册 Viewer3D 类型 |
| 4. 依赖创建 | `_ensureGimbalControlManager()`、`_ensureMt11ControlManager()` 等 | 先确保 Settings 存在，再以插件为 parent 创建业务对象；getter 也使用同一 ensure 路径 |
| 5. 跨模块接线 | `_ensureGimbalModeController()`、`_ensureDualVideoManager()` | 模式事务响应新的姿态动作取消；第二路 receiver/item 变化同步给 MT11 |
| 6. 视频策略 | `AndroidVideoDecoderPolicy::apply()`、`GimbalVideoStreamSupport::installA8MiniDefaults()` | 解码候选和默认视频设置在实际管线启动前就绪 |
| 7. 页面与视频对象 | `createQmlApplicationEngine()`、`createVideoSink()` | 安装 QML URL 拦截器；按 receiver 身份绑定相机、分辨率与恢复观察器 |
| 8. 持续数据 | `mavlinkMessage()`、各对象的 connect/timer | 姿态送 Provider，自动视频源按设置过滤；SDK/蓝牙/receiver 回调各自驱动状态 |
| 9. 退出 | `aboutToQuit` 直接连接、`cleanup()` | 取消姿态请求、关闭 UniRC，`_shutdownMt11Video()` 收尾第二路，A8 `shutdownLocalMedia(true)` 收尾主路，最后解除 interceptor 与翻译 |

业务对象由插件持有，单次命令和管线又有自己的会话标识。阅读异步实现时同时查看“对象何时创建/销毁”和“本次请求何时失效”，二者不是同一个生命周期。

文中的 **Manager/Controller** 负责运行状态和动作编排，**Policy** 负责可独立验证的规则计算，**Protocol/SDK** 分别负责报文编解码与设备收发，**Provider** 向界面提供整理后的数据。**Fact** 是可绑定的设置/参数值；**ACK** 是命令应答；**pending** 表示等待完成；**watchdog** 是检查数据或动作是否超时的定时器。

插件承担统一创建和生命周期管理，业务分散到相应 Manager。主要对象关系如下：

~~~mermaid
flowchart TB
    Plugin["CustomPlugin：初始化、接线与退出清理"]
    Settings["Settings / Fact：配置与持久化"]
    Scene["Viewer3D：地图、车辆、任务"]
    Video["VideoManager / DualVideoManager：两路视频"]
    Camera["GimbalControlManager / Mt11ControlManager：相机与媒体"]
    Posture["CenterCoordinator / ModeController：姿态与模式"]
    Rc["UniRcChannelController：遥控器通道"]
    Azimuth["GimbalAzimuthProvider：遥测方位角"]
    UI["Fly View / Settings / Toolbar"]

    Plugin --> Settings
    Plugin --> Scene
    Plugin --> Video
    Plugin --> Camera
    Plugin --> Posture
    Plugin --> Rc
    Plugin --> Azimuth
    Video -->|"画面、尺寸、录制状态"| Camera
    Rc -->|"CH9"| Camera
    Rc -->|"CH10"| Posture
    Scene --> UI
    Camera --> UI
    Posture --> UI
    Azimuth --> UI
~~~

QML 的统一访问前缀为 `QGroundControl.corePlugin`：

| 对象属性 | 对应职责 | 主要使用方 |
|:---|:---|:---|
| `viewer3DSettings`、`external3DMapManager` | 三维设置、模型选择/转换 | Viewer3D 设置页与场景 |
| `flyViewCustomSettings`、`gimbalControlSettings`、`videoCustomSettings` | 罗盘、相机、UniRC、视频配置 | 设置页及各 Manager |
| `dualVideoManager` | 第二路 receiver、尺寸和播放状态 | 第二路视频显示项、MT11 本地媒体 |
| `gimbalControlManager`、`mt11ControlManager` | 设备能力、相机命令和各自媒体会话 | 右侧相机栏 |
| `gimbalCenterCoordinator`、`gimbalModeController` | 共享姿态事务、实际模式读取和切换 | 顶部云台栏、UniRC |
| `gimbalAzimuthProvider` | 当前 MAVLink 云台的有效世界方位角 | 顶部云台栏、云台罗盘 |
| `uniRcChannelController` | 蓝牙和 SDK 通道状态、CH1～CH16 | UniRC 设置区及动作转发 |

### 2.3 QML 与 Android 的接入方式

- **原生页面覆盖**：资源拦截器检查原生 `qrc:/qml/...` 是否存在对应的 `qrc:/Custom/qml/...`。存在时加载 custom 版本，否则继续用原生文件。General、Fly View、Video、顶部云台栏、电池栏和飞行页均通过该机制接入。
- **独立 QML 模块**：`Custom.Widgets` 提供 Fuel/雷达详情及母线告警；`Custom.FlightDisplay` 提供 DualPipView 和第二路视频组件。
- **Viewer3D 复用**：custom 保留有差异的实现；`OsmParserThread`、地形纹理/瓦片查询、`Viewer3DUtils`、公共几何头文件、航点/航段组件、灯光相机、shader 和 F450 部件 QML 直接引用 `src/Viewer3D`。
- **Android overlay**：CMake 将根 `android/` 模板复制到构建目录，再叠加 `custom/android/`，Gradle 只编译合并后的唯一 Java 类。合并时排除缓存/本地配置，补齐蓝牙权限，并在生成副本中关闭 configuration cache。

### 2.4 原生代码接口与维护边界

产品地址、设备识别、相机策略和界面业务集中在 `custom`。当前依赖的原生定制接口如下；调整这些接口时，应同步检查 custom 调用方。

| 根目录文件 | 当前承担的接口或职责 |
|---|---|
| `src/CMakeLists.txt` | 关闭原生 PX4 Factory 后仍链接 PX4 AutoPilot QML 模块 |
| `src/Vehicle/VehicleSetup/VehicleSummary.qml` | 与关闭 APM 模块的构建保持一致 |
| `src/VideoManager/VideoManager.h/.cc` | 原生主/thermal receiver 的串行启动、停止、重启、会话取消和 RTSP 退避 |
| `src/VideoManager/VideoReceiver/VideoReceiver.h` | 启动 URI 快照、管线 generation、codec、source/decoder/sink 首帧与结构化错误信号 |
| `src/VideoManager/VideoReceiver/GStreamer/GstVideoReceiver.h/.cc` | RTSP 通用连接、OPTIONS 兼容、冻结 parser 格式/显式 decoder、解码输入门禁、录像分支与诊断 |
| `src/VideoManager/VideoReceiver/QtMultimedia/QtMultimediaReceiver.cc` | 与通用 receiver 保持启动通知接口一致 |
| `src/Utilities/QGCLogging.cc` | 按实际日志级别过滤消息 |
| `src/Camera/SimulatedCameraControl.cc` | 使用 `hasVideoChanged` 通知更新模拟相机的视频可用状态 |
| `translations/qgc_json_zh_CN.ts` | 原生参数枚举翻译与元数据项数、分隔符保持一致 |

这里的 **generation** 是一次管线或请求会话的标识。异步回调同时核对对象、URI 和 generation，确保旧连接的返回值不会驱动新会话。原生视频层提供通用机制；A8/MT11 地址分类、硬解候选和恢复决策由 custom 提供。

### 2.5 数据链路与控制对象

运行时需要分别理解下面四条链路。它们可以共享网络，但由不同对象建立、确认和释放。

| 链路 | 输入与输出 | 当前负责人 | 连接成立的观察点 |
|:---|:---|:---|:---|
| 飞控 MAVLink | 飞控遥测、参数、云台姿态命令 | 原生 LinkManager/Vehicle + custom 飞控与云台模块 | Vehicle 出现；参数与云台各自完成加载/发现 |
| 相机 UDP SDK | 倍率、拍照/录像、设备状态和模式 | `SiyiSdk`、`Mt11Sdk` | 收到对应设备的有效状态/应答 |
| RTSP 视频 | 压缩视频、解码帧、本地录像支路 | VideoManager、DualVideoManager、GstVideoReceiver | 对应路有 source、decoder 输出及 sink 首帧 |
| UniRC Bluetooth | 16 通道输入，转为 CH9/CH10 动作 | UniRcChannelController、通道 Policy | 合法 `0x42` 通道流，且动作通道值有效 |

三个界面选择的含义不同：

- **PIP 主视图选择**：决定地图/Video 1/Video 2 的显示位置。
- **右侧相机选择**：决定用户操作 A8 还是 MT11 的私有 SDK。
- **顶部活动云台**：由活动 Vehicle 的 GimbalController 决定，负责 MAVLink 姿态与方位角。

扩展时应沿对应链路取状态。例如，SDK 在线只能说明控制通道收到回应，视频是否可用要检查该路解码状态；点击相机选择器也不应改变 MAVLink 活动云台。

---

<a id="modules"></a>

## 3. 功能模块

各模块按“能力与入口 → 操作/配置 → 函数与状态流转 → 文件与资源协作”展开。末尾按功能环节重新组合跨目录文件，说明各文件在该功能中的输入、处理和衔接关系；单个文件的完整职责与实际层级见 2.1。

本节未带 `custom/` 或原生 `src/` 前缀的源码路径均相对 `custom/src/`；同组后续短文件名沿用已注明目录，`.h/.cc` 表示同名文件对。参数表给出本版默认值；代码中的具体条件优先于设备型号或界面选中状态。

**实现段落阅读约定**：箭头表示主要调用/信号方向；函数表说明入口、处理和输出，不逐行复述源码。修改某个功能时，先从公开动作或输入回调进入，再核对表中状态、完成条件和退出路径。

<a id="viewer3d"></a>

### 3.1 Viewer3D 三维飞行视图

#### 3.1.1 功能与工作模式

| 地图模式 | 当前能力 | 使用条件 |
|---|---|---|
| 本地 OSM | 建筑几何、地图瓦片地表、F450 飞行器、任务点和航段 | 选择 OSM 文件；在线瓦片取决于地图源及网络 |
| 外部模型 | OBJ/glTF/GLB 或 Quick3D QML，按 WGS84 原点、比例和朝向配准；叠加飞行器与任务 | 模型及材质/纹理完整，已知模型原点与单位 |
| Google 3D | 在独立 WebEngine 视图中显示在线三维地图，以车辆位置或当前地图位置为中心 | 构建包含 WebEngineQuick、有效 API Key 和网络 |

地图源优先级为 **Google → 外部模型 → OSM**。Google 视图独立于本地 Quick3D 场景，当前没有接入本地 F450/任务航线叠加；缺少 WebEngine 时显示不可用提示。

#### 3.1.2 使用方式

1. 打开“应用设置 → 飞行视图 → 3D View”，启用三维视图。
2. 选择地图模式并填写下表参数。导入 OBJ 时保留同目录的 MTL 和纹理；FBX/DAE/STL/PLY 由导入管理器调用 Qt Balsam 转成 Quick3D QML，运行环境需能找到 `balsam`。
3. 在飞行页工具条点击“3D View”，再次点击“Fly”返回飞行页。
4. 本地场景支持左键拖动平移、右键拖动旋转、滚轮缩放，以及触摸捏合缩放/移动手势旋转。

| 设置键（`Viewer3D` 分组） | 默认值 | 用途 |
|---|---|---|
| `enabled` | false | 显示三维入口 |
| `useGoogle3DMapSource`、`google3DMapsApiKey` | false、空 | Google 视图及密钥 |
| `useExternal3DMapSource`、`external3DMapFilePath` | false、未选择 | 外部模型开关与文件 |
| `external3DMapOriginLatitude/Longitude/Altitude` | 0、0、0 | WGS84 原点纬度、经度、AMSL 海拔（m） |
| `external3DMapUnitToMeters`、`external3DMapScale` | 0.01、1 | 米/模型单位及额外缩放；UE 厘米单位通常取 0.01 |
| `external3DMapYaw` | 0° | 模型朝向；0° 对应 +X 东、+Y 北 |
| `osmFilePath`、`buildingLevelHeight` | 未选择、3 m | OSM 文件与缺省建筑层高 |
| `altitudeBias` | 0 m | 飞行器、任务点和航段的共同显示高度偏移 |

#### 3.1.3 实现流程

**入口与场景生命周期**

[Viewer3D.qml](custom/src/Viewer3D/Viewer3DQml/Viewer3D.qml) 的 `open()` 启用管理器 Loader 并设置 `isOpen`；`_viewer3DSource()` 选择本地场景或 Google 页面，`_bindLoadedView()` 在两个 Loader 就绪后将管理器交给场景。普通 `close()` 只关闭视图；禁用三维设置会同时停用管理器 Loader。

`CustomViewer3DManager::registerQmlTypes()` 把 C++ 管理器注册为 `QGroundControl.Viewer3D` 中的 `Viewer3DManager`。管理器构造时创建 `Viewer3DQmlBackend`、`OsmParser` 并调用 backend 的 `init()`，因此 QML 使用的类型名与 C++ 类名不同。

**OSM → 建筑 mesh → 地表纹理**

| 步骤 | 源码入口 / 方法 | 实际处理与输出 |
|:---|:---|:---|
| 1. 文件变化 | `CityMapGeometry::setOsmFilePath()`、`loadOsmMap()` | 设置 Fact 改变后清空旧场景，将文件交给 parser；完成状态通过后续信号传播 |
| 2. 后台解析 | `OsmParser::parseOsmFile()` → 原生 `OsmParserThread` | 清空节点/建筑缓存与参考点，在线程中解析 OSM 节点、轮廓和属性 |
| 3. 发布结果 | `OsmParser::osmParserFinished()` | 有效解析完成后保存 GPS 参考点、地图边界和已加载状态，发出 `mapChanged` |
| 4. 建筑三角化 | `OsmParser::buildingToMesh()` | 优先用建筑 height，否则用 levels × 默认层高；无高度信息的建筑跳过。外轮廓/内孔交给 earcut，生成屋顶、底面与挤出的墙面 |
| 5. 提交顶点 | `CityMapGeometry::updateViewer()` | 把 XYZ 顶点字节数组交给 `QQuick3DGeometry`，设置三角形 primitive、位置属性和 stride，再更新场景 |
| 6. 地表数据 | 原生 `Viewer3DTerrainTexture`，在 `Viewer3DModel.qml` 中实例化 | 沿原生瓦片链下载并拼接纹理；`textureGeometryDone` 后将 tileCount 和 ROI 边界交给地形 |
| 7. 地形网格 | `Viewer3DTerrainGeometry::updateEarthData()` → `buildTerrain_2()` | 根据参考点和 ROI 生成顶点、法线与 UV；QML 材质引用拼接纹理及原生地表 shader |

当前地表调用的是 `buildTerrain_2()`。建筑和地表 Model 在 QML 中统一使用 10 倍场景缩放；新增几何时应沿用同一尺度。

**外部模型 → 文件选择/转换 → 场景变换**

1. 设置页调用 `External3DMapManager::importModelFile()`，先转本地路径并检查文件存在、扩展名是否支持。
2. OBJ/glTF/GLB/QML 直接写入 `external3DMapFilePath`；其他支持格式进入 `_startBalsamConversion()`，查找 Balsam 并启动 `QProcess`。同一时刻只允许一个转换任务。
3. `_completeBalsamConversion()` 检查进程结束状态及生成的 QML，再更新文件 Fact；`importingChanged`、`lastImportStatusChanged` 驱动进度和结果提示。转换失败时不把无效输出设成地图。
4. `External3DMap.qml` 按扩展名选择 RuntimeLoader 或 QML Loader，绑定单位、额外比例和 yaw；模型原点由 backend 提供的地理参考点配准，具体公式见 3.1.4。
5. `Viewer3DModel.qml` 的 `mapGeometryLoader` 在建筑组件与外部模型组件之间切换，车辆/任务使用另一组 Loader，因而两个本地地图模式共用叠加逻辑。

**坐标、飞行器与任务更新**

| 数据 | 具体连接与计算 |
|:---|:---|
| 地图参考点 | `Viewer3DQmlBackend::init()` 连接地图设置、parser 的 `gpsRefChanged` 和活动车辆变化；`_restoreBestGpsRef()` 优先使用有效外部模型原点，其次 OSM 参考点，再使用车辆坐标 |
| 参考点变化 | `_externalMapSettingsChanged()`、`_gpsRefChangedEvent()`、`_activeVehicleCoordinateChanged()` 更新相应来源；外部模式保持模型原点，不随飞机移动重设原点 |
| 车辆实例 | `Viewer3DModel.qml` 用 `Repeater3D` 遍历 vehicles，每辆车创建 `Viewer3DVehicleItems` 和绑定该车的 `PlanMasterController` |
| 地理到场景 | `GeoCoordinateType` 将 WGS84 转为局部 ENU；车辆 AMSL、模型原点海拔与 altitudeBias 在本地场景高度计算中合并 |
| 任务点筛选 | `isItemAcceptable()` 过滤支持的任务命令；`displayAltitudeForMissionItem()` 区分外部模型的 AMSL 高度和普通任务高度 |
| 点与线模型 | `addMissionItemsToListModel()` 构建航点列表，`addSegmentToMissionPathModel()` 生成相邻航段；任务项集合、GPS 参考点、Home 变化时重新构建 |
| 飞行器姿态 | `DroneModelDjiF450.qml` 绑定车辆姿态与飞行状态，驱动机体姿态和桨叶动画 |
| 场景操作 | `Viewer3DModel.qml` 的 `moveCamera()`、`rotateCamera()`、`zoomCamera()` 接收鼠标/触控处理器输入并更新相机 |

**Google 页面**

`Google3DMapView.qml::_buildGoogle3DHtml()` 生成包含 API Key、中心点和地图参数的 HTML，`reloadGoogle3DMap()` 用 WebEngine 的 `loadHtml()` 加载。150 ms 单次定时器合并重载；签名相同不重载，坐标连续更新只在尚未初始化时触发加载。重新打开视图或切换活动车辆会重新评估中心点，因此当前实现不是每帧跟随车辆的三维航迹视图。

#### 3.1.4 外部模型配准示例

随仓库的城镇 OBJ 样例采用米制 ENU 坐标。选择 `realistic_town_wgs84_map.obj` 后，按下列参数设置；JSON 文件是样例参数说明，当前导入接口接收模型路径，坐标参数需在设置页填写。

| 参数 | 样例值 | 核对方法 |
|:---|:---|:---|
| 原点纬度 / 经度 | 37.4456 / -122.1616 | 模型所在位置与二维地图一致 |
| 原点海拔 | 9.0 m AMSL | 使用绝对海拔，不填相对起飞高度 |
| 米/模型单位、额外比例 | 1.0、1.0 | 样例已经是米制，无需按厘米缩小 |
| 北向 yaw、显示高度偏移 | 0°、0 m | 模型 +Y 对北；先以无偏移验证高度 |

外部模型使用的场景缩放为：

~~~text
模型场景缩放 = 米/模型单位 × 额外比例 × 10
车辆显示高度 = (车辆 AMSL - 模型原点 AMSL + altitudeBias) × 10
任务点/航段显示高度 = (任务 amslEntryAlt - 模型原点 AMSL + altitudeBias) × 10
~~~

例如车辆 AMSL 为 39 m、模型原点为 9 m、显示偏移为 0 时，车辆位于模型原点上方 30 m，对应 300 个场景单位。`altitudeBias` 同时作用于飞机、任务点与航段：`Viewer3DVehicleItems.qml` 把它交给 Drone/Waypoint 组件，并用于 Line3D 端点。车辆/任务缺少可用 AMSL 时使用代码中的相对/原任务高度后备值；配准验收应确认实际使用的高度来源。

Balsam 转换由独立 QProcess 执行，同一时刻只运行一个导入。工具查找顺序为 `QGC_VIEWER3D_BALSAM` 环境变量、应用目录、Qt Kit 的 bin 目录、PATH；输出保存到应用数据目录下的 `Viewer3DExternalMaps/<名称>_<路径哈希>/`。转换成功且找到 QML 后才更新模型文件设置；文件缺失、工具不可用、转换失败分别更新 `lastImportStatus`。

本地任务场景当前筛选 Waypoint、RTL、Takeoff、ROI（含兼容命令）并显示对应标记；任务中的其他命令不会自动生成三维图元。

#### 3.1.5 功能对应的文件与资源协作

本模块从飞行页入口进入三维窗口，再按地图模式组合不同后端和资源。

| 功能环节 | 文件 / 资源组 | 在本功能中的协作关系 |
|:---|:---|:---|
| 打开与关闭三维窗口 | `FlightDisplay/FlyView.qml`、`FlyViewToolStripActionList.qml`；`Viewer3D/Viewer3DQml/Viewer3D.qml`；`Viewer3D/Images/city_3d_map_icon.svg` | 工具条用图标提供 3D View/Fly 入口，FlyView 承载窗口；Viewer3D 负责 open/close 和本地/Google 页面选择。 |
| 设置与导入入口 | `UI/AppSettings/Viewer3DSettingsGroup.qml`；`Viewer3D/Viewer3DSettings.h/.cc`、`Viewer3D.SettingsGroup.json` | 设置页编辑地图源、文件和配准参数；Settings/JSON 提供 Fact、默认值与持久化，变化驱动后端和场景。 |
| 后端与地理基准 | `Viewer3D/CustomViewer3DManager.h/.cc`、`Viewer3DQmlBackend.h/.cc` | Manager 创建 parser/backend 并注册类型；backend 在外部原点、OSM 参考点和 Vehicle 坐标间选择 GPS 基准，供地图与飞行器共用。 |
| OSM 建筑与地表 | `Viewer3D/OsmParser.cc`、`CityMapGeometry.cc`、`Viewer3DTerrainGeometry.cc`；原生 `src/Viewer3D/` 的解析线程、瓦片与 shader 资源 | 原生线程读取 OSM；custom 生成建筑和地表几何，场景接入瓦片纹理和材质；公共头文件及部分实现由原生目录提供。 |
| 外部模型导入与放置 | `Viewer3D/External3DMapManager.h/.cc`；`Viewer3D/Viewer3DQml/Models3D/External3DMap.qml` | Manager 校验文件并按格式直接使用或调用 Balsam；QML 选择 RuntimeLoader/QML Loader，把单位、比例和 yaw 应用于加载结果。 |
| 本地场景与任务叠加 | `Viewer3D/Viewer3DQml/Models3D/Viewer3DModel.qml`、`Viewer3DVehicleItems.qml`；原生 CameraLightModel、Waypoint3DModel、Line3D | 场景总装地图、材质、相机和操作；每车组件筛选任务点、计算高度并连接航段，复用原生航点/线段图形。 |
| 飞行器模型资源 | `Viewer3D/Viewer3DQml/Drones/DroneModelDjiF450.qml`、`Djif450/*/node.mesh`；原生 F450 部件 QML | 总装读取 Vehicle 位置、姿态及高度；部件 QML 引用对应 mesh，组成四机臂、四电机、机架和螺旋桨并参与动画。 |
| Google 3D 模式 | `Viewer3D/Viewer3DQml/Google3DMapView.qml`、`Google3DMapUnavailable.qml` | 有 WebEngine 时生成并加载地图 HTML，传入 API Key 和中心坐标；未编入该能力时加载提示页。 |
| 类型和资源接入 | `QmlControls/Viewer3D/Models3D/qmldir`；`custom/custom.qrc`、`custom/CMakeLists.txt` | qmldir 声明模型类型；QRC 组合 custom 与原生资源路径；CMake 接入 Quick3D、相关源码和可选 WebEngine。 |
| 手动导入/配准验证 | `Viewer3D/ExternalWGS84_UE5_MapSample/` 下 OBJ、MTL、FBX、`textures/`、两份 JSON 与 README | OBJ/FBX 验证加载及转换，MTL/贴图提供外观；import settings JSON 给出人工填写的配准值，OSM JSON 保存来源，README 说明操作。此组不进入 APK/QRC。 |

样例参数与操作见 [样例说明](custom/src/Viewer3D/ExternalWGS84_UE5_MapSample/README.md)，配准计算见 3.1.4。

---

<a id="video"></a>

### 3.2 双视频、PIP 与 Android 解码

#### 3.2.1 功能与使用方式

视频层提供通用的 Video 1 和 Video 2，各有独立 receiver（视频接收器）、解码器和显示项。地图、Video 1、Video 2 中一个作为主视图，其余作为左下角 PIP 小窗；支持交换主视图、缩放/收起小窗，并保留桌面弹窗能力。

在“应用设置 → Video”选择 RTSP 视频源并启用视频：

| 视频设置 | 默认值/行为 |
|---|---|
| 原生 `Video/rtspUrl`（Video 1） | 安装默认值时补入 `rtsp://192.168.144.25:8554/main.264` |
| `Video/secondaryRtspUrl`（Video 2） | `rtsp://192.168.144.24:8554/video1`；留空禁用第二路 |
| 原生 `rtspTimeout` | A8 默认地址初始化时至少为 20 s |
| 原生 `lowLatencyMode` | Android A8 默认拓扑中，未保存过该项时设为 true |
| `GimbalControl/mavlinkAutoVideoStream` | false；保持手工视频源可编辑，修改后重启 |
| `GimbalControl/forceAndroidH265HardwareDecoder` | true；Android H.264/H.265 厂商硬解要求，修改后重启 |

第二路启用要求原生视频源为 RTSP、视频已开启、URL 2 非空且不与主路占用的源重复。两路不依赖相机 SDK 开关；SDK IP 与 RTSP URL 分开配置。

**当前产品绑定**：相机及本地媒体仍按 **Video 1 → A8 Mini、Video 2 → MT11** 绑定。PIP 主次切换只改变布局；手动交换 URL 不会自动交换媒体管理器。更改 URL 前先结束本地录像并等待收尾。

#### 3.2.2 视频启动与恢复流程

**两路所有者与显示接线**

主视频由原生 `VideoManager` 持有；第二路由 [DualVideoManager](custom/src/VideoManager/DualVideoManager.cc) 独立持有。QML 的第二路内容项经 `initVideoItem(window, videoItem)` 交给 Manager，界面尺寸变化与 receiver 生命周期分开处理。

| 阶段 | 入口 / 方法 | 实现方式 |
|:---|:---|:---|
| 读取配置 | `DualVideoManager::_refreshSettings()` | 监听 secondaryRtspUrl、原生 videoSource/rtspUrl/streamEnabled；计算 enabled、URI 和 duplicateSource |
| 建立接收器 | `_ensureReceiver()` | 通过 corePlugin 创建 receiver/sink，连接完成、状态、尺寸、首帧、错误与销毁信号 |
| 等待渲染就绪 | `_scheduleRenderInitialization()` → `_finishRenderInitialization()` | 将初始化安排到窗口渲染生命周期，完成后再回到 Manager 更新可启动状态 |
| 串行启停 | `_applyDesiredState()` | 统一比较当前与期望状态；需要停止时调用 `_requestStop()`，停止未完成不启动新会话 |
| 冻结会话 | receiver 的 `videoPipelineGenerationStarted` | 保存本代 URI/generation，清空旧首帧事实；后续回调通过 `_matchesVideoPipelineGeneration()` 核对 |
| 完成启动 | `onStartComplete`、`onStartDecodingComplete` | 区分接收启动与解码启动；状态正确后启动首帧 watchdog |
| 故障重连 | `_scheduleRestart()` → restartTimer → `_applyDesiredState()` | 按当前失败次数退避，计时结束再次核对设置及对象；不会复活已取消的旧 URL |
| 释放资源 | `cleanup()`、`_releaseReceiver()` | 停止定时器与接收器，通知媒体模块收尾，解除窗口/sink/receiver 关系后释放 |

`setPrimaryVideoReceiver()` 还订阅主路的 `onStartAttempt`、`onStartComplete`、`onStopComplete` 和销毁通知。主路“配置中、启动中、活动中、释放中”的 URI 均参与第二路重复源判断；`_recordPrimaryActiveUri()` 与 `_schedulePrimaryActiveUriClear()` 负责交接期间的占用信息。

**插件把视频对象交给相机模块**

`CustomPlugin::createVideoSink()` 通过 receiver 的父对象判断是否属于 DualVideoManager，通过 thermal 身份排除热成像原生支路：

- 主路：调用 A8 Manager 的 `setMainVideoItem()`、`setMainVideoReceiver()`；主路录像启动结果进入 `handleMainVideoRecordingStartResult()`。
- 第二路：调用 MT11 Manager 的 `setVideoItem()`、`setVideoReceiver()`；DualVideoManager 对象变更信号持续同步这组绑定。
- 非 thermal sink：安装 `PulledVideoResolutionProbe::install()`，从协商 CAPS 获取源尺寸，通过 `setNegotiatedPulledVideoResolution()` 送到对应相机 Manager。
- `videoObjectsAboutToBeReleased` 在释放前同步执行 MT11 `shutdownLocalMedia(true)` 并解绑；`videoObjectsReleased` 后调用 `finalizeDetachedLocalMedia()`，避免媒体回调继续访问已销毁显示对象。

**三视图 PIP 如何切换**

`FlyView.qml` 将地图、Video 1、Video 2 传入 `DualPipView.item1/item2/item3`。`_itemKey()` 使用稳定身份 map/video1/video2；`_initializeLayout()` 读取 `MainFlyWindowView`，`_reconcileLayout()` 在可用项变化时保留有效主视图和辅槽。

点击辅窗口调用 `_activateSlot()`，先把该项置为 fullState，再把原主视图放入被点击的同一槽；`_applyLayout()` 统一更新各项 pipState。三个 adapter 将内容绑定到下槽、上槽或独立窗口；`_showWindow()` 与窗口关闭回调管理弹出/回收。`_setPipIsExpanded()` 持久化 `IsPIPVisible`。这组操作调整显示状态和位置，不改写相机 SDK 对象或视频 URL。

**双辅窗如何连续缩放**

上下辅窗共用 `_pipSize`，拖动任一右上角手柄时同步改变宽度，单窗保持 16:9。`pipResize` 在按下和移动时通过 `mapToItem(root.parent, mouse.x, mouse.y)` 将指针映射到同一父容器，按“按下时宽度 + 累计水平位移”更新尺寸。图标会随右边缘移动，不能直接将其内部 `mouse.x` 作为固定基准；解除内部 MouseArea 的锚点也无法固定图标的坐标原点。

手柄始终锚定图标，按住时保持可见，移出辅窗仍继续接收拖动；释放或取消后直接恢复普通悬停显示，无需重新绑定锚点。目标宽度钳制在父容器宽度的 10%～75%，越界时停在边界，指针返回有效区间后继续跟随。缩放直接更新现有容器和内容项，不增加缓动动画、延迟定时器或视频管线重建。

`custom/test/FlightDisplay/DualPipResizeTest.py` 已在 Qt/PySide6 6.10.2 离屏环境通过鼠标事件回归；验证几何与交互，不代表完整 QGC、目标 Qt Kit 或 Android 双路实播性能验收。

**主视频默认值与自动流**

`GimbalVideoStreamSupport::installA8MiniDefaults()` 按版本标记安装主视频默认值，只处理空值、受支持的已知默认形式和缺省设置，保留其他用户 URL。A8 默认地址的 RTSP timeout 至少为 20 s；Android 未保存 lowLatencyMode 时启用低延迟。设置生效后仍由 VideoManager 启动实际管线。

`CustomPlugin::mavlinkMessage()` 调用 `shouldFilterMavlinkMessage()`：关闭 mavlinkAutoVideoStream 时仅拦截 `VIDEO_STREAM_INFORMATION`，避免相机自动 URI 接管手动 URL；其他消息继续按原生处理。

**原生管线与四类运行证据**

压缩数据沿 `RTSP → RTP/depay/parser → decoder → sink` 流转。CAPS 是各段协商的编码、尺寸和码流格式；收到 CAPS 只表示格式已知。

| receiver 信号 | 更新的事实 | 负责判断的模块 |
|:---|:---|:---|
| `sourceFrameReceived` | 已收到压缩媒体 | 两路各自的启动/恢复观察者 |
| `videoDecoderSelected` | 实际选中的插件、factory 和 codec | 厂商硬解与候选判断 |
| `decoderFrameReceived` | 解码器已输出帧 | 解码链是否有进展 |
| `sinkFrameReceived` | 显示链取得首帧 | 停止首帧等待、恢复播放成功状态 |
| `onVideoPipelineError` | 错误分支及同代输入/输出事实 | 区分网络错误、解码失败和其他错误 |

主路由 `AndroidVideoDecoderRecovery` 连接这些信号；第二路在 DualVideoManager 内实现对应观察与恢复。因此更改首帧/错误接口时需要同时检查两个调用方。

**RTSP 连接规则**

RTSP 使用 GStreamer 原生 Auto 传输协商，GIO 默认直连；确需代理时通过启动环境 `QGC_GST_USE_SYSTEM_PROXY=1` 启用。同 URI 的 OPTIONS EOF 进入基本头部、跳过 OPTIONS 的有限兼容流程。失败重连按 1/2/4/8/15 s 退避；解码/显示成功复位失败计数。每条路维护自己的启动、停止和重试状态。

#### 3.2.3 Android 硬解工作模式

- 开关开启时，仅使用符合条件的厂商 MediaCodec。H.265 优先经过 `qgcandroidh265hwdec` 适配器，失败按接收器、URI 和输入格式执行有界的硬解候选切换；运行中不改变全局候选 rank，也不自动转软件解码。
- A8 首代保留 `hvc1/AU → adapter → byte-stream/AU → MediaCodec`。默认 MT11 主机匹配后首代采用原生 `byte-stream/AU`。其他 H.265 源只有进入解码失败恢复条件后，才尝试切换输入格式。
- byte-stream 管线重发参数集，并在解码输入端等待同一访问单元中的有效 SPS/PPS/IRAP。adapter 统一 decoder 输入 CAPS，帧率允许范围值，不固定伪造为 25 fps。
- 首帧超时、已确认的 decoder 分支错误或解码启动失败才能推进对应恢复；普通网络错误和已健康播放的另一条流不参与候选切换。
- 关闭硬解开关后恢复原生自动选择，供诊断对照使用。

**硬解策略如何接入**

1. `AndroidVideoDecoderPolicy::apply()` 在 GStreamer 初始化后、decodebin 创建前建立厂商候选，并注册 H.265 适配器。
2. `AndroidH265StreamFormatPolicy::parserOutputFormatForUri()` 根据 RTSP 主机与 MT11 SDK 主机匹配结果选择格式；插件在创建 sink 前设置 receiver 属性。GstVideoReceiver 在启动新 generation 时冻结该属性。
3. `AndroidH265HardwareDecoderAdapter` 负责规范化码流并连接实际 MediaCodec；`AndroidH265DecoderCapsPolicy` 提供 decoder 输入的 byte-stream/AU CAPS；码流格式选择和厂商候选选择是两个独立步骤。
4. 主路 `_handlePipelineError()` / `_restartAfterDecoderFailure()`，或第二路 `_handleDecodeStartupTimeout()`，将本代事实交给 `AndroidH265DecoderFallback::prepareHardwareRetry()`。
5. Fallback 使用 `AndroidH265DecoderRoutePolicy::orderedRetryFactories()` 去重排序，再由 `nextRoute()` 推进；URI 改变由 `install()` 安装的观察逻辑清理，输入格式改变由 `resetForCurrentInputFormat()` 重建该格式下的尝试状态。
6. 路由变化只作用于失败 receiver 的后续管线；候选用完回到首选适配器作为稳定路由，仍保持硬解约束。

#### 3.2.4 A8 播放后停滞恢复

Android 上已出画面的 A8 RTSP 会话额外监测 RTP、解析后媒体和 RTCP 时钟报告。当前台、未录像且媒体停滞达到 6 s 时，可请求重建；近期时钟跳变伴随 RTP 和媒体同时停止时采用 2 s 条件。每代最多触发一次，同 URI 每 60 s 最多两次。单个异常时钟报告不会单独触发重启，MT11 不使用这项 A8 策略。

运行时接线位于 `A8RtspStreamRecovery::install()`。内部观察器的 `attach()` 安装 RTP、解析后媒体和 RTCP 探针，`tick()` 汇总时间戳并调用 `A8RtspRecoveryPolicy::evaluate()`。策略用 `begin()` / `displayed()` / `stop()` 跟踪当前代次，用 `senderReport()` 与 `clockJump()` 处理 RTCP 时钟证据；对象释放时 `detachProbes()` 拆除探针。它处理“已经播放后停滞”，首帧阶段仍由上一节的解码恢复负责。

#### 3.2.5 单路视频的数据分支

两条视频流分别建立下列结构；图中的录像支路在用户开始本地录像后接入。

~~~mermaid
flowchart LR
    Source["RTSP / RTP"] --> Parse["解包与 parser"]
    Parse --> Tee["压缩码流分支"]
    Tee --> Decode["decoder"]
    Decode --> Sink["Qt 视频显示项"]
    Sink --> Photo["离屏截图 → JPEG"]
    Tee --> Mux["录像 parser / mux"]
    Mux --> File["MKV / MOV / MP4"]
~~~

`hvc1/AU` 和 `byte-stream/AU` 是 H.265 的不同输入封装，AU 表示一个访问单元。当前恢复分别处理输入格式和 decoder 候选；最终有画面还依赖输出与显示项成功连接。

#### 3.2.6 运行状态的含义

| 可观察状态 | 表示什么 | 后续使用方式 |
|:---|:---|:---|
| `enabled` | 当前视频源和全局开关允许第二路工作 | 继续核对 URL 与重复源 |
| `hasVideo` | 第二路配置可用：已启用、URL 非空、未重复 | 可创建视图，不代表已经收到画面 |
| `initialized` | 第二路 receiver 对象存在 | 仍需完成窗口、sink 和管线初始化 |
| `streaming` | receiver 报告已进入流接收状态 | 本地录像还需核对格式、所有权和启动结果 |
| `decoding` / `videoSize` | receiver 报告解码状态及当前尺寸 | 媒体抓图同时检查对应显示项 |
| source / decoder / sink 首帧 | 分别证明压缩数据、解码输出、显示链路取得进展 | 按同一 receiver/URI/generation 组合判断 |

设置变更由 `_refreshSettings()` 更新期望状态，`_applyDesiredState()` 统一发起启停。停止未完成时不并发启动新管线；重连计时到达后仍复核当前 URI、启用状态和代次。主路的配置中、启动中、活动中及释放中的 URI 都参与第二路重复源检查，避免同一相机在交接期间被重复占用。

> **配置完成的观察点**：确认两个窗口各自有画面，并分别检查各路首帧与尺寸。单独看到“SDK 在线”、URL 已填写或 `hasVideo=true`，只证明对应配置/控制阶段成立。

#### 3.2.7 功能对应的文件与资源协作

以下把显示、流生命周期和解码恢复分开组合；表中 GStreamer 文件均位于 `VideoManager/VideoReceiver/GStreamer/`。

| 功能环节 | 文件 / 资源组 | 在本功能中的协作关系 |
|:---|:---|:---|
| 三视图与 PIP 操作 | `FlightDisplay/FlyView.qml`、`DualPipView.qml` | FlyView 创建地图/Video 1/Video 2，DualPipView 保存主辅位置并实现交换、展开、缩放与独立窗口。 |
| PIP 缩放验证 | `custom/test/FlightDisplay/DualPipResizeTest.py` | 加载实际 DualPipView 与原生 PipState，检查鼠标位移和两个辅窗尺寸一致、边界限幅、取消/重拖及内容项归属。 |
| 第二路实际画面 | `FlightDisplay/FlyViewSecondaryVideo.qml`、`FlightDisplayViewSecondaryVideo.qml` | 外层维护 PipState、全屏及弹窗重启；内层创建视频显示项、等待提示、适配/裁剪和参考线，再把窗口/显示项交给 Manager。 |
| 视频配置输入 | `UI/AppSettings/VideoSettings.qml`；`Settings/VideoCustomSettings.h/.cc`、`VideoCustom.SettingsGroup.json`；`Gimbal/GimbalControlSettings.h/.cc` 及 JSON | 页面编辑第二路 URL 和解码策略；VideoCustom 持久化第二路地址，GimbalControl 保存 Android 策略及 MT11 端点等关联配置。 |
| 主路与第二路生命周期 | `CustomPlugin.cc`；`VideoManager/DualVideoManager.h/.cc`；原生 `src/VideoManager/` | 主路复用原生 VideoManager；第二路独立持有 receiver/sink、处理重复源和重连；插件把各路显示项、恢复和相机媒体接口接起来。 |
| A8 主路缺省值 | `Gimbal/GimbalVideoStreamSupport.h/.cc` | 在启动/消息入口安装主路默认视频配置，并按产品开关过滤 MAVLink 自动流信息，避免相机设置被自动 URI 接管。 |
| Android 解码器初选 | `AndroidVideoDecoderPolicy.h/.cc`、`AndroidH265HardwareDecoderAdapter.h/.cc` | 启动策略建立厂商候选；H.265 适配器规范化封装并连接实际 MediaCodec。 |
| parser 与 decoder 格式衔接 | `AndroidH265StreamFormatPolicy.h/.cc`、`AndroidH265DecoderCapsPolicy.h/.cc` | 前者根据 URI/MT11 地址选择初始 parser 格式；后者提供 adapter 内部 decoder 接收的 byte-stream/AU CAPS。 |
| 候选切换与首帧恢复 | `AndroidH265DecoderRoutePolicy.h/.cc`、`AndroidH265DecoderFallback.h/.cc`、`AndroidVideoDecoderRecovery.h/.cc`；`DualVideoManager.h/.cc` | 纯策略给出候选顺序，Fallback 保存逐路尝试状态；主路观察器与第二路 Manager 分别根据首帧/错误事实启动有界恢复。 |
| A8 播放后停滞恢复 | `A8RtspRecoveryPolicy.h/.cc`、`A8RtspStreamRecovery.h/.cc` | StreamRecovery 安装 RTP/媒体/RTCP 探针并采样；Policy 判断时钟证据、停滞和恢复额度，再向运行层返回恢复决策。 |
| 画面分辨率反馈 | `PulledVideoResolutionProbe.h/.cc`；两种相机 Manager | 从实际协商的 sink 获取像素尺寸，反馈给对应相机，用于倍率能力门控与本地照片尺寸选择。 |
| 验证与实机证据 | `custom/test/VideoManager/VideoReceiver/GStreamer/` 两个测试；`custom/tools/a8-video-capture.sh` | 主机测试检查候选/格式/CAPS 与 A8 恢复规则；采集脚本收集设备上的播放和恢复证据，不能代替完整真机验收。 |

---

<a id="a8"></a>

### 3.3 SIYI A8 Mini 相机

#### 3.3.1 功能与设置

A8 相机通过私有 UDP SDK 实现倍率查询、短按/长按变倍、拍照和录像。相机栏在启用后常驻，SDK 离线时保留界面和状态提示；相机操作不以飞控连接为前提。拍照/录像还可同时写入本机，见 [3.5](#media)。

在“应用设置 → 飞行视图 → 云台相机 → SIYI A8 Mini”配置：

| `GimbalControl` 设置键 | 默认值 | 说明 |
|---|---|---|
| `enabled` | true | A8 私有 SDK 与相机栏开关 |
| `sdkHost`、`sdkPort` | `192.168.144.25`、37260 | UDP SDK 端点 |
| `zoomStep` | 1.0x，范围 0.1～4.5 | 短按目标步长与长按目标显示分档 |
| `uniRcZoomDirectionReversed` | false | 仅反转 UniRC CH9，不改变触控方向 |

#### 3.3.2 工作模式

- **短按**：从当前目标倍率沿合法档位表前进/后退一档，发送绝对倍率 `0x0F`；本地发送成功后立即更新目标显示。
- **长按**：共享手势组件在 420 ms 后进入连续变倍，以 `0x05` 发送方向。目标显示按总按压时间和 600 ms 分档周期更新，达到端点或松手时停止。
- **取消**：隐藏、失焦、断流、切换管理器或退出会取消手势并发送停止，不额外推进显示目标；停止后有一次延迟安全停止副本。
- **能力限制**：倍率上限由相机 `0x20` 卡录分辨率决定，`0x16` 可进一步收紧。4K 为 1.0x，2K 为 3.5x，1080P 为 5.5x，720P 为 6.0x；这些值不由 QGC 播放分辨率推算。
- **反馈**：栏内显示目标倍率，`0x18` 实测值用于校验和初始化。视频会话及卡录能力未确认时不开放缩放。

默认步长为 1.0x 时，1080P 的合法档位为 `1/2/3/4/5/5.5`，正反方向使用同一表，确保可以到达精确上限。

#### 3.3.3 实现流程

**从按钮到 UDP，再回到界面**

~~~text
GimbalCameraControl / GimbalZoomControl
    → GimbalControlManager：检查能力、规划目标、管理动作
    → SiyiSdk：选择端点、发送请求、派发应答
    → SiyiProtocol：编码帧 / 解码帧与 payload
    → QUdpSocket
    ← SDK 信号 → Manager 更新属性并发出 Changed → QML 重新绑定
~~~

[Manager 实现](custom/src/Gimbal/GimbalControlManager.cc) 的构造函数是阅读起点：集中连接 Fact、SDK 信号及各定时器。界面只调用公开动作，不直接组织协议字节。

**初始化与能力建立**

| 触发 | 方法链 | 状态结果 |
|:---|:---|:---|
| 设置/端点变化 | `_settingsChanged()` → `_configureSdkEndpoint()` | 重新配置 SDK，清理不再适用的能力与动作状态 |
| 周期查询 | `_pollSdk()` | 约 2 s 周期查询相机状态、卡录参数、最大倍率等；真实回复刷新 SDK 在线状态 |
| 卡录能力回复 | `_handleRecordingStreamParameters()` → `_refreshMaximumZoomCapability()` | `A8MiniZoomPolicy::maximumZoomForRecordingResolution()` 给出分辨率上限，与有效设备上限合并 |
| 实际拉流尺寸 | `setNegotiatedPulledVideoResolution()`、`_handlePulledVideoSize()` → `_tryConfirmPulledVideoResolution()` | 核对当前视频会话和尺寸；支持的拉流尺寸由 `isSupportedPulledVideoResolution()` 判断，目前为 1920×1080、1280×720 |
| 当前倍率回复 | `_handleCurrentZoom()` | 更新实测状态，处理当前目标确认和停止后的校验 |
| 能力过期/视频停止 | `_expireRecordingResolutionCapability()`、`_invalidatePulledVideoResolutionCapability()` | 旧能力失效，缩放入口重新等待当前会话条件 |

卡录尺寸用于设备能力，拉流尺寸用于视频有效性，两条来源分别维护；不能把 sink 的 1080P 直接当作相机卡录也是 1080P。

**短按的目标规划与确认**

1. QML 松手判定为短按后调用 `zoomIn()` / `zoomOut()`，进入 `_sendZoomStep(direction)`。
2. `_zoomPlanningReference()` 取得规划参考；`A8MiniZoomPolicy::stepTarget()` 委托共享 `ZoomStepPolicy`，按最小倍率锚定的档位计算下一目标。内部按十分之一倍率计算，精确上限额外作为合法终点。
3. `setZoom()` / `_sendAbsoluteZoomTarget()` 检查合法范围，交给 `SiyiSdk::sendAbsoluteZoom()` → `SiyiProtocol::absoluteZoomPacket()` 编成 `0x0F`。
4. 本地发送成功即发布目标显示；`_handleAbsoluteZoomFeedback()` 处理命令反馈，`_handleCurrentZoom()` 用 `0x18` 实测校验。连续短按采用最新目标，旧目标不再拥有后续显示更新。
5. `_beginStableZoomConfirmation()` / `_finalizeConfirmedZoom()` 组织到位确认；`_handleZoomQueryTimeout()` 使无有效回复的查询结束，避免一直保留在途状态。

**长按、停止和操作来源**

| 阶段 | 入口与实现 |
|:---|:---|
| 捕获手势 | `GimbalZoomControl.qml` 在按下时保存本次 Manager；`beginHeldZoom()` 超过阈值调用 `startZoomWithPressDuration()` |
| 启动运动 | Manager 的 `_startZoomWithPressDuration()` 记录方向、起点、按压时间和来源，发送 `SiyiSdk::sendManualZoom()` |
| 持续控制 | `_pollContinuousZoom()` 处理运行过程；`_advanceHeldZoomDisplayTarget()` 调用 `A8MiniZoomPolicy::heldTarget()`，按总按压时长计算目标档位 |
| 普通释放 | `stopZoom()` → `_stopContinuousZoom()`，进入停止、回读与目标收尾 |
| 生命周期取消 | `cancelZoom()` / `_stopContinuousZoomForSafety()` 结束动作；QML 的 `cancelZoomGesture()` 处理隐藏、失焦和切换对象 |
| 安全停止副本 | `_sendPendingManualZoomStop()`、`_retryManualZoomStop()` 管理停止包及一次延迟副本；新动作前处理旧停止状态 |
| UniRC 来源 | `startUniRcZoom()`、`stopUniRcZoom()`、`cancelUniRcZoom()` 使用同一 A8 控制通路，并区分动作持有者 |

**协议与反馈分层**

`SiyiSdk::_readPendingDatagrams()` 检查来源 IP，交给 `SiyiProtocol::decodeDatagram()` 校验完整帧、长度、CRC，再筛选 ACK。`_dispatchAck()` 按命令调用 `parse*Payload()`，有效数据先发 `packetReceived`，再发业务信号：

- `currentZoomReceived` / `maximumZoomReceived` → 倍率与能力处理。
- `recordingStreamParametersReceived` → 卡录尺寸和上限。
- `cameraSystemStatusReceived` / `functionFeedbackReceived` → SD 状态、拍照/录像反馈。
- `communicationError` → Manager 错误显示与当前动作处理。

模式查询另用专用 mode socket 和 requestId 隔离，见 3.6；普通相机 socket 的接收规则与 MT11 的近期请求窗口规则不同。拍照/录像的公开入口为 `takePhoto()`、`toggleVideoRecording()`，本地支路实现集中说明于 3.5。

#### 3.3.4 操作完成条件与协议分工

从新连接开始，A8 先确认视频会话和卡录能力，再用当前倍率反馈建立操作参考；恢复播放后重新建立视频门控。卡录能力变化时按新范围重新约束目标，避免沿用已不支持的倍率。

| 命令 | Manager 使用目的 | 完成判断 |
|:---|:---|:---|
| `0x20`、`0x16` | 查询卡录参数和设备倍率上限 | 有效回复形成当前倍率能力 |
| `0x18` | 查询真实倍率 | 初始化参考、校验运动及停止后的结果 |
| `0x0F` | 设置绝对倍率 | 发送成功后显示新目标，反馈独立核对 |
| `0x05` | 连续放大、缩小或停止 | 按手势生命周期发送；停止流程负责安全副本 |
| `0x0A` | 查询相机/录像状态 | 更新 SD 状态与录像确认 |
| `0x0C`、`0x0B` | 拍照/录像动作及功能反馈 | 结合功能反馈、状态回读更新操作结果 |

以“1080P 卡录、步长 1.0x、目标 4.0x”为例：一次短按放大选择 5.0x，下一次选择精确 5.5x；从 5.5x 缩小则返回 5.0x。长按使用连续运动命令，界面目标按时间分档；显示值用于表达本次控制目标，不应拿它代替相机实测倍率。

相机栏 `online` 由 `enabled && sdkResponding` 决定；缩放另有 `zoomControlsUnlocked` 及各方向/手势的可用性检查。本地媒体也单独检查视频条件，因此“SDK 状态点变灰”和“本地媒体不可用”不必同时发生。

#### 3.3.5 功能对应的文件与资源协作

| 功能环节 | 文件 / 资源组 | 在本功能中的协作关系 |
|:---|:---|:---|
| 启用、端点和倍率步长 | `UI/AppSettings/GimbalControlSettingsGroup.qml`；`Gimbal/GimbalControlSettings.h/.cc`、`GimbalControl.SettingsGroup.json` | 设置页写入 Fact；Manager 读取开关、SDK 地址/端口和步长，配置 SDK 并更新按钮能力。 |
| 相机选择与面板创建 | `FlightDisplay/FlyViewTopRightColumnLayout.qml`；原生 `src/FlightDisplay/FlyViewWidgetLayer.qml` | custom 右侧列负责 A8/MT11 标签和 Loader，给 A8 面板绑定 gimbalControlManager；原生父布局决定整列锚点和外边距。 |
| 面板外观与操作反馈 | `FlightDisplay/GimbalCameraControl.qml`；原生 `qmlimages/camera_photo.svg`、`camera_video.svg` 图标资源 | 共享面板组织变倍、拍照、录像、计时及 SD/LOCAL 标识；按钮调用 Manager，反馈属性驱动禁用、颜色、闪烁和录制状态。 |
| 变倍输入与目标显示 | `FlightDisplay/GimbalZoomControl.qml`；`Gimbal/GimbalControlManager.h/.cc` | QML 区分短按/长按并在释放、隐藏、切换时取消；Manager 根据能力和当前动作所有者生成目标、发命令并处理回读。 |
| 倍率合法性与能力范围 | `Gimbal/A8MiniZoomPolicy.h/.cc`、`ZoomStepPolicy.h/.cc`；分辨率探针 | A8 策略结合拉流/卡录分辨率确定能力，StepPolicy 计算合法档位和精确上限；能力结果回到 Manager 和 UI。 |
| UDP 协议收发 | `Gimbal/SiyiSdk.h/.cc`、`SiyiProtocol.h/.cc` | SDK 负责端点、socket、查询与信号；Protocol 负责命令编码、CRC 和 payload 解析；Manager 消费反馈更新状态。 |
| 照片与录像 | `Gimbal/GimbalControlManager.h/.cc`；媒体策略、主路 receiver 与 Android 媒体桥 | Manager 同时发设备 SD 命令并维护 Video 1 本地媒体意图；保存、分段、发布的完整文件组合见 3.5.6。 |
| 验证 | `custom/test/Gimbal/SiyiProtocolTest.cc`、`SiyiModeQueryTest.cc` | 分别覆盖帧/倍率策略及专用模式查询关联；真实画面、设备动作和长期稳定性仍按验收矩阵核对。 |

**共享相机栏的 UI 分工与加载关系**

~~~text
原生 FlyViewWidgetLayer.qml
└── FlyViewTopRightColumnLayout.qml       右侧列与 A8/MT11 选择标签
    ├── A8 → GimbalCameraControl.qml     绑定 A8 Manager
    └── MT11 → MT11CameraControl.qml     注入 MT11 Manager、启用模式入口
               └── GimbalCameraControl.qml
                   ├── GimbalZoomControl.qml     + / 倍率 / − 与变倍手势
                   ├── modeButton/videoModeMenu MT11 视频模式
                   ├── photoButton/videoButton  拍照、录像与计时
                   └── recordingStatusRow       SD / LOCAL 状态
~~~

`GimbalCameraControl.qml` 的 `controlColumn` 决定内部顺序，`panelColor/panelBorderColor/panelPadding` 定义面板，`actionSize/itemSpacing` 定义尺寸和间距。`photoSuccessFlash`、`recordingTimeText()`、`sdRecordingBadge/localRecordingBadge` 分别处理成功闪烁、计时和支路状态。

该面板把尺寸/颜色传给 `GimbalZoomControl.qml` 的 `controlSize/controlSpacing` 等属性，因此统一调整整栏样式时应同时看父组件传值。倍率区的 `zoomInButton/zoomOutButton/targetZoomLabel` 定义具体外观；`holdThresholdMs/holdStartRetryMs` 决定手势识别时机。共享面板的外观修改同时作用于 A8 与 MT11。

---

<a id="mt11"></a>

### 3.4 UniPod MT11 相机

#### 3.4.1 功能与设置

MT11 使用独立 UDP socket、请求状态和媒体管理器，复用相机栏外观，增加视频模式选择。飞行页右侧相机选择器切换当前操作对象；控制对象选择不改变视频 URL 或顶部 MAVLink 活动云台。

| `GimbalControl` 设置键 | 默认值 |
|---|---|
| `mt11Enabled` | true |
| `mt11SdkHost`、`mt11SdkPort` | `192.168.144.24`、37260 |
| `mt11ZoomStep` | 1.0x，范围 0.1～29.0 |

#### 3.4.2 使用与工作模式

1. 在“应用设置 → 飞行视图 → 云台相机 → UniPod MT11”启用模块，填写 MT11 SDK 地址和端口。
2. 需要画面与本地保存时，另在 Video 页填写 URL 2；SDK 端点与 RTSP 地址各自配置。确认 Video 2 有画面。
3. 在飞行页右侧选择 MT11，观察 SDK 状态，再操作变倍、模式、拍照或录像。模式回读和实际视频画面分别确认。

| 操作 | 当前行为 |
|---|---|
| 短按 +/− | 在 1.0～30.0x 内按步长发送 `0x0F`；最后不足一步时到精确 30.0x |
| 长按 +/− | 420 ms 后以 `0x05` 连续变倍，可覆盖设备混合倍率；产品上限封顶 165.1x |
| 高于 30x 时短按 | 不发送绝对倍率命令；使用长按回到支持范围 |
| 选择视频模式 | 变焦、热成像、变焦+热成像拼接；等待设备确认后更新 |
| 拍照/录像 | 独立控制 MT11 SD 支路及绑定的 Video 2 本地媒体 |

`mt11ZoomStep` 控制目标档位，不控制镜头连续运动速度；MT11 SDK 没有速度字段。长按期间每 450 ms 保活同方向，实际 `0x18` 倍率用于进展和端点判断。普通松手通常发送停止；具有严格端点确认时可省略普通停止，取消或退出仍执行停止。60 s 内没有请求方向的有效倍率进展时，watchdog 结束动作。

#### 3.4.3 实现流程

**独立对象与输入入口**

`MT11CameraControl.qml` 为共享相机面板注入 `QGroundControl.corePlugin.mt11ControlManager`。它与 A8 共用 QML 手势逻辑，但使用独立 `Mt11ControlManager`、`Mt11Sdk`、socket、定时器和媒体状态。

| 功能 | 具体入口 / 方法 | 实现机制 |
|:---|:---|:---|
| 启用与配置 | `_settingsChanged()`、`_configureSdkEndpoint()` | 应用 MT11 端点，取消旧动作、清理旧请求与能力，再建立本会话状态 |
| 状态探测 | `_pollSdk()` | 查询相机、视频模式、最大/当前倍率及卡录尺寸；在线状态只由有效设备回复维持 |
| 短按 | `zoomIn()/zoomOut()` → `_sendZoomStep()` → `setZoom()` | `Mt11ZoomPolicy::tapTarget()` 用实测倍率检查 30x 协议边界，用显示目标计算下一档 |
| 发送绝对倍率 | `setZoom()` → `Mt11Sdk::sendAbsoluteZoom()` | 检查目标合法、撤销上一 hold 的延迟停止，记录 pending 目标，立即更新目标显示 |
| 确认绝对倍率 | `_pollPendingAbsoluteZoom()`、`_handleCurrentZoom()`、`_handleAbsoluteZoomConfirmationTimeout()` | 持续查询实测值，完成或超时清理命令等待；普通发送成功不代表镜头到位 |
| 长按启动 | `startZoomWithPressDuration()` → `_startPendingContinuousZoom()` | 捕获本次运动参考，清理旧绝对命令的确认状态，立即发送新方向 |
| 长按运行 | `_pollContinuousZoom()`、`_observeZoomFeedback()` | 450 ms 方向保活；使用实测进展更新 watchdog、方向水位及端点确认 |
| 显示对齐 | `_alignDisplayToMeasured()` → `Mt11ZoomPolicy::alignedDisplayTarget()` | 将设备观察值映射回当前配置允许的显示档位 |
| 松手/取消 | `stopZoom()`、`cancelZoom()`、`_finishContinuousZoomState()` | 先撤销待发送方向和保活，再执行停止；取消始终按生命周期停止语义处理 |

**连续操作如何衔接**

`startZoomWithPressDuration()` 会读取正在等待的绝对目标或当前实测作为本次运动参考。新 hold 可以接管旧 `0x0F` 的本地等待状态，也可以结束上一次松手后的等待；不需要把旧目标确认完才能开始新方向。旧延迟停止通过 `_retireContinuousZoomStopRetry()` 退役，防止它随后停掉新手势。

`_zoomBoundaryReached()` 结合实测证据判断端点；正常松手在严格确认端点时可省略一次普通停止，`cancelZoom()` 则清除端点保持并发送停止。QML 保存物理按压是否仍有效，定时重试不能在释放后重新发方向。

**视频模式设置与回读**

1. 共享面板的模式选择调用 `setVideoMode()`，Manager 检查在线/在途状态并取消缩放。
2. `Mt11Sdk::setVideoMode()` → `Mt11Protocol::setVideoModePacket()`，生成 `0x11`：变焦 `[00 02]`、热成像 `[02 00]`、拼接 `[03 02]`。
3. SDK 将有效 `0x10/0x11` 模式 payload 统一解析为 `videoModeReceived(mainStream, subStream)`；Manager 的 `_handleVideoMode()` 识别组合、更新已知模式并处理等待目标。
4. `_handleVideoModeCommandTimeout()` 负责超时退出。模式相关能力重新查询，卡录尺寸通过 `_handleRecordingStreamParameters()` 重建，避免沿用上一画面模式的照片目标尺寸。
5. 界面的 `videoModeKnown`、`videoModePending` 和模式值各自绑定；实际画面由 RTSP 流输出，SDK 回读与视频内容需要分别验收。

**MT11 收包规则与倍率编码**

`Mt11Sdk::_readPendingDatagrams()` 核对配置的 IP 和端口，`Mt11Protocol::decodeDatagram()` 校验帧，`_dispatchAck()` 用 `_takePendingCommand()` 匹配最近 1.5 s 内同命令的请求。异步 `0x0B` 功能反馈不要求普通 ACK 请求窗口；非法 payload 不延长原窗口。

`parseManualZoomAckPayload()` 将 `0x05` 回复按“小端无符号 16 位 / 10”解析；`parseZoomValuePayload()` 将 `0x16/0x18` 按“整数位字节 + 小数位字节 / 10”解析。两种编码不能共用一个倍率解码公式。Protocol 的表示范围、设备报告上限和产品控制上限也分别由协议、Manager 和 Policy 约束。

#### 3.4.4 手势衔接与模式确认

共享 QML 手势层记录本次按压的 Manager。短按在释放时选择步进目标；超过 420 ms 则进入 hold。轻微手指漂移不会立即吃掉仍在按住的动作，真正释放/取消时先清除待发送方向，防止松手后运动被定时器重新启动。

MT11 将“目标”“实测”“命令等待”分开维护：

| 状态 | 作用 |
|:---|:---|
| `currentZoom` | 控制栏显示的目标倍率 |
| `actualZoom / actualZoomKnown` | `0x18` 实测值与有效性，供能力和进展判断 |
| `zoomCommandPending` | 绝对倍率请求仍在等待确认 |
| `continuousZoomActive` | 本次长按连续运动仍有效 |
| `videoModeKnown / videoModePending` | 已知模式与正在执行的切换事务 |

例如步长为 2.0x 时，短按目标序列为 `1/3/5/…/29/30`，反向使用相同档位。倍率超过 30x 时通过长按缩小回到绝对命令支持范围，再使用短按微调。

模式弹层只在 SDK 在线且没有模式请求在途时开放。用户选择后进入等待态，设备回包确认后更新选中项；弹层关闭不代表切换已成功。视频模式变化会使卡录尺寸等能力失效，需重新查询；验收要同时确认模式反馈和 RTSP 中的实际画面。

#### 3.4.5 功能对应的文件与资源协作

| 功能环节 | 文件 / 资源组 | 在本功能中的协作关系 |
|:---|:---|:---|
| MT11 设置与启用 | `UI/AppSettings/GimbalControlSettingsGroup.qml`；`Gimbal/GimbalControlSettings.h/.cc` 及 JSON | 保存独立 SDK 端点、开关和倍率步长；驱动 MT11 Manager 重新配置，并影响模式/变倍按钮是否可用。 |
| 面板选择与型号注入 | `FlightDisplay/FlyViewTopRightColumnLayout.qml`、`MT11CameraControl.qml` | 选择器决定加载 MT11 包装；包装再加载共享栏，绑定 mt11ControlManager 并启用 thermalControlsVisible。 |
| 共享外观与模式菜单 | `FlightDisplay/GimbalCameraControl.qml`、`GimbalZoomControl.qml`；原生相机图标和 `InstrumentValueIcons/view-carousel.svg` | 共用拍照、录像、状态与变倍 UI；modeButton/videoModeMenu/modeOption 展示 ZOOM、IR、MIX，并调用 Manager.setVideoMode。外观分工见 3.3.5。 |
| 变倍与模式事务 | `Gimbal/Mt11ControlManager.h/.cc`、`Mt11ZoomPolicy.h/.cc`、`ZoomStepPolicy.h/.cc` | Manager 区分短按绝对倍率与长按运动，策略计算可达目标/反馈对齐；模式事务通过回读确认，QML 展示实际结果。 |
| 独立 UDP 与反馈校验 | `Gimbal/Mt11Sdk.h/.cc`、`Mt11Protocol.h/.cc` | Protocol 编解码两种倍率及视频模式；SDK 校验来源和近期请求，分发 ACK/异步反馈，避免与 A8 状态混用。 |
| 第二路画面与本地媒体 | `VideoManager/DualVideoManager.h/.cc`；`Gimbal/Mt11ControlManager.h/.cc`；第二路视频 QML | 第二路接收器和显示项供 MT11 抓图/录像使用；媒体会话和文件命名由 MT11 Manager 独立维护。完整保存链路见 3.5。 |
| 验证 | `custom/test/Gimbal/Mt11ProtocolTest.cc`；共享设置布局检查 | 协议测试验证命令字节、倍率编码和目标边界；布局检查覆盖设置页，模式画面与连续变倍需设备验收。 |

---

<a id="media"></a>

### 3.5 双路本地照片、录像与 Android 图库

#### 3.5.1 使用方式与保存语义

在“应用设置 → Video → Local Video Storage”设置 `GimbalControl/localMediaStorageEnabled`，默认 true、即时生效。格式与容量参数沿用原生 Video 设置；自动清理的实际触发条件见 3.5.4。

- 关闭本地开关时，相机按钮只操作设备 SD。
- 开启后，每次拍照/录像同时尝试 SD 与 LOCAL 两条支路；无卡或 SDK 失败只影响 SD，视频条件满足时 LOCAL 仍能工作。
- 观察各支路状态和错误。按钮动作成功、文件暂存成功、公共图库发布成功是不同阶段。
- 本地照片来自解码画面；本地录像复用压缩码流。即使照片按 4K 输出，若 RTSP 只提供 1080P，也只有 1080P 的原始细节。
- A8 本地录像断流后可在条件恢复时自动另起分段；MT11 断流停止本地支路，恢复画面后需结束原会话、重新开始录像。SD 支路的状态独立观察。

#### 3.5.2 本地照片流程

两个 Manager 的公开 `takePhoto()` 分别尝试 SDK 拍照与 `_captureLocalVideoFrame()`。本地抓图只使用插件绑定给自己的视频项；不抓整个 Fly View，也不随 PIP 主辅位置改换相机。

| 步骤 | 方法 / 对象 | 处理细节 |
|:---|:---|:---|
| 1. 检查入口 | 各 Manager 的 `_captureLocalVideoFrame()` | 对应路必须正在解码、视频项和窗口有效、目录可写；pending 与 grab lifetime 共同限制每路只允许一个任务 |
| 2. 确定源尺寸 | negotiated size → 接收器/VideoManager 尺寸 → item implicit size → item × DPR | 源尺寸描述真实视频。A8 在 Manager 中逐级回退，MT11 使用 `GimbalPhotoCapturePolicy::resolveSourcePixelSize()` |
| 3. 确定输出尺寸 | 有效卡录尺寸优先，否则源尺寸 | MT11 的 `0x20` 需连续两份一致回复，4.5 s 内持续刷新；`isPixelSizeWithinBounds()` 检查长边 4096、短边 2160 及总像素限制 |
| 4. 计算抓图几何 | `GimbalPhotoCapturePolicy::captureGeometry()` | 返回 outputPixelSize、contentPixelSize、grabLogicalSize；把物理输出与 Qt 逻辑尺寸/DPR 分开 |
| 5. 异步抓图 | `QQuickItem::grabToImage()` → `QQuickItemGrabResult::ready` | 快照文件名、尺寸和请求序号；5 s 定时器限制抓图等待；lifetime 对象保持 grab result 至回调完成 |
| 6. 图像整理与写盘 | A8 `saveLocalPhotoImage()`；MT11 `savePhoto()` | 在各自单线程照片池运行，调用 `prepareImageForSaving()` 修正小数 DPR 舍入、等比缩放与居中黑边 |
| 7. 原子提交 | `QImageWriter` + `QSaveFile` | JPEG quality=100；编码成功且 commit 完成才作为成品 |
| 8. 返回界面 | queued 回调 → 计数、pending、错误属性 | 重新核对请求身份，防止旧任务改变新任务的等待状态；成功后进入平台发布 |

照片文件名包含时间戳及 `_local_NNN`，MT11 增加 `MT11_` 前缀。切换视频项、窗口销毁或超时时，旧抓图不再拥有当前请求；worker 已持有的图像按其快照完成写盘，退出流程等待 worker 结束。Android 成品的发布阶段见 3.5.4。

#### 3.5.3 本地录像流程

**入口、状态与录像分支**

`toggleVideoRecording()` 进入各 Manager 的 `_startRecordingSession()` / `_stopRecordingSession()`，分别维护 SD 目标和本地意图。两个 Manager 都通过自己绑定的 receiver 调用 `startRecording(outputFile, format)`；GstVideoReceiver 从压缩码流分支接入录像 parser/mux，写 MKV/MOV/MP4，不重新编码屏幕画面。

| 状态 | 在实现中的含义 |
|:---|:---|
| intent | 用户希望本地录像；A8 可在断流期间保留，MT11 在本路流结束时清除 |
| startPending | 已发出启动请求，尚未确认本次文件真正启动 |
| owned / ownershipConfirmed | 启动结果成功且输出匹配本次请求，才获得停止该录像的资格 |
| active | 本地支路当前被计入录制；A8 还区分“观察外部会话”与“自己拥有” |
| stopPending | 已请求停止，等待接收器停止及容器收尾 |
| issued file bases | 记录已发出的分段请求，旧请求未解决前不并发认领下一段 |

**A8 与 MT11 的执行路径**

| 阶段 | A8 / Video 1 | MT11 / Video 2 |
|:---|:---|:---|
| 决定动作 | `_reconcileLocalRecording()` 构造 LocalState，调用 `GimbalMediaSessionPolicy::localAction()` | 会话入口及 receiver 状态回调直接管理本路状态 |
| 创建分段 | `_startLocalRecording()` 校验目录、格式、意图与主 receiver，生成文件并置 pending | `_startLocalRecording()` 为第二路生成带 MT11 前缀的文件并置 pending |
| 启动确认 | `handleMainVideoRecordingStartResult()` 匹配主路启动结果与输出路径 | `handleVideoRecordingStartResult()` 匹配第二路结果与已发出的文件 |
| 状态变化 | `_handleVideoStreamingChanged()`、`_handleVideoRecordingChanged()` | `_handleReceiverStreamingChanged()`、`_handleReceiverRecordingChanged()` |
| 停止 | `_stopLocalRecording()` 只停止已确认拥有的主路录像 | `_stopLocalRecording()` 只停止本 Manager 拥有的第二路录像 |
| 分段收尾 | 主 receiver 停止/录像完成回调清理本段并登记成品 | `_handleReceiverStopRecordingComplete()` → `_finishLocalRecording()`，清理状态并发布非空成品 |
| 断流与恢复 | `_handleVideoStreamingChanged()` 保留续录意图；流和前段收尾就绪后重新协调启动 | `_handleReceiverStreamingChanged(false)` 清除本地意图并停止；流恢复不自动续录 |
| 超时 | `_handleLocalRecordingStartTimeout()`、`_handleLocalRecordingStopTimeout()`；停止超时最多补发一次 | 同名处理函数；仍拥有且仍录制时，停止超时继续重发并重启定时器 |
| 退出/解绑 | `shutdownLocalMedia()` | `shutdownLocalMedia()`，receiver 释放后再 `finalizeDetachedLocalMedia()` |

共享 Policy 中 `StartOwned`、`StopOwned`、`ConfirmOwned` 对应自己发起的录像；`AdoptExternal` / `ReleaseExternal` 供 A8 观察已有原生录像状态。**观察外部会话不会取得停止权**：结束本地会话时只解除观察，不停止外部入口的录像。

两路启动等待定时器均为 3 s，停止等待每轮为 5 s。A8 停止超时最多补发一次，仍失败则结束 pending 并报告错误；MT11 在仍拥有且 receiver 仍录制时继续重发停止，未设置相同的一次重试上限。启动确认前发生禁用、退出或其他取消时，先记录意图，匹配的迟到成功结果取得所有权后再补偿停止。

**断流处理按相机区分**：A8 结束当前分段，在同一用户意图、本地开关和会话有效且前段已收尾时自动创建新文件；视频仍健康而录制异常结束时，会阻止立即反复启动，等待用户或新流会话。MT11 清除本地意图并报告流结束，不因 Video 2 恢复而续录；原会话仍可能保留 SD 录制或用户请求状态，需先结束原会话再开始新的录制。

显式停止、关闭本地开关或退出会结束相应本地录制意图。两种 Manager 正常退出各自最多等 3 s 录像收尾，再等待照片 worker；公共媒体发布另有 120 s 等待上限。

#### 3.5.4 目录与容量管理

| 平台/阶段 | 位置与管理方式 |
|---|---|
| 桌面 | `AppSettings::savePath()/Photo` 和 `Video`；容量清理限定为本功能生成的录像文件 |
| Android 暂存 | 所选存储卷的应用外部文件目录下 `Custom-QGroundControl/Staging/Photo`、`Video` |
| Android 公共照片 | `Pictures/Custom-QGroundControl/` |
| Android 公共录像 | `Movies/Custom-QGroundControl/` |

Android 通过 `AndroidMediaLibrary` 的 JNI 桥交给 Java 发布：API 29+ 使用 MediaStore，旧平台通过公共目录与 MediaScanner。启动时补扫已挂载卷的当前及受支持旧暂存目录；成功发布后清理源文件。

**容量清理的触发边界**：当前 `cleanupOldLocalVideos()` 由 A8 `_startLocalRecording()` 在开始新分段前调用，并检查 `Video/enableStorageLimit` 与 `maxVideoSize`。桌面统计目录中 MKV/MOV/MP4 的总量，只删除符合 `_local_数字.扩展名` 的旧录像；Android 将清理请求交给公共图库登记表。MT11 开始录像没有独立调用此清理入口，因此仅录制 MT11 时不能把该容量设置理解为持续自动限额。

已发布的公共文件可在卸载后保留；发布前的应用暂存文件不具备这一保证。Android 清理范围仅为当前安装登记且可访问的公共录像，可包含两种相机已发布的文件；卸载重装建立新管理边界。发布期间暂存源与公共副本可能并存，设备实际占用可高于录像配额。

**Android 从 C++ 到公共图库的实现**

[AndroidMediaLibrary](custom/src/Android/AndroidMediaLibrary.cc) 是命名空间接口，通过 `QJniObject` 调用 `org/mavlink/qgroundcontrol/QGCCustomMediaLibrary` 的静态方法。

| C++ 入口 → Java 入口 | 实际职责 |
|:---|:---|
| `mediaStagingDirectory()` → `getMediaStagingDirectory()` | 按用户选择的卷找应用暂存目录，保持后续公共目标与源位于相应存储卷 |
| `existingMediaSourceDirectories()` → `getExistingMediaSourceDirectories()` | 枚举已挂载卷上的当前暂存与受支持旧目录，供启动补发 |
| `publishMediaFile()` → `publishFile()` | 路径去重后进入单线程 PUBLICATION_EXECUTOR；返回表示任务接收，不等于已公开可见 |
| Java `publishThroughMediaStore()` | API 29+ 创建/恢复公共条目，复制并确认内容，再完成公开提交；提交后处理源清理 |
| Java `publishThroughLegacyPublicDirectory()` | 旧平台复制到公共目录并调用 `scanFileAndWait()`，图库索引成功后收尾 |
| Java `completePublicationJournal()` / `completeSourceCleanup()` | 分别登记公开提交与源清理进度，重试时可识别已有成品 |
| `cleanupPublishedVideos()` → Java 同名方法 | `cleanupRegisteredVideos()` 只处理本安装登记的可管理公共录像 |
| `waitForPendingPublications()` → Java 同名方法 | 在队列尾加入等待屏障，供有时限的退出收尾 |

Java 使用 pending URI、已发布录像 URI 和待清理源记录区分阶段；`recoverStalePendingPublicationsOnce()` 处理上次未完成发布，`publicationPreferences()` 维护安装登记边界。只有公共副本确认完成后才删除暂存源；发布或清理失败留给后续恢复。这样重复启动补扫不会简单地把同一源再次发布为另一份录像。

#### 3.5.5 从按钮到文件的完成链路

~~~mermaid
flowchart TB
    Click["当前相机：拍照 / 开始录像"] --> SD["SDK 命令 → 相机 SD"]
    Click --> Local{"本地开关开启？"}
    Local -->|"是"| Ready["校验对应视频与保存条件"]
    Ready --> Photo["照片：抓图 → JPEG 原子写入"]
    Ready --> Record["录像：压缩码流 → 文件封装"]
    Photo --> Stored["本地成品已落盘"]
    Record --> Stored
    Stored --> Platform{"Android？"}
    Platform -->|"是"| Publish["发布到公共 Pictures / Movies"]
    Platform -->|"否"| Desktop["保存在 AppSettings 目录"]
    Publish --> Done["发布成功后清理暂存源"]
~~~

SD 与 LOCAL 并行执行，一条失败不会回滚另一条。照片按钮返回成功仅表示至少一条支路已开始/命令已发出；录像还需等待状态确认，文件只有在停止和封装完成后才进入公共发布。

| 场景 | 当前处理 |
|:---|:---|
| 相机无 SD，但视频正常 | SD 单独报告状态；LOCAL 可继续保存 |
| SDK 正常，但对应视频未解码 | SD 可独立尝试；当次本地拍照不执行，视频恢复后需重新点击 |
| 本地照片仍在处理，再次点击 | 本地支路拒绝叠加任务，SD 可独立尝试 |
| 本地录像启动结果尚未返回 | 按钮等待；因禁用/退出等取消时保留请求身份，迟到成功后补偿停止 |
| 主/第二路由其他入口正在录像 | 不认领该文件；只停止本 Manager 已确认拥有的录像 |
| A8 本地录像遇到断流 | 结束当前分段，保留有效意图，流恢复且前段收尾后续录 |
| MT11 本地录像遇到断流 | 清除本地意图并结束当前文件，恢复后需重新开始录制 |
| 公共图库发布失败 | 已写成的暂存文件保留，下次启动重试 |

录像界面的计时依据 `recordingSessionCapturing`：至少一条支路确认在录制时推进。A8 SD 的 `0x0C` 是切换动作，发送后的暂定状态需由 `0x0A` 确认，不能仅凭命令发送就认定 SD 正在录制。停止时也区分“请求已发送”“receiver 已停止”“容器已封装”“图库已发布”。

请求和输出文件名绑定代次，迟到的启动结果只能完成所属请求，不能认领另一条路或下一次录制。两路的超时与恢复差异统一见 3.5.3。

#### 3.5.6 功能对应的文件与资源协作

| 功能环节 | 文件 / 资源组 | 在本功能中的协作关系 |
|:---|:---|:---|
| 媒体开关与操作入口 | `UI/AppSettings/VideoSettings.qml`；`Gimbal/GimbalControlSettings.h/.cc` 及 JSON；`FlightDisplay/GimbalCameraControl.qml` | localMediaStorageEnabled 决定是否同时保存本地媒体；相机栏向所选 Manager 发动作，设备 SD 命令与本地支路独立执行，并分别显示状态。 |
| 两路会话与所有权 | `Gimbal/GimbalControlManager.h/.cc`、`Mt11ControlManager.h/.cc` | A8 对应主路，MT11 对应第二路；分别维护抓图请求、录像意图、pending、输出文件名及已认领录像，隔离切换和迟到回调。 |
| 照片尺寸与写盘 | `Gimbal/GimbalPhotoCapturePolicy.h/.cc`；两种 Manager；`PulledVideoResolutionProbe.h/.cc`（GStreamer 目录） | 探针提供源尺寸，Policy 计算 DPR/像素限制及补边；Manager 从绑定显示项异步抓图并交 worker 写盘，使用请求快照完成回调。 |
| 录像决策与分段 | `Gimbal/GimbalMediaSessionPolicy.h/.cc`；两种 Manager；主路 VideoManager 与 `VideoManager/DualVideoManager.h/.cc` | A8 使用 Policy 决策并支持有效意图下的断流续录；MT11 在自身 Manager 中维护状态，断流清除意图。两路 receiver 均封装压缩码流，恢复行为不同。 |
| Android 平台桥 | `Android/AndroidMediaLibrary.h/.cc`；`custom/android/src/org/mavlink/qgroundcontrol/QGCCustomMediaLibrary.java` | C++ 统一传递目录、发布与等待请求；Java 选择卷、暂存、提交 MediaStore/媒体扫描、重试和清理录像配额。 |
| 退出和发布收尾 | `CustomPlugin.cc`；两种 Manager；Android 媒体桥 | 插件协调停止录像、等待照片 worker 与公共发布；只有录像封装完成后才进入发布，失败暂存保留供后续恢复。 |
| 自动验证 | `custom/test/Gimbal/GimbalPhotoCapturePolicyTest.cc`、`GimbalMediaSessionPolicyTest.cc` | 检查照片尺寸计算与 A8 会话决策；实际文件完整性、双路隔离、图库和卸载保留仍需平台验收。 |

照片、录像、暂存和公共图库内容均为运行期生成文件，路径规则见 3.5.4；它们不属于 2.1 的源码/资源树。

---

<a id="gimbal"></a>

### 3.6 云台姿态、控制权与模式同步

#### 3.6.1 功能与工作模式

顶部云台栏作用于 `activeVehicle.gimbalController.activeGimbal`，提供 Yaw Lock/Follow、Center、Tilt 90 和 Retract。姿态动作走飞控 MAVLink Gimbal Manager；A8 的实际运动模式另由私有 SDK 确认。

| 状态/操作 | 当前行为 |
|---|---|
| 重连或模式反馈过期 | 显示“模式同步中”，等待有效实际模式 |
| Yaw Lock/Follow | 保留点击目标，申请控制权后执行模式切换并等待回读 |
| Center | 交给共享回中协调器；必要时先发有限俯仰预激活，再回中 |
| Tilt 90 | 使用 `sendPitchBodyYaw(-90, 0)` |
| Retract | 经当前云台控制器执行收回动作 |
| 失联、切车/切云台、SDK 端点或路由改变 | 取消对应旧会话的待执行动作 |

操作前需要活动飞行器和可用 Gimbal Manager。点击按钮即可启动接管过程；不需要另行点击“获取控制权”。SDK 相机选择器与此处的 MAVLink 活动云台是两个独立对象。

#### 3.6.2 回中与 CH10 姿态流程

**请求入口与上下文**

顶部 `GimbalIndicator.qml::_requestCenter()` 调用共享 `requestCenter()`；UniRC 调用 `requestNextCh10Action()`。两者进入 [GimbalCenterCoordinator::_beginRequest()](custom/src/Gimbal/GimbalCenterCoordinator.cc)，快照 Vehicle、GimbalController、Gimbal、manager component、动作、requestGeneration 与 CH10 revision。

`_beginRequest()` 连接本次 `Vehicle::mavCommandResult` 和对象销毁信号，置 busy、发出 `gimbalActionRequestStarted`，再调用 `acquireGimbalControl()`。每次显式动作都重新发 CONFIGURE；缓存显示已经持有控制权，也需确认本次请求成功。

| Phase | 进入条件与处理函数 | 离开条件 |
|:---|:---|:---|
| `Idle` | 尚无请求或 `_finishRequest()` 已清理 | 有效显式动作进入 WaitingForOwnership |
| `WaitingForOwnership` | `_beginRequest()`；`_mavCommandResult()` 记录 CONFIGURE ACK；`_ownershipChanged()` 更新所有权 | `_reviewRequest()` 同时确认 ACK 接受与实际控制权 |
| `WaitingForPrimerAck` | 回中需要预激活时调用 `_sendPrimer()` | 本次俯仰命令 ACK 接受后进入稳定等待 |
| `SettlingPrimer` | primerSettleTimer 等待 400 ms | 上下文与控制权仍有效时执行最终回中 |
| `WaitingForFinalAck` | `_sendFinalCenter()` 或 `_sendPitch90()` | 匹配最终 ACK 后更新动作状态并完成；超时/拒绝/失权则结束 |

预激活由当前俯仰夹紧到 [-90°, 0°] 后选择约 1° 的范围内偏移，通过 `sendPitchBodyYaw(primerPitch, 0, false)` 发出。最终 Center 使用 `centerGimbal()`；CH10 的俯视动作使用 `sendPitchBodyYaw(-90, 0)`。最终发送前后还核对消息计数及 generation，处理同步拒绝导致请求已经结束的情况。

**共享 CH10 下一动作如何更新**

`Ch10GimbalActionState::commandAccepted(action, revision)` 只接纳当前 revision 的完成结果。手动姿态输入调用 `noteManualAttitudeInput()`；顶部姿态操作通过 `noteRecenterCommandDispatched()`、`notePitch90CommandDispatched()`、`noteYawLockCommandDispatched()` 通知共享状态，推进 revision。这样请求发出后若用户另作操作，迟到 ACK 不会覆盖新决定。

顶部 Tilt 90/Retract 仍经过 QML 的 `_dispatchOwnershipAction()`、`_reviewPendingOwnership()` 和 `_invokeOwnershipAction()`：确认对象、控制权后调用原生姿态接口。顶部 Center 的常规产品路径和 CH10 使用 C++ 协调器；不能假定所有顶部按钮都走同一 C++ 请求状态机。

`cancel()` / `_finishRequest()` 停止请求、预激活和最终 ACK 定时器，断开本次连接并清空快照。切车、切云台、对象销毁、失权均会使旧请求退出；全程时限和最终 ACK 时限见 3.6.4。

#### 3.6.3 模式读取与切换流程

**实际模式来源**

`GimbalModeController::_bindVehicle()` / `_bindGimbal()` 连接活动对象；`_poll()` 负责周期查询、样本过期与命令超时。`_publish()` 统一发布 Unknown/Follow/Locked/Fpv，`_applyToNative()` 同步原生 yawLock。

| 路由 | 判定与接收函数 | 使用的实际状态 |
|:---|:---|:---|
| 本产品 A8 | `_isProductA8Route()`：Vehicle compId=1、managerCompid=1、deviceId=154 | `_handleSdkMode()` 接收专用 SIYI `0x0A` 查询结果：0=Locked、1=Follow、2=Fpv |
| A8 可查询条件 | `_canQueryA8()` 还要求已连接、A8 已启用、仅一辆车且仅一个云台 | 避免把唯一 SDK 端点任意配给多车/多云台 |
| 其他适用云台 | `_handleMessage()` 匹配 system/component/device 身份 | 有效 `GIMBAL_DEVICE_ATTITUDE_STATUS` 的 yaw-lock flags，并检查报文时间顺序 |

产品 A8 的 Gimbal Manager flags 仅供观察；确认后的 SDK 实际模式拥有显示权。专用查询使用 requestId，`_handleSdkMode()` 拒绝旧查询结果。`_reset()` 在连接或路由变化时推进 sessionRevision，取消请求并发出 `sessionChanged`；普通样本过期只发布 Unknown。

**Lock/Follow 命令闭环**

1. QML 点击时保存目标 bool 和 sessionRevision，`_dispatchOwnershipAction()` 等待控制权；执行时不再次用当前显示值反转目标。
2. `requestYawLock(locked, sessionRevision)` 检查会话、控制权及同 component 是否已有 PITCHYAW 命令；设置 `AwaitingAck` 和目标。
3. 将原生 pitchRate/yawRate 置 0，再调用 `GimbalController::sendRate()`。该路径发送模式与零速率，停止原生旧速率定时重发，不依赖可能变化的绝对姿态 Fact。
4. `_handleCommandResult()` 只处理本 Vehicle/component 的 PITCHYAW 结果；接受且仍持权后，产品 A8 才调用 `GimbalControlManager::setGimbalYawLock()` 发送显式 SDK 命令。
5. 进入 `AwaitingFeedback`，400 ms 后允许查询新样本。`_confirmCommand()` 要求样本时间不早于此次等待边界、仍持权且模式匹配目标，才调用 `_finishCommand()`。
6. ACK/回读超时或取消通过 `commandFailed` 通知界面，`commandPendingChanged` 结束等待。`CustomPlugin` 将协调器的 `gimbalActionRequestStarted` 接到 `cancelModeCommand()`，新的姿态动作会结束在途模式事务。

等待期间，原生 yawLock 可暂按命令目标维持发送语义，但 UI 模式仍来自已确认样本。这里的目标锁存不等于把未回读的状态显示为成功。

#### 3.6.4 Lock/Follow 切换时序（产品 A8 路由）

~~~mermaid
sequenceDiagram
    participant User as 用户
    participant UI as 顶部云台栏
    participant Mode as GimbalModeController
    participant FC as PX4 Gimbal Manager
    participant Camera as A8 SDK

    User->>UI: 点击 Lock / Follow
    UI->>UI: 保存目标与 sessionRevision
    UI->>FC: 必要时申请控制权
    FC-->>UI: 控制权状态
    UI->>Mode: requestYawLock(目标, 会话)
    Mode->>FC: 零速率与目标模式
    FC-->>Mode: 命令 ACK
    Mode->>Camera: 显式 Lock / Follow
    Mode->>Camera: 查询实际运动模式
    Camera-->>Mode: 模式回读
    Mode-->>UI: 确认目标 / 保持等待 / 报告失败
~~~

ACK 表示飞控接受了命令；实际模式匹配才是本次模式切换的完成条件。若 SDK 回读为 FPV，则按实际模式显示，不把它解释为已完成用户的 Lock/Follow 请求。

| 时间与版本约束 | 当前值/行为 |
|:---|:---|
| 模式查询周期 / 模式样本有效期 | 约 2 s / 3.5 s |
| 模式命令 ACK 等待 / 回读等待 | 各自最多 5 s |
| 回中请求超时 / 最终姿态 ACK 等待 | 10 s / 4 s |
| 回中预激活稳定时间 | 400 ms |
| `sessionRevision` | 标识车辆、云台和 SDK 路由会话；发送入口再次核对 |
| CH10 动作 revision | 手动输入、顶部操作和复位均推进；旧 ACK 不能覆盖新状态 |

这些状态与方位角计算相互独立：模式未知时等待回读，罗盘是否显示由姿态样本的有效性决定。接入新的云台时，应分别确认控制权、实际模式来源和姿态参考系。

#### 3.6.5 功能对应的文件与资源协作

| 功能环节 | 文件 / 资源组 | 在本功能中的协作关系 |
|:---|:---|:---|
| 顶部控制入口 | `UI/toolbar/GimbalIndicator.qml`；`FirmwarePlugin/CustomFirmwarePlugin.h/.cc` | 插件提供工具栏入口；QML 展示模式、控制权及等待状态，接收 Lock/Follow、Center、Tilt 90、Retract 操作。 |
| 回中/俯视动作编排 | `Gimbal/GimbalCenterCoordinator.h/.cc`；原生 Vehicle/GimbalController | 协调器把界面与 CH10 请求统一成事务，依次处理控制权、预激活、最终动作及 ACK/超时；原生接口完成 MAVLink 发送。 |
| CH10 下一动作共享 | `Gimbal/Ch10GimbalActionState.h`；`Android/UniRcChannelController.h/.cc` | 保存下一次回中/俯视及 revision；遥控输入和顶部手动动作更新同一状态，迟到结果不能覆盖更新后的选择。 |
| 实际模式与切换闭环 | `Gimbal/GimbalModeController.h/.cc`；`CustomPlugin.cc` | 插件连接当前车辆、相机与控制器；模式控制器选择反馈路由、采样实际模式并确认切换，使用 sessionRevision 隔离旧会话。 |
| A8 私有模式路由 | `Gimbal/GimbalControlManager.h/.cc`、`SiyiSdk.h/.cc`、`SiyiProtocol.h/.cc` | Manager 提供设备状态/入口，SDK 使用专用模式查询和命令反馈，Protocol 编解码；模式控制器据此更新实际状态。 |
| 控制权与原生状态 | 原生 Vehicle、Gimbal、GimbalController；`UI/toolbar/GimbalIndicator.qml` | 底层报告活动云台、控制权及命令结果，界面按可用性/等待状态决定操作入口；当前选中的右侧相机面板不替代活动 MAVLink 云台。 |
| 验证 | `custom/test/Gimbal/GimbalCenterCoordinatorTest.cc`、`GimbalModeControllerTest.cc`、`GimbalModeUiTest.py`；CoordinatorStubs/ModeStubs | 用替身模拟接管、ACK、超时和切车，Python 检查实际 QML 绑定/回调；共同覆盖事务和界面等待行为。 |

---

<a id="unirc"></a>

### 3.7 UniRC 10 Pro 蓝牙通道控制

#### 3.7.1 配置与使用

1. 在 Android 开启蓝牙，并在系统设置完成遥控器内置 SDK 蓝牙设备的配对。
2. 在 UniGCS 将“遥控 SDK 连接方式”设为蓝牙。当前目标固件已有实测的配套配置是“数传 1 = UDP、数传 2 = 关闭、SDK = 蓝牙”。
3. 在 QGC“飞行视图 → 云台相机”启用 UniRC SDK，填写已配对设备的 MAC，并允许“附近设备”权限。
4. 保持 QGC 前台运行，确认 CH1～CH16 实时值更新；CH9 先回中、CH10 先释放后再操作。

| `GimbalControl` 设置键 | 默认值 |
|---|---|
| `uniRcChannelControlEnabled` | true |
| `uniRcSdkInterface` | 0：Bluetooth，当前唯一接口 |
| `uniRcSdkBluetoothAddress` | `41:42:9E:3D:A5:D2`，按实际设备修改 |
| `uniRcZoomDirectionReversed` | false，在 A8 设置区调整 |

QGC 直接连接配置的 MAC，不承担扫描和配对。控制器仅在 Android 启动；QGC 的 CH10 命令沿 MAVLink 到 PX4，再由飞控侧配置转发到云台。

#### 3.7.2 通道工作模式

| 通道 | 触发条件 | 作用 |
|---|---|---|
| CH9 | 首次/失联后先进入 1475～1525 | 解除初始保护，允许连续变倍 |
| CH9 | <1475 / >1525 | 默认缩小 / 放大；反向设置交换方向；回中停止 |
| CH10 | 先 ≤1250 释放，再 ≥1750 按下 | 按下沿触发一次；持续按住不重复 |
| CH10 下一动作 | 初始为回中；动作确认后交替 | 回中 → 俯仰 -90° → 回中 |
| CH7/CH8 | 合理值越出 [1400,1600] | 将下一次 CH10 复位为回中，不额外发送姿态命令 |

通道合理范围为 900～2100。CH9/CH10 无效时停用动作并重新等待初始状态；合法 SDK 回包仍维持蓝牙在线状态。CH7/CH8 无效值不当作手动输入。顶部 Center、Tilt 90、Yaw 模式动作也会同步共享的下一动作状态。

#### 3.7.3 实现流程

**连接、请求、通道流**

| 阶段 | 方法链 | 实际处理 |
|:---|:---|:---|
| 生命周期入口 | `_settingsChanged()` / `_applicationStateChanged()` → `_reconcile()` | `_shouldRun()` 核对启用、前台和关闭状态，决定连接或停止 |
| 权限与设备 | `_ensureBluetoothPermission()`、`_ensureBluetoothPoweredOn()` | 处理 Android 蓝牙权限和电源状态，按配置 MAC 检查目标 |
| 建立传输 | `_connectBluetooth()` → `_socketConnected()` | Qt Bluetooth 经典 RFCOMM；连接超时由 `_connectionTimeoutExpired()` 处理 |
| 请求通道 | `_sendChannelRequest()` | 通过 `UniRcProtocol::channelDataRequestPacket()` 生成 20 Hz 请求，分三份独立写入 |
| 确认写出阶段 | `_socketBytesWritten()` → `_markChannelRequestTransmitted()` | 区分 socket 写入排队与完成本地传输，再进入首份目标帧等待 |
| 读取字节 | `_socketReadyRead()` → `_readAvailableBluetoothData()` | 把每批字节交给 `UniRcProtocol::StreamParser::append()`，处理拆包、连包和不完整帧 |
| 校验通道 | `_handleChannelPacket()` → `UniRcProtocol::parseChannelData()` | 只接受 control=0、command=0x42、payload=32；解析 16 个小端 int16 并更新 channelValues |

通道帧结构为 `55 66 + control + length(LE16) + sequence(LE16) + command + payload + CRC16/XMODEM`。字节流解析器保留未完整到达的数据，完整帧才进入通道策略。发送停止输出同样使用三份请求，频率码改为 Off。

**从 16 通道到两个动作入口**

`_handleChannelPacket()` 对合法目标帧先刷新 watchdog、保存实际通道值，再调用 `UniRcChannelPolicy::update(CH7, CH8, CH9, CH10, reversed)`。Policy 只返回 channelsValid、zoomDirection/changed、manualAttitudeInputDetected 和 ch10Pressed，不直接操作设备。

1. CH9/CH10 越界：Policy 调用 `linkLost()` / `reset()`，清除已允许动作的状态，重新要求 CH9 回中、CH10 释放；控制器停掉当前 UniRC 动作。合法 SDK 帧仍刷新连接 watchdog。
2. 有手动姿态输入：先调用协调器 `noteManualAttitudeInput()`，把下一 CH10 动作复位。
3. CH9 方向改变：`_applyZoomDirection()` → `_tryStartZoom()` → A8 `startUniRcZoom()`；回中调用停止，暂不可启动时按当前输入状态重试。
4. CH10 出现按下沿：调用 `requestNextCh10Action()`。下一动作由共享协调器确认结果决定，不由蓝牙层计数翻转。

通道数组从 0 开始，因此源码索引 6/7/8/9 对应界面的 CH7/CH8/CH9/CH10。同帧先处理手动姿态再处理 CH10，确保“手动后按键”以回中为目标。CH9 与触控 hold 区分持有者，触控释放不能停掉拨轮接管后的动作。

**断流与停止清理**

`_inputWatchdogExpired()` 按写入队列、收到字节、合法帧、目标通道帧区分阶段；`_receiveTimeoutMessage()` / `diagnosticSummary()` 输出对应运行状态。`_scheduleBluetoothFailure()` 清空动作并安排关闭/重连，`_closeBluetooth()` 停定时器、复位 parser、解除旧 socket 信号并释放连接。

失焦、禁用、配置变化和 `shutdown()` 同样进入停止路径。重新建立连接后重新等待 CH9 回中、CH10 释放；连接成功本身不会恢复上一方向。

#### 3.7.4 从蓝牙连接到动作可用

| 状态属性 | 成立条件 | 可得出的结论 |
|:---|:---|:---|
| `bluetoothConnected` | RFCOMM socket 已连接 | 蓝牙传输建立 |
| `sdkRouteActive` | 收到合法目标 `0x42` 通道帧 | 当前连接已接通 SDK 通道路由 |
| `channelInputActive` | 合法通道帧中的 CH9/CH10 值通过范围检查 | 可进入动作策略；首次仍需回中/释放保护 |
| `channelValues` | 保存最新 16 路实际值 | 设置页可以查看映射是否正确 |

首次通道帧等待窗口为 1.5 s；建立通道流后，以每份合法 `0x42` 帧刷新 350 ms watchdog。SDK 回包合法但 CH9/CH10 越界时，watchdog 仍刷新，动作状态解除，保留实际值供检查映射。

CH10 的典型操作序列：

~~~text
连接建立 → CH10 释放 → 首次按下：回中
回中命令确认 → 释放 → 再按下：俯仰 -90°
俯仰命令确认 → 释放 → 再按下：回中
~~~

在两次按键之间操作 CH7/CH8 姿态通道，会把下一次动作恢复为回中；同一份通道帧中先处理手动输入，再处理 CH10 按下沿。若动作忙碌、请求取消或确认失败，不应仅以按键次数推算下一动作，应以协调器状态为准。

从后台返回、蓝牙断流重连或运行中改变 CH9 反向设置后，先让 CH9 回中、CH10 释放，再开始新的动作序列。

#### 3.7.5 功能对应的文件与资源协作

| 功能环节 | 文件 / 资源组 | 在本功能中的协作关系 |
|:---|:---|:---|
| 配置与通道观察 | `UI/AppSettings/GimbalControlSettingsGroup.qml`；`Gimbal/GimbalControlSettings.h/.cc` 及 JSON | 页面编辑蓝牙配置并显示 CH1～CH16；Fact 保存设置，通道网格绑定控制器实时值和连接状态。 |
| Bluetooth 生命周期 | `Android/UniRcChannelController.h/.cc`；`CustomPlugin.cc` | 插件创建并连接控制器；控制器负责权限、指定 MAC 的 RFCOMM、前后台、20 Hz 请求、watchdog、断开和重连。 |
| 字节流转通道值 | `Android/UniRcProtocol.h/.cc` | 生成启停请求，缓存接收字节并处理半帧/连帧，经 CRC/帧长校验输出 16 路 int16 通道。 |
| 通道值转动作 | `Android/UniRcChannelPolicy.h/.cc`；`UniRcChannelController.h/.cc` | Policy 判断有效范围、CH9 死区/方向、CH10 按下沿及 CH7/8 手动输入；Controller 根据连接和新鲜度执行或释放动作。 |
| CH9 连续变倍 | `Gimbal/GimbalControlManager.h/.cc` | 接收遥控方向并维护 UniRC 动作持有者；回中、失联、禁用等条件释放运动，与相机栏触控输入协调。 |
| CH10 姿态交替 | `Gimbal/GimbalCenterCoordinator.h/.cc`、`Ch10GimbalActionState.h` | 请求复用共享回中/俯视事务，动作完成与手动输入更新下一动作，顶部控制也使用同一状态。 |
| 验证 | `custom/test/Android/UniRcProtocolTest.cc`；设置布局检查 | 主机验证帧拆解、通道保护、反向和 CH10 边沿/共享状态；布局脚本验证 16 通道显示，蓝牙和硬件动作需真机验收。 |

---

<a id="compass"></a>

### 3.8 飞行器航向与云台指向双罗盘

#### 3.8.1 使用方式

在“应用设置 → 飞行视图 → Instrument Panel”分别开启“飞行器航向罗盘条”和“云台指向罗盘条”。两个开关默认 false，即时生效，保存于 `FlyView/showHeadingCompassBar` 和 `FlyView/showGimbalHeadingCompassBar`。

| 罗盘 | `FlyViewCustomLayer.qml` 中的实例 | 角度来源 | 显示条件 |
|:---|:---|:---|:---|
| 飞控航向（底部中央） | `compassBarLoader` | 组件默认读取 `activeVehicle.heading.rawValue`，表示机头航向 | 开关开启、页面可见、活动 Vehicle 存在且 heading 为有限数 |
| 云台方位角（顶部中央并避让工具区） | `gimbalCompassBarLoader` | 父层把 `gimbalAzimuthProvider.absoluteYaw` 绑定给组件的 `directionDegrees` | 开关开启、页面可见、活动云台存在、车辆链路正常且 Provider 输出有效 |

两条罗盘都加载 [FlyViewCompassBar.qml](custom/src/FlightDisplay/FlyViewCompassBar.qml)，外观共用；创建、位置和显隐在 [FlyViewCustomLayer.qml](custom/src/FlightDisplay/FlyViewCustomLayer.qml)。修改共同外观会同时影响两条；需要不同样式时，通过组件属性由两个 Loader 分别传入。

两者是遥测界面，不写入视频 OSD。云台指向与右侧 A8/MT11 选择器、MT11 视频模式独立；没有有效姿态时不显示伪造角度。

#### 3.8.2 实现流程

**两条数据链分别进入同一个显示组件**

~~~text
飞控航向：原生 Vehicle.heading.rawValue
          → FlyViewCompassBar 的默认 directionDegrees → 底部罗盘

云台方位角：CustomPlugin 转交 MAVLink 姿态/航向消息
          → GimbalAzimuthProvider 缓存和选择活动云台
          → GimbalHeadingTelemetry 提供换算所需飞控航向
          → GimbalAzimuthPolicy 计算世界方位角
          → Provider.absoluteYaw → 顶部 Loader 绑定 directionDegrees → 顶部罗盘
~~~

`GimbalHeadingTelemetry.h/.cc` 用于云台参考系换算；底部罗盘直接绑定原生 Vehicle 的显示航向。下面的接收、缓存与计算步骤均属于云台方位角链路。

**云台链路的遥测接收与样本选择**

| 步骤 | 源码 / 方法 | 处理结果 |
|:---|:---|:---|
| 接收入口 | `CustomPlugin::mavlinkMessage()` → `GimbalAzimuthProvider::handleMavlinkMessage()` | 在 custom 插件入口接收遥测，按车辆区分缓存 |
| 换算所需飞控航向 | Provider 的 `_handleHeadingTelemetry()` → `GimbalHeadingTelemetry::update()` | 接收物理 ATTITUDE/ATTITUDE_QUATERNION yaw，不使用 UI 取整值或显示偏移 |
| 航向选择 | `GimbalHeadingTelemetry::heading(nowMs)` | 最新姿态样本优先，同时间取四元数；高延迟航向只作后备；各来源独立 2 s 过期 |
| 云台缓存 | `_sampleKey(component, device)` 与 CachedSample | 保存四元数、frame flags、delta_yaw 可用性、设备时间与本地接收时间 |
| 重新计算 | `_refreshVehicleSamples()` → `_recalculateSample()` | 云台或航向更新后补齐 Input，调用 `GimbalAzimuthPolicy::calculate()` |
| 当前对象输出 | `_activeVehicleChanged()` / `_bindActiveGimbal()` → `_publishActiveSample()` | 只选择当前活动 Vehicle/云台对应样本，不能用其他设备最近收到的数据 |
| 发布到界面 | `_publishResult()` → `attitudeChanged` | 更新 valid、absoluteYaw、usingDeltaYaw、referenceSource；顶部栏和罗盘共用 |

Provider 使用单调时钟和过期检查定时器，云台姿态样本有效期为 2 s。非法四元数、冲突参考系、缺少必要世界参考时计算失败；重复/乱序数据不能刷新旧样本寿命。失联或对象切换会重新选择/清理有效输出。底部飞控罗盘没有另外设置这套 2 s 过期检查，也未在 Loader 中加入通信丢失条件；若原生 Vehicle 仍保留有限 heading，底部条可能继续显示最后的值。

**世界方位角计算分支**

`GimbalAzimuthPolicy::calculate()` 先检查两个 frame 标志不能同时成立，并归一化有效四元数，然后按下表选择唯一分支：

| 输入约定 | 计算方式 |
|:---|:---|
| 明确 Earth frame | 从四元数取世界 yaw，调用 `wrap180()` |
| 明确 Vehicle frame 且 delta_yaw 有效 | 将 delta_yaw 对应的世界参考旋转与云台四元数组合，再取 yaw |
| 明确 Vehicle frame，delta_yaw 不可用 | 使用新鲜飞控航向完成参考旋转；航向不可用则结果无效 |
| 没有明确 frame 的当前产品接入 | Provider 固定配置 legacy VehicleHeading 与反向约定，计算 `wrap180(heading - feedbackYaw)` |

legacy 分支的安装方向是固定产品输入约定，锁定/跟随均使用该约定；不得用“哪个角度变化小”动态猜测参考系。MAVLink 扩展字段解码为零也不等于字段实际存在，Provider 分开维护支持与可用标记。

**QML 绘制与布局**

`FlyViewCustomLayer.qml` 创建两个 Loader：底部保留组件的 Vehicle 航向默认绑定；顶部在 `onLoaded` 中用 `Qt.binding()` 覆盖 `directionDegrees`，并设置 `indicatorPrefix="Gimbal"`。顶部 Loader 额外检查活动云台、Provider.valid 和通信状态；无效时卸载该显示项。

样式入口集中在 `FlyViewCompassBar.qml`：`compassBar` 定义条背景，`headingIndicator/headingLabel` 定义中心角度框，`compassArrowIndicator` 加载 `FlightMap/Images/compassPointer.svg`；`implicitWidth`、`_barHeight`、`_pointerSize` 控制尺寸。两条的位置、边距和可用宽度由对应 Loader 的 anchors/width/x 决定。

`FlyViewCompassBar.qml` 的 `_normalize()` 转为 [0°, 360°)，`_directionLabel()` 生成八方位字母，`_indicatorText()` 生成中心角度。Repeater 只保留中心附近 11 个、间隔 45° 的标签：

~~~text
标签横坐标 = 条宽 / 2 + (标签未环绕角 - 当前航向) × 条宽 / 360 - 标签宽 / 2
~~~

固定指针与滚动刻度分离；顶部云台条考虑右侧相机栏预留宽度，底部条按整个 Fly View 可用宽度布局。`QGCToolInsets` 只增加实际可见控件占用的中央边距。

#### 3.8.3 角度含义与计算示例

`GimbalAzimuthPolicy` 输出的角度归一化到 [-180°, 180°)，绘制组件再按方向刻度表达。跨越 ±180°/0° 是角度环绕，不能按两个显示数值的普通减法判断物理转动量。

以当前无显式 frame 的产品反馈为例：

~~~text
飞控 heading = 100°，云台反馈 yaw = 20°
世界方位角 = wrap(100° - 20°) = 80°

基座转到 heading = 130°，反馈 yaw 同时变为 50°
世界方位角 = wrap(130° - 50°) = 80°
~~~

这是固定接入约定下的计算示例。接入另一种有显式 Earth/Vehicle frame 的设备时，按其标志和 delta_yaw 分支计算，不套用上述减法。Provider 只采用当前活动云台的样本；切换车辆/云台后必须重新匹配对象身份和有效数据。

布局由 `FlyViewCustomLayer` 提供上下 inset：底部航向占底部中央，顶部云台方向结合左右工具区和相机栏预留宽度。关闭某条罗盘时释放相应空间，不保留空白占位。

#### 3.8.4 功能对应的文件与资源协作

| 功能环节 | 文件 / 资源组 | 在本功能中的协作关系 |
|:---|:---|:---|
| 显隐配置 | `Settings/FlyViewCustomSettings.h/.cc`、`FlyViewCustom.SettingsGroup.json`；`UI/AppSettings/FlyViewSettings.qml` | 设置页编辑两条罗盘开关，Fact 写入 FlyView 分组；覆盖层根据开关及有效数据控制显示。 |
| MAVLink 接收与对象接线 | `CustomPlugin.cc`；`Gimbal/GimbalAzimuthProvider.h/.cc` | 插件转交消息和活动 Vehicle；Provider 按车辆/component/device 缓存姿态并选择活动云台，拒绝过期或不匹配数据。 |
| 航向与参考系换算 | `Gimbal/GimbalHeadingTelemetry.h/.cc`、`GimbalAzimuthPolicy.h/.cc` | Telemetry 保存未取整飞控航向并选择有效来源；Policy 检查四元数，按 Earth/Vehicle/legacy 规则转换世界方位角。 |
| 双罗盘创建与位置 | `FlightDisplay/FlyViewCustomLayer.qml`、`FlyView.qml` | 飞行页承载覆盖层；底部 Loader 使用组件的 Vehicle 航向默认绑定，顶部显式绑定 Provider.absoluteYaw；两实例分别处理显隐、上下位置和避让。 |
| 航向读取、刻度与指针绘制 | `FlightDisplay/FlyViewCompassBar.qml`；`FlightMap/Images/compassPointer.svg` | 默认 directionDegrees 读取 Vehicle.heading.rawValue，顶部实例覆盖此输入；组件绘制方位标签、角度框和 SVG 指针，云台参考系换算由后端完成。 |
| 验证 | `custom/test/Gimbal/GimbalAzimuthPolicyTest.cc`、`GimbalHeadingTelemetryTest.cc`、`GimbalAzimuthProviderTest.cc`；AzimuthStubs | 分别检查数学换算、来源/时序和活动对象匹配；设备转动、锁定/跟随和失联表现按真机矩阵核对。 |

---

<a id="power"></a>

### 3.9 电源、Fuel 与发电机母线告警

#### 3.9.1 电源显示与使用

顶部电池区域按 `vehicle.batteries` 显示各电池的电压和功率，功率由 `voltage × current` 计算。点击打开电源状态、电压、功率、电流和累计耗电详情；展开后可编辑低压动作及阈值。

| 飞控参数 | 用途 |
|---|---|
| `UAVCAN_POW_LOW` | 低电压阈值；custom 元数据默认 47.4 V |
| `UAVCAN_POW_CRITI` | 严重低压阈值；元数据默认 45.6 V |
| `UAVCAN_POW_EMERG` | 紧急低压阈值；元数据默认 44.4 V |
| `COM_LOW_BAT_ACT` | 飞控低电量处置动作 |

阈值必须满足 `LOW > CRITI > EMERG > 0`。这里的默认值用于参数元数据，**不会在启动时覆盖飞控实际参数**；三个阈值标记为需要飞控重启。

电压/阈值有效时，界面按低于哪个阈值计算图标状态；FAILED、UNHEALTHY、CHARGING 保留飞控上报状态。参数缺失或阈值无效时回退上报状态，编辑区只在所需参数齐全时出现。飞控低压处置由飞控执行。

**代码如何生成电源显示**

| 入口 / 方法 | 实现 |
|:---|:---|
| `BatteryIndicator.qml` 中 Repeater | 遍历 activeVehicle.batteries，每个对象生成一个 batteryVisual |
| `_parameterFact(name)` | 参数就绪后通过 FactPanelController 取原生飞控 Fact；缺失返回 null |
| `_formatPower(battery)` | 读取 voltage/current 的 rawValue，相乘并取整显示 W；无值显示 n/a |
| `_batteryState(battery)` | 先保留 FAILED/UNHEALTHY/CHARGING；其余按 EMERG → CRITI → LOW 的顺序检查严格低于阈值 |
| `_stateText()`、`_stateVisual()` | 将计算状态对应到枚举文字、图标与颜色 |
| `CustomFirmwarePlugin::_getMetaDataForFact()` | 在原生参数元数据基础上补单位、默认值、步进、小数位及重启标记 |

编辑控件绑定的是飞控 Parameter Fact，写入沿原生参数管理器进行。这里只读取遥测并计算显示状态；改变告警颜色不会改写电池原始 chargeState，也不会代替飞控执行 COM_LOW_BAT_ACT。

#### 3.9.2 Fuel 使用与流程

1. 原生 Vehicle 收到 `FUEL_STATUS`，更新 `fuelStatus`。
2. 有遥测时在电池后显示 Fuel 百分比；>50% 绿色，>25% 橙色，其余红色。
3. 点击打开剩余量、最大量、已消耗、流量及温度；液体/气体单位跟随 Fuel Fact。没有遥测时隐藏指示器。

**具体接线**：`CustomFirmwarePlugin::toolIndicators()` 把 Fuel URL 插入 Battery 后。工具栏的 `_hasFuel` 绑定 `fuelStatus.telemetryAvailable`，`getFuelColor()` 和 `getFuelText()` 生成颜色/百分比；点击 `mainWindow.showIndicatorDrawer()` 创建 `FuelStatusIndicatorPage`，详情继续绑定同一组 Fact 的 valueString/units。燃料消息解析沿用原生 Vehicle，本模块不另建协议接收器。

#### 3.9.3 母线告警工作模式

`GeneratorBusVoltageAlert` 读取 `vehicle.generator.busVoltage` 和飞控参数 `COM_GEN_V_LOW`、`COM_GEN_LOW_T`：

1. 电压连续低于阈值达到确认时间，显示低压告警。
2. 电压连续高于同一阈值达到确认时间，解除告警。
3. 等于阈值时不切换，并取消本次待确认计时；确认时间为 0 时即时切换。
4. 参数/遥测无效或失联时取消计时并隐藏；切换车辆时重置状态。

该告警使用飞控下发的阈值与时间，界面本身不实施飞行处置。

**计时状态机的函数分工**

- `_updateWarningState()` 根据已确认的 `_warningActive` 决定待进入“告警”还是“正常”；只有目标方向改变时才启动 transitionTimer，同方向连续遥测不会不断重启计时。
- `_resetPendingTransition()` 停止定时器并清空 `_pendingTransition`，用于条件不再成立或参数改变。
- `_completePendingTransition()` 在定时器结束时再次检查遥测与阈值条件，成立才改变 `_warningActive`。
- `onVehicleChanged` 清空已确认状态；普通遥测无效/失联会取消待转换，`visible = _warningActive && _telemetryValid` 使告警隐藏。该分支保留已确认状态，恢复后按当前输入重新评估。
- `FlyViewCustomLayer.qml` 实例化此组件并传入当前 Vehicle；所有计时与显隐在组件内部完成。

#### 3.9.4 母线告警的时序示例

以下仅演示状态机：假设 `COM_GEN_V_LOW=48 V`、`COM_GEN_LOW_T=3 s`，实际运行使用飞控读取值。

| 电压变化 | 计时/界面结果 |
|:---|:---|
| 低于 48 V 持续 2 s 后恢复 | 不触发告警，未达到完整确认时间 |
| 连续低于 48 V 满 3 s | 激活告警 |
| 告警中，高于 48 V 仅 1 s 后又降低 | 保持告警，解除计时取消 |
| 连续高于 48 V 满 3 s | 解除告警 |
| 计时中回到恰好 48 V | 取消当前转换计时，保持已确认状态 |
| 更改阈值/确认时间 | 取消原计时，用新参数重新评估 |

电池多级阈值与发电机母线告警是两套状态来源。前者使用 `batteries` 和 UAVCAN 参数决定电池显示；后者使用 `generator.busVoltage` 和 COM_GEN 参数控制独立提示，维护时应分别验证。

Fuel 详情只展示已存在且有效的字段。百分比颜色用于概览，流量、剩余量、消耗量等继续按原生 Fact 的数值与单位显示。

#### 3.9.5 功能对应的文件与资源协作

| 功能环节 | 文件 / 资源组 | 在本功能中的协作关系 |
|:---|:---|:---|
| 电池概览、详情与阈值编辑 | `QmlControls/BatteryIndicator.qml`；原生 Vehicle 电池 Fact/参数 | QML 读取多电池电压/功率等遥测，计算显示档位并组织详情和低压参数编辑；参数写入仍通过原生 Fact 体系。 |
| 阈值元数据与入口注册 | `FirmwarePlugin/CustomFirmwarePlugin.h/.cc` | 为产品 UAVCAN 阈值补充元数据，并组织工具栏顺序；给 Battery/Fuel 等界面提供产品接入点。 |
| Fuel 顶部概览 | `UI/toolbar/FuelStatusIndicator.qml`；`UI/toolbar/Images/FuelIcon.svg` | 指示器绑定燃料剩余比例，决定分档颜色；SVG 提供图形，点击后打开 Fuel 详情组件。 |
| Fuel 详情 | `QmlControls/FuelStatusIndicatorPage.qml`；原生燃料 Fact | 按有效性显示剩余量、最大量、消耗量、流量和温度，供顶部概览展开查看。 |
| 发电机母线告警判断与提示 | `FlightDisplay/GeneratorBusVoltageAlert.qml`；飞控 COM_GEN 参数与母线遥测 | 该 QML 同时实现阈值/持续时间判断和提示条外观，参数与实时电压决定何时进入、维持及解除告警。 |
| 告警在飞行页的位置 | `FlightDisplay/FlyViewCustomLayer.qml`、`FlyView.qml` | 总页创建覆盖层，覆盖层接入活动 Vehicle 并放置告警条，处理与其他叠加元素的空间关系。 |
| 资源覆盖与文字 | `custom/custom.qrc`；`custom/translations/custom_zh_CN.ts` | QRC 使产品电池/工具栏组件替换或接入原生页面，并打包 Fuel 图标；翻译覆盖用户可见标签和提示。 |

---

<a id="radar"></a>

### 3.10 Proximity Radar 距离提示

#### 3.10.1 功能与使用

GPS 指示器旁显示雷达图标，覆盖前、前右、右、后右、后、后左、左、前左、上、下十个方向。点击查看各方向已有距离；任一有效距离小于 5.0 m 时，图标红色闪烁。无任何有效距离时不显示。当前为提示功能，5 m 阈值在 QML 中定义。

#### 3.10.2 实现流程

1. `CustomFirmwarePlugin::toolIndicators()` 将 custom ProximityRadarIndicator 的资源 URL 插入 GPS 指示器后，建立工具栏入口。
2. [ProximityRadarIndicator.qml](custom/src/UI/toolbar/ProximityRadarIndicator.qml) 内部 `radarModel.entries` 把 `activeVehicle.distanceSensors` 的十个 Fact 与方向文字配对，映射见 3.10.3。
3. `factAvailable(fact)` 判断 Fact 是否存在且值非 NaN；`_hasTelemetry()` 遍历 entries，只要一个可用即令 showIndicator 为 true。
4. `factInAlert(fact)` 判断小于 alertDistanceMeters；`_hasProximityAlert()` 对全部方向做“任一成立”的聚合，驱动图标颜色和动画。
5. `SequentialAnimation on opacity` 在告警中循环，透明度 1.0 → 0.25 → 1.0，两段各 400 ms；告警结束后 `onRunningChanged` 恢复 opacity=1。
6. MouseArea 点击调用 `mainWindow.showIndicatorDrawer()`，`ProximityRadarIndicatorPage.qml` 使用同一 radarModel 列出有效方向及数值，告警方向沿用相同判断。

整个 custom 模块是 Fact 上的 QML 计算与显示，没有新增 C++ 雷达解析器或避障控制器。新增方向时同时扩展 entries、详情展示和下表；修改阈值时改唯一的 alertDistanceMeters，图标和详情继续共用。

#### 3.10.3 方向映射与有效性

| 方位组 | 方向 → 原生 Fact |
|:---|:---|
| 前半区 | 前 → `rotationNone`；前右 → `rotationYaw45`；前左 → `rotationYaw315` |
| 左右 | 右 → `rotationYaw90`；左 → `rotationYaw270` |
| 后半区 | 后 → `rotationYaw180`；后右 → `rotationYaw135`；后左 → `rotationYaw225` |
| 垂直 | 上 → `rotationPitch90`；下 → `rotationPitch270` |

当前有效性条件是 Fact 存在且数值非 NaN，告警判断为严格小于 5 m，恰好 5 m 不告警。多个方向同时触发时，图标保持统一告警，详情列出各有效方向。该 QML 不另设样本超时计时器，数据失效与清除依赖原生距离 Fact 的生命周期。

#### 3.10.4 功能对应的文件与资源协作

| 功能环节 | 文件 / 资源组 | 在本功能中的协作关系 |
|:---|:---|:---|
| 工具栏入口 | `FirmwarePlugin/CustomFirmwarePlugin.h/.cc` | toolIndicators() 把产品雷达资源 URL 插入 GPS 后方，使飞行页工具栏加载对应 QML。 |
| 方向模型与报警显示 | `UI/toolbar/ProximityRadarIndicator.qml`；原生距离传感器 Fact | 指示器组织十方向 radarModel、过滤无效值并计算小于 5 m 的聚合状态，控制顶部图标与红色闪烁。 |
| 距离详情 | `QmlControls/ProximityRadarIndicatorPage.qml` | 接收指示器传入的 radarModel，将同一组方向和值排成详情页，避免概览和详情分别维护数据。 |
| QML/文字资源 | `custom/custom.qrc`、`custom/CMakeLists.txt`；`custom/translations/custom_zh_CN.ts` | QRC 接入工具栏组件，CMake 的 Custom.Widgets 模块提供详情页，翻译提供方向与提示文字；传感器方向映射仍在指示器模型中维护。 |

---

<a id="comms"></a>

### 3.11 默认通信链路与 Android USB

#### 3.11.1 默认 UDP 链路

保存的通信链路数量为 0 时，启动阶段安装一条标准 QGC 配置：

| 名称 | 类型 | 本地端口 | 目标服务器 | 自动连接 | 高延迟 |
|---|---|---|---|---|---|
| `local` | UDP | 14550 | `192.168.144.20:19856` | 关闭 | 关闭 |

在“应用设置 → 通信链路”选择该项并连接，或按部署环境编辑。只要已有至少一条保存的链路，安装器就保留全部用户配置；删除所有链路后，下次启动会再次补齐默认项。原生动态 UDP AutoConnect 的缺省值为 false，用户仍可开启；需避免与手动链路同时占用同一本地端口。

**默认配置的写入步骤**

`CustomPlugin::init()` 在 LinkManager 读取持久化列表前调用 `DefaultCommunicationLinkInstaller::ensureInstalled()`：

1. 用 `LinkConfiguration::settingsRoot()` 取得原生配置分组，读取 `count`，缺省按 0 处理。
2. 转整数失败或 count 非 0 时直接返回；不按名称查重、合并或修改已有链路。
3. 仅空列表时调用文件内 `writeDefaultLink()`：清理不活动的旧 Link0 槽，写入 name/type/auto/high_latency/port/hostCount/host0/port0。
4. 将 count 写为 1，调用 `QSettings::sync()` 并检查结果；后续由原生 LinkManager 创建和持久化链路。
5. `CustomPlugin::adjustSettingMetaData()` 单独把原生 autoConnectUDP 的未保存默认值设为 false，不修改用户已经保存的选择。

#### 3.11.2 Android USB 使用与流程

使用支持数据的 USB/OTG 连接，将遥控器端口设为 USB Host，并在系统授权框允许访问飞控串口。匹配到飞控后由 QGC 原生 AutoConnect 建立 MAVLink，也可按原生通信界面管理串口。

[QGCUsbSerialManager.java](custom/android/src/org/mavlink/qgroundcontrol/QGCUsbSerialManager.java) 通过同名 Java overlay 接入原生 JNI 调用，保留 Qt 所需接口：

| 阶段 | Java 入口 / 方法 | 实现内容 |
|:---|:---|:---|
| 初始化 | `initialize()` | 获取应用 Context 和 UsbManager，建立权限 PendingIntent 并注册广播 |
| 枚举与匹配 | `updateCurrentDriversLocked()` → `probeCurrentDrivers()` | 先用默认 UsbSerialProber；`hasCdcAcmInterfaces()` 为标准 CDC communication+data 接口提供兜底 |
| 权限请求 | `requestUsbPermission()` → `handleUsbPermission()` | 分开记录请求中/拒绝设备，授权结果触发后续枚举更新 |
| 输出串口列表 | `availableDevicesInfo()` → `formatDeviceInfo()` | 只返回匹配、已授权且有端口的设备；按 deviceId 去重，并整理 Qt 侧需要的描述字段 |
| 打开资源 | `open(deviceName, classPtr)` → `openDriver()` | 重查权限与驱动，拒绝重复打开；当前取驱动第一个串口，建立 UsbDeviceResources |
| 建立 I/O | `createIoManager()`、`startIoManager()` | SerialInputOutputManager 与 QGCSerialListener 负责异步数据，`nativeDeviceNewData` 将字节交回 Qt |
| 串口操作 | `setParameters()`、`read()`、`write()`、`writeAsync()` | 提供波特率/数据位/停止位/校验、读写及控制线接口；参数由 Qt 侧实际配置 |
| 普通关闭 | `close()` → `releaseDeviceResources()` | 停止 I/O、关闭端口/连接、清除已打开资源；保留可再次打开的发现信息 |
| 拔插/销毁 | `handleUsbDeviceAttached()`、`handleUsbDeviceDetached()`、`cleanup()` | 更新驱动集合、释放脱离设备，销毁时注销广播并清理管理器 |

Java 内的 `drivers` 保存“发现的驱动”，`deviceResourcesMap` 保存“已打开资源”，`pendingPermissionRequests` 保存“授权事务”；这三种状态分开管理。Qt 的原生 AndroidSerial/QGCSerialPortInfo 枚举列表，USBBoardInfo 分类后由 LinkManager 连接，MAVLink heartbeat 才形成 Vehicle。

打开任一步失败都会走资源释放并向 native 报错。设备未被 USB Host 枚举时先核对端口模式和数据连接；已枚举但未匹配的设备，应依据实际 USB 接口扩展驱动匹配。

#### 3.11.3 连接状态与配置生效

| 观察阶段 | 说明 | 下一阶段所需条件 |
|:---|:---|:---|
| 已有 `local` 配置 | 本地 QSettings 中存在连接参数 | 用户启动连接，目标网络可达 |
| UDP 链路已打开 | 本地端口与目标地址已交给 LinkManager | 收到有效 MAVLink heartbeat |
| Android 已枚举 USB | USB Host 看到了物理设备 | 驱动匹配与访问授权 |
| USB 串口可枚举 | 已匹配、已授权，进入 Qt 串口列表 | 串口打开成功，参数和数据接线正确 |
| Vehicle 已出现 | 已识别飞控心跳 | 等待参数下载及对应遥测/云台发现 |

默认链路安装写入原生 LinkConfiguration 的 QSettings 结构，随后由 LinkManager 负责连接和保存。列表数量非法时安装器不重写配置；已有列表不按名称合并或去重。首次部署应核对界面中的实际值，后续升级保留用户编辑。

USB 的设备发现、已打开 resource 和 I/O manager 分开维护：普通关闭后允许保留已发现驱动供再次打开；拔出、清理和 Activity 销毁统一释放已打开资源并注销相应生命周期对象。端口存在与 MAVLink 连通是两个阶段，串口能打开并不意味着已经识别到飞控。

#### 3.11.4 功能对应的文件与资源协作

| 功能环节 | 文件 / 资源组 | 在本功能中的协作关系 |
|:---|:---|:---|
| 产品启动与默认值 | `CustomPlugin.cc` | 在原生链路配置加载前调用安装器，并提供产品 UDP AutoConnect 缺省值，保证默认项在首次加载时可见。 |
| 首次 UDP 配置安装 | `Comms/DefaultCommunicationLinkInstaller.h/.cc`；QSettings 链路分组 | 读取活动链路数量；仅空列表时清理目标旧槽并写入 local UDP 参数，已有用户链路交给原生系统继续使用。 |
| UDP 连接与 Vehicle 建立 | 原生 `src/Comms/` 的 LinkManager/UDPLink 与 MAVLink/Vehicle 流程 | 安装器只写配置；实际创建链路、收发报文以及由 heartbeat 识别飞行器都走原生 QGC。 |
| Android USB 枚举、授权与 I/O | `custom/android/src/org/mavlink/qgroundcontrol/QGCUsbSerialManager.java` | Java 枚举驱动/设备、管理授权事务和已打开资源，提供串口读写及控制线，在拔插和销毁时清理。 |
| Java 到 Qt 串口衔接 | 原生 AndroidSerial、QGCSerialPortInfo、USBBoardInfo 和 LinkManager | 原生层从 Java 获取端口、识别设备并发起串口连接；串口打开后仍需 MAVLink 消息形成 Vehicle。 |
| Android 构建接入 | `custom/CMakeLists.txt` | 将产品 Java 文件合并到 Android 构建模板，保证运行时使用 custom 的串口实现；该文件不由 QRC 加载。 |

---

<a id="px4"></a>

### 3.12 PX4 飞控与设备设置定制

#### 3.12.1 功能与工作模式

- 产品 Factory 声明支持 PX4、多旋翼；构建关闭 APM 插件/方言和原生 PX4 Factory。
- 常规飞行模式选择列表开放 Pause、Return、Mission；其他模式仍可识别和显示。该列表限制与起飞、降落等 GuidedAction 入口分别管理。
- 普通模式的设备组件页保留 Safety；开启 QGC 高级模式后增加 Airframe、Sensors、Radio、Flight Modes、Power、Actuators/Motors 和 Tuning。
- 移除顶部 RC RSSI，保留原生指示器并加入 Fuel、Radar；声明云台支持 pitch/yaw、不支持 roll。
- 为电源模块提供 UAVCAN 参数元数据；视频时钟诊断只观察发送路径中的 `SYSTEM_TIME`，不修改或额外发送时钟报文。

#### 3.12.2 使用与实现流程

| 环节 | 方法与入口 | 当前实现 |
|:---|:---|:---|
| Factory 注册 | 全局 `CustomFirmwarePluginFactoryImp` | custom Factory 随产品构建参与插件选择，原生 PX4 Factory 由构建开关关闭 |
| 能力声明 | `supportedFirmwareClasses()`、`supportedVehicleClasses()` | 对外声明 PX4、多旋翼 |
| 插件匹配 | `firmwarePluginForAutopilot()` | 实际匹配条件是 autopilotType=PX4；当前函数不再按 vehicleType 二次筛选。首次创建 CustomFirmwarePlugin，之后复用 |
| 车辆设置对象 | `CustomFirmwarePlugin::autopilotPlugin()` | 每辆 Vehicle 创建自己的 CustomAutoPilotPlugin，并由 Vehicle 持有 |
| 模式列表 | 构造函数、`updateAvailableFlightModes()` | 基于 PX4 mode 编号设置机型适用性与 canBeSet，再调用 `_updateFlightModeList()`；Pause/Return/Mission 可从常规列表设置 |
| 设备组件页 | `CustomAutoPilotPlugin::vehicleComponents()` | 检查 Vehicle、parametersReady 和参数版本；按高级模式开关创建组件，并逐一调用 `setupTriggerSignals()` |
| 高级模式变化 | `showAdvancedUIChanged` → `_advancedChanged()` | 清空列表缓存，发 `vehicleComponentsChanged`；下次查询按当前开关重新生成 |
| 工具栏 | `toolIndicators()` | 首次从原生列表构造缓存，移除 RC RSSI，在 Battery 后插 Fuel、GPS 后插 Radar；找不到参照项时追加 |
| 云台能力 | `hasGimbal()` | 返回具备云台，pitch/yaw=true、roll=false |
| 参数与诊断 | `_getMetaDataForFact()`、`adjustOutgoingMavlinkMessageThreadSafe()` | 补 UAVCAN 元数据；发送线程仅在诊断启用时观察 SYSTEM_TIME 内容 |

连接 PX4 后按“Factory 匹配 → FirmwarePlugin 能力 → AutoPilotPlugin 组件 → QML 页面”追踪即可定位设置功能。需要新增页面时改 AutoPilotPlugin；需要修改飞行模式可选性或工具栏时改 FirmwarePlugin；设备/固件匹配则改 Factory。

#### 3.12.3 设备页生成条件

设备组件列表只在 Vehicle 存在、参数准备完成且参数版本未被判定不兼容时生成。高级模式切换触发 `vehicleComponentsChanged`，重新查询时按当前开关构建列表；Actuators 可用时使用其页面，否则采用 Motors。

这里的“高级模式”控制设置页的可见范围，与飞行器的 Pause/Return/Mission 飞行模式不同。扩展一项新能力时，先确定它属于 Factory 匹配、FirmwarePlugin 车辆行为，还是 AutoPilotPlugin 设置页，再修改对应文件。

#### 3.12.4 功能对应的文件与资源协作

| 功能环节 | 文件 / 资源组 | 在本功能中的协作关系 |
|:---|:---|:---|
| 产品插件编入 | `custom/cmake/CustomOverrides.cmake`、`custom/CMakeLists.txt` | 关闭被产品接管的原生 Factory 等目标，编入 custom 插件和需要复用的原生实现。 |
| 固件到插件匹配 | `FirmwarePlugin/CustomFirmwarePluginFactory.h/.cc` | 根据固件/机型支持范围提供 CustomFirmwarePlugin，使后续车辆行为和设备页采用本分支实现。 |
| 车辆行为与界面能力 | `FirmwarePlugin/CustomFirmwarePlugin.h/.cc` | 定义常规飞行模式、工具栏、云台能力与参数元数据，创建产品 AutoPilotPlugin；具体工具栏 QML 按所属功能加载。 |
| 设备页生成 | `AutoPilotPlugin/CustomAutoPilotPlugin.h/.cc`；原生 PX4 设备组件 | 参数就绪后依据固件能力、普通/高级状态创建组件列表；高级开关变化通知界面刷新，组件内部继续复用原生页面。 |
| 产品页面资源 | `custom/custom.qrc`；`UI/toolbar/` 与 `QmlControls/` 中对应 QML | 插件给出资源入口，QRC 提供实际 custom 页面/图标；电源、Fuel、雷达和云台控件各自处理数据绑定与外观。 |

---

<a id="settings"></a>

### 3.13 设置体系、界面适配与翻译

#### 3.13.1 设置入口与持久化

Fact 是 QGC 的设置/参数对象：C++ 管理值与元数据，QML 绑定显示和编辑，QSettings 保存本地设置。飞控参数通过 ParameterManager 读写，不作为本机设置保存。

| 分组/键 | 内容 | 界面入口 |
|---|---|---|
| `Viewer3D` | 地图模式、文件与配准 | 飞行视图 → 3D View |
| `FlyView` 的两个 custom 键 | 双罗盘显隐 | 飞行视图 → Instrument Panel |
| `GimbalControl` | A8/MT11、UniRC、本地媒体和视频策略 | 飞行视图 → 云台相机；Video |
| 原生 `Video` 分组中的 `secondaryRtspUrl` | 由 VideoCustomSettings 提供的第二路 URL | Video |
| 原生 `Video` | 主视频、录制格式、容量等 | Video |
| 根级 `appFontPointSize` | 应用字号/界面缩放 | General |

Android 未保存字号时，custom 元数据默认设为 12 pt；目标遥控器采用 14 pt 平台基准时显示约 86%。General 的 +/- 仍按 1 pt 调整，已有用户值在升级和重启后保留；非 Android 使用原生默认值。

#### 3.13.2 布局工作方式

Fly View 设置采用可滚动、字体尺度决定最大宽度的居中布局；窄屏自动收缩，行组件按空间调整标签和控件。章节顺序为原有飞行设置、Instrument Panel、云台相机、Viewer3D。云台组不依赖设备在线状态，Android UniRC 区显示自适应的 CH1～CH16 网格。Video 页面沿用原生自适应设置布局。

#### 3.13.3 实现与翻译流程

**Fact 定义、实例和持久化**

| 层次 | 具体实现方法 | 产生的能力 |
|:---|:---|:---|
| JSON 元数据 | `*.SettingsGroup.json` | 定义 name/type/default/min/max/枚举等，作为本地 Fact 的类型与校验来源 |
| 头文件声明 | `DEFINE_SETTING_NAME_GROUP()`、`DEFINE_SETTINGFACT(name)` | 提供分组信息、Fact getter 和 QML 可访问属性 |
| 实现文件 | `DECLARE_SETTINGGROUP(...)`、`DECLARE_SETTINGSFACT(...)` | 连接 SettingsGroup 的 Fact 创建/持久化机制；明确实际 QSettings 分组 |
| QML 类型 | `qmlRegisterUncreatableType()` | 暴露属性类型，实际实例由 C++ 创建，QML 引用已有对象 |
| 插件实例 | `CustomPlugin::_ensure...Settings()` 与 Q_PROPERTY getter | 首次创建并持有设置对象，QML 通过 corePlugin 访问 |
| 界面编辑 | Fact 控件绑定 `fact` / `rawValue` | 用户操作进入 Fact 原生校验和持久化路径 |
| 运行时监听 | Manager 构造函数的 `Fact::rawValueChanged` 连接 | 即时调用配置处理；只在启动时读取的设置按页面提示重启生效 |

例如 `DECLARE_SETTINGGROUP(VideoCustom, "Video")` 使用独立 VideoCustom 元数据，但实际值写入 Video；`DECLARE_SETTINGGROUP(FlyViewCustom, "FlyView")` 同理。新增键时必须同时核对 JSON 的 name、头文件 DEFINE、实现 DECLARE 与 QML 引用。

当前设置构造函数还维护**升级保留语义**：VideoCustomSettings 只在新键不存在时读取受支持的旧 URL；GimbalControlSettings 用版本标记处理已知默认端点/空蓝牙地址。已存在的当前键和用户自定义端点按代码条件保留。这属于本版启动行为，新增默认值时不能简单覆盖所有保存值。

**QML 文件如何真正被加载**

1. 修改原生页面：在 `custom.qrc` 的 `/Custom/qml` 前缀下提供与原生对应的 alias，`CustomPlugin::createQmlApplicationEngine()` 安装 URL interceptor，命中时加载 custom 页面。
2. 新增独立组件：`custom/CMakeLists.txt` 使用 `qt_add_library()` / `qt_add_qml_module()` 建立 Custom.Widgets、Custom.FlightDisplay；调用方使用相应 import。
3. 本地设置布局组件经 QRC 路径参与页面加载，不能只把 QML 文件放进磁盘目录；新增文件后检查资源或 QML 模块清单。
4. 控件的 `Fact` 绑定来自 Settings；双视频、相机、罗盘的业务状态来自对应 Manager/Provider，设置页不另存一套业务状态。

**布局如何随宽度变化**

`FlyViewSettingsPage` 决定滚动、居中和最大宽度，`FlyViewSettingsSection` 提供组内容，`FlyViewSettingsRow` 是 GridLayout：`stacked = width < defaultFontPixelWidth × 66` 时改成单列，否则标签与控件两列。控件通过 contentItem alias 放入行内 RowLayout；FactTextField/Switch/ComboBox 保留原生 Fact 绑定并适配布局。

字号默认通过 `CustomPlugin::adjustSettingMetaData()` 注入，General 页面仍操作原生 appFontPointSize。已有保存值优先于 metadata default，所以升级后不强制恢复 12 pt。

**翻译生成与加载**

- C++ `tr()`、QML `qsTr()` 及设置元数据文本由 `custom/translations/custom-lupdate.sh` 提取；更新 `custom_zh_CN.ts`。
- `custom.ts` 作为英文源模板；构建将 locale TS 编译为 QM 并放入 `:/i18n`。
- `CustomPlugin::init()` 用当前 locale 加载 `custom_` 翻译并 installTranslator，插件释放时 removeTranslator。
- 验证文本时同时检查 context/source、枚举数量与分隔符；布局验证使用 `custom/test/UI/FlyViewSettingsLayout/run.py` 加载实际设置资源。

新增或修改设置完成后，应能沿“JSON → Settings Fact → corePlugin → QML → Manager”找到完整接线。第 3.13.4 给出具体实例。

#### 3.13.4 一个设置从界面到运行时的完整路径

以“显示云台指向罗盘条”为例：

~~~text
FlyViewCustom.SettingsGroup.json 定义名称、类型与默认值
    → FlyViewCustomSettings 声明并创建 Fact
    → CustomPlugin.flyViewCustomSettings 向 QML 暴露
    → FlyViewSettings.qml 的开关绑定 Fact
    → rawValue 改变，写入 FlyView/showGimbalHeadingCompassBar
    → FlyViewCustomLayer 的绑定重新求值
    → 有效云台姿态存在时创建/隐藏对应罗盘
~~~

JSON 资源名与实际 QSettings 分组不要求同名：`FlyViewCustomSettings` 写入 `FlyView`，`VideoCustomSettings` 写入 `Video`。后续新增 Fact 时需要同时核对元数据名、类中的声明/实现、QML 属性和持久化分组，避免界面能显示但读写不同键。

翻译维护除运行 lupdate 外，还需检查设置 JSON 文本与 TS 的 source/context、枚举选项数量和分隔符，以及中文是否有空译文。生成的 QM 和布局截图属于验证产物，不作为功能源文件维护。

#### 3.13.5 功能对应的文件与资源协作

| 功能环节 | 文件 / 资源组 | 在本功能中的协作关系 |
|:---|:---|:---|
| 相机/UniRC/媒体设置 | `Gimbal/GimbalControlSettings.h/.cc`、`GimbalControl.SettingsGroup.json` | JSON 定义类型/默认值/范围，头文件声明 Fact，实现文件创建与持久化；Manager 监听或读取 Fact 后改变运行行为。 |
| 罗盘、第二路与三维设置 | `Settings/FlyViewCustomSettings.h/.cc`、`VideoCustomSettings.h/.cc` 及各自 JSON；`Viewer3D/Viewer3DSettings.h/.cc` 及 JSON | 分别提供 FlyView、Video 和 Viewer3D 设置；共享 Fact 机制，但保存分组和监听对象按功能区分。 |
| 页面总入口 | `UI/AppSettings/GeneralSettings.qml`、`VideoSettings.qml`、`FlyViewSettings.qml` | 三个页面选择并排列产品控件，分别承担通用、视频和飞行视图配置入口。 |
| 功能设置组 | `UI/AppSettings/GimbalControlSettingsGroup.qml`、`Viewer3DSettingsGroup.qml` | 前者组合两相机与 UniRC 设置/通道状态，后者组合地图源、模型导入与配准；各组绑定所属 Settings/Manager。 |
| 页面、分组与行布局 | `UI/AppSettings/FlyViewSettingsPage.qml`、`FlyViewSettingsSection.qml`、`FlyViewSettingsRow.qml` | Page 负责滚动和内容宽度，Section 负责分组容器，Row 在宽屏双列与窄屏堆叠间切换，统一标签/控件尺寸。 |
| Fact 编辑控件 | `UI/AppSettings/FlyViewFactTextField.qml`、`FlyViewFactSwitch.qml`、`FlyViewFactComboBox.qml` | 保留原生 Fact 校验/写入，封装输入、开关和枚举选择的布局/外观，保证编辑结果进入持久化链路。 |
| 普通下拉框与选项 | `UI/AppSettings/FlyViewComboBox.qml`、`FlyViewComboBoxDelegate.qml` | 普通组合框由页面处理选择结果；共用 delegate 统一选项宽度、文字省略与选中样式。 |
| 对象、默认字号与加载 | `CustomPlugin.h/.cc`；`custom/custom.qrc`、`custom/CMakeLists.txt` | 插件向 QML 暴露设置对象并提供 Android 默认字号；QRC alias/资源拦截器与 QML 模块共同保证实际加载这些页面。 |
| 翻译资源生成 | `custom/translations/custom-lupdate.sh`、`custom.ts`、`custom_zh_CN.ts`、`README.md` | 提取脚本更新 source/context，TS 保存源模板和中文；构建生成 QM 并由运行期加载。新增文案还需核对 JSON 枚举与布局。 |
| 布局与绑定验证 | `custom/test/UI/FlyViewSettingsLayout/run.py`、README 和该目录 QML 替身 | 加载真实设置资源，替身仅补足完整应用依赖；检查宽窄屏、字号、主题、通道网格、Fact 写入及三维模式切换。 |

---

<a id="verification"></a>

## 4. 构建、验证与配套工具

### 4.1 构建入口与自动检查

主线 CI（如 `.github/workflows/custom.yml`、`windows.yml`）配置 Qt 6.8.3、GStreamer 1.22.12；仓库另保留 Qt 6.6.3 的 Android 工作流。根 CMake 要求 3.25+，C++20。具体 SDK、NDK 和平台参数以仓库构建配置为准。custom 需要 Qt Bluetooth、Quick3D、Quick3DAssetUtils；Google 3D 的 WebEngineQuick 可选。双路及 Android 解码策略应使用 GStreamer 构建验证。

- 从工程根目录进行源码外构建；保留 `custom/` 即会自动接入。
- 使用已配置的 Desktop/Android Qt Kit，Android 同时需要对应 SDK/NDK、Java 和 GStreamer。
- 当前 `cmake/CustomOptions.cmake` 将 `QGC_BUILD_TESTING` 依赖于 `CMAKE_BUILD_TYPE=Debug`；Release 下会关闭测试。`custom/CMakeLists.txt` 仅在测试开启且非 Android/iOS 时注册桌面用例。
- 新增 C++、QML、资源或 Java overlay 后，重新运行 CMake，检查目标源码、QRC alias 与合并后的 Android 文件。

以下命令用于**已经配置好 Qt Kit 的桌面构建目录**。先将该目录配置为 Debug 并启用测试，再构建、执行；首次配置所需 Qt/平台路径仍使用项目已有 Kit 设置。

~~~sh
cmake -S . -B <desktop-build> -DCMAKE_BUILD_TYPE=Debug -DQGC_BUILD_TESTING=ON
cmake --build <desktop-build> --parallel
ctest --test-dir <desktop-build>/custom --output-on-failure
~~~

| 测试/工具位置 | 覆盖范围 |
|---|---|
| `custom/test/Gimbal/SiyiProtocolTest.cc`、`SiyiModeQueryTest.cc` | A8 协议、倍率策略、模式查询 |
| `custom/test/Gimbal/Mt11ProtocolTest.cc` | MT11 编解码、模式、倍率与端点策略 |
| `custom/test/Gimbal/GimbalMediaSessionPolicyTest.cc`、`GimbalPhotoCapturePolicyTest.cc` | 媒体会话、尺寸和 DPR |
| `custom/test/Gimbal/GimbalAzimuthPolicyTest.cc`、`GimbalHeadingTelemetryTest.cc`、`GimbalAzimuthProviderTest.cc` | 方位角换算、航向时效、活动云台匹配 |
| `custom/test/Gimbal/GimbalCenterCoordinatorTest.cc`、`GimbalModeControllerTest.cc` | 控制权/回中事务和模式会话 |
| `custom/test/Gimbal/GimbalModeUiTest.py` | 顶部模式 UI 回归脚本 |
| `custom/test/FlightDisplay/DualPipResizeTest.py` | PIP 实际鼠标事件与内容几何回归；独立于地图/视频后端 |
| `custom/test/Android/UniRcProtocolTest.cc` | UniRC 帧、通道保护及 CH10 状态 |
| `custom/test/VideoManager/VideoReceiver/GStreamer/AndroidH265DecoderRoutePolicyTest.cc` | 硬解路由、格式及 CAPS 策略 |
| 同目录 `A8RtspRecoveryPolicyTest.cc` | A8 停滞、时钟与恢复预算 |
| `custom/test/UI/FlyViewSettingsLayout/` | Qt 6/PySide6 加载实际资源，检查宽窄屏、字号、主题、通道网格与 Fact 写入 |

当前 custom 注册 **13 个 C++ CTest 用例**；三个 Python 检查脚本不由这组 CTest 自动执行。已安装 PySide6 时，可单独检查顶部模式 UI 和 PIP 缩放：

~~~sh
python custom/test/Gimbal/GimbalModeUiTest.py
python custom/test/FlightDisplay/DualPipResizeTest.py
~~~

布局检查另需 Qt 6 的 `rcc`，会生成截图，完整命令见[布局测试 README](custom/test/UI/FlyViewSettingsLayout/README.md)。各 `*Stubs/` 目录只为测试补足依赖，不进入产品构建。翻译更新见[翻译 README](custom/translations/README.md)。主机纯策略测试不覆盖真实 MediaCodec、蓝牙、USB、相机时序或 Android 画面。

需要定向回归时，可在已构建对应目标的前提下使用 CTest 名称过滤，例如：

~~~sh
ctest --test-dir <desktop-build>/custom -R '^(GimbalModeControllerTest|SiyiModeQueryTest)$' --output-on-failure
~~~

验证的交付物按层次区分：CMake/编译结果确认接入和类型依赖；策略测试确认纯逻辑；QML 检查及截图确认绑定/布局；新 APK 的真机日志与实际画面确认平台和设备行为。第 1 节的进度应与实际完成的层次对应。

<a id="acceptance"></a>

### 4.2 当前真机验收矩阵

每次验收记录 APK/提交、平台/设备、测试项、结果和证据位置；只将适用于当前版本的结论写回第 1 节。

| 范围 | 必测行为 |
|---|---|
| 视频 | A8、MT11 各自位于 URL 1/2；同时播放与交换；持续至少 10 min；断流重连；PIP 切换、前后台、surface 重建 |
| PIP 缩放 | 双路播放及地图作为辅窗时，分别拖动上下手柄；连续放大/缩小、快速反向、拖出辅窗、触及上下限并返回、释放/取消后重拖、主辅切换及父窗口改变大小；确认跟手、无跳变和视频持续显示 |
| Android 解码 | 按 receiver/generation 确认 CAPS、source、实际 decoder、decoder 输出和 sink 首帧；保持硬解，健康另一条流不被重建 |
| A8 停滞恢复 | 连续至少 5 次关闭/打开 QGC，每次双路播放至少 120 s；无停滞时不重建；恢复遵守次数限制 |
| 相机 | A8 各分辨率上限；MT11 短按 1～30x、长按全倍率、释放/取消/反向；三种 MT11 模式实际画面 |
| 本地媒体 | SD/LOCAL 各自成功与失败；两路独立；PIP 大小不降低输出目标；A8 断流续录、MT11 断流停止/手动重开；容量清理触发、停止重试和退出封装 |
| Android 图库 | 同卷保存、失败重试、公开发布、切换存储卷；已发布媒体卸载后保留 |
| 云台姿态/模式 | RC 接管后 Center/Tilt 90/Lock/Follow；重连同步、等待期间失联/切车；迟到 ACK 不执行旧动作 |
| UniRC | 16 通道、CH9 回中/反向、CH10 交替、CH7/8 复位、顶部联动；失焦/断流后停止并重新保护 |
| 遥测与界面 | 底部飞控航向显示与顶部云台换算；云台姿态 2 s 过期/失联隐藏及底部保留值的边界；电源阈值/缺参数回退、母线计时；Fuel/雷达有效值及失联显示 |
| 平台集成 | USB 权限/插拔/重开；空配置 UDP 默认值及已有值保留；净安装字号、升级持久化、宽窄屏与中文 |

### 4.3 A8 稳定性采集工具

`custom/tools/a8-video-capture.sh` 是随仓库维护的 Ubuntu/USB ADB 工具，不参与 APK 构建。用于启动采集后拔掉 USB，在实际图传条件下播放，再接回 USB 导出证据。

#### 4.3.1 操作步骤

1. Ubuntu 安装可用 ADB，进入工程根目录，USB 连接遥控器并确认调试授权。遥控器至少保留 1 GiB 空间。
2. 执行启动命令；该步骤会重启 QGC 一次，成功后拔掉 USB。
3. 保持双路前台播放约 10 min，接回 USB，再执行导出命令：

~~~sh
bash custom/tools/a8-video-capture.sh start
# 启动成功后拔 USB，完成播放测试，再接回 USB
bash custom/tools/a8-video-capture.sh finish
~~~

多设备时设置 `ANDROID_SERIAL`；自定义输出位置用 `A8_CAPTURE_OUTPUT_DIR`，目录必须预先存在。默认在 Ubuntu 用户目录生成 `a8-stability-*.tar.gz`，遥控器原件位于 `/sdcard/Download/QGC_A8_Stability/`。重复 start 不覆盖未导出的会话，finish 可在新终端执行。

#### 4.3.2 工作方式与结果解读

- 通过 Qt `applicationArguments` 启用 QGC/GStreamer 诊断；采集进程脱离 ADB 会话，最长约 15 min，轮转日志约 512 MiB，并保存进程、网络、USB 和媒体服务状态。
- 按两路 receiver/generation 区分数据，结合日志覆盖、输入/输出间隔和恢复事件分析。脚本检查记录完整性，不自动给出“零断流”结论。
- 目标遥控器插 USB 会影响图传，分析时排除插拔前后约 10 s，并根据日志扩大边界。详细采集有额外负担，短间隔异常应与低日志量播放对照。
- 日志和压缩包不纳入 Git；对外分享前核对设备信息及流地址。当前保留的验证基础是脚本语法与替身 ADB 检查，目标设备脱离 USB 后的采集存活仍需实测。

---

<a id="maintenance"></a>

## 5. 后续维护规则

### 5.1 更新原则

1. **先更新功能，再更新进度**：行为改变时直接改第 3 节对应模块；第 1 节只写当前状态和下一项工作。
2. **一个事实只维护一处**：参数默认值放所在模块，进度表链接模块；共享媒体、控制权和视频机制不在各相机章节重复展开。
3. **目录与功能同步维护**：新增/移动/删除文件时，先更新 2.1 文件树中的路径和职责，再更新第 3 节所属功能的文件/资源协作表；新增原生接口时更新 2.4。
4. **区分实现与验证**：注明“已集成、主机测试通过、真机通过、待验收”等证据范围，不用旧版本通过记录代替当前验收。
5. **只保留当前约束**：保留影响使用/扩展的硬件条件、绑定关系和平台限制；复现步骤、失败尝试、临时补丁及逐次编译日志留在提交、Issue 或测试归档。
6. **不追加重复修复史**：问题解决后改写最终行为，移除已失效待办；大型设计说明放模块独立文档，本手册保留摘要和链接。

### 5.2 新模块模板

~~~markdown
### 3.x 模块名称

#### 3.x.1 功能与工作模式

说明用户能完成什么、支持哪些模式及当前边界。

#### 3.x.2 配置与使用

| 设置/入口 | 默认值或条件 | 生效方式 |
|---|---|---|
| 设置键或界面位置 | 本版值 | 即时生效 / 重启生效 |

1. 前置条件与配置。
2. 操作步骤、状态反馈和完成条件。

#### 3.x.3 实现流程

**调用链**：界面/输入 → Manager/Controller → 协议/原生接口 → 回调 → 属性与显示。

| 阶段 | 文件 / 函数 | 输入、处理与输出 |
|---|---|---|
| 初始化 | 创建/接线函数 | 依赖、connect、timer 和默认状态 |
| 操作入口 | 公开方法 / 输入回调 | 参数校验与会话快照 |
| 核心处理 | 策略 / 协议函数 | 算法、命令或数据转换 |
| 结果确认 | ACK / 状态处理函数 | 成功条件、属性与信号 |
| 结束 | 停止 / 超时 / 清理函数 | 清理资源，拒绝过期回调 |

补充实际状态/阶段、关键算法、协议字段，以及切换对象或退出时的行为。共享机制链接已有章节。

#### 3.x.4 功能对应的文件与资源协作

| 功能环节 | 文件 / 资源组 | 在本功能中的协作关系 |
|:---|:---|:---|
| 配置与入口 | 设置页、Settings 类、JSON | 哪个值驱动哪个业务对象，何时生效 |
| 用户交互 | 页面、子控件、图标 | 谁提供布局/外观，谁接收输入与显示结果 |
| 核心处理 | Manager/Controller、Policy、Protocol | 各层的输入输出与调用先后 |
| 资源与接入 | QRC、QML 模块、模型/贴图、平台文件 | 由谁加载，怎样与功能代码关联 |
| 验证 | 测试、测试替身或手动样例 | 覆盖的行为与仍需实机验证的范围 |

按本模块实际情况保留环节；共享机制链接已有章节。新增文件同时补入 2.1 的真实目录位置。
~~~

### 5.3 后续开发的落点与完成步骤

| 变更类型 | 主要修改位置 | 同步检查 |
|:---|:---|:---|
| 新增本地设置 | 对应 Settings 类与 JSON，必要时扩展插件属性 | QRC、分组/默认值、QML 绑定、翻译、升级保留 |
| 新增界面控件 | 对应 FlightDisplay、QmlControls 或 UI 模块 | 原生覆盖 alias / 独立 QML 模块、尺寸、主题、触控 |
| 新增相机命令 | Protocol 编解码 → SDK 收发 → Manager 会话 → QML 动作 | 请求/应答关联、能力门控、取消、超时及协议测试 |
| 新增视频策略 | custom VideoManager/GStreamer 策略及插件接线 | receiver/URI/generation 隔离、健康另一路、录像分支 |
| 修改飞控行为 | FirmwarePlugin / AutoPilotPlugin | 参数版本、普通/高级页、原生接口依赖 |
| 新增 Android 平台能力 | `custom/src/Android` 及 `custom/android` | 权限、JNI、overlay、Activity/应用生命周期 |

以新增设置为例，完成顺序为：

1. 在所属模块的 JSON 定义稳定键名、类型、默认值和范围。
2. 在 Settings 头文件声明 Fact，在实现文件接入；需要新 Settings 对象时由 CustomPlugin 创建并暴露。
3. QML 绑定 Fact，业务 Manager 监听或读取值，明确即时生效还是重启生效。
4. 检查 CMake/QRC、翻译和相关验证；涉及用户已有值时确认升级保留。
5. 更新 2.1 文件树、对应功能的文件/资源协作表与实现流程、参数表及第 1 节状态，最后更新导航。

同路径覆盖示例：`custom/src/UI/AppSettings/FlyViewSettings.qml` 在 `custom.qrc` 中以 `QGroundControl/AppSettings/FlyViewSettings.qml` 为 alias、位于 `/Custom/qml` 前缀下；资源拦截器据此接管原生页面。新增子组件时也要提供实际可解析的资源路径。

### 5.4 验收记录模板

每个模块只保留**当前版本最近一次有效结论**；详细原始日志放证据目录，后续验收更新原行。

| 版本 / APK | 平台与设备 | 验证范围 | 结果与剩余项 | 证据位置 |
|:---|:---|:---|:---|:---|
| 提交号或包标识 | 系统、设备/固件版本 | 用例、时长、输入条件 | 通过 / 部分通过 / 未验证 | 日志、截图或报告路径 |

完成更新后检查：本页导航可跳转、源码路径有效、参数与代码一致、流程图与正文一致、已失效待办已移除。
