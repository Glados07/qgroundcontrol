# QGC Viewer3D 开发说明

适用项目：`F:\qgroundcontrol_viewer3d`，当前目标分支：`SecDev/ft/3D_map`

核心结论：当前 Viewer3D 已经迁移到 `custom`。后续阅读、移植、优化 3D 功能，主线看 `custom`，不要再把 `src/Viewer3D` 当成功能入口。

阅读导航：

```text
先理解合并结构    看第 0 章
先理解功能        看第 1、2、3 章
先找代码入口      看第 5、6、10 章
准备迁移合并      看第 7、8、9、12 章
准备深入修改      按第 11 章阅读顺序读代码
```

---

## 0. 当前整理状态：按项目 custom 入口风格拆分

### 0.1 目标

当前文件说明的是已经合并后的目标项目：`F:\qgroundcontrol_viewer3d` / `SecDev/ft/3D_map`。目标项目原有的 FirmwarePlugin、AutoPilotPlugin、FuelStatusIndicator、FlyViewCustomLayer 等功能保持为主；Viewer3D 作为独立模块接入 custom。

```text
入口层遵循项目 custom 入口风格
  -> QML 覆盖文件放在 custom/src 根目录
  -> 资源注册放在 qrc:/Custom/qml
  -> CustomPlugin 使用 CustomOverrideInterceptor 做 qrc:/qml 到 qrc:/Custom/qml 的覆盖选择

Viewer3D 核心保持独立模块
  -> C++ 后端仍在 custom/src/Viewer3D
  -> QML 3D 场景仍在 custom/src/Viewer3D/Viewer3DQml
  -> Viewer3D 模块资源仍在 qrc:/qml/Viewer3D
  -> 不放入 FirmwarePlugin / AutoPilotPlugin 等飞控插件模块
```

### 0.2 合并边界

| 类型 | 文件/目录 | 后续合并方式 |
|---|---|---|
| custom 主入口 | `custom/src/CustomPlugin.h/.cc` | 以目标项目入口文件为底，把 Viewer3D 的属性、初始化和 `External3DMapManager` 合进去 |
| 必须合并的 UI 覆盖 | `custom/src/FlyViewToolStripActionList.qml` | 只把 Viewer3D 按钮项合入目标项目同名文件 |
| 必须合并的 UI 覆盖 | `custom/src/FlyView.qml` | 把 `import Viewer3D`、`Viewer3D {}` 窗口和 `mapControl.enabled` 逻辑合入目标项目 FlyView |
| 必须合并的 UI 覆盖 | `custom/src/FlyViewWidgetLayer.qml` | 把 `isViewer3DOpen` 及 3D 打开时隐藏 2D 比例尺逻辑合入目标项目覆盖层 |
| Viewer3D 设置页覆盖 | `custom/src/UI/preferences/FlyViewSettings.qml`、`custom/src/UI/preferences/Viewer3DSettingsGroup.qml` | Fly View 设置页保留目标分支原有设置项，Viewer3D 设置组单独拆出，避免与目标项目原有设置页耦合 |
| Viewer3D 独立模块 | `custom/src/Viewer3D/**` | 整体迁移，不与 FirmwarePlugin/AutoPilotPlugin 等飞控插件模块混放 |
| Viewer3D QML module | `custom/src/QmlControls/Viewer3D/**` | 整体迁移，保持 `import Viewer3D` 可用 |
| 资源注册 | `custom/custom.qrc` | 目标项目已有 `/Custom/qml`，合并时加入 Viewer3D 覆盖项、Viewer3D 模块项和 `/custom/img/viewer3d_city_3d_map_icon.svg` |
| 编译入口 | `custom/CMakeLists.txt` | 目标分支使用 `CUSTOM_INCLUDE_DIRECTORIES`，已追加 `custom/src/Viewer3D` 和 Viewer3D 源码 |

### 0.3 当前 custom/src 结构

```text
custom/src
  CustomPlugin.h / CustomPlugin.cc        # custom 总入口，加入 Viewer3D 设置、外部模型导入管理器、QML 类型注册
  AppSettings.qml                         # Application Settings 外壳覆盖，Fly View 设置页显式导向 custom
  FlyView.qml                             # QGC FlyView 覆盖入口，挂载 Viewer3D 窗口
  CustomFlyViewToolStrip.qml              # Fly View 工具条覆盖，显式加载 custom ActionList
  FlyViewToolStripActionList.qml          # QGC FlyView 左侧工具条覆盖，增加 3D View/Fly 按钮
  FlyViewWidgetLayer.qml                  # QGC FlyView Widget 层覆盖，3D 打开时隐藏 2D 地图比例尺
  UI/preferences/FlyViewSettings.qml       # Application Settings -> Fly View 覆盖页，保留目标分支原有设置项
  UI/preferences/Viewer3DSettingsGroup.qml # Viewer3D 设置组，独立承载 3D View 相关 Fact 控件
  Viewer3D/                               # Viewer3D 独立 C++/QML/资源模块
  QmlControls/Viewer3D/                   # Viewer3D QML module 的 qmldir
```

