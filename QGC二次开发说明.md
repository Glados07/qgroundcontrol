# QGC 二次开发说明

适用工程：`F:\qgroundcontrol_viewer3d`

当前分支：`SecDev/ft/rtsp`

最后更新：2026-07-17

## 1. 当前状态

二次开发主体位于 `custom`；另按 `SecDev/feature` 要求保留两处受控 `src` 修改。当前已接入六个功能模块：

| 模块 | 已完成功能 |
|---|---|
| Viewer3D | 2D/3D 切换、本地 OSM 三维建筑、外部 OBJ/glTF/GLB/Balsam 模型、WGS84 原点配准、可选 Google 3D Maps、飞机/任务/航线三维显示 |
| Gimbal | 思翼 A8 Mini 私有 UDP SDK、1.0x-5.5x 缩放、可调分度值、右侧缩放控件、视频流默认配置和 MAVLink 自动流开关 |
| Video | Android H.265 厂商 MediaCodec 硬解优先；只有确认硬解可用后才禁用 H.265 软解回退，否则保留原 rank；提供 A8 Mini 低延迟默认值和解码器选择日志 |
| Fuel | 顶部燃料状态、燃料详情、20.0 V 触发且 20.4 V 恢复的母线低电压告警 |
| Comms | 首次运行自动补充 `local` UDP 链路，目标 `192.168.144.20:19856` |
| PX4 定制 | 自定义 FirmwarePlugin/AutoPilotPlugin、仅支持 PX4 多旋翼、限制飞行模式和车辆设置页、Fuel 指示器排序 |

本轮以整理后的 gimbal 为基线选择性移植 feature，`custom` 当前共 93 个文件。仍然不引入：

- QGC `custom-example` 的六个未使用 `Custom*.qml` 控件。
- 示例自定义动作、示例姿态仪、指南针和未使用工具栏图标。
- 示例品牌、平台安装图、Android 覆盖包和全局配色。
- 与 `src/Viewer3D` 字节完全相同的 C++、QML、qmldir、shader 和示例 OSM 副本。
- 不再需要的 `AppSettings.qml` 根页副本；原生 AppSettings 通过 URL 拦截器直接加载 custom Fly View 和 Video 设置页。

feature 要求的 PX4 定制逻辑已恢复：自定义 Factory 替代原生 PX4 Factory，关闭 APM，Fuel 由 `CustomFirmwarePlugin::toolIndicators()` 插入 Battery 后，并移除 RC RSSI。该行为是项目功能，不再按 custom-example 冗余处理。

## 2. 开发边界

1. 除 `src/CMakeLists.txt` 和 `src/Vehicle/VehicleSetup/VehicleSummary.qml` 两处 feature 必需改动外，不修改其他 `src` 文件。
2. custom 新增代码按 QGC 模块放置，例如 `FlightDisplay`、`Gimbal`、`Comms`、`QmlControls`、`UI/AppSettings`、`VideoManager/VideoReceiver/GStreamer`。
3. 只有需要改变原生行为时才保存同名覆盖 QML；没有差异的文件继续使用 `src`。
4. 与 `src/Viewer3D` 相同的公共实现由 `custom/CMakeLists.txt` 或 `custom.qrc` 直接引用，不在 custom 保存副本。
5. custom QML 覆盖使用 `/Custom/qml` 前缀，Viewer3D 独立模块仍使用 `/qml/Viewer3D`。
6. 设置 Fact 名和 QSettings 分组保持稳定，升级程序不会丢失已有 Viewer3D、Gimbal 和链路设置。
7. 复杂协议、坐标转换和跨模块行为使用中文注释；普通布局和赋值不增加无意义注释。

## 3. custom 完整目录结构

当前共 93 个文件：

```text
custom/
  CMakeLists.txt
  custom.qrc
  cmake/
    CustomOverrides.cmake
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
      FlyViewCustomLayer.qml
      FlyViewToolStripActionList.qml
      FlyViewTopRightColumnLayout.qml
      GimbalZoomControl.qml
    Gimbal/
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
    UI/
      AppSettings/
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
          AndroidVideoDecoderPolicy.h
          AndroidVideoDecoderPolicy.cc
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
  translations/
    README.md
    custom.ts
    custom_zh_CN.ts
    custom-lupdate.sh
```

## 4. 每个文件的作用

### 4.1 构建入口

