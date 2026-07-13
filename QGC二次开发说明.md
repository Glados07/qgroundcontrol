# QGC 二次开发说明

适用项目：`F:\qgroundcontrol_viewer3d`

当前整理分支：`SecDev/ft/gimbal`

整理日期：2026-07-13

## 1. 开发原则

本项目的二次开发代码统一放在 `custom` 目录中。Viewer3D、思翼云台缩放、默认通信链路、燃料状态显示等功能均作为 custom 模块接入，原则上不直接修改 QGC 原生 `src` 目录，便于后续在不同分支之间迁移。

当前 custom 代码采用三层入口：

| 层级 | 入口 | 作用 |
|---|---|---|
| 编译层 | `custom/CMakeLists.txt` | 启用 custom build，收集 custom C++ 源码，追加 Qt 组件和资源文件 |
| 资源层 | `custom/custom.qrc` | 注册 QML 覆盖文件、Viewer3D 资源、设置 JSON、图标和 shader |
| 运行层 | `custom/src/CustomPlugin.h/.cc` | 初始化 custom 插件，暴露 Viewer3D 和云台控制对象给 QML，安装 QML 覆盖拦截器 |

## 2. custom 完整结构与作用

```text
custom/
  CMakeLists.txt                         # custom 编译入口，收集 Viewer3D/Gimbalcontrol/CommunicationLink 源码
  custom.qrc                             # custom 资源入口，注册 QML 覆盖、设置 JSON、图标、Viewer3D 模型和 shader
  android/                               # Android custom 包模板覆盖目录
  cmake/CustomOverrides.cmake            # custom CMake 覆盖配置
  deploy/windows/                        # Windows 打包部署相关 custom 文件
  res/                                   # custom 通用资源和控件
    Custom/Widgets/                      # Custom.Widgets QML 模块
    Images/                              # custom 图标资源，包含 FuelIcon 和 Viewer3D 图标依赖
    QGCLogoFull.svg                      # custom 品牌图
    icons/                               # custom 应用图标
  translations/                          # custom 翻译文件
  src/
    CustomPlugin.h
    CustomPlugin.cc                      # custom 插件总入口，初始化默认链路、Viewer3D、云台缩放、翻译和 QML 覆盖
    AppSettings.qml                      # Application Settings 外壳覆盖入口
    AutoPilotPlugin/
      CustomAutoPilotPlugin.h
      CustomAutoPilotPlugin.cc           # custom 自动驾驶插件扩展点
    FirmwarePlugin/
      CustomFirmwarePlugin.h
      CustomFirmwarePlugin.cc            # custom 固件插件扩展点，包含 toolbar 指示器接入
      CustomFirmwarePluginFactory.h
      CustomFirmwarePluginFactory.cc     # custom 固件插件工厂
    FuelStatusIndicator.qml              # 燃料/电池状态类指示器 QML
    CommunicationLink/
      DefaultCommunicationLinkInstaller.h/.cc # 幂等安装项目默认 UDP 通信链路
    FlyView.qml                          # Fly View 主界面覆盖，挂载 Viewer3D 主窗口
    FlyViewWidgetLayer.qml               # Fly View 工具/控件覆盖层，不再直接挂载云台缩放控件
    FlyViewToolStripActionList.qml       # 左侧工具栏 Action 列表，加入 3D View/Fly 切换按钮
    CustomFlyViewToolStrip.qml           # Fly View 工具栏覆盖，加载 custom ActionList
    FlyViewCustomLayer.qml               # Fly View custom 图层扩展点
    CustomGuidedActionsController.qml    # 引导动作控制覆盖
    UI/preferences/
      FlyViewSettings.qml                # Application Settings -> Fly View 覆盖页
      Viewer3DSettingsGroup.qml          # Viewer3D 设置组
    Viewer3D/
      Viewer3D.SettingsGroup.json        # Viewer3D Fact 参数定义
      Viewer3DSettings.h/.cc             # Viewer3D 设置对象
      Viewer3DManager.h/.cc              # Viewer3D QML 类型注册和后端对象管理
      Viewer3DQmlBackend.h/.cc           # Viewer3D QML 后端数据接口
      Viewer3DQmlVariableTypes.h         # Viewer3D QML/C++ 共享类型
      External3DMapManager.h/.cc         # 外部 3D 模型地图导入和 Balsam 转换管理
      OsmParser.h/.cc                    # OSM 地图解析
      OsmParserThread.h/.cc              # OSM 异步解析线程
      CityMapGeometry.h/.cc              # OSM 建筑物 3D 几何生成
      Viewer3DTerrainGeometry.h/.cc      # 3D 地形几何
      Viewer3DTerrainTexture.h/.cc       # 3D 地形纹理
      Viewer3DTileQuery.h/.cc            # 地图瓦片查询
      Viewer3DTileReply.h/.cc            # 地图瓦片响应
      Viewer3DUtils.h/.cc                # Viewer3D 通用工具函数
      earcut.hpp                         # 多边形三角剖分依赖
      Images/
        city_3d_map_icon.svg             # 3D View 工具栏图标
      Shaders/
        earthMaterial.vert
        earthMaterial.frag               # Viewer3D 地球/地形材质 shader
      SampleOsmMap/
        map_sim_small.osm                # 本地 OSM 示例数据
      ExternalWGS84_UE5_MapSample/       # 外部 3D 模型地图示例和导入参数
      Viewer3DQml/
        Viewer3D.qml                     # Viewer3D 主 QML 场景
        Google3DMapView.qml              # Google 3D Maps 在线视图
        Google3DMapUnavailable.qml       # 未编译 WebEngine 时的降级提示
        Viewer3DProgressBar.qml          # Viewer3D 加载进度组件
        Models3D/                        # 任务点、航线、飞机和外部地图模型组件
        Drones/                          # DJI F450 3D 飞机模型及 mesh 资源
    QmlControls/Viewer3D/
      qmldir                             # Viewer3D QML 模块声明
      Models3D/qmldir                    # Viewer3D Models3D 子模块声明
      Models3D/Drones/qmldir             # Viewer3D Drones 子模块声明
    Gimbalcontrol/
      GimbalControl.SettingsGroup.json   # 思翼云台缩放 Fact 参数定义
      GimbalControlSettings.h/.cc        # 云台缩放设置对象
      GimbalControlManager.h/.cc         # 云台缩放业务管理器，给 QML 暴露 zoomIn/zoomOut
      GimbalVideoStreamSupport.h/.cc     # A8 Mini RTSP 默认值及 MAVLink 自动视频流过滤
      SiyiProtocol.h/.cc                 # 思翼私有 SDK 协议封包和解析
      SiyiSdk.h/.cc                      # 思翼 UDP 通信封装
      GimbalFlyViewTopRightColumnLayout.qml # 覆盖右侧原生相机栏，负责私有缩放/原生控件切换
      GimbalZoomControl.qml              # Fly View 右侧白色 +/- 缩放控件
      GimbalControlSettingsGroup.qml     # Application Settings -> Fly View 中的云台缩放设置组
```

