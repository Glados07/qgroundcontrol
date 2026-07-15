# QGC 二次开发说明

适用项目：`F:\qgroundcontrol_viewer3d`

当前整理分支：`SecDev/ft/arrange`

最后更新：2026-07-15

## 1. 文档目的与当前状态

本项目在 QGroundControl 的 `custom` 目录中实现二次开发功能，不直接修改 QGC 原生 `src`。当前已经完成并接入的主要模块如下：

| 模块 | 当前功能 |
|---|---|
| Viewer3D | 2D/3D 飞行视图切换、本地 OSM 三维地图、外部三维模型地图、可选 Google 3D Maps、飞机/任务/航线三维显示 |
| Gimbal | 思翼 A8 Mini 私有 UDP SDK 缩放控制、1.0x-5.5x 倍率限制、设置页、右侧缩放栏、视频流默认配置和 MAVLink 自动流控制 |
| Comms | 首次运行自动补充 `local` UDP 通信链路 |
| Fuel | 顶部工具栏燃料状态指示器、燃料详情页、飞行界面低母线电压提示 |
| QML 覆盖 | Application Settings、Fly View、工具栏、右侧相机区域和自定义引导动作覆盖 |
| Custom 插件 | 自定义固件插件、自动驾驶插件、翻译、品牌、配色和 QML URL 拦截器 |

本次目录整理的核心结果是：`custom/src` 按 QGC 原生 `src` 的模块边界排列。覆盖原生 QML 时，物理目录、文件名和运行时模块路径均与原文件对应；新增功能也放入最接近的原生模块中。

## 2. 强制开发规范

后续开发必须遵守以下规则：

1. 不直接修改 `src`。所有二次开发代码、覆盖 QML 和自定义资源均放在 `custom`。
2. `custom/src` 的一级目录使用 QGC 原生目录名，例如 `FlightDisplay`、`Gimbal`、`Comms`、`AutoPilotPlugins`，不得再创建 `Gimbalcontrol`、`CommunicationLink`、`AppSettingsUI` 等平行命名。
3. 覆盖原生 QML 时必须使用原文件名。例如覆盖 `src/FlightDisplay/FlyViewToolStrip.qml` 时，对应文件必须是 `custom/src/FlightDisplay/FlyViewToolStrip.qml`。
4. 新增 QML 使用 QGC 的 PascalCase 组件命名，并放在实际使用模块中。例如新增缩放控件为 `custom/src/FlightDisplay/GimbalZoomControl.qml`。
5. 图标按使用模块归档。Viewer3D 图标放 `Viewer3D/Images`，飞行地图仪表资源放 `FlightMap/Images`，顶部工具栏资源放 `UI/toolbar/Images`。
6. QRC 运行时使用 `/Custom/...` 独立前缀是必要例外，用于避免与 QGC 原生同名资源冲突；该前缀不影响物理目录严格镜像。
7. C++ 类名、QML 类型名和设置 Fact 名保持稳定。目录调整不得改变已有 QSettings 键，否则升级后会丢失用户设置。
8. 新增代码需要中文说明复杂业务和协议边界；普通赋值、控件布局不添加无意义注释。
9. 修改覆盖 QML 前先和对应的 `src` 原文件比较。QGC 上游升级后，应优先同步原生变更，再重新合入 custom 功能。

## 3. custom 完整目录结构

本章清单覆盖当前 `custom` 下的 171 个文件。源码和配置文件逐个说明；同一模型的生成式 QML、mesh 和纹理虽然用途相近，也保留具体文件名，便于排查资源缺失。

```text
custom/
  CMakeLists.txt
  custom.qrc
  README.md
  README.jpg
  android/
    AndroidManifest.xml
    res/drawable-*/icon.png
  cmake/
    CustomOverrides.cmake
  deploy/windows/
    installheader.bmp
    WindowsQGC.ico
  res/
    Images/
      dronecode-black.svg
      dronecode-white.svg
      void.png
    icons/
      custom_qgroundcontrol.ico
      custom_qgroundcontrol.png
    QGCLogoFull.svg
    macx.icns
  src/
    CustomPlugin.h
    CustomPlugin.cc
    AutoPilotPlugins/
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
      CustomGuidedActionsController.qml
      FlyView.qml
      FlyViewCustomLayer.qml
      FlyViewToolStrip.qml
      FlyViewToolStripActionList.qml
      FlyViewTopRightColumnLayout.qml
      FlyViewWidgetLayer.qml
      GimbalZoomControl.qml
    FlightMap/
      Images/
        attitude_crosshair.svg
        attitude_dial.svg
        attitude_pointer.svg
        compass_needle.svg
        compass_pointer.svg
      Widgets/
        CustomArtificialHorizon.qml
        CustomAttitudeWidget.qml
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
      AppSettings.qml
      FuelStatusIndicator.qml
      CustomIconButton.qml
      CustomOnOffSwitch.qml
      CustomQuickButton.qml
      CustomSignalStrength.qml
      CustomToolBarButton.qml
      CustomVehicleButton.qml
      Viewer3D/
        qmldir
        Models3D/qmldir
        Models3D/Drones/qmldir
    UI/
      AppSettings/
        FlyViewSettings.qml
        Viewer3DSettingsGroup.qml
        GimbalControlSettingsGroup.qml
      toolbar/Images/
        Fuel.svg
        PaperPlane.svg
        altitude.svg
        chronometer.svg
        distance.svg
        horizontal_speed.svg
        microSD.svg
        odometer.svg
        vertical_speed.svg
    Viewer3D/
      Viewer3D.SettingsGroup.json
      Viewer3DSettings.h/.cc
      Viewer3DManager.h/.cc
      Viewer3DQmlBackend.h/.cc
      Viewer3DQmlVariableTypes.h
      External3DMapManager.h/.cc
      OsmParser.h/.cc
      OsmParserThread.h/.cc
      CityMapGeometry.h/.cc
      Viewer3DTerrainGeometry.h/.cc
      Viewer3DTerrainTexture.h/.cc
      Viewer3DTileQuery.h/.cc
      Viewer3DTileReply.h/.cc
      Viewer3DUtils.h/.cc
      earcut.hpp
      Images/city_3d_map_icon.svg
      Shaders/earthMaterial.vert
      Shaders/earthMaterial.frag
      SampleOsmMap/map_sim_small.osm
      ExternalWGS84_UE5_MapSample/
        qgc_viewer3d_import_settings.json
        osm_overpass_source.json
        realistic_town_wgs84_map.obj/.mtl/.fbx
        textures/*
      Viewer3DQml/
        Viewer3D.qml
        Viewer3DProgressBar.qml
        Google3DMapView.qml
        Google3DMapUnavailable.qml
        Models3D/
          CameraLightModel.qml
          External3DMap.qml
          Line3D.qml
          Viewer3DModel.qml
          Viewer3DVehicleItems.qml
          Waypoint3DModel.qml
        Drones/
          DroneModelDjiF450.qml
          Djif450/*/*.qml
          Djif450/*/*.mesh
  translations/
    custom.ts
    custom_zh_CN.ts
    custom-lupdate.sh
```

`Djif450` 下的各子目录是 Qt Quick 3D 生成的飞机部件和 mesh 资源，属于同一模型资产，不应单独移动或重命名。

非源码目录职责：

| 目录/文件 | 作用 |
|---|---|
| `custom/android` | AndroidManifest 和各分辨率应用图标，构建时覆盖 QGC 默认 Android 包模板 |
| `custom/cmake/CustomOverrides.cmake` | custom 构建参数和平台覆盖入口 |
| `custom/deploy/windows` | Windows 安装程序头图和应用图标 |
| `custom/res` | 应用 Logo、品牌图片、Windows/macOS 图标；只存应用级资源，不再存功能 QML |
| `custom/translations` | custom 英文源翻译、中文翻译和 lupdate 脚本 |
| `custom/CMakeLists.txt` | custom C++、Qt 组件、QML 模块和翻译的编译入口 |
| `custom/custom.qrc` | 覆盖 QML、图标、JSON、shader、Viewer3D QML/mesh 的资源注册入口 |

### 3.1 根目录、构建与平台文件