| 文件 | 详细作用 |
|---|---|
| `custom/CMakeLists.txt` | 启用 `QGC_CUSTOM_BUILD` 和 `CustomPlugin`；构建只包含 Fuel 详情页的 `Custom.Widgets` 模块；加入 AutoPilot/Firmware、Viewer3D、Gimbal、Comms 和 VideoManager/GStreamer custom 源码；声明 Quick3D、可选 WebEngineQuick、资源和翻译。 |
| `custom/custom.qrc` | 注册 57 个运行时资源。AppSettings 同名覆盖页位于 `/Custom/qml/QGroundControl/AppSettings`；Fuel 图标位于 `/custom/img/FuelIcon.svg`，工具栏组件位于 `/Custom/qml/QGroundControl/Toolbar/FuelStatusIndicator.qml`；Viewer3D 无差异资源继续引用 `../src`。 |
| `custom/cmake/CustomOverrides.cmake` | 保持应用名和 QSettings 路径；关闭原生 Viewer3D 后端；关闭 APM dialect/plugin/factory 和原生 PX4 Factory，使 custom PX4 Factory 成为唯一 PX4 Factory。 |

### 4.2 CustomPlugin 与通信链路

| 文件 | 详细作用 |
|---|---|
| `custom/src/CustomPlugin.h` | 声明 custom 核心插件、Viewer3D/Gimbal QML 属性、MAVLink 消息过滤入口和 QML URL 拦截器。Fuel 改由车辆 FirmwarePlugin 管理。 |
| `custom/src/CustomPlugin.cc` | 初始化默认链路、翻译、Viewer3D 和 Gimbal；在 VideoReceiver 创建前按设置应用 Android H.265 解码策略，再安装 A8 Mini 视频默认值和 `/Custom/qml` 覆盖拦截器；不再重复追加 Fuel。 |
| `custom/src/Comms/DefaultCommunicationLinkInstaller.h` | 声明默认通信链路的幂等安装接口。 |
| `custom/src/Comms/DefaultCommunicationLinkInstaller.cc` | 在 LinkManager 读取设置前检查 `LinkConfigurations`。不存在 `local`/`Local` 时写入 UDP 链路：本地端口 0、远端 `192.168.144.20:19856`、不开机自动连接、非高延迟；已有同名链路时不覆盖。 |

### 4.3 PX4 FirmwarePlugin 与 AutoPilotPlugin

| 文件 | 详细作用 |
|---|---|
| `custom/src/FirmwarePlugin/CustomFirmwarePluginFactory.h` | 声明项目 PX4 Factory，只公开 PX4 firmware class 和多旋翼 vehicle class。 |
| `custom/src/FirmwarePlugin/CustomFirmwarePluginFactory.cc` | 创建全局 Factory 注册对象；收到 PX4 飞行器时返回单例 `CustomFirmwarePlugin`，其他固件返回空并由 QGC 通用逻辑处理。 |
| `custom/src/FirmwarePlugin/CustomFirmwarePlugin.h` | 声明 PX4 固件行为覆盖：AutoPilotPlugin、车辆工具栏、云台能力和可用飞行模式。 |
| `custom/src/FirmwarePlugin/CustomFirmwarePlugin.cc` | 创建 CustomAutoPilotPlugin；移除 RC RSSI；将 Fuel 插到 Battery 后；声明 pitch/yaw 云台能力；只允许 Loiter、RTL、Mission 作为可设置飞行模式。 |
| `custom/src/AutoPilotPlugin/CustomAutoPilotPlugin.h` | 声明定制 PX4 车辆设置页列表，并监听高级模式变化。 |
| `custom/src/AutoPilotPlugin/CustomAutoPilotPlugin.cc` | 普通模式只提供 Safety；高级模式提供 Airframe、Sensors、Radio、Flight Modes、Power、Motors、Safety 和 Tuning。 |

### 4.4 FlightDisplay QML

| 文件 | 详细作用 |
|---|---|
| `custom/src/FlightDisplay/FlyViewCustomLayer.qml` | 只实现燃料电池母线低电压告警。低于 20.0 V 显示，回升到 20.4 V 以上关闭；完整透传 QGC 原生 tool insets，不再包含示例指南针和姿态仪。 |
| `custom/src/FlightDisplay/FlyViewToolStripActionList.qml` | 在 QGC 原生起飞、返航、暂停、附加动作和夹爪动作之前增加 Viewer3D 切换按钮。关闭 3D 时使用白色 3D 图标，打开后使用原生 PaperPlane 图标返回 Fly。 |
| `custom/src/FlightDisplay/FlyViewTopRightColumnLayout.qml` | 当存在活动飞行器且 Gimbal `enabled=true` 时，通过明确的 custom QRC URL 加载私有 SDK 缩放栏并覆盖原生 PhotoVideoControl；Loader 尺寸绑定控件隐式尺寸，避免透明零尺寸占位；关闭模块时恢复 QGC 原生拍照/录像控件。 |
| `custom/src/FlightDisplay/GimbalZoomControl.qml` | 半透明白色云台缩放 UI，提供加号、当前倍率和减号；调用 `gimbalControlManager.zoomIn/zoomOut()`；不再显示 `SDK Waiting` 文本。 |