资源型目录说明：

| 目录 | 作用 |
|---|---|
| `custom/res/Custom/Widgets` | 注册为 `Custom.Widgets` QML 模块，提供 custom 仪表、按钮和工具栏控件 |
| `custom/res/Images` | 存放 custom 图标、品牌图依赖和 Fly View 指示器图标 |
| `custom/src/Viewer3D/Viewer3DQml/Drones` | 存放 3D 飞机模型 QML 和 mesh 文件 |
| `custom/src/Viewer3D/ExternalWGS84_UE5_MapSample` | 存放外部 WGS84 3D 地图导入示例、纹理、OBJ/FBX 和参数文件 |

## 3. 编译与资源接入

### 3.1 `custom/CMakeLists.txt`

关键作用：

| 配置 | 作用 |
|---|---|
| `QGC_CUSTOM_BUILD` | 告诉 QGC 使用 custom 插件 |
| `CUSTOMHEADER=CustomPlugin.h` | 指定 custom 插件头文件 |
| `CUSTOMCLASS=CustomPlugin` | 指定 custom 插件类 |
| `CUSTOM_QT_COMPONENTS` | 追加 `Quick3D`、`Quick3DAssetUtils`，用于 Viewer3D 本地 3D 渲染和模型加载 |
| `Qt6::WebEngineQuick` | 可选依赖。存在时启用 `QGC_VIEWER3D_GOOGLE_WEBENGINE`，支持 Google 3D Maps |
| `CUSTOM_RESOURCES` | 将 `custom/custom.qrc` 注入 QGC 资源 |
| `VIEWER3D_SOURCES` | 递归收集 `custom/src/Viewer3D/*.cc/*.h/*.hpp` |
| `GIMBALCONTROL_SOURCES` | 递归收集 `custom/src/Gimbalcontrol/*.cc/*.h` |
| `COMMUNICATION_LINK_SOURCES` | 递归收集 `custom/src/CommunicationLink/*.cc/*.h` |
| `CUSTOM_SOURCES` | 汇总 CustomPlugin、FirmwarePlugin、AutoPilotPlugin、Viewer3D、Gimbalcontrol、CommunicationLink 源码 |
| `CUSTOM_LIBRARIES` | 链接 `CustomModule`、`Qt6::Quick3D` 和可选 WebEngine |
| `CUSTOM_INCLUDE_DIRECTORIES` | 加入 `custom/src`、`AutoPilotPlugin`、`FirmwarePlugin`、`Viewer3D`、`Gimbalcontrol`、`CommunicationLink` |

### 3.2 `custom/custom.qrc`

关键资源前缀：

| 前缀 | 内容 |
|---|---|
| `/custom/img` | Fuel 图标、QGC 品牌图、Viewer3D 工具栏图标 |
| `/Custom/qml` | QGC QML 覆盖入口，包括 Fly View、Application Settings、FuelStatusIndicator、GimbalZoomControl |
| `/qml` | Viewer3D 独立 QML 模块，供 `import Viewer3D` 使用 |
| `/json` | `Viewer3D.SettingsGroup.json` 和 `GimbalControl.SettingsGroup.json` |
| `/ShaderVertex` | Viewer3D 顶点 shader |
| `/ShaderFragment` | Viewer3D 片元 shader |