### 0.4 QML 覆盖机制

```text
QGC 原始加载路径
  qrc:/qml/QGroundControl/FlightDisplay/FlyView.qml

CustomOverrideInterceptor 检查
  qrc:/Custom/qml/QGroundControl/FlightDisplay/FlyView.qml 是否存在

存在则使用 custom 覆盖文件
  custom/src/FlyView.qml

不存在则回退 QGC 原始文件
  src/FlightDisplay/FlyView.qml
```

这个机制与目标项目当前 custom 覆盖机制一致。区别是：Viewer3D 自己的模块不是覆盖 QGC 文件，而是新增模块，仍然注册在 `qrc:/qml/Viewer3D` 下，由 `import Viewer3D` 使用。

Application Settings 的 Fly View 设置页在当前 Qt6 模块中的真实加载路径是：

```text
qrc:/qml/QGroundControl/AppSettings/FlyViewSettings.qml
```

因此 `custom/custom.qrc` 必须注册同路径覆盖：

```text
qrc:/Custom/qml/QGroundControl/AppSettings/FlyViewSettings.qml
qrc:/Custom/qml/QGroundControl/AppSettings/SettingsPage.qml
qrc:/Custom/qml/QGroundControl/AppSettings/Viewer3DSettingsGroup.qml
```

这里不能只注册旧路径 `qrc:/Custom/qml/FlyViewSettings.qml`。旧路径只保留兼容用途，当前分支实际依赖 `QGroundControl/AppSettings/FlyViewSettings.qml` 这一条覆盖链路。

### 0.5 本次移植后的实际状态

```text
项目原有模块仍在
  custom/src/AutoPilotPlugin
  custom/src/FirmwarePlugin
  custom/src/FlyViewCustomLayer.qml
  custom/src/FuelStatusIndicator.qml
  custom/src/CustomGuidedActionsController.qml

Viewer3D 新增模块已迁入
  custom/src/Viewer3D
  custom/src/QmlControls/Viewer3D
  custom/src/AppSettings.qml
  custom/src/UI/preferences/FlyViewSettings.qml
  custom/src/UI/preferences/Viewer3DSettingsGroup.qml
  custom/src/FlyView.qml
  custom/src/CustomFlyViewToolStrip.qml
  custom/src/FlyViewWidgetLayer.qml

必须合并的入口文件已经合并
  custom/src/CustomPlugin.h/.cc
  custom/src/FlyViewToolStripActionList.qml
  custom/CMakeLists.txt
  custom/custom.qrc
```

合并原则：项目原有 custom 功能负责飞控插件、品牌、主题、燃料状态、FlyView 自定义覆盖层；Viewer3D 负责 3D 地图、3D 模型导入、3D 飞机/航点/航线显示。后续开发仍按这个边界拆分，避免把 Viewer3D 逻辑写入 FirmwarePlugin 或 AutoPilotPlugin。

本次排查确认的关键问题：

```text
1. 目标分支 App Settings 加载的是 qrc:/qml/QGroundControl/AppSettings/FlyViewSettings.qml。
2. 之前 custom.qrc 只注册 qrc:/Custom/qml/FlyViewSettings.qml，覆盖路径不匹配，Fly View 设置页会继续落到 src 中的半迁移版本。
3. src/UI/AppSettings/FlyViewSettings.qml 仍引用 QGroundControl.settingsManager.viewer3DSettings，但当前分支没有在 SettingsManager 暴露这个对象。
4. 修复后 custom 覆盖页使用 QGroundControl.corePlugin.viewer3DSettings，并把 Viewer3D 设置组拆到 Viewer3DSettingsGroup.qml。
5. Fly View 工具条图标使用 qrc:/custom/img/viewer3d_city_3d_map_icon.svg，按钮显示由 viewer3DSettings.enabled 控制。
```

二次构建测试后补充确认：

```text
1. Qt6 静态 QML 模块解析不完全等同于普通 qrc URL 加载，只靠 CustomOverrideInterceptor 不足以稳定替换模块内部同名 QML 类型。
2. Application Settings 外壳新增 custom/src/AppSettings.qml，Fly View 页面显式指向 qrc:/Custom/qml/QGroundControl/AppSettings/FlyViewSettings.qml。
3. Fly View 工具条新增 custom/src/CustomFlyViewToolStrip.qml，通过唯一类型名 Viewer3DToolStripActionList 显式实例化 custom 的 FlyViewToolStripActionList.qml。
4. custom/src/FlyView.qml 使用 Viewer3DFlyViewWidgetLayer 和 SecDevFlyViewCustomLayer 这两个唯一类型名，避免与 QGroundControl.FlightDisplay 模块中的同名 src 文件冲突。
5. CustomOverrideInterceptor 额外处理 QmldirFile、JavaScriptFile 和 /qt/qml 路径，作为模块解析场景下的兜底。
6. Viewer3D 设置页的布尔开关直接写 Fact.rawValue，避免 enabled 开关在跨 custom QObject 绑定时出现 UI 状态回弹。
```