未在 custom 保存 `FlyView.qml`、`FlyViewWidgetLayer.qml` 和 `FlyViewToolStrip.qml`，因为当前 `src` 已经具备 Viewer3D 容器、地图交互禁用、比例尺隐藏和工具栏装载逻辑。

### 4.5 Gimbal 后端

| 文件 | 详细作用 |
|---|---|
| `custom/src/Gimbal/GimbalControl.SettingsGroup.json` | 定义 `enabled`、`sdkHost`、`sdkPort`、`zoomStep`、`mavlinkAutoVideoStream` 和 `forceAndroidH265HardwareDecoder` 六个持久化 Fact；Android H.265 硬解默认开启并要求重启生效。 |
| `custom/src/Gimbal/GimbalControlSettings.h` | 声明 Gimbal SettingsGroup 和六个 Fact 访问器。 |
| `custom/src/Gimbal/GimbalControlSettings.cc` | 加载 `/json/GimbalControl.SettingsGroup.json` 并注册六个设置 Fact。 |
| `custom/src/Gimbal/GimbalControlManager.h` | 暴露 `currentZoom`、`zoomStep`、`sdkResponding`、`lastError` 和缩放请求接口给 QML；定义 1.0x-5.5x 限制。 |
| `custom/src/Gimbal/GimbalControlManager.cc` | 连接设置与 SDK；执行加减倍率、范围钳制、乐观 UI 更新、当前倍率轮询和 1.5 秒响应超时。 |
| `custom/src/Gimbal/GimbalVideoStreamSupport.h` | 声明 A8 Mini 视频默认设置安装和 MAVLink 相机流消息过滤接口。 |
| `custom/src/Gimbal/GimbalVideoStreamSupport.cc` | 默认设置 RTSP 地址 `rtsp://192.168.144.25:8554/main.264` 和 20 秒超时，编码方式仍由 RTSP SDP 判定；默认值版本为 4，Android 使用 A8 Mini URL 且用户从未保存 `lowLatencyMode` 时默认开启低延迟，已有选择不覆盖；根据 `mavlinkAutoVideoStream` 决定是否允许 MAVLink 相机流信息锁定视频源。 |
| `custom/src/Gimbal/SiyiProtocol.h` | 声明思翼私有协议帧头、命令字、CRC16、组包和解包接口。 |
| `custom/src/Gimbal/SiyiProtocol.cc` | 实现思翼帧序号、长度、CRC16、绝对倍率编码、倍率查询命令和响应解析。 |
| `custom/src/Gimbal/SiyiSdk.h` | 声明 UDP SDK 封装、终端地址、绝对缩放和倍率查询接口。 |
| `custom/src/Gimbal/SiyiSdk.cc` | 使用 `QUdpSocket` 向 A8 Mini SDK 端口发包，读取数据报并交给 SiyiProtocol 解析，向管理器发出倍率和错误信号。 |

### 4.6 Android 视频解码策略

| 文件 | 详细作用 |
|---|---|
| `custom/src/VideoManager/VideoReceiver/GStreamer/AndroidVideoDecoderPolicy.h` | 声明 Android H.265 解码策略入口；必须在 GStreamer 初始化完成、VideoReceiver 创建之前调用。 |
| `custom/src/VideoManager/VideoReceiver/GStreamer/AndroidVideoDecoderPolicy.cc` | 仅在 Android + GStreamer 且设置开启时枚举与当前 parser `hvc1` 输出兼容的 H.265 decoder。排除 Google OMX、C2 Android、C2 Google、C2 Goldfish 和 FFmpeg 软件实现；发现真实厂商硬解后，将其 rank 提升到不低于 `GST_RANK_PRIMARY + 1`，并将 H.265 软件解码 rank 设为 `GST_RANK_NONE`。没有兼容厂商硬解时保持全部原 rank；H.264 和桌面平台不受该 rank 策略影响。日志类别为 `gcs.custom.video.androidvideodecoderpolicy`。 |

### 4.7 Application Settings、Fuel 和 qmldir