QML 覆盖机制由 `CustomOverrideInterceptor` 完成。QGC 尝试加载 `qrc:/qml/QGroundControl/...` 时，拦截器会优先检查 `qrc:/Custom/qml/QGroundControl/...` 下是否存在同路径文件，存在则加载 custom 覆盖版本，不存在则回落到 QGC 原生文件。

## 4. CustomPlugin 总入口

关键文件：

| 文件 | 作用 |
|---|---|
| `custom/src/CustomPlugin.h` | 声明 custom 插件、custom options、QML 暴露属性 |
| `custom/src/CustomPlugin.cc` | 初始化默认通信链路、翻译、Viewer3D、云台缩放、QML 覆盖拦截器和调色板 |

向 QML 暴露的对象：

| QML 属性 | C++ 对象 | 作用 |
|---|---|---|
| `QGroundControl.corePlugin.viewer3DSettings` | `Viewer3DSettings` | Viewer3D 设置 Fact 组 |
| `QGroundControl.corePlugin.external3DMapManager` | `External3DMapManager` | 外部 3D 模型地图导入管理 |
| `QGroundControl.corePlugin.google3DMapsAvailable` | `bool` | 当前构建是否支持 Google 3D Maps |
| `QGroundControl.corePlugin.gimbalControlSettings` | `GimbalControlSettings` | 思翼云台缩放设置 Fact 组 |
| `QGroundControl.corePlugin.gimbalControlManager` | `GimbalControlManager` | 思翼云台缩放控制接口 |

其他行为：

| 行为 | 说明 |
|---|---|
| 翻译加载 | 根据当前 locale 从 `:/i18n/custom_*.qm` 加载 custom 翻译 |
| 品牌图 | `brandImageIndoor` 和 `brandImageOutdoor` 使用 `/custom/img/dronecode-*.svg` |
| Fly View Options | 隐藏默认 instrument panel 和多机列表，使用 custom UI |
| 调色板 | 覆盖 QGC 部分颜色，使 custom UI 保持一致 |
| QML 覆盖 | `createQmlApplicationEngine` 中安装 `CustomOverrideInterceptor` |
| 默认通信链路 | `init()` 中先补充缺失的 `local` UDP 链路，再由 QGC 原生 `LinkManager` 加载 |

## 5. Fly View 与 Application Settings 覆盖

### 5.1 Fly View 覆盖文件

| 文件 | 作用 |
|---|---|
| `custom/src/FlyView.qml` | Fly View 主入口覆盖，加载 Viewer3D 场景，控制 2D 地图和 3D 视图切换 |
| `custom/src/FlyViewToolStripActionList.qml` | 左侧工具栏按钮列表，加入 3D View/Fly 切换按钮 |
| `custom/src/CustomFlyViewToolStrip.qml` | 工具栏容器覆盖，使工具栏使用 custom action list |
| `custom/src/FlyViewWidgetLayer.qml` | Fly View 控件覆盖层；在 3D 视图开启时隐藏 2D 地图比例尺，云台缩放改由右侧布局覆盖文件挂载 |
| `custom/src/FlyViewCustomLayer.qml` | custom Fly View 扩展图层 |
| `custom/src/CustomGuidedActionsController.qml` | custom 引导动作控制器入口 |

### 5.2 Application Settings 覆盖文件

| 文件 | 作用 |
|---|---|
| `custom/src/AppSettings.qml` | Application Settings 外壳覆盖入口 |
| `custom/src/UI/preferences/FlyViewSettings.qml` | Fly View 设置页覆盖，保留 QGC 原有设置，并在底部加载 Viewer3D 和云台缩放设置组 |
| `custom/src/UI/preferences/Viewer3DSettingsGroup.qml` | Viewer3D 设置组 |
| `custom/src/Gimbalcontrol/GimbalControlSettingsGroup.qml` | 思翼云台缩放设置组 |

设置页加载顺序：

```text
Application Settings
  Fly View
    QGC 原有 Fly View 设置项
    Viewer3D 设置组
    SIYI Gimbal Zoom 设置组
```

## 6. Viewer3D 功能模块

### 6.1 功能范围

Viewer3D 是独立的 3D 视图模块，当前在 custom 中完成以下功能：

| 功能 | 说明 |
|---|---|
| Fly View 3D 切换 | 左侧工具栏显示 3D View/Fly 按钮，在 Fly View 中切换 2D 地图和 3D 视图 |
| 本地 OSM 3D 地图 | 读取 OSM 文件，解析建筑轮廓，生成 3D 建筑物 |
| 外部 3D 模型地图 | 支持导入 OBJ、glTF、GLB、Balsam QML 等可直接加载格式 |
| Balsam 模型转换 | 对 FBX、DAE、STL、PLY 等格式尝试通过 Qt Balsam 转换 |
| 3D 飞机模型 | 在 3D 场景中显示飞机模型并跟随飞控位置 |
| 航线与航点显示 | 在 3D 场景中显示航点和航线模型 |
| 地形/瓦片纹理 | 提供地形几何、纹理和瓦片查询封装 |
| Google 3D Maps | 如果构建环境存在 Qt WebEngineQuick，可启用在线 Google 3D Maps 视图 |