| 文件 | 详细作用 |
|---|---|
| `custom/CMakeLists.txt` | custom 总编译入口。启用 `QGC_CUSTOM_BUILD`，声明 `CustomPlugin`，追加 Quick3D/WebEngine 可选依赖，注册 `Custom.Widgets`，收集 Viewer3D、Gimbal、Comms 和插件 C++ 源码，并导出 custom 翻译。 |
| `custom/custom.qrc` | custom 总资源清单。注册 QML 覆盖、Viewer3D QML/mesh、设置 JSON、shader、功能图标、品牌 Logo，并决定所有 `qrc:/Custom/...` 路径。 |
| `custom/README.md` | QGC custom build 示例说明。当前主要作为 custom build 来源说明保留，其中部分描述仍是上游示例语境，不是本项目功能清单。 |
| `custom/README.jpg` | `README.md` 使用的上游 custom build 示例截图，不参与程序运行。 |
| `custom/cmake/CustomOverrides.cmake` | 设置应用名 `Custom-QGroundControl`、Windows/macOS/AppImage 图标，关闭 APM 支持和原生 PX4 插件工厂，使程序使用 custom PX4 固件插件工厂。 |
| `custom/android/AndroidManifest.xml` | Android custom 包清单，定义应用组件、权限和 Android 打包属性，构建时覆盖默认 Manifest。 |
| `custom/android/res/drawable-ldpi/icon.png` | Android ldpi 密度应用启动图标。 |
| `custom/android/res/drawable-mdpi/icon.png` | Android mdpi 密度应用启动图标。 |
| `custom/android/res/drawable-hdpi/icon.png` | Android hdpi 密度应用启动图标。 |
| `custom/android/res/drawable-xhdpi/icon.png` | Android xhdpi 密度应用启动图标。 |
| `custom/android/res/drawable-xxhdpi/icon.png` | Android xxhdpi 密度应用启动图标。 |
| `custom/android/res/drawable-xxxhdpi/icon.png` | Android xxxhdpi 密度应用启动图标。 |
| `custom/deploy/windows/installheader.bmp` | Windows 安装程序顶部横幅，由 `QGC_WINDOWS_INSTALL_HEADER_PATH` 使用。 |
| `custom/deploy/windows/WindowsQGC.ico` | Windows 可执行文件和安装包图标，由 `QGC_WINDOWS_ICON_PATH` 使用。 |
| `custom/res/icons/custom_qgroundcontrol.ico` | QRC 中 `/Custom/res/qgroundcontrol.ico` 的 ICO 资源，供应用内部或 Windows 资源引用。 |
| `custom/res/icons/custom_qgroundcontrol.png` | Linux AppImage 图标，由 `QGC_APPIMAGE_ICON_PATH` 使用。 |
| `custom/res/Images/dronecode-black.svg` | 室外/浅色背景品牌图，`CustomPlugin::brandImageOutdoor()` 返回该资源。 |
| `custom/res/Images/dronecode-white.svg` | 室内/深色背景品牌图，`CustomPlugin::brandImageIndoor()` 返回该资源。 |
| `custom/res/Images/void.png` | 上游 custom 示例遗留占位图片；当前未注册到 QRC、没有运行时引用，保留时不影响功能。 |
| `custom/res/QGCLogoFull.svg` | custom 完整 Logo，注册为 `qrc:/Custom/res/QGCLogoFull.svg`。 |
| `custom/res/macx.icns` | macOS 应用图标；`QGC_MACOS_ICON_PATH` 指向其所在目录。 |

### 3.2 custom 插件、飞控插件与通信后端

| 文件 | 详细作用 |
|---|---|
| `custom/src/CustomPlugin.h` | 声明 `CustomPlugin`、`CustomOptions`、`CustomFlyViewOptions` 和 `CustomOverrideInterceptor`；定义 Viewer3D/Gimbal QML 属性及插件生命周期接口。 |
| `custom/src/CustomPlugin.cc` | custom 运行总入口实现。安装默认 UDP 链路、加载翻译、创建 Viewer3D/Gimbal 对象、安装 A8 Mini 视频默认值、过滤自动视频流消息、配置品牌和调色板，并安装 QML URL 拦截器。 |
| `custom/src/AutoPilotPlugins/CustomAutoPilotPlugin.h` | 声明继承 `PX4AutoPilotPlugin` 的 custom 自动驾驶插件和自定义车辆组件接口。 |
| `custom/src/AutoPilotPlugins/CustomAutoPilotPlugin.cc` | 实现 PX4 custom 自动驾驶插件，响应高级 UI 状态并生成当前项目需要的车辆组件列表。 |
| `custom/src/Comms/DefaultCommunicationLinkInstaller.h` | 声明默认通信链路幂等安装器 `ensureInstalled()`。 |
| `custom/src/Comms/DefaultCommunicationLinkInstaller.cc` | 在 QGC 加载链路前检查 QSettings；不存在 `local/Local` 时写入 UDP、本地端口 0、远端 `192.168.144.20:19856`，自动连接和高延迟均关闭。 |
| `custom/src/FirmwarePlugin/CustomFirmwarePlugin.h` | 声明 custom PX4 固件插件，覆盖自动驾驶插件创建、工具栏指示器、云台能力和飞行模式列表。 |
| `custom/src/FirmwarePlugin/CustomFirmwarePlugin.cc` | 实现 custom 固件行为：创建 `CustomAutoPilotPlugin`、移除 RC RSSI 指示器、追加 Fuel 指示器、声明 pitch/yaw 云台能力，并限制可设置的飞行模式。 |
| `custom/src/FirmwarePlugin/CustomFirmwarePluginFactory.h` | 声明 custom 固件插件工厂，替代被禁用的 QGC 原生 PX4 工厂。 |
| `custom/src/FirmwarePlugin/CustomFirmwarePluginFactory.cc` | 根据 MAV_AUTOPILOT/PX4 条件创建并返回 `CustomFirmwarePlugin` 实例。 |

### 3.3 FlightDisplay 文件

| 文件 | 详细作用 |
|---|---|
| `custom/src/FlightDisplay/CustomGuidedActionsController.qml` | 同名覆盖 QGC custom 引导动作控制器，定义 custom action ID、按钮标题和动作处理入口。 |
| `custom/src/FlightDisplay/FlyView.qml` | 同名覆盖 Fly View 根界面；保留原生地图、视频、任务控制器和控件层，增加 `Viewer3D` 根组件，并在 3D 打开时禁用 2D 地图交互。 |
| `custom/src/FlightDisplay/FlyViewCustomLayer.qml` | Fly View custom 叠加层；显示 GPS/母线电压告警、顶部罗盘条、罗盘指针和 custom 姿态仪。 |
| `custom/src/FlightDisplay/FlyViewToolStrip.qml` | 同名覆盖左侧 ToolStrip 容器，加载 custom `FlyViewToolStripActionList`，其余 ToolStrip 行为继续使用 QGC 原生控件。 |
| `custom/src/FlightDisplay/FlyViewToolStripActionList.qml` | 同名覆盖飞行动作列表；加入 3D View/Fly 切换按钮，并保留检查单、起飞、降落、返航、暂停、附加动作和 custom 引导动作。 |
| `custom/src/FlightDisplay/FlyViewTopRightColumnLayout.qml` | 同名覆盖右侧控制列；启用私有云台缩放时显示 `GimbalZoomControl` 并替换原生拍照/录像栏，关闭后恢复 `PhotoVideoControl`。 |
| `custom/src/FlightDisplay/FlyViewWidgetLayer.qml` | 同名覆盖飞行控件层；组织工具栏、虚拟摇杆、地图比例尺、右侧布局等，在 Viewer3D 打开时隐藏仅适用于二维地图的控件。 |
| `custom/src/FlightDisplay/GimbalZoomControl.qml` | 新增思翼缩放 UI；提供白色圆形加减按钮、当前倍率胶囊和半透明面板，每 2 秒请求一次真实倍率。 |

### 3.4 FlightMap 文件

| 文件 | 详细作用 |
|---|---|
| `custom/src/FlightMap/Images/attitude_crosshair.svg` | custom 姿态仪固定准星图层。 |
| `custom/src/FlightMap/Images/attitude_dial.svg` | custom 姿态仪角度刻度盘。 |
| `custom/src/FlightMap/Images/attitude_pointer.svg` | custom 姿态仪滚转/俯仰指针。 |
| `custom/src/FlightMap/Images/compass_needle.svg` | Fly View 圆形罗盘的航向针。 |
| `custom/src/FlightMap/Images/compass_pointer.svg` | Fly View 顶部罗盘条的固定中心指针。 |
| `custom/src/FlightMap/Widgets/CustomArtificialHorizon.qml` | 根据飞行器 roll/pitch 绘制和旋转人工地平线背景。 |
| `custom/src/FlightMap/Widgets/CustomAttitudeWidget.qml` | 组合人工地平线、姿态刻度盘、指针和准星，对外提供稳定尺寸的 custom 姿态组件。 |

### 3.5 Gimbal 文件

| 文件 | 详细作用 |
|---|---|
| `custom/src/Gimbal/GimbalControl.SettingsGroup.json` | 定义 `enabled`、`sdkHost`、`sdkPort`、`zoomStep`、`mavlinkAutoVideoStream` 五个持久化 Fact 的类型、范围和默认值。 |
| `custom/src/Gimbal/GimbalControlManager.h` | 声明 QML 可调用的缩放管理器，暴露启用状态、当前倍率、步长、1.0x/5.5x 边界、SDK 响应状态、错误和缩放方法。 |
| `custom/src/Gimbal/GimbalControlManager.cc` | 实现倍率增减/钳制、SDK endpoint 更新、乐观 UI 更新、真实倍率回读、1.5 秒响应超时和设置变化处理。 |
| `custom/src/Gimbal/GimbalControlSettings.h` | 声明 `SettingsGroup` 子类及五个 Fact 访问器。 |
| `custom/src/Gimbal/GimbalControlSettings.cc` | 将设置组名绑定为 `GimbalControl`，并加载 `/json/GimbalControl.SettingsGroup.json` 元数据。 |
| `custom/src/Gimbal/GimbalVideoStreamSupport.h` | 声明 A8 Mini 视频默认值安装和 MAVLink 视频流消息过滤接口。 |
| `custom/src/Gimbal/GimbalVideoStreamSupport.cc` | 安装 `rtsp://192.168.144.25:8554/main.264`、20 秒超时和 RTSP source；迁移旧 rtspt URL，并按设置过滤 `VIDEO_STREAM_INFORMATION`。 |
| `custom/src/Gimbal/SiyiProtocol.h` | 声明思翼私有协议命令、解码结果结构、缩放封包、倍率查询、CRC 和 payload 解析接口。 |
| `custom/src/Gimbal/SiyiProtocol.cc` | 实现 `0x55 0x66` 帧、CRC16、小端字段、`0x0F` 绝对缩放和 `0x18` 当前倍率查询，行为与 Python SDK 对齐。 |
| `custom/src/Gimbal/SiyiSdk.h` | 声明基于 `QUdpSocket` 的思翼 SDK 客户端、endpoint、发送接口和响应信号。 |
| `custom/src/Gimbal/SiyiSdk.cc` | 发送私有 UDP 报文、读取 datagram、过滤非目标相机源地址、解码倍率响应并报告通信错误。 |