| 文件 | 详细作用 |
|---|---|
| `custom/src/QmlControls/FuelStatusIndicatorPage.qml` | Fuel 独立详情页，显示剩余比例、剩余/最大/已消耗燃料、流量、温度和液体/气体单位。由精简 `Custom.Widgets` 模块注册。 |
| `custom/src/QmlControls/Viewer3D/Models3D/qmldir` | 在原生 Viewer3D.Models3D 类型清单中增加 `External3DMap`，其他类型名称保持 QGC 原生一致。 |
| `custom/src/UI/AppSettings/FlyViewSettings.qml` | 保留原生 Fly View 设置组，移除原生旧 Viewer3D 设置块，在底部加载 custom Viewer3D 和 Gimbal 设置组。显式导入原生 `QGroundControl.AppSettings`，因此不需要复制 `SettingsPage.qml`。 |
| `custom/src/UI/AppSettings/VideoSettings.qml` | 与原生 Video 设置页同名覆盖，完整保留 Video Source、Connection、Settings 和 Local Video Storage，在 Connection 后增加独立的 Video Stream Integration 设置组。该组在所有平台显示 MAVLink 自动视频流、Android H.265 强制硬解开关和重启提示；硬解策略本身仍只在 Android 生效。 |
| `custom/src/UI/AppSettings/Viewer3DSettingsGroup.qml` | 提供 Viewer3D 启用、Google/外部/OSM 地图源、文件选择、WGS84 原点、单位、比例、yaw、建筑层高和高度偏移 UI。 |
| `custom/src/UI/AppSettings/GimbalControlSettingsGroup.qml` | Fly View 页面中的思翼缩放设置组，仅提供缩放开关、SDK IP/端口和缩放分度值；视频源及解码策略已经迁移到 custom Video 设置页。 |
| `custom/src/UI/toolbar/FuelStatusIndicator.qml` | 顶部只显示 Fuel 图标和剩余百分比；有 Fuel 遥测时显示，点击后创建 `FuelStatusIndicatorPage`。 |
| `custom/src/UI/toolbar/Images/FuelIcon.svg` | FuelStatusIndicator 使用的气瓶矢量图标，QRC 路径为 `/custom/img/FuelIcon.svg`。 |

### 4.8 Viewer3D C++ 扩展

| 文件 | 详细作用 |
|---|---|
| `custom/src/Viewer3D/Viewer3D.SettingsGroup.json` | 定义 14 个 Viewer3D Fact 的类型、默认值和单位。 |
| `custom/src/Viewer3D/Viewer3DSettings.h` | 声明 custom Viewer3D SettingsGroup，在原生四项基础上增加 Google 和外部模型参数。 |
| `custom/src/Viewer3D/Viewer3DSettings.cc` | 加载 custom Viewer3D 元数据并注册全部 Fact。 |
| `custom/src/Viewer3D/CustomViewer3DManager.h` | 声明 custom Viewer3D QML 类型注册器，持有 OsmParser 和扩展后的 Viewer3DQmlBackend。采用 Custom 前缀，避免与原生 `Viewer3DManager` C++ 类混淆。 |
| `custom/src/Viewer3D/CustomViewer3DManager.cc` | 创建 Parser/Backend，并把 custom 类以 QML 名称 `Viewer3DManager` 注册到 `QGroundControl.Viewer3D`，保持现有 QML API 不变。 |
| `custom/src/Viewer3D/CityMapGeometry.cc` | 复用原生 CityMapGeometry 声明和几何算法，但从 `CustomPlugin::viewer3DSettingsFactGroup()` 获取设置。 |
| `custom/src/Viewer3D/OsmParser.cc` | 复用原生 OSM 解析流程，但绑定 custom Viewer3DSettings。 |
| `custom/src/Viewer3D/Viewer3DTerrainGeometry.cc` | 复用原生地形网格算法，但绑定 custom Viewer3DSettings。 |
| `custom/src/Viewer3D/Viewer3DQmlBackend.h` | 在原生 QML 后端上增加外部模型地图原点、设置变化响应和参考点恢复接口。 |
| `custom/src/Viewer3D/Viewer3DQmlBackend.cc` | 在外部模型模式下使用用户配置的 WGS84 原点；退出外部模式后优先恢复 OSM 参考点，再回退飞行器坐标。 |
| `custom/src/Viewer3D/External3DMapManager.h` | 声明外部模型路径检查、Balsam 转换、状态和错误信息接口。 |
| `custom/src/Viewer3D/External3DMapManager.cc` | 直接加载 OBJ/glTF/GLB/QML；对 FBX/DAE/STL/PLY 调用 Qt Balsam 转换；保存可加载 URL 并输出明确错误。 |
| `custom/src/Viewer3D/Images/city_3d_map_icon.svg` | Viewer3D 工具栏白色图标，资源路径为 `qrc:/Custom/qmlimages/Viewer3D/City3DMapIcon.svg`。 |

### 4.9 Viewer3D custom QML