### 6.2 使用说明

1. 打开 `Application Settings -> Fly View -> 3D View`。
2. 打开 `Enabled`。
3. 根据地图来源选择一种方式：
   - 本地 OSM：设置 `OSM File`。
   - 外部 3D 模型地图：打开 `Use External 3D Model Map`，选择模型文件，并填写 WGS84 原点和比例参数。
   - Google 3D Maps：构建时需要 Qt WebEngineQuick，设置 `Use Google 3D Maps` 和 API Key。
4. 返回 Fly View。
5. 点击左侧工具栏的 `3D View` 图标进入 3D 视图，点击 `Fly` 返回普通飞行视图。

### 6.3 Viewer3D 参数说明

参数来自 `custom/src/Viewer3D/Viewer3D.SettingsGroup.json`。

| 参数 | 类型 | 默认值 | 说明 |
|---|---|---|---|
| `enabled` | bool | `false` | 是否启用 3D viewer |
| `useGoogle3DMapSource` | bool | `false` | 是否使用 Google 3D Maps 在线源 |
| `google3DMapsApiKey` | string | 空 | Google Maps JavaScript API key |
| `useExternal3DMapSource` | bool | `false` | 是否使用外部 3D 模型地图 |
| `external3DMapFilePath` | string | `Please select an external 3D model file` | 外部 3D 模型文件路径 |
| `external3DMapOriginLatitude` | double | `0` | 外部模型本地原点纬度，单位 deg |
| `external3DMapOriginLongitude` | double | `0` | 外部模型本地原点经度，单位 deg |
| `external3DMapOriginAltitude` | double | `0` | 外部模型本地原点高度，单位 m |
| `external3DMapUnitToMeters` | double | `0.01` | 一个模型单位对应多少米。UE 厘米导出常用 `0.01`，米制模型常用 `1.0` |
| `external3DMapScale` | double | `1` | 单位换算后的额外缩放倍率 |
| `external3DMapYaw` | double | `0` | 模型旋转到 QGC ENU 坐标的偏航角，单位 deg |
| `osmFilePath` | string | `Please select an OSM file` | OSM 文件路径 |
| `buildingLevelHeight` | double | `3` | OSM 建筑每层平均高度，单位 m |
| `altitudeBias` | double | `0` | 3D 视图中飞机高度偏置，单位 m |

### 6.4 Viewer3D 关键文件

| 文件 | 作用 |
|---|---|
| `Viewer3DSettings.h/.cc` | 创建 Viewer3D SettingsGroup，提供 QML Fact 访问 |
| `Viewer3DManager.h/.cc` | 注册 Viewer3D QML 类型，管理 OSM parser 和 QML backend |
| `Viewer3DQmlBackend.h/.cc` | 向 QML 提供坐标、任务、飞机状态等后端数据 |
| `External3DMapManager.h/.cc` | 处理外部 3D 模型导入、格式判断和 Balsam 转换 |
| `OsmParser.h/.cc` | 解析 OSM 文件 |
| `OsmParserThread.h/.cc` | 后台解析 OSM，避免阻塞界面 |
| `CityMapGeometry.h/.cc` | 根据 OSM 建筑数据生成 3D 几何 |
| `Viewer3D.qml` | Viewer3D 主 QML 场景 |
| `Google3DMapView.qml` | Google 3D Maps 在线场景 |
| `Google3DMapUnavailable.qml` | WebEngine 不可用时的提示页 |
| `Models3D/*.qml` | 飞机、航线、航点、外部地图等 3D 模型组件 |

## 7. 思翼云台缩放功能模块

### 7.1 功能范围

思翼云台缩放功能放在 `custom/src/Gimbalcontrol`，与 Viewer3D、燃料状态和 QGC 原生 MAVLink 云台控制隔离。

当前完成内容：

| 功能 | 说明 |
|---|---|
| Python SDK 转 C++ | 将思翼私有 SDK 中缩放相关协议转为 C++ 封装 |
| UDP 通信 | 通过 `QUdpSocket` 向思翼相机 SDK 端口发送命令并接收响应 |
| 缩放控制 | 支持 `zoomIn()`、`zoomOut()`、`setZoom(double)` |
| 当前倍率查询 | 支持 `requestCurrentZoom()`，2 秒轮询当前倍率 |
| 倍率范围限制 | 1080p 分辨率按 `1.0x` 到 `5.5x` 限制 |
| 分度值设置 | 可在 Application Settings 中设置 `Zoom Step`，默认 `1.0x` |
| Fly View UI | 覆盖右侧原生相机栏，显示白色圆形 `+`、`-` 按钮、当前倍率和 SDK 状态 |
| 视频流手动模式 | 默认过滤 `VIDEO_STREAM_INFORMATION`，避免 QGC 锁定视频源设置 |
| A8 Mini 视频默认值 | 首次运行默认选择 RTSP，并写入 `rtsp://192.168.144.25:8554/main.264` |

### 7.2 使用说明