### 3.6 QmlControls 文件

| 文件 | 详细作用 |
|---|---|
| `custom/src/QmlControls/AppSettings.qml` | 同名覆盖 Application Settings 外壳；保留原生设置导航，只将 Fly View URL 重定向到 custom 设置页。 |
| `custom/src/QmlControls/CustomIconButton.qml` | `Custom.Widgets` 通用图标按钮，统一图标着色、尺寸和点击区域；当前作为可复用预留控件注册。 |
| `custom/src/QmlControls/CustomOnOffSwitch.qml` | `Custom.Widgets` On/Off 二态开关，提供中文翻译入口；当前作为可复用预留控件注册。 |
| `custom/src/QmlControls/CustomQuickButton.qml` | `Custom.Widgets` 快捷按钮基础组件；当前作为可复用预留控件注册。 |
| `custom/src/QmlControls/CustomSignalStrength.qml` | 根据百分比选择 QGC 原生 Signal0/20/40/80/100 图标的信号强度组件；当前作为可复用预留控件注册。 |
| `custom/src/QmlControls/CustomToolBarButton.qml` | custom 工具栏按钮基础组件，封装按钮颜色、图标和交互状态；当前作为可复用预留控件注册。 |
| `custom/src/QmlControls/CustomVehicleButton.qml` | 显示当前飞行器或 None 状态的车辆按钮；当前作为可复用预留控件注册。 |
| `custom/src/QmlControls/FuelStatusIndicator.qml` | 顶部 Fuel 指示器，读取活动飞行器 `fuelStatus` FactGroup，显示剩余百分比并提供剩余量、最大量、消耗、流量和温度详情。 |
| `custom/src/QmlControls/Viewer3D/qmldir` | 声明 `Viewer3D` QML 模块及 `Viewer3D`、`Viewer3DProgressBar` 类型。 |
| `custom/src/QmlControls/Viewer3D/Models3D/qmldir` | 声明 `Viewer3D.Models3D` 模块及相机、地图、航线、飞机、任务点类型。 |
| `custom/src/QmlControls/Viewer3D/Models3D/Drones/qmldir` | 声明 `Viewer3D.Models3D.Drones` 模块和 `DroneModelDjiF450` 类型。 |

### 3.7 UI 文件

| 文件 | 详细作用 |
|---|---|
| `custom/src/UI/AppSettings/FlyViewSettings.qml` | 同名覆盖 Application Settings 的 Fly View 页面；保留原生设置，并在底部加载 Viewer3D 和 Gimbal 两个设置组。 |
| `custom/src/UI/AppSettings/GimbalControlSettingsGroup.qml` | 显示 SIYI Gimbal Zoom 设置：启用、SDK Host、SDK Port、Zoom Step 和 MAVLink 自动视频流。 |
| `custom/src/UI/AppSettings/Viewer3DSettingsGroup.qml` | 显示 Viewer3D 设置；控制三种地图源互斥、OSM/外部模型文件选择、Google API Key、原点、单位、比例、yaw、楼层高度和高度偏移。 |
| `custom/src/UI/toolbar/Images/Fuel.svg` | FuelStatusIndicator 使用的顶部燃料图标。 |
| `custom/src/UI/toolbar/Images/PaperPlane.svg` | Viewer3D 已打开时，左侧按钮切换为 Fly 状态所用的白色返回飞行视图图标。 |
| `custom/src/UI/toolbar/Images/altitude.svg` | 高度工具栏图标，已注册到 custom QRC，当前 custom QML 未直接使用，作为原有遥测 UI 资源保留。 |
| `custom/src/UI/toolbar/Images/chronometer.svg` | 计时器工具栏图标，已注册，当前作为预留遥测资源。 |
| `custom/src/UI/toolbar/Images/distance.svg` | 距离工具栏图标，已注册，当前作为预留遥测资源。 |
| `custom/src/UI/toolbar/Images/horizontal_speed.svg` | 水平速度工具栏图标，已注册，当前作为预留遥测资源。 |
| `custom/src/UI/toolbar/Images/microSD.svg` | microSD 状态工具栏图标，已注册，当前作为预留遥测资源。 |
| `custom/src/UI/toolbar/Images/odometer.svg` | 里程工具栏图标，已注册，当前作为预留遥测资源。 |
| `custom/src/UI/toolbar/Images/vertical_speed.svg` | 垂直速度工具栏图标，已注册，当前作为预留遥测资源。 |

### 3.8 Viewer3D C++、设置与算法文件

| 文件 | 详细作用 |
|---|---|
| `custom/src/Viewer3D/CityMapGeometry.h` | 声明 OSM 城市建筑 `QQuick3DGeometry`、建筑顶点/索引数据和几何更新接口。 |
| `custom/src/Viewer3D/CityMapGeometry.cc` | 将 OSM 建筑轮廓、楼层和高度数据转成墙面/屋顶三维网格，并使用 earcut 进行屋顶三角剖分。 |
| `custom/src/Viewer3D/earcut.hpp` | Mapbox Earcut 单头文件三角剖分库，用于把建筑多边形转换为可渲染三角形。 |
| `custom/src/Viewer3D/External3DMapManager.h` | 声明外部模型格式识别、Balsam 查找/转换、导入状态和 QML 调用接口。 |
| `custom/src/Viewer3D/External3DMapManager.cc` | 实现 OBJ/glTF/GLB/QML 直接加载路径和 FBX/DAE/STL/PLY 的 Balsam 转换流程，管理子进程、输出目录、错误和最终设置路径。 |
| `custom/src/Viewer3D/OsmParser.h` | 声明 OSM 解析器、地图边界、建筑数据、道路数据、解析状态和线程协作接口。 |
| `custom/src/Viewer3D/OsmParser.cc` | 读取 `.osm` XML，解析 node/way/tag，计算 ROI、建筑高度和局部坐标，并向 Viewer3D 发出加载状态。 |
| `custom/src/Viewer3D/OsmParserThread.h` | 声明实际执行 OSM 文件解析的工作线程对象及完成/错误信号。 |
| `custom/src/Viewer3D/OsmParserThread.cc` | 在线程中打开和解析 OSM 文件，避免大地图解析阻塞 QML 主线程。 |
| `custom/src/Viewer3D/Viewer3D.SettingsGroup.json` | 定义 Viewer3D 启用、Google/外部/OSM 源、外部模型定位和缩放、楼层高度、飞行器高度偏移等 14 个 Fact。 |
| `custom/src/Viewer3D/Viewer3DManager.h` | 声明 Viewer3D QML 类型注册器，持有 `OsmParser` 和 `Viewer3DQmlBackend`。 |
| `custom/src/Viewer3D/Viewer3DManager.cc` | 创建 Viewer3D 后端对象，连接解析器，并向 `QGroundControl.Viewer3D` 注册地形、纹理、城市和坐标类型。 |
| `custom/src/Viewer3D/Viewer3DQmlBackend.h` | 声明 QML 后端及 `gpsRef` 属性，用于统一 3D 场景地理参考点。 |
| `custom/src/Viewer3D/Viewer3DQmlBackend.cc` | 监听活动飞行器、地图和外部模型设置，选择 GPS 参考点并把地理状态同步到三维 QML。 |
| `custom/src/Viewer3D/Viewer3DQmlVariableTypes.h` | 定义可注册到 QML 的地理坐标包装类型，在 WGS84 坐标与局部三维坐标之间联动转换。 |
| `custom/src/Viewer3D/Viewer3DSettings.h` | 声明 `Viewer3DSettings` SettingsGroup 和全部 Viewer3D Fact 访问器。 |
| `custom/src/Viewer3D/Viewer3DSettings.cc` | 注册 `Viewer3DSettings` QML 只读类型，并实现 14 个 Fact 与 JSON 元数据/QSettings 的绑定。 |
| `custom/src/Viewer3D/Viewer3DTerrainGeometry.h` | 声明地球曲面/地图 ROI 的 `QQuick3DGeometry`，暴露分段数、半径、ROI 和参考坐标。 |
| `custom/src/Viewer3D/Viewer3DTerrainGeometry.cc` | 根据 ROI 和 WGS84 地球参数生成带法线、UV 的三维地形网格，并在地图文件变化时清理重建。 |
| `custom/src/Viewer3D/Viewer3DTerrainTexture.h` | 声明 `QQuick3DTextureData`，暴露瓦片数量、下载进度、纹理加载和几何完成状态。 |
| `custom/src/Viewer3D/Viewer3DTerrainTexture.cc` | 根据 QGC 当前地图 provider/type 请求地图瓦片，拼接图片并生成地形纹理数据。 |
| `custom/src/Viewer3D/Viewer3DTileQuery.h` | 声明瓦片范围、缩放级别、经纬度/像素换算、瓦片统计和拼图容器。 |
| `custom/src/Viewer3D/Viewer3DTileQuery.cc` | 计算不超过 200 张瓦片的最高缩放级别，创建单瓦片请求并在完成后拼接纹理。 |
| `custom/src/Viewer3D/Viewer3DTileReply.h` | 声明单个地图瓦片请求对象、瓦片坐标/数据结构及完成/错误信号。 |
| `custom/src/Viewer3D/Viewer3DTileReply.cc` | 通过 QGC MapProvider 发起网络请求，处理超时、无瓦片图和返回数据。 |
| `custom/src/Viewer3D/Viewer3DUtils.h` | 声明 WGS84、ECEF、ENU 和 Viewer3D 局部坐标的双向转换函数。 |
| `custom/src/Viewer3D/Viewer3DUtils.cc` | 实现 WGS84 椭球参数下的 geodetic/ECEF/ENU 转换，供飞机、任务点、建筑和外部地图定位使用。 |