| 文件 | 详细作用 |
|---|---|
| `custom/src/Viewer3D/Viewer3DQml/Viewer3D.qml` | Viewer3D 根组件；使用 custom settings；在 Google Web 视图、本地 Quick3D 场景和依赖缺失提示之间切换。 |
| `custom/src/Viewer3D/Viewer3DQml/Google3DMapView.qml` | 通过 Qt WebEngine 和 Google Maps JavaScript API 显示在线三维地图，以活动飞行器或地图中心作为初始坐标。 |
| `custom/src/Viewer3D/Viewer3DQml/Google3DMapUnavailable.qml` | Google 3D 已开启但构建中没有 WebEngineQuick 时显示依赖不可用提示。 |
| `custom/src/Viewer3D/Viewer3DQml/Models3D/External3DMap.qml` | 加载 External3DMapManager 输出的模型 URL，应用模型单位、额外比例和 yaw，并显示加载状态或阻塞错误。 |
| `custom/src/Viewer3D/Viewer3DQml/Models3D/Viewer3DModel.qml` | 本地 Quick3D 主场景；在 OSM 建筑与外部模型之间切换，组合相机、地形、飞行器、任务点和交互控制。 |
| `custom/src/Viewer3D/Viewer3DQml/Models3D/Viewer3DVehicleItems.qml` | 将飞行器和任务坐标转换到局部三维坐标；外部模型模式使用 AMSL 高度减模型原点高度。 |
| `custom/src/Viewer3D/Viewer3DQml/Drones/DroneModelDjiF450.qml` | F450 飞行器总装；增加外部模型地图下的 AMSL 高度配准，组合 14 个原生部件 QML 和 custom mesh。 |

以下基础 QML 不在 custom 保存：`CameraLightModel.qml`、`Line3D.qml`、`Waypoint3DModel.qml`、`Viewer3DProgressBar.qml` 和 14 个 F450 部件 QML。它们由 `custom.qrc` 直接引用 `src/Viewer3D`。

### 4.10 F450 运行时 mesh

这些 mesh 与当前 `src` 版本不同，属于 custom 运行时资产，因此保留。每个文件由同名原生部件 QML加载：

| 文件 | 作用 |
|---|---|
| `custom/src/Viewer3D/Viewer3DQml/Drones/Djif450/DroneModel_arm_1/node.mesh` | 第 1 个 F450 机臂几何。 |
| `custom/src/Viewer3D/Viewer3DQml/Drones/Djif450/DroneModel_arm_2/node.mesh` | 第 2 个 F450 机臂几何。 |
| `custom/src/Viewer3D/Viewer3DQml/Drones/Djif450/DroneModel_arm_3/node.mesh` | 第 3 个 F450 机臂几何。 |
| `custom/src/Viewer3D/Viewer3DQml/Drones/Djif450/DroneModel_arm_4/node.mesh` | 第 4 个 F450 机臂几何。 |
| `custom/src/Viewer3D/Viewer3DQml/Drones/Djif450/DroneModel_Base_bottom_1/node.mesh` | F450 机身下板几何。 |
| `custom/src/Viewer3D/Viewer3DQml/Drones/Djif450/DroneModel_Base_Top_1/node.mesh` | F450 机身上板几何。 |
| `custom/src/Viewer3D/Viewer3DQml/Drones/Djif450/DroneModel_BLDC_1/node.mesh` | 第 1 个无刷电机几何。 |
| `custom/src/Viewer3D/Viewer3DQml/Drones/Djif450/DroneModel_BLDC_2/node.mesh` | 第 2 个无刷电机几何。 |
| `custom/src/Viewer3D/Viewer3DQml/Drones/Djif450/DroneModel_BLDC_3/node.mesh` | 第 3 个无刷电机几何。 |
| `custom/src/Viewer3D/Viewer3DQml/Drones/Djif450/DroneModel_BLDC_4/node.mesh` | 第 4 个无刷电机几何。 |
| `custom/src/Viewer3D/Viewer3DQml/Drones/Djif450/DroneModel_propeller2_2/node.mesh` | 第 1 组 propeller2 螺旋桨几何。 |
| `custom/src/Viewer3D/Viewer3DQml/Drones/Djif450/DroneModel_propeller2_7/node.mesh` | 第 2 组 propeller2 螺旋桨几何。 |
| `custom/src/Viewer3D/Viewer3DQml/Drones/Djif450/DroneModel_propeller22_1/node.mesh` | 第 1 组 propeller22 螺旋桨几何。 |
| `custom/src/Viewer3D/Viewer3DQml/Drones/Djif450/DroneModel_propeller22_2/node.mesh` | 第 2 组 propeller22 螺旋桨几何。 |

未注册的 `DroneModel_arm_1/meshes/node.mesh` 辅助副本已经删除。

### 4.11 外部 WGS84 城镇样例