1. 连接飞控，并确保 QGC 可以识别 active vehicle。
2. A8 Mini 网口必须与电脑处于同一网段；TELEM2 只承载 MAVLink 云台通信，不能代替私有 SDK 的 UDP 网络连接。
3. 电脑需要能访问思翼相机 SDK 网络地址，默认 `192.168.144.25:37260`。
4. PX4 使用 TELEM2 时确认以下参数，保存后重启飞控：

| PX4 参数 | 必需值 |
|---|---|
| `MAV_1_CONFIG` | `TELEM 2` |
| `SER_TEL2_BAUD` | `115200` |
| `MAV_1_MODE` | `Gimbal` |
| `MAV_1_FLOW_CTRL` | `Off` |
| `MAV_1_FORWARD` | `Enabled` |
| `MNT_MODE_IN` | `MAVLink Gimbal Protocol v2` |
| `MNT_MODE_OUT` | `MAVLink Gimbal Protocol v2` |

5. A8 Mini 使用 1080p 视频模式，当前缩放业务范围为 `1.0x` 到 `5.5x`。
6. 打开 `Application Settings -> Fly View -> SIYI Gimbal Zoom`。
7. 保持 `Enabled` 打开。
8. 如设备地址不同，修改 `SDK Host` 和 `SDK Port`。
9. 设置 `Zoom Step`，默认 `1.0x`。
10. 保持 `Use MAVLink automatic video stream` 关闭，使原生 Video 设置页可手动编辑；修改该项后重启 QGC。
11. 在 `Application Settings -> Video` 确认 Source 为 `RTSP Video Stream`，RTSP URL 为 `rtsp://192.168.144.25:8554/main.264`。
12. 回到 Fly View，右侧原生拍照/录像控件位置会显示白色 `+`、`-` 缩放控件。
13. 点击 `+` 增加倍率，点击 `-` 降低倍率。倍率会被限制在 `1.0x` 到 `5.5x`。
14. `SDK Ready` 和绿色状态点表示私有 SDK 已返回有效报文；`SDK Waiting` 和橙色表示正在等待响应或网络不可达。
15. 关闭 `Enabled` 后，右侧区域自动恢复 QGC 原生拍照/录像控件，同时不再过滤 MAVLink 相机视频流信息。

显示条件：

| 条件 | 说明 |
|---|---|
| `gimbalControlSettings.enabled == true` | 设置中已启用 |
| `QGroundControl.multiVehicleManager.activeVehicle` 存在 | 已连接飞控或有活动飞机 |
| `!QGroundControl.videoManager.fullScreen` | 视频未处于全屏状态 |

### 7.3 云台缩放参数说明

参数来自 `custom/src/Gimbalcontrol/GimbalControl.SettingsGroup.json`。

| 参数 | 类型 | 默认值 | 范围 | 说明 |
|---|---|---|---|---|
| `enabled` | bool | `true` | true/false | 是否启用思翼云台缩放模块 |
| `sdkHost` | string | `192.168.144.25` | 合法 IP 地址 | 思翼 SDK UDP 主机地址 |
| `sdkPort` | uint32 | `37260` | `1` 到 `65535` | 思翼 SDK UDP 端口 |
| `zoomStep` | double | `1.0` | `0.1` 到 `4.5` | 每次点击 `+` 或 `-` 调整的倍率分度值，保留 1 位小数 |
| `mavlinkAutoVideoStream` | bool | `false` | true/false | 是否允许 QGC 使用相机上报 URI 自动配置视频；开启后原生视频源设置会锁定 |

业务层固定限制：

| 限制 | 值 | 位置 |
|---|---|---|
| 最小倍率 | `1.0x` | `GimbalControlManager::kMinZoom` |
| 最大倍率 | `5.5x` | `GimbalControlManager::kMaxZoom` |
| SDK 响应超时 | `1500 ms` | `GimbalControlManager::_sdkResponseTimer` |
| 当前倍率轮询 | `2000 ms` | `GimbalZoomControl.qml` |

### 7.4 思翼协议说明

协议封装位于 `SiyiProtocol.h/.cc`，当前仅保留缩放功能所需命令。

| 项 | 值 |
|---|---|
| UDP 默认地址 | `192.168.144.25` |
| UDP 默认端口 | `37260` |
| 包头 | `0x55 0x66` |
| 控制字 | `0x01` |
| 序列号 | 当前按 Python SDK 行为固定为 `0x0000` |
| CRC | CRC16-CCITT，初始值 `0`，低字节在前 |
| 绝对缩放命令 | `0x0f` |
| 当前倍率查询命令 | `0x18` |

绝对缩放 payload：

```text
payload[0] = 倍率整数部分
payload[1] = 倍率一位小数部分

示例：
  1.0x -> 01 00
  3.5x -> 03 05
  5.5x -> 05 05
```

### 7.5 云台缩放关键文件