### 3.9 Viewer3D 基础资源

| 文件 | 详细作用 |
|---|---|
| `custom/src/Viewer3D/Images/city_3d_map_icon.svg` | Fly View 左侧 3D View 白色工具栏图标，QRC alias 为 `/Custom/qmlimages/Viewer3D/City3DMapIcon.svg`。 |
| `custom/src/Viewer3D/Shaders/earthMaterial.vert` | Viewer3D 地形/地球材质顶点着色器，处理顶点位置、法线和纹理坐标。 |
| `custom/src/Viewer3D/Shaders/earthMaterial.frag` | Viewer3D 地形/地球材质片元着色器，输出地图纹理和光照颜色。 |
| `custom/src/Viewer3D/SampleOsmMap/map_sim_small.osm` | 小型本地 OSM 示例，用于验证 OSM 选择、解析、建筑生成和地形加载。 |

### 3.10 ExternalWGS84_UE5_MapSample 文件

| 文件 | 详细作用 |
|---|---|
| `custom/src/Viewer3D/ExternalWGS84_UE5_MapSample/README.md` | 外部 WGS84 城镇样例使用说明，记录推荐 OBJ、依赖文件、场景内容、QGC 原点/比例/yaw 参数、数据来源和限制。 |
| `custom/src/Viewer3D/ExternalWGS84_UE5_MapSample/qgc_viewer3d_import_settings.json` | 机器可读的样例元数据，记录 WGS84 原点、ENU 轴约定、模型单位、比例、yaw、资产文件、统计和 OSM 许可。 |
| `custom/src/Viewer3D/ExternalWGS84_UE5_MapSample/osm_overpass_source.json` | 生成样例所用 Overpass 查询、bbox、下载时间和原始 OSM JSON，便于追溯/重新生成模型；不由 QGC 运行时读取。 |
| `custom/src/Viewer3D/ExternalWGS84_UE5_MapSample/realistic_town_wgs84_map.obj` | Viewer3D 推荐直接加载的带 UV 城镇模型，引用同目录 MTL 和 textures。 |
| `custom/src/Viewer3D/ExternalWGS84_UE5_MapSample/realistic_town_wgs84_map.mtl` | OBJ 材质定义，建立道路、草地、建筑立面、屋顶、商铺、树木等材质与纹理文件的映射。 |
| `custom/src/Viewer3D/ExternalWGS84_UE5_MapSample/realistic_town_wgs84_map.fbx` | 同一城镇的 FBX 交换格式样例，用于验证 Balsam 转换路径；运行时优先使用 OBJ。 |
| `custom/src/Viewer3D/ExternalWGS84_UE5_MapSample/textures/asphalt_worn.png` | 道路磨损沥青纹理。 |
| `custom/src/Viewer3D/ExternalWGS84_UE5_MapSample/textures/facade_brick_windows.png` | 砖墙窗户建筑立面纹理。 |
| `custom/src/Viewer3D/ExternalWGS84_UE5_MapSample/textures/facade_light_windows.png` | 浅色墙面窗户建筑立面纹理。 |
| `custom/src/Viewer3D/ExternalWGS84_UE5_MapSample/textures/facade_modern_windows.png` | 现代玻璃/窗格建筑立面纹理。 |
| `custom/src/Viewer3D/ExternalWGS84_UE5_MapSample/textures/facade_tan_windows.png` | 棕黄色墙面窗户建筑立面纹理。 |
| `custom/src/Viewer3D/ExternalWGS84_UE5_MapSample/textures/grass_mixed.png` | 草地和绿化区域纹理。 |
| `custom/src/Viewer3D/ExternalWGS84_UE5_MapSample/textures/roof_flat_gray.png` | 灰色平屋顶纹理。 |
| `custom/src/Viewer3D/ExternalWGS84_UE5_MapSample/textures/roof_tile_red.png` | 红色瓦片屋顶纹理。 |
| `custom/src/Viewer3D/ExternalWGS84_UE5_MapSample/textures/shopfront_facade.png` | 商铺橱窗/沿街店面纹理。 |
| `custom/src/Viewer3D/ExternalWGS84_UE5_MapSample/textures/sidewalk_concrete.png` | 人行道混凝土纹理。 |
| `custom/src/Viewer3D/ExternalWGS84_UE5_MapSample/textures/tree_leaf.png` | 低多边形树冠叶片纹理。 |

### 3.11 Viewer3D QML 场景文件

| 文件 | 详细作用 |
|---|---|
| `custom/src/Viewer3D/Viewer3DQml/Viewer3D.qml` | Viewer3D 根组件；维护 `isOpen`，按设置选择 Google 或本地 Quick3D 场景，并提供 open/close 入口。 |
| `custom/src/Viewer3D/Viewer3DQml/Viewer3DProgressBar.qml` | 显示 OSM 解析、地形几何和瓦片纹理加载进度。 |
| `custom/src/Viewer3D/Viewer3DQml/Google3DMapView.qml` | 使用 Qt WebEngine 构造 Google Maps JavaScript 3D 页面，以活动飞行器或地图中心为初始坐标。 |
| `custom/src/Viewer3D/Viewer3DQml/Google3DMapUnavailable.qml` | Google 3D 已启用但构建未包含 Qt WebEngine 时显示明确的依赖提示。 |
| `custom/src/Viewer3D/Viewer3DQml/Models3D/CameraLightModel.qml` | 定义三维场景相机节点、视角重置、环绕位置和多方向光源。 |
| `custom/src/Viewer3D/Viewer3DQml/Models3D/External3DMap.qml` | 根据文件扩展名加载 OBJ/glTF/GLB 或转换后 QML，应用单位、用户比例和 yaw，并输出阻塞错误提示。 |
| `custom/src/Viewer3D/Viewer3DQml/Models3D/Line3D.qml` | 在任意两个三维点之间生成有宽度和颜色的线段，用于任务航线。 |
| `custom/src/Viewer3D/Viewer3DQml/Models3D/Viewer3DModel.qml` | 本地 Quick3D 主场景；组合相机、光照、地形/外部地图、建筑、飞机与任务，并实现鼠标/触控旋转、平移和缩放。 |
| `custom/src/Viewer3D/Viewer3DQml/Models3D/Viewer3DVehicleItems.qml` | 将活动飞行器、任务点、返航状态和任务航线转换到局部三维坐标并创建对应模型。 |
| `custom/src/Viewer3D/Viewer3DQml/Models3D/Waypoint3DModel.qml` | 使用锥体和颜色区分起飞、返航、降落及普通任务点，并应用任务高度偏移。 |

### 3.12 DJI F450 模型生成文件

`DroneModelDjiF450.qml` 负责组合下列部件。每个部件 QML 定义材质、Transform 和对应 `node.mesh`；mesh 是 Qt Quick 3D 实际读取的二进制几何，不能只移动 QML 而遗漏 mesh。