---
## 1. 功能总览

### 1.1 Viewer3D 是什么

Viewer3D 是接入 QGC Fly View 的 3D 飞行态势显示模块。它的作用是：

```text
在 Fly View 中增加 3D View 按钮
打开后显示 3D 场景
支持本地 OSM 3D 地图源
支持可选 Google 3D Maps 在线源
读取 QGC 原有车辆、任务、地图设置和 Viewer3D 设置
```

它不是完整的 3D Plan View，也不是 QGC 原生 2D 地图的完整替代品。当前重点是显示和监视，不负责完整任务编辑。

### 1.2 用户入口

```text
Application Settings -> Fly View -> 3D View
  -> Enabled
  -> Use Google 3D Maps
  -> Google 3D Maps API Key
  -> 3D Map File
  -> Average Building Level Height
  -> Vehicles Altitude Bias

Fly View 左侧工具条
  -> 3D View / Fly 切换按钮
```

对应文件：

```text
设置页：custom/src/UI/preferences/FlyViewSettings.qml
工具条：custom/src/FlyViewToolStripActionList.qml
主界面：custom/src/FlyView.qml
覆盖层：custom/src/FlyViewWidgetLayer.qml
```

### 1.3 三种地图源

```text
本地 OSM 模式
  条件：Use Google 3D Maps = false，Use External 3D Model Map = false
  输入：3D Map File 指定的 .osm 文件
  显示：OSM 建筑 + QGC 地图瓦片地面 + QGC 3D 无人机模型 + 任务航点航线
  主文件：Viewer3DModel.qml

外部 3D 模型地图模式
  条件：Use Google 3D Maps = false，Use External 3D Model Map = true
  输入：OBJ / glTF / GLB / Balsam QML 直接加载，FBX / DAE / STL / PLY 通过 Qt Balsam 导入转换；同时配置 WGS84 原点、单位换算、比例、yaw
  显示：虚幻/Unity/Blender 等工具导出的 3D 场景 + QGC 3D 无人机模型 + 任务航点航线
  主文件：Viewer3DModel.qml + External3DMap.qml
  说明：Qt Quick3D 运行时直接加载 OBJ / glTF / GLB；FBX / DAE / STL / PLY 在设置页选择后由 External3DMapManager 自动调用 Qt Balsam 转为 QML/mesh。

Google 3D 在线模式
  条件：Use Google 3D Maps = true
  输入：Google 3D Maps API Key + 有效中心经纬度
  显示：Google Maps JavaScript 3D Maps 在线 3D 地图
  主文件：Google3DMapView.qml
  降级：未编译 Qt WebEngine 时加载 Google3DMapUnavailable.qml
```

---

## 2. 数据流说明

### 2.1 总入口

所有 3D 视图都由 `Viewer3D.qml` 管理。它先根据设置选择 Google WebEngine 在线视图或本地 Quick3D 视图；本地 Quick3D 视图再在 `Viewer3DModel.qml` 中选择 OSM 地图或外部 3D 模型地图。

```text
用户点击 Fly View 左侧 3D View
  -> FlyViewToolStripActionList.qml 调用 viewer3DWindow.open()
  -> Viewer3D.qml 激活 Viewer3DManager
  -> Viewer3D.qml 判断 useGoogle3DMapSource
       |-- false：加载 Models3D/Viewer3DModel.qml
       |-- true 且有 Qt WebEngine：加载 Google3DMapView.qml
       |-- true 但无 Qt WebEngine：加载 Google3DMapUnavailable.qml
```

关键文件：

```text
custom/src/Viewer3D/Viewer3DQml/Viewer3D.qml
custom/src/Viewer3D/Viewer3DQml/Models3D/Viewer3DModel.qml
custom/src/Viewer3D/Viewer3DQml/Google3DMapView.qml
custom/src/Viewer3D/Viewer3DQml/Google3DMapUnavailable.qml
custom/src/Viewer3D/Viewer3DManager.h/.cc
```

### 2.2 本地 OSM 文件 -> 3D 建筑

```text
用户选择 .osm 文件
  -> viewer3DSettings.osmFilePath 变化
  -> CityMapGeometry 监听到路径变化
  -> CityMapGeometry 调用 OsmParser.parseOsmFile(filePath)
  -> OsmParserThread 在线程中读取 OSM 文件
  -> 解析 node / way / relation / building 标签
  -> OsmParser 根据 height 或 levels * buildingLevelHeight 计算建筑高度
  -> earcut.hpp 把建筑轮廓三角化
  -> CityMapGeometry 生成 QQuick3DGeometry
  -> Viewer3DModel.qml 显示灰色 3D 建筑
```

关键文件：

```text
custom/src/UI/preferences/FlyViewSettings.qml
custom/src/Viewer3D/OsmParser.h/.cc
custom/src/Viewer3D/OsmParserThread.h/.cc
custom/src/Viewer3D/CityMapGeometry.h/.cc
custom/src/Viewer3D/earcut.hpp
```