| 文件 | 详细作用 |
|---|---|
| `custom/src/Viewer3D/ExternalWGS84_UE5_MapSample/README.md` | 样例导入步骤、坐标约定、推荐参数、资产来源和限制。 |
| `custom/src/Viewer3D/ExternalWGS84_UE5_MapSample/osm_overpass_source.json` | 生成城镇模型所用的 Overpass 查询、bbox、下载时间和原始来源信息。 |
| `custom/src/Viewer3D/ExternalWGS84_UE5_MapSample/qgc_viewer3d_import_settings.json` | 样例 WGS84 原点、ENU 轴、单位、比例、yaw 和资产统计。 |
| `custom/src/Viewer3D/ExternalWGS84_UE5_MapSample/realistic_town_wgs84_map.obj` | 推荐直接加载的带 UV 城镇模型。 |
| `custom/src/Viewer3D/ExternalWGS84_UE5_MapSample/realistic_town_wgs84_map.mtl` | OBJ 的道路、草地、立面、屋顶、商铺和树木材质定义。 |
| `custom/src/Viewer3D/ExternalWGS84_UE5_MapSample/realistic_town_wgs84_map.fbx` | 同场景 FBX，用于验证 Balsam 转换链路。 |
| `custom/src/Viewer3D/ExternalWGS84_UE5_MapSample/textures/asphalt_worn.png` | 道路磨损沥青纹理。 |
| `custom/src/Viewer3D/ExternalWGS84_UE5_MapSample/textures/facade_brick_windows.png` | 砖墙窗户立面纹理。 |
| `custom/src/Viewer3D/ExternalWGS84_UE5_MapSample/textures/facade_light_windows.png` | 浅色墙面窗户立面纹理。 |
| `custom/src/Viewer3D/ExternalWGS84_UE5_MapSample/textures/facade_modern_windows.png` | 现代玻璃窗格立面纹理。 |
| `custom/src/Viewer3D/ExternalWGS84_UE5_MapSample/textures/facade_tan_windows.png` | 棕黄色墙面窗户立面纹理。 |
| `custom/src/Viewer3D/ExternalWGS84_UE5_MapSample/textures/grass_mixed.png` | 草地和绿化区域纹理。 |
| `custom/src/Viewer3D/ExternalWGS84_UE5_MapSample/textures/roof_flat_gray.png` | 灰色平屋顶纹理。 |
| `custom/src/Viewer3D/ExternalWGS84_UE5_MapSample/textures/roof_tile_red.png` | 红色瓦片屋顶纹理。 |
| `custom/src/Viewer3D/ExternalWGS84_UE5_MapSample/textures/shopfront_facade.png` | 商铺橱窗和沿街店面纹理。 |
| `custom/src/Viewer3D/ExternalWGS84_UE5_MapSample/textures/sidewalk_concrete.png` | 人行道混凝土纹理。 |
| `custom/src/Viewer3D/ExternalWGS84_UE5_MapSample/textures/tree_leaf.png` | 低多边形树冠叶片纹理。 |

### 4.12 翻译

| 文件 | 详细作用 |
|---|---|
| `custom/translations/README.md` | 说明 custom 翻译模板、语言目录、Qt Linguist 和 `LUPDATE` 的维护流程。 |
| `custom/translations/custom.ts` | custom 可翻译源文本清单；包含 Video Stream Integration 中的 MAVLink 自动视频流和 Android H.265 硬解设置文本，已移除示例动作、示例开关和示例车辆按钮文本。 |
| `custom/translations/custom_zh_CN.ts` | Fuel 工具栏、Fuel 独立详情页、母线低电压告警和 Video Stream Integration 设置的中文翻译。 |
| `custom/translations/custom-lupdate.sh` | 使用 `LUPDATE` 或 `PATH` 中的 Qt 6 `lupdate` 扫描 `custom/src`，同时刷新模板和全部 `custom_*.ts` 语言目录。 |

## 5. 复用的 QGC 原生 Viewer3D 文件

### 5.1 C++ 复用

`custom/CMakeLists.txt` 直接编译以下无差异源文件，custom 不保存副本：

| 原生文件 | 用途 |
|---|---|
| `src/Viewer3D/CityMapGeometry.h` | OSM 建筑几何类声明。 |
| `src/Viewer3D/earcut.hpp` | 多边形三角剖分。 |
| `src/Viewer3D/OsmParser.h` | OSM Parser 类声明。 |
| `src/Viewer3D/OsmParserThread.h/.cc` | 后台 OSM 解析线程。 |
| `src/Viewer3D/Viewer3DQmlVariableTypes.h` | C++/QML 共享坐标类型。 |
| `src/Viewer3D/Viewer3DTerrainGeometry.h` | 地形几何类声明。 |
| `src/Viewer3D/Viewer3DTerrainTexture.h/.cc` | 地图瓦片纹理生成。 |
| `src/Viewer3D/Viewer3DTileQuery.h/.cc` | 瓦片范围、下载和拼接。 |
| `src/Viewer3D/Viewer3DTileReply.h/.cc` | 单瓦片网络请求。 |
| `src/Viewer3D/Viewer3DUtils.h/.cc` | WGS84/ECEF/ENU 坐标转换。 |

### 5.2 QML 和资源复用

`custom.qrc` 直接引用：