| 文件 | 作用 |
|---|---|
| `GimbalControl.SettingsGroup.json` | 定义 Enabled、SDK Host、SDK Port、Zoom Step 的 Fact 元数据 |
| `GimbalControlSettings.h/.cc` | 将 JSON 中的参数注册为 SettingsGroup，供 C++ 和 QML 使用 |
| `GimbalControlManager.h/.cc` | 业务管理器，负责倍率限制、分度值读取、调用 SDK、维护当前倍率和错误状态 |
| `GimbalVideoStreamSupport.h/.cc` | 首次安装 A8 Mini RTSP 默认值；手动模式下过滤 `VIDEO_STREAM_INFORMATION`，避免原生视频页锁定 |
| `SiyiProtocol.h/.cc` | 思翼私有协议封包、CRC、响应解析 |
| `SiyiSdk.h/.cc` | UDP 发送和接收封装 |
| `GimbalFlyViewTopRightColumnLayout.qml` | 覆盖 QGC 右侧单机相机栏；静态挂载私有缩放控件，关闭模块时回退 `PhotoVideoControl` |
| `GimbalZoomControl.qml` | 具有稳定隐式尺寸的右侧白色缩放按钮、实时倍率和 SDK 响应状态 UI；SDK 离线时不再隐藏 |
| `GimbalControlSettingsGroup.qml` | Application Settings 中的云台缩放设置 UI |

QML 调用接口：

| 接口 | 作用 |
|---|---|
| `gimbalControlManager.zoomIn()` | 当前倍率加 `zoomStep` |
| `gimbalControlManager.zoomOut()` | 当前倍率减 `zoomStep` |
| `gimbalControlManager.setZoom(zoomLevel)` | 设置指定倍率，会自动限制到 `1.0` 到 `5.5` |
| `gimbalControlManager.requestCurrentZoom()` | 向相机查询当前倍率 |

QML 状态属性：

| 属性 | 说明 |
|---|---|
| `enabled` | 当前模块是否启用 |
| `currentZoom` | 当前倍率 |
| `zoomStep` | 当前分度值 |
| `minimumZoom` | 最小倍率，固定 `1.0` |
| `maximumZoom` | 最大倍率，固定 `5.5` |
| `sdkResponding` | SDK 是否在超时时间内响应 |
| `lastError` | 最近一次通信或配置错误 |

### 7.6 视频自动锁定与 Ubuntu 虚拟机排查

QGC 原生 `VideoManager::autoStreamConfigured()` 只要收到非空的 MAVLink `VIDEO_STREAM_INFORMATION.uri` 就返回 true，Video 设置页随后进入只读状态。该判断不测试 URI 是否能从当前操作系统访问，因此“设置被锁定”不等于“视频可以播放”。

custom 当前默认采用手动视频模式：

1. `mavlinkAutoVideoStream=false` 时，`CustomPlugin::mavlinkMessage()` 在消息进入 QGC 相机管理器前过滤 `VIDEO_STREAM_INFORMATION`。
2. 首次运行通过 `GimbalVideoStreamSupport` 写入 A8 Mini RTSP 默认值；标记 `GimbalControl/a8MiniVideoDefaultsInstalled` 保证只初始化一次，不持续覆盖用户修改。
3. 如需恢复 QGC 原生自动流行为，打开 `Use MAVLink automatic video stream` 并重启 QGC。

Ubuntu 24.04 虚拟机无法显示、Windows 可以显示时，按以下顺序检查：

```bash
ip addr
ip route
ping -c 4 192.168.144.25
nc -vz 192.168.144.25 8554
gst-inspect-1.0 rtspsrc
gst-inspect-1.0 avdec_h264
gst-launch-1.0 rtspsrc location=rtsp://192.168.144.25:8554/main.264 latency=50 ! decodebin ! autovideosink
```

| 检查项 | 要求 |
|---|---|
| 虚拟机网卡 | 使用桥接模式，并桥接到实际连接云台的物理网口；NAT 模式通常不能访问主机的云台专用网段 |
| Ubuntu 网口地址 | 与云台处于 `192.168.144.0/24`，例如 `192.168.144.10/24`，且不与其他接口冲突 |
| RTSP 端口 | `192.168.144.25:8554` 可建立 TCP 连接 |
| GStreamer | `rtspsrc` 和 H.264 解码器存在，独立 `gst-launch-1.0` 命令可以出图 |
| 虚拟机图形 | 开启 3D 加速并保证 OpenGL 可用，否则 QGC 视频 sink 可能建立但无法渲染 |

如果 `ping` 或 `nc` 失败，应先修复虚拟机桥接和 Ubuntu 静态 IP；这类网络不可达无法由 QGC 内部代码绕过。如果独立 GStreamer 命令失败，则修复 Ubuntu 的 RTSP/H.264 插件或解码环境后再测试 QGC。

## 8. 默认通信链路模块

### 8.1 功能目的

默认通信链路功能位于 `custom/src/CommunicationLink`。应用首次启动、配置被清理或升级后缺少目标链路时，custom 插件会自动向 QGC 标准链路配置中追加 `local`，用户不需要再次手动填写。

该功能只负责创建和持久化默认配置，链路列表显示、编辑、手动连接和 UDP 收发仍由 QGC 原生 `LinkManager`、`UDPConfiguration` 处理。

### 8.2 默认参数