| 文件 | 详细作用 |
|---|---|
| `custom/src/Viewer3D/Viewer3DQml/Drones/DroneModelDjiF450.qml` | F450 飞行器总装组件，组合机身上下板、四个机臂、四个电机和四组螺旋桨部件。 |
| `custom/src/Viewer3D/Viewer3DQml/Drones/Djif450/DroneModel_arm_1/DroneModel_arm_1.qml` | 第 1 个 F450 机臂部件 QML。 |
| `custom/src/Viewer3D/Viewer3DQml/Drones/Djif450/DroneModel_arm_1/node.mesh` | 第 1 个机臂运行时几何。 |
| `custom/src/Viewer3D/Viewer3DQml/Drones/Djif450/DroneModel_arm_1/meshes/node.mesh` | 第 1 个机臂转换产物中的辅助 mesh 副本；当前 `custom.qrc` 未注册，保留用于资产重新导入对照。 |
| `custom/src/Viewer3D/Viewer3DQml/Drones/Djif450/DroneModel_arm_2/DroneModel_arm_2.qml` | 第 2 个 F450 机臂部件 QML。 |
| `custom/src/Viewer3D/Viewer3DQml/Drones/Djif450/DroneModel_arm_2/node.mesh` | 第 2 个机臂运行时几何。 |
| `custom/src/Viewer3D/Viewer3DQml/Drones/Djif450/DroneModel_arm_3/DroneModel_arm_3.qml` | 第 3 个 F450 机臂部件 QML。 |
| `custom/src/Viewer3D/Viewer3DQml/Drones/Djif450/DroneModel_arm_3/node.mesh` | 第 3 个机臂运行时几何。 |
| `custom/src/Viewer3D/Viewer3DQml/Drones/Djif450/DroneModel_arm_4/DroneModel_arm_4.qml` | 第 4 个 F450 机臂部件 QML。 |
| `custom/src/Viewer3D/Viewer3DQml/Drones/Djif450/DroneModel_arm_4/node.mesh` | 第 4 个机臂运行时几何。 |
| `custom/src/Viewer3D/Viewer3DQml/Drones/Djif450/DroneModel_Base_bottom_1/DroneModel_Base_bottom_1.qml` | F450 机身下板部件 QML。 |
| `custom/src/Viewer3D/Viewer3DQml/Drones/Djif450/DroneModel_Base_bottom_1/node.mesh` | F450 机身下板运行时几何。 |
| `custom/src/Viewer3D/Viewer3DQml/Drones/Djif450/DroneModel_Base_Top_1/DroneModel_Base_Top_1.qml` | F450 机身上板部件 QML。 |
| `custom/src/Viewer3D/Viewer3DQml/Drones/Djif450/DroneModel_Base_Top_1/node.mesh` | F450 机身上板运行时几何。 |
| `custom/src/Viewer3D/Viewer3DQml/Drones/Djif450/DroneModel_BLDC_1/DroneModel_BLDC_1.qml` | 第 1 个无刷电机部件 QML。 |
| `custom/src/Viewer3D/Viewer3DQml/Drones/Djif450/DroneModel_BLDC_1/node.mesh` | 第 1 个无刷电机运行时几何。 |
| `custom/src/Viewer3D/Viewer3DQml/Drones/Djif450/DroneModel_BLDC_2/DroneModel_BLDC_2.qml` | 第 2 个无刷电机部件 QML。 |
| `custom/src/Viewer3D/Viewer3DQml/Drones/Djif450/DroneModel_BLDC_2/node.mesh` | 第 2 个无刷电机运行时几何。 |
| `custom/src/Viewer3D/Viewer3DQml/Drones/Djif450/DroneModel_BLDC_3/DroneModel_BLDC_3.qml` | 第 3 个无刷电机部件 QML。 |
| `custom/src/Viewer3D/Viewer3DQml/Drones/Djif450/DroneModel_BLDC_3/node.mesh` | 第 3 个无刷电机运行时几何。 |
| `custom/src/Viewer3D/Viewer3DQml/Drones/Djif450/DroneModel_BLDC_4/DroneModel_BLDC_4.qml` | 第 4 个无刷电机部件 QML。 |
| `custom/src/Viewer3D/Viewer3DQml/Drones/Djif450/DroneModel_BLDC_4/node.mesh` | 第 4 个无刷电机运行时几何。 |
| `custom/src/Viewer3D/Viewer3DQml/Drones/Djif450/DroneModel_propeller2_2/DroneModel_propeller2_2.qml` | 第 1 组 propeller2 螺旋桨部件 QML。 |
| `custom/src/Viewer3D/Viewer3DQml/Drones/Djif450/DroneModel_propeller2_2/node.mesh` | 第 1 组 propeller2 螺旋桨运行时几何。 |
| `custom/src/Viewer3D/Viewer3DQml/Drones/Djif450/DroneModel_propeller2_7/DroneModel_propeller2_7.qml` | 第 2 组 propeller2 螺旋桨部件 QML。 |
| `custom/src/Viewer3D/Viewer3DQml/Drones/Djif450/DroneModel_propeller2_7/node.mesh` | 第 2 组 propeller2 螺旋桨运行时几何。 |
| `custom/src/Viewer3D/Viewer3DQml/Drones/Djif450/DroneModel_propeller22_1/DroneModel_propeller22_1.qml` | 第 1 组 propeller22 螺旋桨部件 QML。 |
| `custom/src/Viewer3D/Viewer3DQml/Drones/Djif450/DroneModel_propeller22_1/node.mesh` | 第 1 组 propeller22 螺旋桨运行时几何。 |
| `custom/src/Viewer3D/Viewer3DQml/Drones/Djif450/DroneModel_propeller22_2/DroneModel_propeller22_2.qml` | 第 2 组 propeller22 螺旋桨部件 QML。 |
| `custom/src/Viewer3D/Viewer3DQml/Drones/Djif450/DroneModel_propeller22_2/node.mesh` | 第 2 组 propeller22 螺旋桨运行时几何。 |

### 3.13 翻译文件

| 文件 | 详细作用 |
|---|---|
| `custom/translations/custom.ts` | custom `qsTr()` 文本的源语言 TS，保存上下文和源码位置，供翻译工具更新。 |
| `custom/translations/custom_zh_CN.ts` | custom 简体中文翻译；运行时编译为 QM，由 `CustomPlugin::init()` 按 `zh_CN` locale 加载。 |
| `custom/translations/custom-lupdate.sh` | 调用 Qt `lupdate` 扫描 `custom/src` 和 `custom/res`；脚本中的默认 Qt 路径是开发机示例，其他环境应通过 `LUPDATE` 环境变量覆盖。 |

## 4. 与 QGC 原生 src 的对应关系

| custom 路径 | QGC 原生参照路径 | 规则 |
|---|---|---|
| `custom/src/QmlControls/AppSettings.qml` | `src/QmlControls/AppSettings.qml` | 同名覆盖 Application Settings 外壳 |
| `custom/src/UI/AppSettings/FlyViewSettings.qml` | `src/UI/AppSettings/FlyViewSettings.qml` | 同名覆盖 Fly View 设置页 |
| `custom/src/FlightDisplay/FlyView.qml` | `src/FlightDisplay/FlyView.qml` | 同名覆盖飞行主界面 |
| `custom/src/FlightDisplay/FlyViewWidgetLayer.qml` | `src/FlightDisplay/FlyViewWidgetLayer.qml` | 同名覆盖飞行控件层 |
| `custom/src/FlightDisplay/FlyViewToolStrip.qml` | `src/FlightDisplay/FlyViewToolStrip.qml` | 同名覆盖左侧工具栏 |
| `custom/src/FlightDisplay/FlyViewToolStripActionList.qml` | `src/FlightDisplay/FlyViewToolStripActionList.qml` | 同名覆盖工具栏动作列表 |
| `custom/src/FlightDisplay/FlyViewTopRightColumnLayout.qml` | `src/FlightDisplay/FlyViewTopRightColumnLayout.qml` | 同名覆盖右侧相机区域 |
| `custom/src/FlightDisplay/FlyViewCustomLayer.qml` | `src/FlightDisplay/FlyViewCustomLayer.qml` | 同名 custom 图层入口 |
| `custom/src/FlightDisplay/CustomGuidedActionsController.qml` | `src/FlightDisplay/CustomGuidedActionsController.qml` | 同名引导动作扩展入口 |
| `custom/src/Viewer3D` | `src/Viewer3D` | Viewer3D 后端、QML、图标、shader 和模型保持同结构 |
| `custom/src/Gimbal` | `src/Gimbal` | 私有 SDK 后端归入云台模块，但不覆盖原生 Gimbal 类 |
| `custom/src/Comms` | `src/Comms` | 默认链路安装逻辑归入通信模块 |
| `custom/src/AutoPilotPlugins` | `src/AutoPilotPlugins` | 保持原生复数目录名 |
| `custom/src/FirmwarePlugin` | `src/FirmwarePlugin` | 自定义固件插件和工厂 |
| `custom/src/FlightMap/Images` | `src/FlightMap/Images` | 姿态仪、罗盘等飞行地图资源 |
| `custom/src/FlightMap/Widgets` | `src/FlightMap/Widgets` | 自定义姿态仪表 QML |
| `custom/src/UI/toolbar/Images` | `src/UI/toolbar/Images` | 顶部栏、飞行工具栏图标 |

## 5. 编译与资源接入

### 5.1 custom/CMakeLists.txt