- `src/QmlControls/Viewer3D/qmldir`
- `src/QmlControls/Viewer3D/Models3D/Drones/qmldir`
- `CameraLightModel.qml`、`Line3D.qml`、`Waypoint3DModel.qml`
- `Viewer3DProgressBar.qml`
- 14 个 F450 部件 QML
- `earthMaterial.vert`、`earthMaterial.frag`

这些文件升级 QGC 时会自动跟随 `src`，不需要在 custom 手工同步。

## 6. 受控 src 修改

`SecDev/feature` 相对 gimbal 的 `src` 修改只有以下两处，已经完整移植：

| 文件 | 修改原因 |
|---|---|
| `src/CMakeLists.txt` | 原生 PX4 Factory 被关闭时仍链接 `AutoPilotPluginsPX4Module`，保证 VehicleSummary 和 CustomAutoPilotPlugin 使用的 PX4 QML 页面存在。 |
| `src/Vehicle/VehicleSetup/VehicleSummary.qml` | 注释 APM QML import；当前构建关闭 APM 模块，继续导入会造成运行时 `module QGroundControl.AutoPilotPlugins.APM is not installed`。 |

除这两处外，feature 没有其他 `src` 差异。本次 Android H.265 修复完全位于 `custom`，未新增任何 `src` 修改。

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

## 8. Gimbal 与视频参数及使用

| Fact | 范围/默认值 | 说明 |
|---|---|---|
| `enabled` | bool / `true` | 启用私有 SDK 缩放 UI 和后端。 |
| `sdkHost` | `192.168.144.25` | A8 Mini SDK IP。 |
| `sdkPort` | 1-65535 / `37260` | A8 Mini 私有 UDP SDK 端口。 |
| `zoomStep` | 0.1-4.5 / `1.0x` | 每次点击加减的倍率分度值。 |
| `mavlinkAutoVideoStream` | bool / `false` | 是否接受 MAVLink 相机流 URI 并允许其锁定视频源。修改后重启 QGC。 |
| `forceAndroidH265HardwareDecoder` | bool / `true` | 仅 Android 生效。优先真实厂商 MediaCodec H.265 硬解；修改后重启 QGC。 |

RTSP URL 的 `.264` 后缀只是 A8 Mini 的固定路径名，不代表当前一定为 H.264；QGC 依据 RTSP SDP 中的 `H264`/`H265` 编码声明组建管线。Android 策略只过滤 `video/x-h265` decoder，不修改 H.264 decoder rank。

开启强制硬解后，策略在 GStreamer 初始化后、`decodebin3` 创建前执行：

- 排除 Google OMX、C2 Android、C2 Google、C2 Goldfish 和 FFmpeg 软件 MediaCodec wrapper。
- 仅将与当前 parser `hvc1` 输出兼容的厂商 H.265 硬解视为有效候选；找到后将其 rank 提升到至少 `GST_RANK_PRIMARY + 1`，再将当前 H.265 软解设为 `GST_RANK_NONE`。
- 找不到真实硬解时不改动任何 rank，保留软件回退并输出告警，避免直接黑屏。
- Android 首次安装默认值时，仅当 A8 Mini URL 匹配且用户从未保存 `Video/lowLatencyMode` 才将它设为 `true`；用户已有的开关选择始终保留。

使用流程：

1. 电脑网口连接 A8 Mini，确认可访问 `192.168.144.25`。
2. Application Settings -> Fly View -> SIYI Gimbal Zoom 中确认 IP、端口、分度值和 Enabled。
3. Application Settings -> Video -> Video Stream Integration 中选择是否使用 MAVLink 自动视频流；Android 设备确认 H.265 硬解开关开启，然后重启 QGC。
4. 连接活动飞行器后，右侧原生拍照/录像区域被缩放栏替换。
5. `+` 和 `-` 在 1.0x-5.5x 内按 `zoomStep` 调整；管理器随后查询相机实际倍率。

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

TELEM2 参数负责飞控与云台 MAVLink；custom 缩放命令由电脑直接发往 `192.168.144.25:37260/UDP`，两条链路相互独立。

## 9. Fuel 与默认链路

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

## 10. 关键运行链路

```text
PX4 HEARTBEAT
  -> CustomFirmwarePluginFactory（仅 PX4 + MultiRotor）
  -> CustomFirmwarePlugin
     -> CustomAutoPilotPlugin 控制车辆设置页
     -> toolIndicators 移除 RC RSSI、插入 Fuel
     -> updateAvailableFlightModes 限制可设置模式
     -> hasGimbal 声明 pitch/yaw 能力
```