### 2.3 QGC 地图设置 -> 本地 3D 地面贴图

```text
QGC 原有地图设置
  -> SettingsManager::instance()->flightMapSettings()
  -> 读取 mapProvider 和 mapType
  -> Viewer3DTerrainTexture 判断当前地图源
  -> Viewer3DTileQuery 根据 OSM 地图范围计算瓦片
  -> Viewer3DTileReply 请求单张地图瓦片
  -> Viewer3DTileQuery 拼接瓦片
  -> Viewer3DTerrainTexture 生成 QQuick3DTextureData
  -> Viewer3DTerrainGeometry 生成地面网格
  -> Viewer3DModel.qml 把贴图贴到地面几何上
```

关键文件：

```text
custom/src/Viewer3D/Viewer3DTerrainTexture.h/.cc
custom/src/Viewer3D/Viewer3DTerrainGeometry.h/.cc
custom/src/Viewer3D/Viewer3DTileQuery.h/.cc
custom/src/Viewer3D/Viewer3DTileReply.h/.cc
custom/src/Viewer3D/Viewer3DUtils.h/.cc
```

### 2.4 Google 3D 在线源 -> 在线 3D 地图

```text
Use Google 3D Maps = true
  -> Viewer3D.qml 不加载本地 Viewer3DModel.qml
  -> 加载 Google3DMapView.qml
  -> Google3DMapView.qml 读取 Google 3D Maps API Key
  -> 优先使用 active vehicle 坐标作为地图中心
  -> 没有 active vehicle 时使用 QGC.flightMapPosition
  -> Qt WebEngine 加载 Google Maps JavaScript API
  -> Google Maps JavaScript API 渲染真实经纬度 3D 地图
```

关键文件：

```text
custom/src/UI/preferences/FlyViewSettings.qml
custom/src/Viewer3D/Viewer3D.SettingsGroup.json
custom/src/Viewer3D/Viewer3DQml/Viewer3D.qml
custom/src/Viewer3D/Viewer3DQml/Google3DMapView.qml
custom/src/Viewer3D/Viewer3DQml/Google3DMapUnavailable.qml
custom/CMakeLists.txt
```

注意：Google 3D 在线源由 Google Web 端渲染。当前没有把 QGC 的 3D 无人机模型、任务航点、任务航线叠加到 Google WebEngine 视图里。要继续叠加，需要后续开发 WebChannel 或在 Google 3D JS 中同步 marker/polyline。

### 2.5 外部 3D 模型地图 -> 本地 Quick3D 地图

```text
Use External 3D Model Map = true
  -> FlyViewSettings.qml 显示外部模型文件和配准参数
  -> 用户点击 Select File 选择 OBJ / glTF / GLB / QML / FBX / DAE / STL / PLY
  -> External3DMapManager.importModelFile(file)
       |-- .obj / .gltf / .glb / .qml：直接写入 external3DMapFilePath
       |-- .fbx / .dae / .stl / .ply：自动查找 Qt Balsam 并转换到 QML/mesh，再写入 external3DMapFilePath
       |-- 找不到 balsam 或转换失败：lastImportStatus 返回错误，设置页显示提示
  -> Viewer3DQmlBackend 使用外部模型 originLat/originLon/originAlt 设置 gpsRef
  -> Viewer3DModel.qml 仍然加载本地 Quick3D 车辆/任务层
  -> Viewer3DModel.qml 切换地图层为 External3DMap.qml
  -> External3DMap.qml 读取 external3DMapFilePath
       |-- .obj / .gltf / .glb：RuntimeLoader 运行时直接加载
       |-- .qml：Loader3D 加载 Qt Balsam 生成的 QML 模型
  -> 外部模型按 unitToMeters * scale * 10 缩放
  -> 外部模型按 yaw 旋转到 QGC ENU 坐标系
  -> 无人机和任务航点继续使用同一个 gpsRef 转成本地 3D 坐标
  -> 外部模式下车辆高度优先使用 altitudeAMSL - originAlt，任务高度优先使用 amslEntryAlt - originAlt
```

关键文件：

```text
custom/src/UI/preferences/FlyViewSettings.qml
custom/src/Viewer3D/Viewer3D.SettingsGroup.json
custom/src/Viewer3D/Viewer3DSettings.h/.cc
custom/src/Viewer3D/External3DMapManager.h/.cc
custom/src/Viewer3D/Viewer3DQmlBackend.h/.cc
custom/src/Viewer3D/Viewer3DQml/Models3D/Viewer3DModel.qml
custom/src/Viewer3D/Viewer3DQml/Models3D/External3DMap.qml
custom/CMakeLists.txt
custom/custom.qrc
```

坐标配准含义：