| 配置项 | 默认值 | 说明 |
|---|---|---|
| 名称 | `local` | 检查时忽略大小写，已有 `local` 或 `Local` 均不会重复创建 |
| 类型 | `UDP` | 使用 QGC 原生 UDP 链路 |
| 本地端口 | `0` | 连接时由操作系统分配可用本地端口 |
| 服务器地址 | `192.168.144.20:19856` | UDP 目标服务器及端口 |
| 开始时自动连接 | `false` | 默认不在 QGC 启动时连接 |
| 高延迟 | `false` | 按普通低延迟链路处理 |

### 8.3 初始化规则

1. `CustomPlugin::init()` 在 QGC 原生 `LinkManager::loadLinkConfigurationList()` 之前执行。
2. `DefaultCommunicationLinkInstaller::ensureInstalled()` 读取 `LinkConfigurations/count` 和已有链路名称。
3. 已存在名称为 `local` 或 `Local` 的链路时直接返回，不覆盖用户修改过的地址、端口或开关。
4. 不存在时按 QGC 标准 QSettings 字段追加一个 UDP 链路并更新 `count`。
5. 原生 `LinkManager` 随后加载该配置，所以它与用户在通信链路页面手动创建的链路行为一致。

关键文件：

| 文件 | 作用 |
|---|---|
| `custom/src/CommunicationLink/DefaultCommunicationLinkInstaller.h/.cc` | 检查并写入缺失的默认 UDP 链路 |
| `custom/src/CustomPlugin.cc` | 在 custom 插件初始化阶段调用安装器 |
| `custom/CMakeLists.txt` | 通过 `COMMUNICATION_LINK_SOURCES` 收集模块源码 |

## 9. 燃料/电池状态显示模块

该模块为当前 custom 中已有的 Fly View 指示器类功能，和 Viewer3D、Gimbalcontrol 分开维护。

| 文件 | 作用 |
|---|---|
| `custom/src/FuelStatusIndicator.qml` | 燃料/电池状态指示器 UI |
| `custom/res/Images/FuelIcon.svg` | 指示器图标 |
| `custom/src/FirmwarePlugin/CustomFirmwarePlugin.h/.cc` | custom 固件插件扩展点，用于接入 custom toolbar/status indicator |
| `custom/custom.qrc` | 注册 `FuelStatusIndicator.qml` 和 `FuelIcon.svg` |

后续如果要扩展燃料、电池、电源类监测功能，应继续放在独立文件或独立子目录中，不应混入 Viewer3D 或 Gimbalcontrol。

## 10. 模块边界

| 模块 | 所在目录 | 是否独立 | 对外入口 |
|---|---|---|---|
| CustomPlugin 总入口 | `custom/src/CustomPlugin.*` | 否，负责聚合 | `QGroundControl.corePlugin` |
| Fly View 覆盖 | `custom/src/FlyView*.qml` | 否，负责界面入口 | QML 覆盖路径 `/Custom/qml/QGroundControl/FlightDisplay` |
| Application Settings 覆盖 | `custom/src/UI/preferences` | 否，负责设置页入口 | QML 覆盖路径 `/Custom/qml/QGroundControl/AppSettings` |
| Viewer3D | `custom/src/Viewer3D` | 是 | `viewer3DSettings`、`external3DMapManager`、`import Viewer3D` |
| Gimbalcontrol | `custom/src/Gimbalcontrol` | 是 | `gimbalControlSettings`、`gimbalControlManager` |
| CommunicationLink | `custom/src/CommunicationLink` | 是 | `DefaultCommunicationLinkInstaller::ensureInstalled()` |
| 燃料/电池状态 | `custom/src/FuelStatusIndicator.qml` | 相对独立 | FirmwarePlugin 指示器入口 |

迁移或合并时优先保持以下边界：

| 原则 | 说明 |
|---|---|
| Viewer3D 不依赖 Gimbalcontrol | 3D 视图可单独迁移 |
| Gimbalcontrol 不依赖 Viewer3D | 云台缩放只依赖 CustomPlugin 暴露和 Fly View 挂载点 |
| CommunicationLink 不依赖界面模块 | 默认链路只写入 QGC 标准持久化配置，不依赖 Viewer3D 或 Gimbalcontrol |
| 设置页只做组合 | `FlyViewSettings.qml` 只加载各模块设置组，不写具体业务逻辑 |
| 资源按模块注册 | Viewer3D QML 放 `/qml/Viewer3D`，Gimbal QML 放 `/Custom/qml/Gimbalcontrol` |
| QGC 原生 `src` 不作为二次开发入口 | 后续修改优先落在 `custom` |

## 11. 当前开发状态

已完成：