```text
QGCApplication
  -> VideoManager 构造
     -> GStreamer::initialize() 注册解码插件
  -> CustomPlugin::init()
     -> DefaultCommunicationLinkInstaller
     -> Viewer3DSettings / External3DMapManager / CustomViewer3DManager
     -> GimbalControlSettings / GimbalControlManager
     -> AndroidVideoDecoderPolicy::apply()
     -> GimbalVideoStreamSupport 安装 A8 Mini 默认值
  -> VideoManager::init()
     -> 创建 VideoReceiver / decodebin3，使用已更新的 H.265 decoder rank
  -> CustomPlugin::createQmlApplicationEngine()
     -> CustomOverrideInterceptor
        -> /Custom/qml 中存在才覆盖
        -> 其他 QML 使用 src 原生模块
```

```text
Application Settings / Fly View
  -> 原生 AppSettings.qml 请求 FlyViewSettings.qml
  -> CustomOverrideInterceptor 映射到 custom FlyViewSettings.qml
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
  -> custom FlyViewTopRightColumnLayout.qml 替换云台缩放栏
  -> custom FlyViewCustomLayer.qml 增加母线告警
```

## 11. 构建与验证

Ubuntu 24.04 推荐使用项目要求的 CMake 3.25+ 和 Qt 6.8.x，切换分支或改动 QRC/CMake 后执行干净配置和构建。

Android arm64 Release 建议与当前 CI 环境保持一致：Qt 6.8.3 Android kit、JDK 17、Android SDK 35、NDK r26b 和 `arm64-v8a`。若遥控器安装的是 32 位 APK，还需单独构建并验证 `armeabi-v7a`。

重点验证：

1. Application Settings -> Fly View 同时显示 Viewer3D 和 SIYI Gimbal Zoom，且不再显示两个视频流开关。
2. Application Settings -> Video 保留全部原生设置组，并在所有平台显示 Video Stream Integration 及两个开关；H.265 强制硬解设置仅在 Android 生效。
3. Viewer3D Enabled 持久化，重启后图标状态正确。
4. 3D 图标白色，2D/3D 可往返切换。
5. 本地 OSM、外部 OBJ/glTF/GLB 和可选 Google 3D 正常加载。
6. Gimbal Enabled 时右侧显示缩放控件，按钮可控制 1.0x-5.5x。
7. Gimbal Disabled 时恢复原生拍照/录像控件。
8. Ubuntu 24.04 播放同一路 H.265 RTSP 保持正常；Ubuntu/虚拟机代理需将 `192.168.144.25` 加入忽略列表。
9. Android 使用云台 H.264 编码回归测试，画面、延迟和断流重连均不退化。
10. Android 使用云台 H.265 编码连续播放至少 10 分钟，延迟不随时间增长；同时测试应用前后台切换和断流重连。
11. 真机日志中确认存在厂商 `amcviddec-*` H.265 硬解，它的 rank 大于等于 257，`avdec_h265`、Google/C2 软解 rank 为 0。
12. 在没有 H.265 硬解的 Android 设备上，日志应告警“未找到硬解”且软解 rank 保持原值，不应直接黑屏。
13. Fuel 遥测存在时顶部显示 Fuel，无数据时隐藏。
14. 首次运行出现 `local` 链路，已有同名链路不会重复或被覆盖。
15. 仅识别 PX4 多旋翼；APM 不出现在支持列表中。
16. 普通模式只显示 Safety 设置页，高级模式显示完整定制 PX4 设置页。
17. 飞行模式仅 Loiter、RTL、Mission 可由该列表设置，RC RSSI 不显示，Fuel 紧随 Battery。

Android 调试时关注日志类别 `gcs.custom.video.androidvideodecoderpolicy`。正常硬解日志会列出厂商 `amcviddec-*` 为 `hardware`，并列出每个 H.265 decoder 的 `oldRank -> newRank`。可用 QGC Application Messages 或 `adb logcat` 查看。

若 rank 日志正确但真机出现 `not-negotiated`、`Failed to configure codec` 或 `Codec only supports GL output but downstream does not`，说明该设备 MediaCodec 的 H.265 `stream-format`/GL 输出与当前管线协商失败；这与“软解速度不足”不是同一问题，需保留完整 logcat 再针对该遥控器做 caps 兼容。

运行日志出现 `GimbalZoomControl is not a type`，表示新增 QML 被当作原生 `QGroundControl.FlightDisplay` 模块类型直接实例化，但原生 qmldir 没有注册该类型。当前实现由 `FlyViewTopRightColumnLayout.qml` 使用完整 custom QRC URL 的 Loader 加载，并绑定控件隐式尺寸；修改后应重新构建 QRC。若仍看到旧错误，需删除旧构建目录后重新配置，避免使用缓存中的 `custom.qrc`。

常用静态检查：

```powershell
rg --files custom
rg -n "CustomIconButton|CustomOnOffSwitch|CustomVehicleButton" custom
git diff --check
```