```text
Origin Latitude / Origin Longitude
  外部模型局部原点对应的 WGS84 经纬度。

Origin Altitude
  外部模型局部原点对应的 AMSL 海拔参考。外部模型模式下，车辆高度优先使用 vehicle.altitudeAMSL - originAlt；任务点高度优先使用 VisualMissionItem.amslEntryAlt - originAlt。缺少 AMSL 数据时回退到原来的相对高度/任务 altitude.value。Vehicles Altitude Bias 仍可用于最终垂直微调。

Model Unit To Meters
  1 个模型单位等于多少米。虚幻默认 1 unit = 1 cm，因此填 0.01；米制 glTF/OBJ 通常填 1.0。

Model Scale
  额外比例系数，用于模型导出比例不准确时微调。

North/Yaw Angle
  把模型北向旋到 QGC ENU 坐标系的角度。默认 0 表示模型 +Y 为北、+X 为东。

FBX 等创作格式导入
  设置页不会把 .fbx 直接交给 RuntimeLoader。External3DMapManager 会查找 Qt Balsam：优先 QGC_VIEWER3D_BALSAM 环境变量，其次 QGC 程序目录、当前 Qt Kit 的 BinariesPath，最后 PATH。
  转换输出目录在 QStandardPaths::AppDataLocation/Viewer3DExternalMaps 下，转换成功后设置项保存生成的 .qml 文件路径。
```

### 2.6 QGC 车辆数据 -> 本地 3D 无人机模型

```text
QGC 连接飞控或 SITL
  -> MultiVehicleManager 生成 Vehicle 对象
  -> QGroundControl.multiVehicleManager.vehicles 提供车辆列表
  -> Viewer3DModel.qml 用 Repeater3D 遍历 vehicles
  -> 每个 vehicle 创建 Viewer3DVehicleItems
  -> Viewer3DVehicleItems 创建 DroneModelDjiF450
  -> DroneModelDjiF450 读取经纬度、高度、航向、横滚、俯仰
  -> GeoCoordinateType 把经纬度转换为本地 3D 坐标
  -> 3D 场景显示无人机模型、编号、位置和姿态
```

读取的主要 Vehicle 数据：

```text
vehicle.coordinate
vehicle.altitudeRelative.value
vehicle.heading.value
vehicle.roll.value
vehicle.pitch.value
vehicle.armed
vehicle.flying
vehicle.id
```

关键文件：

```text
custom/src/Viewer3D/Viewer3DQml/Models3D/Viewer3DModel.qml
custom/src/Viewer3D/Viewer3DQml/Models3D/Viewer3DVehicleItems.qml
custom/src/Viewer3D/Viewer3DQml/Drones/DroneModelDjiF450.qml
custom/src/Viewer3D/Viewer3DQmlVariableTypes.h
custom/src/Viewer3D/Viewer3DUtils.h/.cc
```

### 2.7 QGC 任务数据 -> 本地 3D 航点和航线

```text
每个 vehicle 创建 Viewer3DVehicleItems
  -> 内部创建 PlanMasterController
  -> startStaticActiveVehicle(vehicle)
  -> 得到该 vehicle 对应的 missionController
  -> 读取 missionController.visualItems
  -> 筛选 Viewer3D 当前支持的 mission command
  -> 把任务项经纬度转换为本地 3D 坐标
  -> Waypoint3DModel 显示航点
  -> Line3D 显示航线
```

当前支持的任务命令：

```text
16    Waypoint
20    Return To Launch
22    Takeoff
195   ROI
201   ROI Deprecated
```

关键文件：

```text
custom/src/Viewer3D/Viewer3DQml/Models3D/Viewer3DVehicleItems.qml
custom/src/Viewer3D/Viewer3DQml/Models3D/Waypoint3DModel.qml
custom/src/Viewer3D/Viewer3DQml/Models3D/Line3D.qml
```

### 2.8 GPS 参考点

本地 OSM 模式和外部 3D 模型地图模式中，3D 场景不能直接使用经纬度渲染，需要先转换成本地 `x/y/z` 坐标。这个转换需要 GPS 参考点 `gpsRef`。

```text
优先来源 1：OSM 文件
  -> OSM 解析完成后给出地图参考点
  -> Viewer3DQmlBackend 设置 gpsRef

备用来源 3：active vehicle
  -> 如果没有 OSM 参考点
  -> active vehicle 第一次给出有效经纬度
  -> Viewer3DQmlBackend 用车辆坐标设置 gpsRef
```

Google 3D 在线模式不走这个本地坐标转换链路，它直接把真实经纬度传给 Google Maps JavaScript 3D Maps。

---

## 3. 设置项说明

当前 `Application Settings -> Fly View -> 3D View` 中有这些设置：

```text
Enabled
  启用 3D View。关闭后 Fly View 左侧不会显示 3D View 按钮。

Use Google 3D Maps
  是否使用 Google 3D 在线地图源。
  开启后不再加载本地 3D Map File。

Google 3D Maps API Key
  Google Maps JavaScript API Key。
  仅 Use Google 3D Maps 开启时显示并生效。

3D Map File
  本地 OSM 文件路径。
  仅 Use Google 3D Maps 关闭时显示并生效。

Average Building Level Height
  OSM 建筑没有明确 height 时，用 levels * 该值计算建筑高度。
  仅本地 OSM 模式使用。

Vehicles Altitude Bias
  本地模式下车辆和航线高度偏置。
  当前 Google WebEngine 视图没有叠加 QGC 车辆模型，因此该项只在本地模式显示。
```