| 配置 | 作用 |
|---|---|
| `QGC_CUSTOM_BUILD` | 启用 QGC custom build |
| `CUSTOMHEADER=CustomPlugin.h` | 指定 custom 插件头文件 |
| `CUSTOMCLASS=CustomPlugin` | 指定 custom 插件类 |
| `CUSTOM_QT_COMPONENTS` | 追加 `Quick3D` 和 `Quick3DAssetUtils` |
| `Qt6::WebEngineQuick` | 可选查找；存在时定义 `QGC_VIEWER3D_GOOGLE_WEBENGINE` |
| `CUSTOM_RESOURCES` | 将 `custom.qrc` 注入主程序 |
| `CustomModule` | 静态 QML 模块，URI 为 `Custom.Widgets` |
| `VIEWER3D_SOURCES` | 收集 `src/Viewer3D` C++ 源码 |
| `GIMBAL_SOURCES` | 收集 `src/Gimbal` C++ 源码 |
| `COMMS_SOURCES` | 收集 `src/Comms` C++ 源码 |
| `CUSTOM_SOURCES` | 汇总插件、Viewer3D、Gimbal 和 Comms 源码 |
| `CUSTOM_INCLUDE_DIRECTORIES` | 注册与 QGC 原生同名的 custom 模块包含目录 |

`Custom.Widgets` 的物理文件可以位于 `QmlControls` 和 `FlightMap/Widgets`，CMake 通过 `QT_RESOURCE_ALIAS` 保持 QML 类型名不变。

### 5.2 custom/custom.qrc

| 资源前缀 | 内容与用途 |
|---|---|
| `/custom/img` | custom 品牌图片，仅保留 `dronecode-black/white.svg` |
| `/Custom/qmlimages` | custom 图标，使用独立前缀避免覆盖 QGC 同名资源 |
| `/Custom/qml` | 与 QGC 模块路径一致的 QML 覆盖文件和新增组件 |
| `/Custom/res` | custom Logo 和应用图标 |
| `/qml` | `Viewer3D`、`Viewer3D.Models3D`、`Viewer3D.Models3D.Drones` QML 模块 |
| `/json` | Viewer3D 与 Gimbal 的 FactMetaData JSON |
| `/ShaderVertex` | Viewer3D 顶点 shader |
| `/ShaderFragment` | Viewer3D 片元 shader |

QML 覆盖示例：

```text
QGC 请求：qrc:/qml/QGroundControl/FlightDisplay/FlyView.qml
拦截检查：:/Custom/qml/QGroundControl/FlightDisplay/FlyView.qml
存在：加载 custom 覆盖
不存在：继续加载 QGC 原生资源
```

该逻辑由 `CustomOverrideInterceptor` 完成，同时兼容 Qt 6 的 `/qml`、`/qt/qml` 和 `/QGroundControl` URL 形式。只有 `custom.qrc` 中真实存在的同路径资源才会覆盖原生 QML。

## 6. CustomPlugin 运行入口

### 6.1 关键职责

| 文件/接口 | 作用 |
|---|---|
| `CustomPlugin::init()` | 安装默认链路、加载 custom 翻译、注册 Viewer3D 类型、创建 Viewer3D/Gimbal 设置和管理器、安装 A8 Mini 视频默认值 |
| `CustomPlugin::createQmlApplicationEngine()` | 获取 QML Engine 并安装 `CustomOverrideInterceptor` |
| `CustomPlugin::mavlinkMessage()` | 根据设置决定是否过滤 `VIDEO_STREAM_INFORMATION` |
| `CustomPlugin::adjustSettingMetaData()` | 调整 custom 相关 QGC 设置元数据 |
| `CustomPlugin::options()` | 提供 custom 应用选项和 Fly View 选项 |
| `CustomPlugin::brandImageIndoor/Outdoor()` | 返回 custom 品牌图片 |
| `CustomPlugin::paletteOverride()` | 覆盖部分 QGC 调色板颜色 |

### 6.2 暴露给 QML 的对象

| QML 属性 | C++ 对象 | 用途 |
|---|---|---|
| `QGroundControl.corePlugin.viewer3DSettings` | `Viewer3DSettings` | Viewer3D Fact 设置组 |
| `QGroundControl.corePlugin.external3DMapManager` | `External3DMapManager` | 外部模型导入、转换和状态反馈 |
| `QGroundControl.corePlugin.google3DMapsAvailable` | `bool` | 当前构建是否包含 WebEngineQuick |
| `QGroundControl.corePlugin.gimbalControlSettings` | `GimbalControlSettings` | 思翼缩放 Fact 设置组 |
| `QGroundControl.corePlugin.gimbalControlManager` | `GimbalControlManager` | 当前倍率、缩放接口和 SDK 状态 |

## 7. Application Settings 与 Fly View 覆盖

### 7.1 Application Settings

| 文件 | 作用 |
|---|---|
| `QmlControls/AppSettings.qml` | 保留 QGC 左侧设置导航，只将 Fly View 页路由到 custom 版本 |
| `UI/AppSettings/FlyViewSettings.qml` | 保留原生 Fly View 设置，并在底部依次加载 Viewer3D、Gimbal 设置组 |
| `UI/AppSettings/Viewer3DSettingsGroup.qml` | Viewer3D 开关、地图源、文件和坐标参数 UI |
| `UI/AppSettings/GimbalControlSettingsGroup.qml` | 思翼私有 SDK、分度值和自动视频流 UI |

页面顺序：

```text
Application Settings
  Fly View
    QGC 原有 Fly View 设置
    3D View
    SIYI Gimbal Zoom
```

### 7.2 Fly View

| 文件 | 作用 |
|---|---|
| `FlightDisplay/FlyView.qml` | 挂载 Viewer3D；3D 打开时禁用 2D 地图交互 |
| `FlightDisplay/FlyViewToolStrip.qml` | 使用 custom 动作列表，保留原生 ToolStrip 结构 |
| `FlightDisplay/FlyViewToolStripActionList.qml` | 在原生动作序列前加入 3D View/Fly 切换按钮 |
| `FlightDisplay/FlyViewWidgetLayer.qml` | 保留原生飞行控件层；3D 打开时隐藏 MapScale |
| `FlightDisplay/FlyViewTopRightColumnLayout.qml` | 私有缩放开启时替换 `PhotoVideoControl`，关闭时回退原生拍照/录像控件 |
| `FlightDisplay/GimbalZoomControl.qml` | 半透明 `+ / 当前倍率 / -` 缩放控件 |
| `FlightDisplay/FlyViewCustomLayer.qml` | 低母线电压告警、罗盘条、罗盘和姿态控件 |
| `FlightDisplay/CustomGuidedActionsController.qml` | custom 引导动作入口 |

## 8. Viewer3D 模块

### 8.1 使用流程

1. 打开 `Application Settings -> Fly View -> 3D View`。
2. 打开 `Enabled`。
3. 选择一种地图源：Google 3D Maps、外部三维模型或本地 OSM。
4. 返回 Fly View，左侧出现白色 `3D View` 图标。
5. 点击进入三维视图；按钮切换为 `Fly`，再次点击返回原飞行界面。

Google、外部模型、本地 OSM 三种模式互斥：Google 开启时关闭外部模型；外部模型开启时关闭 Google；两者都关闭时使用本地 OSM。

### 8.2 Viewer3D 参数

| 参数 | 类型/范围 | 默认值 | 说明 |
|---|---|---|---|
| `enabled` | bool | `false` | 是否启用 Viewer3D 和工具栏入口 |
| `useGoogle3DMapSource` | bool | `false` | 使用 Google 3D Maps 在线源 |
| `google3DMapsApiKey` | string | 空 | Google Maps JavaScript API Key |
| `useExternal3DMapSource` | bool | `false` | 使用外部模型地图 |
| `external3DMapFilePath` | string | 未选择提示 | 外部模型或转换后 QML 路径 |
| `external3DMapOriginLatitude` | double/deg | `0` | 外部模型局部原点 WGS84 纬度 |
| `external3DMapOriginLongitude` | double/deg | `0` | 外部模型局部原点 WGS84 经度 |
| `external3DMapOriginAltitude` | double/m | `0` | 外部模型局部原点高度 |
| `external3DMapUnitToMeters` | double/m per unit | `0.01` | 一个模型单位对应的米数；UE 厘米导出使用 0.01 |
| `external3DMapScale` | double | `1` | 单位换算后的附加缩放 |
| `external3DMapYaw` | double/deg | `0` | 模型朝向转到 QGC ENU 的偏航角；0 表示 +Y 北、+X 东 |
| `osmFilePath` | string | 未选择提示 | 本地 `.osm` 文件 |
| `buildingLevelHeight` | double/m | `3` | OSM 建筑每层平均高度 |
| `altitudeBias` | double/m | `0` | 三维视图中飞行器显示高度偏移 |

### 8.3 外部模型格式

| 类型 | 格式 | 处理方式 |
|---|---|---|
| 直接加载 | `.obj`、`.gltf`、`.glb`、Balsam 生成的 `.qml` | 保存路径后由 Viewer3D 加载 |
| 需要转换 | `.fbx`、`.dae`、`.stl`、`.ply` | `External3DMapManager` 调用 Qt Balsam 转为 QML/mesh |

Qt 安装中找不到 Balsam 时，转换格式不能使用，但不会影响 OSM 和直接加载格式。`ExternalWGS84_UE5_MapSample` 提供 WGS84 原点、导入参数、OBJ/FBX 和纹理示例。

### 8.4 关键调用链

```text
Viewer3DSettingsGroup.qml
  -> Viewer3DSettings (Fact)
  -> FlyViewToolStripActionList.qml 控制入口可见性
  -> FlyView.qml 挂载 Viewer3D.qml
  -> Viewer3DManager / Viewer3DQmlBackend
  -> OsmParser / Terrain / Tile / External3DMapManager
```