| 功能 | 状态 |
|---|---|
| Viewer3D 迁移到 `custom/src/Viewer3D` | 已完成 |
| Viewer3D Fly View 图标和界面入口 | 已接入 custom Fly View 工具栏 |
| Viewer3D Application Settings 设置组 | 已接入 Fly View 设置页 |
| 外部 3D 模型地图导入管理 | 已接入 `External3DMapManager` |
| Google 3D Maps 可选能力判断 | 已通过 `google3DMapsAvailable` 暴露 |
| 思翼 Python SDK 缩放相关协议转 C++ | 已完成 |
| 思翼云台缩放设置页 | 已接入 Fly View 设置页，位置在 Viewer3D 设置组下方 |
| 思翼云台缩放 Fly View UI | 已覆盖右侧原生拍照/录像控件栏；模块关闭时自动回退原生控件 |
| A8 Mini 视频流手动模式 | 已默认关闭 MAVLink 自动视频配置，原生 Video 设置页保持可编辑 |
| A8 Mini RTSP 默认值 | 已按首次运行幂等写入 `rtsp://192.168.144.25:8554/main.264` |
| 默认 `local` UDP 通信链路 | 已接入 custom 初始化，缺失时自动创建且不重复覆盖 |
| 燃料/电池状态指示器 | 保留在 custom 模块中 |

已做的静态检查：

| 检查 | 结果 |
|---|---|
| `custom.qrc` 注册文件存在性 | 通过 |
| `git diff --check` | 未发现空白错误，存在历史 CRLF/LF 提示 |
| Viewer3D 旧未注册 QML 类型引用 | 已清理为 custom 注册路径 |
| 默认 UDP 链路字段与 QGC 加载格式 | 已核对 `name/type/auto/high_latency/port/hostCount/host0/port0` |

仍需实机或完整构建验证：

| 项 | 说明 |
|---|---|
| Qt 完整构建 | 需要在本机 Qt 6.8.3 环境中执行完整 build |
| SIYI 相机实机通信 | 需要连接思翼相机后验证 UDP 命令是否生效 |
| Ubuntu 虚拟机视频 | 需要桥接实际云台网口并验证 RTSP 8554、GStreamer H.264 解码和 OpenGL 渲染 |
| Google 3D Maps | 需要构建环境包含 Qt WebEngineQuick，并配置可用 API Key |
| 外部 3D 模型导入 | Balsam 转换依赖本机 Qt 工具链和模型格式 |

## 12. 后续开发建议

### 12.1 新增 Viewer3D 功能

建议修改位置：

| 需求 | 建议位置 |
|---|---|
| 增加 Viewer3D 参数 | `Viewer3D.SettingsGroup.json`、`Viewer3DSettings.h/.cc`、`Viewer3DSettingsGroup.qml` |
| 增加 3D 场景元素 | `Viewer3D/Viewer3DQml/Models3D` |
| 增加后端数据 | `Viewer3DQmlBackend.h/.cc` |
| 增加外部模型导入能力 | `External3DMapManager.h/.cc` |
| 增加图标或资源 | `custom/src/Viewer3D/Images` 并同步注册到 `custom.qrc` |

### 12.2 新增云台功能

建议修改位置：

| 需求 | 建议位置 |
|---|---|
| 增加云台协议命令 | `SiyiProtocol.h/.cc` |
| 增加 UDP 发送/响应处理 | `SiyiSdk.h/.cc` |
| 增加业务状态和 QML 接口 | `GimbalControlManager.h/.cc` |
| 增加云台参数 | `GimbalControl.SettingsGroup.json`、`GimbalControlSettings.h/.cc` |
| 增加设置页控件 | `GimbalControlSettingsGroup.qml` |
| 增加 Fly View 控件 | `GimbalFlyViewTopRightColumnLayout.qml`、`GimbalZoomControl.qml` 或新增同目录 QML |

### 12.3 修改默认通信链路

| 需求 | 修改位置 |
|---|---|
| 修改默认名称、地址或端口 | `DefaultCommunicationLinkInstaller.cc` 中的 `kDefault*` 常量 |
| 增加新的默认链路 | 在 `CommunicationLink` 中新增独立安装逻辑，继续保证名称或端点检查幂等 |
| 强制迁移旧配置 | 增加明确版本标记；不要在每次启动时覆盖用户已编辑的链路 |

### 12.4 常用检索命令

```powershell
rg -n "viewer3DSettings|Viewer3DManager|External3DMapManager" custom
rg -n "gimbalControlSettings|gimbalControlManager|SiyiProtocol|SiyiSdk" custom
rg -n "DefaultCommunicationLinkInstaller|COMMUNICATION_LINK_SOURCES" custom
rg -n "FuelStatusIndicator|FuelIcon" custom
rg -n "QGroundControl/AppSettings/FlyViewSettings|QGroundControl/FlightDisplay/FlyView" custom/custom.qrc
```

### 12.5 修改资源后的注意事项

1. 新增 QML、JSON、图片、shader 后，需要同步检查是否已注册到 `custom/custom.qrc`。
2. 新增 C++ 文件后，如果在 `Viewer3D`、`Gimbalcontrol` 或 `CommunicationLink` 目录下，当前 CMake 会通过 `GLOB_RECURSE` 自动收集。
3. 新增 custom 模块时，建议建立独立子目录，不要混入 Viewer3D 或 Gimbalcontrol。
4. 新增 QML 类型时，优先通过现有 QML 模块或 qrc 路径加载，避免直接修改 QGC 原生 `src`。
5. 修改设置参数后，需要同步更新 SettingsGroup JSON、Settings 类、设置页 QML 和本文档参数表。