关键文件：

```text
custom/src/UI/preferences/FlyViewSettings.qml
custom/src/Viewer3D/Viewer3D.SettingsGroup.json
custom/src/Viewer3D/Viewer3DSettings.h
custom/src/Viewer3D/Viewer3DSettings.cc
```

---

## 4. 2D 视图和 3D 视图对比

### 4.1 3D 已实现

```text
本地 OSM 3D 建筑
本地地面地图瓦片贴图
3D 相机平移、旋转、缩放
多机 3D 模型显示
车辆经纬度、高度、航向、横滚、俯仰显示
基础任务航点显示
基础任务航线显示
Google 3D Maps 在线地图显示
外部模型地图上叠加 QGC 无人机模型、任务航点和任务航线
```

### 4.2 3D 部分实现

```text
地图底图
  本地模式复用 QGC 地图设置下载瓦片，但不是完整 2D 地图控件。
  Google 模式由 Google Web 端渲染，当前没有叠加 QGC 车辆和任务。

任务航点航线
  本地模式只支持 Waypoint、Takeoff、RTL、ROI 等基础命令。
  Survey / Corridor / Structure Scan 等复杂任务未完整实现。

车辆显示
  本地模式有 3D 无人机模型。
  Google 模式目前只显示 Google 3D 地图，没有叠加 QGC 模型。
```

### 4.3 3D 当前没有实现

```text
3D 任务新增、拖拽、删除、插入航点
复杂任务形状完整显示
地理围栏显示和编辑
Rally Point 显示和编辑
Guided Actions 3D 专用操作界面
3D 自动跟随车辆 / 回中 / fit mission
ADSB / 避障 / 相机触发点 3D 叠加
Google WebEngine 视图上的 QGC 车辆/航线叠加
```

---

## 5. QGC 程序框架

```text
QGroundControl 根目录
  CMakeLists.txt
    统一组织 QGC 编译目标、资源、库链接。
    重建项目时打开这个文件。

  src/API
    QGCCorePlugin、QGCOptions 等二次开发接口。
    Viewer3D 通过 CustomPlugin 接入这里。

  src/Settings
    QGC 设置系统，Fact / SettingsGroup。
    Viewer3D 自己定义 Viewer3DSettings，然后挂到 CustomPlugin。

  src/FlightDisplay
    Fly View 主界面、工具条、HUD、引导动作。
    Viewer3D 覆盖其中 3 个 QML 接入 Fly View。

  src/FlightMap
    2D 地图、地图 item、地图 fit、地图控件。
    本地 Viewer3D 复用部分地图设置和瓦片能力。

  src/PlanView
    任务规划、任务编辑、围栏、集结点、上传下载。
    Viewer3D 只读取任务显示，不替代 Plan View。

  src/Vehicle / MultiVehicleManager
    车辆对象、active vehicle、多机列表、遥测 Fact。
    Viewer3D 从这里读取车辆位置和姿态。

  custom
    官方 custom build 扩展位置。
    当前 Viewer3D 的主要开发位置。
```

---

## 6. custom 结构树

```text
custom/
  CMakeLists.txt
    custom 构建入口。
    加入 Viewer3D 源码。
    链接 Qt Quick3D 和 Qt Quick3DAssetUtils。
    可选查找 Qt WebEngineQuick，用于 Google 3D 在线源。
    使用目标分支的 CUSTOM_INCLUDE_DIRECTORIES，并加入 custom/src/Viewer3D。

  custom.qrc
    /Custom/qml
      注册 QGC 入口层覆盖文件，路径风格与目标项目 custom 覆盖机制一致。
    /qml/Viewer3D
      注册 Viewer3D 独立 QML 模块。
    /custom/img
      注册 Viewer3D 工具条图标。
    /json
      注册 Viewer3D.SettingsGroup.json。
    /ShaderVertex 和 /ShaderFragment
      注册 Quick3D 地面材质 shader。

  qgroundcontrol.exclusion
    排除 QGC 原始 FlyView/FlyViewToolStripActionList/FlyViewWidgetLayer/FlyViewSettings，避免资源 alias 重复。

  src/
    CustomPlugin.h/.cc
      custom 插件入口。
      使用 CustomOverrideInterceptor，优先加载 qrc:/Custom/qml 下的覆盖文件。
      暴露 viewer3DSettings。
      暴露 external3DMapManager。
      暴露 google3DMapsAvailable。
      注册 Viewer3D QML 类型。

    FlyView.qml
      覆盖 Fly View 主容器。
      加入 Viewer3D 窗口。
      3D 打开时禁用 2D 地图交互。

    FlyViewToolStripActionList.qml
      覆盖左侧工具条。
      增加 3D View / Fly 切换按钮。
      后续合并到目标分支时，只合并 Viewer3D 这一项。

    FlyViewWidgetLayer.qml
      覆盖 Fly View 上层 UI。
      3D 打开时隐藏 2D MapScale。

    UI/preferences/FlyViewSettings.qml
      覆盖 Fly View 设置页。
      增加 3D View 设置组、Google 3D Maps 开关、外部 3D 模型地图导入和配准参数。

    Viewer3D/
      Viewer3DSettings.*
      Viewer3D.SettingsGroup.json
      Viewer3DManager.*
      External3DMapManager.*
      Viewer3DQmlBackend.*
      OsmParser* / CityMapGeometry.*
      Viewer3DTerrain* / Viewer3DTile*
      Viewer3DQml/
        Viewer3D.qml
        Google3DMapView.qml
        Google3DMapUnavailable.qml
        Models3D/Viewer3DModel.qml
        Models3D/External3DMap.qml
        Models3D/Viewer3DVehicleItems.qml
        Models3D/Waypoint3DModel.qml
        Models3D/Line3D.qml
        Drones/DroneModelDjiF450.qml

    QmlControls/Viewer3D/
      qmldir 文件，声明 Viewer3D QML module。
```
---