### 8.5 Viewer3D 关键文件职责

| 文件 | 作用 |
|---|---|
| `Viewer3D.SettingsGroup.json` | 定义 14 个 Viewer3D Fact 的类型、单位和默认值 |
| `Viewer3DSettings.h/.cc` | `SettingsGroup` 实现，向 C++/QML 提供持久化 Fact |
| `Viewer3DManager.h/.cc` | 注册 Viewer3D QML 类型，持有 OSM 解析器和 QML 后端 |
| `Viewer3DQmlBackend.h/.cc` | 连接活动飞行器、任务数据和三维 QML 场景 |
| `Viewer3DQmlVariableTypes.h` | Viewer3D C++ 与 QML 共享的数据结构 |
| `External3DMapManager.h/.cc` | 判断模型格式、查找 Balsam、启动转换进程并更新导入状态 |
| `OsmParser.h/.cc` | 解析 OSM 节点、道路、建筑和地理范围 |
| `OsmParserThread.h/.cc` | 在线程中执行 OSM 解析，避免阻塞 Fly View |
| `CityMapGeometry.h/.cc` | 根据 OSM 建筑轮廓和楼层数据生成三维几何 |
| `Viewer3DTerrainGeometry.h/.cc` | 生成地图 ROI 对应的三维地形网格 |
| `Viewer3DTerrainTexture.h/.cc` | 组织地图瓦片并生成地形纹理数据 |
| `Viewer3DTileQuery.h/.cc` | 计算瓦片范围、缩放级别和下载任务 |
| `Viewer3DTileReply.h/.cc` | 处理单个地图瓦片网络响应和错误 |
| `Viewer3DUtils.h/.cc` | 经纬度、局部坐标和 Viewer3D 通用转换函数 |
| `earcut.hpp` | 建筑多边形三角剖分第三方头文件 |
| `Viewer3DQml/Viewer3D.qml` | 三维视图根组件，负责打开、关闭和地图源选择 |
| `Viewer3DQml/Google3DMapView.qml` | WebEngineQuick 可用时显示 Google 3D Maps |
| `Viewer3DQml/Google3DMapUnavailable.qml` | 未编译 WebEngineQuick 时显示明确的降级状态 |
| `Viewer3DQml/Viewer3DProgressBar.qml` | OSM、地形和纹理加载进度 |
| `Models3D/External3DMap.qml` | 加载外部模型并应用原点、单位、比例和 yaw |
| `Models3D/Viewer3DModel.qml` | 三维相机、旋转、平移和缩放交互 |
| `Models3D/Viewer3DVehicleItems.qml` | 将飞行器状态映射到三维飞机模型 |
| `Models3D/Waypoint3DModel.qml` | 三维任务点显示 |
| `Models3D/Line3D.qml` | 航线和三维线段显示 |
| `Drones/DroneModelDjiF450.qml` | 组合 DJI F450 各部件 mesh |

## 9. 思翼 A8 Mini 云台缩放模块

### 9.1 UI 显示条件与使用

右侧缩放栏的实际显示条件是：

```text
存在 activeVehicle
AND GimbalControl.enabled == true
```

当前不依赖 QGC 是否发现 MAVLink Camera/Gimbal 组件，因此只要飞控成为活动飞行器且设置开启，私有缩放栏就会替换右侧原生 `PhotoVideoControl`。关闭 `Enabled` 后自动恢复原生拍照/录像栏。

使用步骤：

1. 电脑或虚拟机网络能够访问 A8 Mini 的 `192.168.144.25`。
2. 打开 `Application Settings -> Fly View -> SIYI Gimbal Zoom`。
3. 确认 SDK Host、SDK Port 和 Zoom Step。
4. 连接飞控，使其成为 QGC 活动飞行器。
5. Fly View 右侧显示半透明缩放栏；点击 `+` 或 `-`。
6. UI 先更新目标倍率，再请求相机返回真实倍率进行校正。

### 9.2 参数说明

| 参数 | 类型/范围 | 默认值 | 说明 |
|---|---|---|---|
| `enabled` | bool | `true` | 开启私有 SDK 控制并显示右侧缩放栏 |
| `sdkHost` | IPv4 string | `192.168.144.25` | 思翼相机 SDK 地址 |
| `sdkPort` | uint32, 1-65535 | `37260` | 思翼私有 SDK UDP 端口 |
| `zoomStep` | double, 0.1-4.5 | `1.0x` | 每次点击增加或减少的倍率 |
| `mavlinkAutoVideoStream` | bool | `false` | 是否接受 MAVLink 相机上报的视频流 URI |

1080p 缩放范围固定为 `1.0x-5.5x`。目标值会被 `GimbalControlManager` 限制到该范围，例如当前 5.0x、步长 1.0x 时，放大结果为 5.5x。

### 9.3 后端调用链

```text
GimbalZoomControl.qml
  -> GimbalControlManager::zoomIn/zoomOut
  -> GimbalControlManager::setZoom
  -> SiyiSdk::sendAbsoluteZoom
  -> SiyiProtocol::absoluteZoomPacket
  -> QUdpSocket -> 192.168.144.25:37260

相机 UDP 响应
  -> SiyiSdk::_readPendingDatagrams
  -> SiyiProtocol::decodePacket
  -> currentZoomReceived
  -> GimbalControlManager.currentZoom
  -> QML 倍率显示
```

QML 每 2 秒查询一次当前倍率；单次响应超时为 1.5 秒。控件已移除 `SDK Waiting` 文本，通信状态仍保留在 `sdkResponding` 和 `lastError` 属性中，便于日志或后续诊断页使用。

### 9.4 私有协议实现

| 项目 | 值 |
|---|---|
| 帧头 | `0x55 0x66` |
| Control | `0x01` |
| 长度/序号 | 16 位小端 |
| 发送序号 | 固定 `0`，与 Python SDK 当前行为一致 |
| CRC | CRC16 查表计算，结果 16 位小端 |
| 绝对缩放命令 | `0x0F`，payload 为倍率整数部分和一位小数 |
| 当前倍率查询 | `0x18`，请求 payload 为空 |

`SiyiSdk` 只接受当前配置 `_host` 返回的数据，避免同网段其他思翼设备影响倍率状态。

关键文件职责：

| 文件 | 作用 |
|---|---|
| `GimbalControl.SettingsGroup.json` | 定义启用、SDK 地址、端口、缩放步长和自动视频流 Fact |
| `GimbalControlSettings.h/.cc` | 创建 `GimbalControl` SettingsGroup 并提供 Fact 访问器 |
| `GimbalControlManager.h/.cc` | 实现倍率边界、增减倍率、状态回读、1.5 秒超时和 QML 接口 |
| `SiyiSdk.h/.cc` | 维护 `QUdpSocket`、SDK endpoint、报文发送和响应源地址过滤 |
| `SiyiProtocol.h/.cc` | 从 Python SDK 转写的帧编码、解码、CRC16 和缩放命令 |
| `GimbalVideoStreamSupport.h/.cc` | 安装 A8 Mini RTSP 默认值并控制 MAVLink 自动流消息过滤 |
| `UI/AppSettings/GimbalControlSettingsGroup.qml` | 云台缩放设置 UI |
| `FlightDisplay/GimbalZoomControl.qml` | 右侧半透明缩放操作 UI |
| `FlightDisplay/FlyViewTopRightColumnLayout.qml` | 在私有缩放栏与原生 `PhotoVideoControl` 之间切换 |

### 9.5 视频流默认值和自动锁定

`GimbalVideoStreamSupport::installA8MiniDefaults()` 的行为：

| 项目 | 行为 |
|---|---|
| 默认 URL | `rtsp://192.168.144.25:8554/main.264` |
| RTSP timeout | A8 Mini URL 下不足 20 秒时提升到 20 秒 |
| Video Source | 仅在空、Disabled 或 No Video 时设置为 RTSP |
| 用户已有其他 URL | 不覆盖 |
| 旧 `rtspt://...` 地址 | 迁移为标准 `rtsp://...` |
| 默认配置版本 | `3`，通过 QSettings 幂等迁移 |

尽管地址后缀为 `.264`，实际编码类型应以 RTSP SDP 为准；当前 A8 Mini 实测 SDP 为 H.265。

`Use MAVLink automatic video stream` 关闭时，custom 会过滤 `MAVLINK_MSG_ID_VIDEO_STREAM_INFORMATION`，避免 QGC 原生 VideoManager 根据相机上报 URI 进入自动模式并锁定手工视频源。打开后不再过滤，视频源由 MAVLink Camera 自动配置，设置页可能按 QGC 原生逻辑锁定。

Ubuntu 虚拟机保留系统代理时，应将相机和链路网段加入 `no_proxy/NO_PROXY`，至少包含：

```text
192.168.144.25,192.168.144.20,localhost,127.0.0.1
```

代理忽略列表只决定本地地址是否绕过 HTTP/桌面代理，不改变 RTSP 编码和 UDP SDK 协议。

### 9.6 PX4 与 TELEM2 参数

A8 Mini 串口连接飞控 TELEM2 时，常用必需参数如下；修改 `MAV_1_CONFIG` 后应重启飞控：

| PX4 参数 | 值 |
|---|---|
| `MAV_1_CONFIG` | `TELEM 2` |
| `SER_TEL2_BAUD` | `115200` |
| `MAV_1_MODE` | `Gimbal` |
| `MAV_1_FLOW_CTRL` | `Off` |
| `MAV_1_FORWARD` | `Enabled` |
| `MNT_MODE_IN` | `MAVLink Gimbal Protocol v2` |
| `MNT_MODE_OUT` | `MAVLink Gimbal Protocol v2` |

这些参数负责飞控与云台的 MAVLink 云台/相机链路；custom 缩放命令本身从电脑通过 `192.168.144.25:37260/UDP` 直接发送，两条链路相互独立。

## 10. 默认通信链路模块

`Comms/DefaultCommunicationLinkInstaller` 在 `CustomPlugin::init()` 早期写入 QGC 标准 LinkConfiguration QSettings 结构，后续编辑、连接和持久化仍由原生 `LinkManager` 完成，不需要新增 QML。

| 参数 | 默认值 |
|---|---|
| 名称 | `local` |
| 类型 | UDP |
| 本地监听端口 | `0` |
| 服务器地址 | `192.168.144.20` |
| 服务器端口 | `19856` |
| 开始时自动连接 | `false` |
| 高延迟 | `false` |

安装器按名称进行不区分大小写的查重。用户已有 `local` 或 `Local` 时直接保留，不覆盖地址、端口或开关；不存在时追加一个链路并更新 `LinkConfigurations/count`。

## 11. Fuel、FirmwarePlugin 与 AutoPilotPlugins

### 11.1 FuelStatusIndicator

`QmlControls/FuelStatusIndicator.qml` 读取活动飞行器的 `fuelStatus` FactGroup：

| 数据 | Fact |
|---|---|
| 遥测是否可用 | `telemetryAvailable` |
| 剩余百分比 | `percentRemaining` |
| 燃料类型 | `fuelType` |
| 剩余燃料 | `remainingFuel` |
| 最大燃料 | `maximumFuel` |
| 已消耗燃料 | `consumedFuel` |
| 流量 | `flowRate` |
| 温度 | `temperature` |

`CustomFirmwarePlugin::toolIndicators()` 先继承 QGC 原生指示器列表，移除 `RCRSSIIndicator`，再追加 Fuel 指示器。这样上游新增的其他工具栏指示器仍会自动保留。

### 11.2 CustomFirmwarePlugin

除 Fuel 指示器外，该插件还负责：

- 创建 `CustomAutoPilotPlugin`。
- 声明飞行器具有云台，支持 pitch/yaw，不支持 roll。
- 调整可用 PX4 飞行模式及是否允许用户设置。

### 11.3 CustomAutoPilotPlugin

`AutoPilotPlugins/CustomAutoPilotPlugin` 继承 PX4 自动驾驶插件，保留自定义组件列表和高级 UI 变化处理。目录使用原生 `AutoPilotPlugins` 复数形式。

### 11.4 Custom.Widgets 通用控件

这些文件由 `CustomModule` 注册为 `Custom.Widgets`，物理目录按 QGC 原生组件归属拆分：

| 文件 | 作用 |
|---|---|
| `FlightMap/Widgets/CustomArtificialHorizon.qml` | 自定义人工地平仪背景与姿态旋转层 |
| `FlightMap/Widgets/CustomAttitudeWidget.qml` | 组合姿态盘、指针、准星和地平仪；由 Fly View custom 图层使用 |
| `QmlControls/CustomIconButton.qml` | custom 图标按钮基础组件 |
| `QmlControls/CustomOnOffSwitch.qml` | 带 On/Off 文本的二态开关 |
| `QmlControls/CustomQuickButton.qml` | custom 快捷操作按钮 |
| `QmlControls/CustomSignalStrength.qml` | 按百分比选择 QGC 原生 Signal0-Signal100 图标 |
| `QmlControls/CustomToolBarButton.qml` | custom 工具栏按钮基础组件 |
| `QmlControls/CustomVehicleButton.qml` | 当前飞行器选择按钮和无飞行器状态 |

## 12. 图标、QML 和文件命名规则

### 12.1 覆盖已有 QML

```text
原文件：src/FlightDisplay/Example.qml
覆盖：custom/src/FlightDisplay/Example.qml
QRC alias：QGroundControl/FlightDisplay/Example.qml
```

不得命名为 `CustomExample.qml` 后再伪装成原类型。文件名一致能让开发人员直接比较 custom 和原生版本。

### 12.2 新增 QML

新增组件放到调用方模块，使用 PascalCase 和职责后缀：

```text
FlightDisplay/GimbalZoomControl.qml
UI/AppSettings/GimbalControlSettingsGroup.qml
QmlControls/FuelStatusIndicator.qml
```

设置组使用 `...SettingsGroup.qml`，指示器使用 `...Indicator.qml`，布局覆盖使用原生 `...Layout.qml` 文件名。

### 12.3 新增图标

| 使用位置 | 目录 |
|---|---|
| Viewer3D | `custom/src/Viewer3D/Images` |
| 飞行地图/姿态仪 | `custom/src/FlightMap/Images` |
| 顶部工具栏/工具按钮 | `custom/src/UI/toolbar/Images` |
| 应用品牌和安装包 | `custom/res`、`custom/deploy` |

添加文件后必须同步 `custom.qrc`；QML 应引用 QRC URL，不使用本机绝对路径。

## 13. 后续开发固定流程

1. 在 QGC `src` 中找到功能最接近的模块和命名方式。
2. 在 `custom/src` 创建相同模块目录；已有目录直接复用。
3. 覆盖原文件时复制上游当前版本并保持同名，只合入必要 custom 修改。
4. 新增功能时先明确 C++ 后端、QML UI、设置 Fact、资源分别归属哪个模块。
5. C++ 文件加入 `CUSTOM_SOURCES` 或对应模块 glob。
6. QML 覆盖、新增 QML、JSON、图标、shader 加入 `custom.qrc` 或 `CustomModule`。
7. 需要 QML 调用的 C++ 对象由 `CustomPlugin` 暴露或注册，避免在多个 QML 中重复创建业务对象。
8. 新增可配置参数使用 `SettingsGroup + FactMetaData JSON`，不要直接在 QML 中写 QSettings。
9. 更新 `custom.ts`、`custom_zh_CN.ts` 和本文档。
10. 执行 QRC 文件存在性、旧路径、QML 类型、CMake 配置、完整构建和目标硬件验证。

## 14. 当前开发进度与验证重点

| 功能 | 代码状态 | 运行验证重点 |
|---|---|---|
| Viewer3D 本地 OSM | 已接入 | OSM 选择、建筑生成、飞机与任务位置 |
| Viewer3D 外部模型 | 已接入 | Balsam 可用性、WGS84 原点、单位、比例和 yaw |
| Google 3D Maps | 条件接入 | Qt WebEngineQuick、API Key 和网络访问 |
| 3D 工具栏白色图标 | 已接入 | 深浅背景可见性、3D/Fly 切换 |
| 思翼私有 SDK 缩放 | 已接入 | UDP 37260、1.0x-5.5x、步长和真实倍率回读 |
| 右侧缩放栏覆盖 | 已接入 | activeVehicle/Enabled 条件、关闭后恢复拍照录像 |
| A8 Mini 视频默认值 | 已接入 | GStreamer RTSP/H.265、20 秒超时、代理绕过 |
| MAVLink 自动视频流开关 | 已接入 | 关闭时可手工编辑，打开时原生自动锁定 |
| 默认 local UDP 链路 | 已接入 | 新配置创建、已有同名配置不覆盖 |
| Fuel 顶部指示器 | 已接入 | `fuelStatus` 遥测可用性和单位显示 |
| custom 目录规范化 | 已完成 | 不再出现旧目录名和根目录散落 QML |

每次合并新的 QGC 上游版本后，优先检查以下覆盖文件，因为它们包含较多原生代码，最容易与上游接口变化产生冲突：

```text
custom/src/QmlControls/AppSettings.qml
custom/src/UI/AppSettings/FlyViewSettings.qml
custom/src/FlightDisplay/FlyView.qml
custom/src/FlightDisplay/FlyViewWidgetLayer.qml
custom/src/FlightDisplay/FlyViewTopRightColumnLayout.qml
custom/src/FlightDisplay/FlyViewToolStripActionList.qml
```

## 15. 常用检查命令

```powershell
# 查看 custom 文件结构
rg --files custom | Sort-Object

# 检查旧目录名是否重新出现
rg -n "AppSettingsUI|Gimbalcontrol|CommunicationLink|AutoPilotPlugin/" custom

# 检查 QML/资源旧路径
rg -n "CustomFlyViewToolStrip|GimbalFlyViewTopRightColumnLayout|res/Custom/Widgets" custom

# 检查差异空白错误
git diff --check

# 查看本次整理状态
git status --short
```

目标 Linux/Qt 6.8.3 环境完成构建后，还应启动 QGC 并确认日志中没有 `Type ... unavailable`、`... is not a type`、QRC 图片加载失败或 GStreamer 管线错误。