## 7. qrc 覆盖机制

当前分支已经改成目标项目 custom 覆盖风格：QGC 原始资源仍保留在 `qrc:/qml/...`，custom 覆盖文件注册在 `qrc:/Custom/qml/...`，由 `CustomOverrideInterceptor` 在运行时选择。

```text
QGC 请求
  qrc:/qml/QGroundControl/FlightDisplay/FlyView.qml

CustomOverrideInterceptor 查找
  qrc:/Custom/qml/QGroundControl/FlightDisplay/FlyView.qml

如果存在
  使用 custom/src/FlyView.qml

如果不存在
  回退使用 QGC 原始 src/FlightDisplay/FlyView.qml
```

当前覆盖的 QML：

```text
qrc:/Custom/qml/FlyViewSettings.qml
  -> custom/src/UI/preferences/FlyViewSettings.qml

qrc:/Custom/qml/QGroundControl/FlightDisplay/FlyView.qml
  -> custom/src/FlyView.qml

qrc:/Custom/qml/QGroundControl/FlightDisplay/FlyViewToolStripActionList.qml
  -> custom/src/FlyViewToolStripActionList.qml

qrc:/Custom/qml/QGroundControl/FlightDisplay/FlyViewWidgetLayer.qml
  -> custom/src/FlyViewWidgetLayer.qml
```

Viewer3D 自己新增的 qrc 资源：

```text
qrc:/qml/Viewer3D/Viewer3D.qml
qrc:/qml/Viewer3D/Google3DMapView.qml
qrc:/qml/Viewer3D/Google3DMapUnavailable.qml
qrc:/qml/Viewer3D/Models3D/...
qrc:/qml/Viewer3D/Models3D/External3DMap.qml
qrc:/custom/img/viewer3d_city_3d_map_icon.svg
qrc:/json/Viewer3D.SettingsGroup.json
qrc:/ShaderVertex/earthMaterial.vert
qrc:/ShaderFragment/earthMaterial.frag
```

合并到目标分支时，要优先保留目标项目已有的 `/Custom/qml` 机制，再把 Viewer3D 的覆盖项和独立模块资源加进去。
---

## 8. API 调用关系

```text
QGCCorePlugin
  -> CustomPlugin 继承它

QGroundControl.corePlugin.viewer3DSettings
  -> QML 读取 Viewer3D 设置

QGroundControl.corePlugin.external3DMapManager
  -> 设置页导入外部 3D 模型文件
  -> OBJ/glTF/GLB/QML 直接写入 external3DMapFilePath
  -> FBX/DAE/STL/PLY 调用 Qt Balsam 转换后写入 external3DMapFilePath

QGroundControl.corePlugin.google3DMapsAvailable
  -> QML 判断当前构建是否支持 Google 3D WebEngine 视图

QGroundControl.multiVehicleManager.vehicles
  -> 本地模式 3D 多机模型来源

MultiVehicleManager::instance()->activeVehicle()
  -> 本地 gpsRef 参考点来源
  -> Google 3D 地图中心点优先来源

PlanMasterController.startStaticActiveVehicle(vehicle)
  -> 本地模式读取每个 vehicle 的 missionController

SettingsManager::instance()->flightMapSettings()
  -> 本地模式读取 QGC 地图源和地图类型

Qt Quick3D
  -> 本地 OSM 模式和外部模型模式 3D 场景

Qt Quick3D.AssetUtils / RuntimeLoader
  -> 外部 OBJ/glTF/GLB 模型运行时加载

Qt Balsam
  -> FBX/DAE/STL/PLY 转换为 Quick3D QML/mesh

Qt WebEngineQuick / WebEngineView
  -> Google 3D 在线源承载层

Google Maps JavaScript API 3D Maps
  -> Google 在线真实经纬度 3D 地图渲染
```

---

## 9. 移植到其他 custom 项目

### 9.1 可以整体复制

```text
custom/src/Viewer3D/
custom/src/QmlControls/Viewer3D/
```

### 9.2 必须手工合并

```text
custom/CMakeLists.txt
custom/custom.qrc
custom/qgroundcontrol.exclusion
custom/src/CustomPlugin.h
custom/src/CustomPlugin.cc
custom/src/FlyView.qml
custom/src/FlyViewToolStripActionList.qml
custom/src/FlyViewWidgetLayer.qml
custom/src/UI/preferences/FlyViewSettings.qml
```

这些文件在其他二次开发项目中很可能已有改动，不能直接覆盖。

### 9.3 移植后检查

```powershell
rg -n "viewer3DSettings|Viewer3DManager|viewer3DWindow|useGoogle3DMapSource|Google3DMapView" custom
rg -n "QGroundControl.settingsManager.viewer3DSettings" custom
rg -n "src/Viewer3D|Quick3D|WebEngineQuick" src CMakeLists.txt cmake qgroundcontrol.qrc qgcimages.qrc
```

期望结果：

```text
Viewer3D 接入点主要在 custom 中。
不再使用 QGroundControl.settingsManager.viewer3DSettings。
src 中不再有 Viewer3D 功能本体。
Quick3D 由 custom/CMakeLists.txt 引入。
WebEngineQuick 只作为 Google 3D 在线源的可选依赖。
```

---

## 10. 开发定位速查

```text
3D View 按钮不显示
  -> FlyViewSettings.qml
  -> FlyViewToolStripActionList.qml
  -> CustomPlugin.viewer3DSettings

Google 3D 开关不显示
  -> Viewer3D.SettingsGroup.json
  -> Viewer3DSettings.h/.cc
  -> FlyViewSettings.qml

Google 3D 打开后显示未编译 WebEngine
  -> 检查 Qt Kit 是否安装 Qt WebEngine
  -> 重新运行 CMake
  -> 查看 custom/CMakeLists.txt 是否找到 Qt6::WebEngineQuick

Google 3D 打开后提示 API Key
  -> Application Settings -> Fly View -> 3D View -> Google 3D Maps API Key

改本地 OSM 场景
  -> Viewer3DQml/Models3D/Viewer3DModel.qml

改 Google 3D 在线视图
  -> Viewer3DQml/Google3DMapView.qml

改本地车辆和航线显示
  -> Viewer3DQml/Models3D/Viewer3DVehicleItems.qml
  -> Viewer3DQml/Drones/DroneModelDjiF450.qml

改 OSM 建筑解析
  -> OsmParser.cc
  -> OsmParserThread.cc
  -> CityMapGeometry.cc

改本地地图瓦片
  -> Viewer3DTerrainTexture.cc
  -> Viewer3DTileQuery.cc
  -> Viewer3DTileReply.cc
```

---

## 11. 推荐阅读顺序

```text
1. custom/src/UI/preferences/FlyViewSettings.qml
2. custom/src/FlyViewToolStripActionList.qml
3. custom/src/FlyView.qml
4. custom/src/CustomPlugin.h/.cc
5. custom/src/Viewer3D/Viewer3DSettings.* 和 Viewer3D.SettingsGroup.json
6. custom/src/Viewer3D/Viewer3DQml/Viewer3D.qml
7. custom/src/Viewer3D/Viewer3DQml/Google3DMapView.qml
8. custom/src/Viewer3D/Viewer3DQml/Models3D/Viewer3DModel.qml
9. custom/src/Viewer3D/Viewer3DQml/Models3D/Viewer3DVehicleItems.qml
10. custom/src/Viewer3D/Viewer3DManager.* 和 Viewer3DQmlBackend.*
11. custom/src/Viewer3D/OsmParser* 和 CityMapGeometry.*
12. custom/src/Viewer3D/Viewer3DTerrain* 和 Viewer3DTile*
```

---

## 12. 重要注意事项

```text
重建项目
  打开根目录 CMakeLists.txt。
  不要打开 src/CMakeLists.txt，也不要打开 custom/src/CMakeLists.txt。

本地 OSM 模式依赖
  需要 Qt Quick3D。
  需要有效 .osm 文件才能显示建筑。

Google 3D 在线模式依赖
  需要 Google Maps JavaScript API Key。
  建议在 Google Cloud 中启用 Maps JavaScript API 和 3D Maps 相关能力。
  需要 Qt Kit 安装 Qt WebEngine。
  没有 Qt WebEngine 时仍可编译，但会显示 Google3DMapUnavailable 提示。

飞控连接
  打开 3D View 不必须连接飞控。
  本地模式下车辆模型、位置、姿态、任务航点和航线需要真实飞控或 SITL 数据。
  Google 模式下地图中心优先使用 active vehicle 坐标，没有车辆时使用 QGC.flightMapPosition。

当前边界
  Google 3D 模式当前只显示 Google 在线 3D 地图。
  QGC 本地 3D 无人机模型和任务航线尚未叠加到 Google WebEngine 视图。

src 修改
  当前 Viewer3D 框架不应再依赖 src/Viewer3D。
  后续移植以 custom 为主。
```
