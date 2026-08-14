# QGC 二次开发说明

适用工程：`F:\qgroundcontrol_viewer3d`

当前分支：`SecDev/ft/gimbal`

最后更新：2026-08-13

## 1. 当前开发进度

### 1.1 总体进度

当前开发分支为 `SecDev/ft/gimbal`，二次开发已形成十一个面向用户的功能模块和一套 `custom` 工程化集成架构：

1. Viewer3D 三维飞行视图。
2. 思翼 A8 Mini 云台控制与独立本地照片/录像。
3. RTSP 视频流集成与 Android H.265 低延迟硬件解码。
4. 飞行界面底部航向罗盘条。
5. Android 遥控器默认界面缩放。
6. Android USB 飞控连接。
7. Fuel 燃料状态与低电压告警。
8. 默认通信链路安装。
9. PX4 FirmwarePlugin/AutoPilotPlugin 定制。
10. Proximity Radar 距离传感器告警。
11. A8 Mini + UniPod MT11 双云台、独立双视频和 Map/A8/MT11 三视图切换。

各模块当前所处阶段如下：

- **已集成**：Viewer3D、思翼云台、Fuel、Proximity Radar、默认通信链路和 PX4 定制均已接入 `custom` 构建、资源及运行链路。
- **代码已集成，待目标遥控器真机回归验收**：本轮A8 Mini缩放即时单击/原生连续长按状态机、SD卡与本地媒体双支路、UniPod MT11私有SDK、独立第二路RTSP、Map/A8/MT11三视图、双相机右栏、底部航向罗盘条、Android 86%界面缩放缺省值、Android H.265低延迟硬解和Android USB串口管理器已经进入当前工作树。仍需按第12章完成目标设备上的MT11抓包、双路解码、热成像、存储、布局、延迟、切换和重连测试，不能仅凭协议测试、静态接线或decoder rank判定整机验收通过。
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

- 在 `custom` 中实现思翼私有 UDP SDK 和协议封装，包括帧组装、CRC16、固定为0的协议sequence字段，以及0x05、0x0a、0x0b、0x0c、0x0f、0x16、0x18和0x20。短按使用0x0f绝对倍率，每次沿唯一合法目标表推进一档；长按在420 ms成立后通常只发送一次0x05方向命令，让相机原生连续变倍，避免每个步长重新启动绝对变倍控制器造成顿挫。长按显示目标仍根据总按压时长 `qRound(totalMs / 600.0)` 从手势起点单调推进，到达当前有效端点立即停止0x05；若成立时只剩最后一个合法区间，则直接发送一次同方向端点0x0f，避免极短0x05无法到位。活动0x05路径在正常释放和取消时都会立即发送停止并安排一份80 ms有界安全重复；长按结束后不发送可能造成先放大后缩小或先缩小后放大的0x0f归整。0x18独立记录真实反馈，不覆盖合法显示目标。
- A8 Mini数字变焦上限改由卡录编码参数确认。Manager通过0x20查询 `stream_type=0` 的卡录流，按3840×2160或4096×2160（4K）→1.0x且不可变倍、2560×1440（2K）→3.5x、1920×1080→5.5x、1280×720→6.0x映射能力；合法0x16设备上限只作为安全交叉校验，只能通过取较小值收紧该能力，不能把上限扩展到卡录分辨率规则之外。有效0x20会刷新4.5秒专用新鲜度期限，连续两轮没有有效卡录参数时立即失效旧能力、停止活动手势并锁定缩放，其他命令有回包不能替代该期限。QGC实际解码拉流尺寸不再推导倍率，只通过真实首帧CAPS、最终 `GstVideoInfo` 或稳定的 `VideoManager::videoSize`确认受支持视频会话是否可用。
- 2026-07-29的“1920×1080拉流＋2K卡录”日志中，0x16持续报告3.5x且0x18最终不超过3.5x，证明拉流1080P不能把卡录2K的相机上限提升为5.5x。2026-07-30把卡录改为1080P后，0x16持续报告5.5x，0x0f目标1.0/2.0/3.0/4.0/5.0/5.5对应稳定0x18约为1.0/2.0/2.9/3.8～4.0/4.7/5.2，目标与实际总体吻合但仍有固件量化误差。因此UI继续显示合法目标，0x18用于核对和诊断，不能用运动中raw触发释放后的反向纠偏。
- `zoomStep`采用唯一的最小值锚网格：从1.0x按 `zoomStep`递增，并把当前卡录能力与合法0x16交叉校验后的有效上限作为最后一个合法目标；放大和缩小都在同一有序目标表中按相邻项移动。默认步长1.0x时，2K卡录双向使用1.0、2.0、3.0、3.5；1080P卡录使用1.0、2.0、3.0、4.0、5.0、5.5；720P卡录使用1.0、2.0、3.0、4.0、5.0、6.0；4K卡录只有1.0。
- `currentZoom`表示当前合法目标倍率。tap的新0x0f目标在本地发送成功后立即写入并显示；hold在0x05成功启动后按时间更新同一合法表中的显示目标。两者都不等待0x18证明镜头已经到位。0x16/0x18仍按新版“整数字节+一位小数字节”优先解析，并兼容真机小端uint16/10格式；0x20读取卡录流分辨率。0x18实际值保存在独立反馈状态中，用于核对和同步，不能把运动中raw直接写成新的目标倍率。
- `GimbalControlManager` 把视频会话门控、卡录能力、0x16设备安全上限、当前目标倍率、最近实际反馈和在途命令拆成独立状态。tap从当前目标立即取同一合法表的相邻一档并发送；快速tap从上一成功目标继续规划。hold锁存手势起点和方向，420 ms成立后通常仅发送一次0x05方向命令，随后按总按压时长每600 ms重新计算并显示目标档数，不再周期性发送0x0f；目标首次到达有效端点便立即停止原生运动。若hold成立时第一目标已经是端点，则只发送一次同方向端点0x0f而不启动0x05。普通release在最后一次时间计算后停止0x05；取消、移出、隐藏、后台和销毁不推进目标但同样可靠停止0x05。停止后不发送0x0f归整，断流、重连和设置变化也不会复活旧手势。
- 飞行界面右侧使用单个始终可见的纵向半透明控制栏：只要Gimbal功能已启用，无论是否连接飞控或云台都会显示；从上到下依次为放大、当前目标倍率、缩小、横向分隔线、拍照/录像图标按钮以及SD/LOCAL状态徽标。空闲时两个相机按钮均为 `actionSize` 圆形触控区，录像图标与拍照图标等大且不显示“录像/REC”；开始录像后仅为计时、pending或失败状态文字按内容展开。受支持拉流会话或卡录能力尚未确认时缩放按钮锁定；两者确认后按钮启用状态只由当前目标在同一合法表中的方向边界决定。到上限仅禁用加号，到1.0x仅禁用减号；真实断流重新锁定两键并显示 `--`，同分辨率重连后会用已确认或重新查询的卡录能力解锁。本地录像按钮可用性另由本地开关与流状态决定，不要求云台SD能力。
- 拍照成功只在存在本次思翼拍照请求时由0x0b功能反馈累计机内照片数；录像按钮通过0x0a确认SD卡录像状态，0x0c切换没有ACK，约400 ms后主动查询并以2.5秒超时保护。与此同时，Application Settings -> Video -> Local Video Storage 新增 `localMediaStorageEnabled`，默认 `true`、即时生效；开启后，同一次拍照/录像按钮操作并行驱动“思翼SD卡”和“本机”两条互不回滚的支路。无云台SD卡、SDK离线或SDK命令失败均不阻断本地支路；反过来本地路径、码流或写盘失败也不改写相机支路状态。
- 本地照片是当前主视频渲染项的解码帧截图，不是从相机SD卡下载原始照片。Manager优先把新鲜且合法的0x20卡录宽高作为JPG物理像素尺寸，并用 `effectiveDevicePixelRatio()` 换算 `QQuickItem::grabToImage(targetSize)` 所需的逻辑尺寸，因此Android PIP与视频主画面不再决定输出分辨率；云台0x0a报告无卡不会单独清除此配置，也不会阻断本地支路，只有0x20从未确认、超时失效或尺寸不支持时才回退实际拉流、VideoManager、视频Item隐式源尺寸和最终物理显示尺寸。4K卡录而实时拉流仅1080P时，文件按4K尺寸输出但新增像素来自实时帧上采样，细节不会超过拉流；4096×2160与16:9拉流不等比时居中保留完整画面并补黑边。等待Qt Quick `ready`由5秒Timer保护，超时只让业务generation失效；抓图强引用由 `QQuickWindow`托管到安全完成/销毁边界，不能在GUI线程直接删除仍可能运行于渲染线程的对象，退休holder安全释放前也不再创建第二份本地grab。拿到QImage后，尺寸修正、黑边、质量100 JPEG编码和 `QSaveFile`原子提交转入最大并发1的专用线程池；整个grab/worker期间只允许一张本地照片在途。桌面端仍使用 `AppSettings::photoSavePath()`；Android先把JPG原子写入同卷 `getExternalFilesDirs(null)/Custom-QGroundControl/Staging/Photo`，提交成功即累计LOCAL反馈并把暂存文件交给单线程发布器；Android 10+最终发布到 `MediaStore.Images` 的 `Pictures/Custom-QGroundControl/`，而不是把最终照片留在应用专属目录。因此“JPG暂存成功/LOCAL计数”与“公共图库发布完成”仍是两个阶段。
- 本地录像不调用会同时遍历主/thermal接收器的 `VideoManager::startRecording()/stopRecording()`；`CustomPlugin`只把主（非thermal）`VideoReceiver`交给Manager，Manager直接调用该receiver的 `startRecording(outputFile, format)`/`stopRecording()`记录当前主压缩码流。桌面目录仍为 `AppSettings::videoSavePath()`；Android先把容器写入同卷 `getExternalFilesDirs(null)/Custom-QGroundControl/Staging/Video`，只有confirmed-owned主receiver最终报告 `recording=false`、容器已封装后才异步发布到 `MediaStore.Video` 的 `Movies/Custom-QGroundControl/`。文件格式仍读取 `VideoSettings::recordingFormat`；发布需要在同一存储卷上暂时同时保留完整暂存源与公共目标，因此长视频必须预留约一份成品大小的额外空间。Android公共录像的 `maxVideoSize` 只统计并删除当前安装记录在 `SharedPreferences`、名称含本功能 `_local_NNN`锚点（兼容MediaStore同名后缀）的公开Movies URI，按 `DATE_ADDED` 从旧到新清理；配额绝不删除尚未公开的Staging源，发布失败源会在重试成功或用户处理前额外占用空间。卸载会清除该私有注册表，重装前已发布的历史公共媒体仍保留且不会被新安装自动删除。VideoManager仍通过主receiver既有信号更新全局录像状态和字幕，thermal接收器不会因本功能开始或停止。
- Android新增custom媒体库V2桥。Android 10+向暂存源所在的具体可写卷插入 `MediaStore.Images` 或 `MediaStore.Video`，先写 `IS_PENDING=1`，再通过 `ParcelFileDescriptor` 复制、flush/fsync并校验复制字节数精确等于源文件长度，最后清除pending状态、原子提交 `SharedPreferences` URI journal并删除暂存/旧源；失败会删除不完整MediaStore行并保留源文件供下次启动重试。Android 7.1至9（API 25–28）则复制到公共 `Pictures/Custom-QGroundControl/` 或 `Movies/Custom-QGroundControl/` 的隐藏 `.publication.partial`，flush/fsync、字节校验及最终改名后等待最多30秒MediaScanner回传非空URI，获得URI后才删除源。启动时枚举所有已挂载卷上已存在的新Staging与V1 `getExternalMediaDirs()/Custom-QGroundControl/{Photo,Video}`，另加当前AppSettings配置的Photo/Video目录，只发布符合 `*_local_NNN`命名且非空的本功能文件。正常退出会等待照片worker并补扫遗漏源；若录像封装超过3秒，补扫仅排除该精确活动输出。随后以JNI executor barrier最多等待120秒完成此前排队的公共发布；强杀进程或直接卸载没有这一生命周期保证。V1迁移必须先用同包名与签名覆盖升级；若旧版已先卸载，原 `Android/media` 或 `Android/data` 文件已被系统删除时无法恢复。已完成公开发布的照片/录像在后续卸载和重装后仍保留；API 29+图库仅能看见 `IS_PENDING=0` 的公共持久文件，尚未公开时强制卸载会删除应用专属暂存，不在保留承诺内。MKV/MOV虽会按MIME发布，但厂商图库可能过滤不支持的容器，Android验收优先使用MP4。
- 根Manifest仍为 `allowBackup=true`，所以V2不能只假设SharedPreferences一定会随卸载永久消失。Java在 `getNoBackupFilesDir()` 保存 `qgc_custom_public_media_v2.install` 安装marker；新安装首次访问V2注册表时若marker不存在，先清空可能从云备份恢复的pending、录像管理和照片源清理URI集，再创建、flush和fsync marker。这保证重装后卸载前历史公共媒体不会因恢复的旧URI被自动容量清理。
- 右侧控制栏以一个按钮协调双支路，并分别显示 `SD` 与 `LOCAL` 徽标：绿色为实际录制，黄色/省略号为pending，红色为失败，灰色表示未录制或不可用。空闲主按钮只显示录像图标，不显示 `REC`；总计时只在至少一条支路已实际捕获时运行，不把思翼toggle的乐观状态当成已落盘。因此无卡但本地录像正常时，LOCAL仍独立显示录制状态。两条支路均未开始且已无pending时，主按钮显示 `FAILED`，但仍保留停止/清理当前会话的操作入口。
- Gimbal关闭时才在存在活动飞行器的前提下回退QGC原生 `PhotoVideoControl`。缩放手势统一为Idle/Pressed/Holding/Consumed状态：短按释放立即发送并显示同一合法表的下一档0x0f目标；按住420 ms后进入Holding，通常只启动一次0x05连续变倍，并以总按压时长 `qRound(totalMs / 600.0)` 计算相对手势起点的合法显示目标，到端点时Manager主动停止。正常release调用 `stopZoom()`完成最后一次时间计算；取消、移出、控件隐藏、应用后台及销毁调用 `cancelZoom()`且不推进目标，活动0x05路径均发送停止。QML释放判断使用本次释放事件坐标，不依赖Android上不稳定的 `containsMouse`悬停状态，长按释放不会再补发短按。
- 在Fly View设置页以 `SIYI Gimbal Camera` 标题提供云台相机启用、SDK IP、SDK端口和缩放步长设置，并明确tap每次发送一档0x0f且成功即显示目标、hold从420 ms成立起用一次0x05连续运动并按总按压时长每600 ms计算单调显示目标；合法目标只包含1.0x起始的min锚网格和当前卡录能力的有效精确上限。
- 顶部原生云台姿态栏与上述思翼私有UDP相机栏是两条独立控制链路。`custom/src/UI/toolbar/GimbalIndicator.qml` 以原生同名QML为基线，只为 `Yaw Lock/Follow`、`Center`、`Tilt 90` 和 `Retract` 增加MAVLink控制权自动接管：若同一活动云台尚未确认由QGC控制，则缓存最后一次点击，只发送一次 `MAV_CMD_DO_GIMBAL_MANAGER_CONFIGURE`，等待 `GIMBAL_MANAGER_STATUS` 同时确认 `gimbalHaveControl=true`、`gimbalOthersHaveControl=false` 后再执行按钮动作，不再弹出接管确认框。
- 真机对照进一步确认：RC之前最后一个MAVLink目标不是 `0°,0°` 时，RC移动后Center可用；最后目标恰为 `0°,0°` 时，RC虽然改变了物理姿态，再发Center仍无动作，而Tilt 90或Yaw模式命令先执行后Center立即恢复。这说明控制权切换和Center专属的旧目标去重是两个问题。上一版发送“当前实际姿态”的无位移预激活仍可能同时等于RC当前输出，继续被下游变化检测吞掉，因此已改为确定变化的预激活：先缓存当前pitch，把它钳制到本项目原生Center/Tilt 90已经验证合法的 `[-90°,0°]` 区间，再选取与该值相差1°、严格非0且仍在区间内的pitch；通过原生 `sendPitchBodyYaw(primerPitch, 0, false)` 使用与最终Center完全相同的body-yaw坐标系、yaw目标和flags，只让pitch不同。该接口还会停止500 ms速率发送Timer，避免另一条命令1000干扰ACK归属。严格等待预激活的 `COMMAND_ACK=ACCEPTED`，再延迟400 ms让飞控到厂商云台的输出桥锁存，复核同一对象和控制权后调用原生 `centerGimbal()`；最终Center自己的ACK也必须Accepted才清除该云台的预激活标记，Duplicate、Denied、无响应或断链都保留标记供下次重试。预激活命令目标相对钳制后的上报pitch严格相差1°；遥测新鲜时额外预动作通常也约1°，但遥测陈旧时不能承诺实际物理位移只有1°。该有意变化用于绕过下游旧目标去重，最终Center仍保持精确 `0°,0°` 语义。
- 自动接管不是循环争抢。控制权、预激活ACK和400 ms稳定窗口的事务等待时限为10秒，用于覆盖最慢约5秒一次的状态兜底和普通链路3秒命令ACK窗口；最终Center发出后另有4秒结果监视，只决定是否清除预激活标记，不延迟已经发出的居中动作。切换活动Vehicle、GimbalController或活动云台、对象销毁、手动Acquire/Release、预激活ACK失败以及超时都会取消待执行动作，迟到状态或ACK不能跨对象重放。接管等待期间快速点击多个姿态按钮采用last-click-wins，只保留最后一个动作且不重复发送Configure；若RC持续输入重新取得控制，QGC不会在后台反复抢权。速率控制、屏幕拖动和摇杆连续输入不进入延迟重放。
- `Point Home` 保持原生 `Vehicle.guidedModeROI(homePosition)` 直发并取消此前待执行姿态动作，因为它是飞控级ROI命令，不是Gimbal Manager的pitch/yaw控制权命令。显式Acquire/Release按钮也保持原生含义；本功能不调用思翼私有SDK。

### 1.3.1 A8 Mini + UniPod MT11 双云台（代码已集成，待双机真机验收）

- MT11 SDK V0.1.0已在 `custom/src/Gimbal` 转换为 `Mt11Protocol`、`Mt11Sdk` 和 `Mt11ControlManager` 三层：Protocol只做严格帧/payload编解码，Sdk持有独立UDP socket并校验来源IP、ACK和1.5秒命令窗口，Manager负责2秒轮询、缩放、拍照、录像、热成像以及MT11第二视频的本地媒体协调。它与现有A8 `SiyiProtocol/SiyiSdk/GimbalControlManager`并存，不把MT11行为塞入A8状态机。
- MT11帧使用 `55 66`头、control、payload length LE、production请求固定sequence 0、command、payload和末尾CRC16 LE；CRC多项式为 `0x1021`、初值为0。当前接入命令为0x05手动变倍/停止、0x0A相机系统状态、0x0B异步功能反馈、0x0C拍照/录像切换、0x0F绝对倍率、0x10查询视频模式、0x11设置视频模式、0x16最大倍率和0x18当前倍率。0x10/0x11以主可见光+副热成像 `[0,2]` 和主热成像+副可见光 `[2,0]`实现RGB/热成像往返。
- `DualVideoManager`为MT11单独创建 `VideoReceiver`、QML视频背景Item、原生视频sink和重启/停止状态，不复用原生/A8 receiver。设置变化、主动暂停、PIP弹窗切换、无有效URL和应用cleanup均走有界停止/释放；释放前发出 `videoObjectsAboutToBeReleased`，让MT11 Manager停止其owned本地录像并清空弱引用，随后先析构receiver、再释放sink，避免使用陈旧视频Item。
- custom同路径覆盖 `FlyView.qml`并引入 `DualPipView.qml`和 `MT11Video.qml`。两个新增类型仍位于与原生一致的 `custom/src/FlightDisplay`，但由独立静态模块 `Custom.FlightDisplay`注册，`FlyView.qml`以限定名显式导入；不修改原生 `QGroundControl.FlightDisplay`模块，也不依赖仅有QRC alias时不稳定的隐式类型发现。Map、A8、MT11各自保留原生 `PipState`语义，同一时刻一个居中全尺寸显示，其余可用项按 `item1=Map -> item2=A8 -> item3=MT11`顺序堆叠于左下；点击任一缩略框立即把它切到中心并把原中心放回PIP。选择保存到 `MainFlyWindowView`，首次升级仍读取旧 `MainFlyWindowIsMap`；任一路禁用/URL为空时从布局移除，若它此前居中则回退Map。
- A8和MT11视频都支持从当前中心画面双击进入自身全屏；任一路全屏时统一隐藏toolbar、双PIP、WidgetLayer和custom overlay，退出或失联时清除相应fullScreen状态。MT11画面复用原生视频fit、网格、Proximity Radar和障碍距离叠加，PIP弹窗前后暂停两秒再启动，避免OpenGL视频Item重挂时沿用旧sink。
- Fly View右侧栏在A8和MT11都启用时显示 `A8 Mini / MT11`切换按钮，只启用一路时自动归一到该后端。两栏共用 `GimbalCameraControl.qml`的缩放、拍照、录像、SD/LOCAL徽标和触控尺寸；`MT11CameraControl.qml`只负责注入MT11 Manager并在拍照上方开启热成像按钮。热成像按钮在0x10确认模式前不可用，点击后以0x11发送相反模式，匹配回包前保持pending，超时后重新查询而不以本地乐观值冒充切换成功。
- MT11设备IP已由 `192.168.144.25`改为 `192.168.144.24`，因此程序默认把SDK控制endpoint同步为 `192.168.144.24:37260`，第二视频URL为 `rtsp://192.168.144.24:8554/video1`。SDK和RTSP仍使用独立Fact、socket和故障状态，只是默认主机指向同一台MT11；SDK在线不能证明RTSP可解码，RTSP有画面也不能证明相机命令可用。A8默认继续使用 `.25`，MT11默认使用 `.24`，两台设备在同一二层网络中已完成地址区分。升级迁移仅把精确旧默认的MT11 SDK Host `.25`和RTSP `.25/video1`改为新默认 `.24`；键缺失时由新JSON默认建立，任何用户自定义值原样保留，独立版本标记避免重复迁移。
- 当前代码已完成 `Mt11ProtocolTest`独立编译运行，QtTest为9 passed、0 failed（7个业务槽加init/cleanup），覆盖SDK文档命令帧、热成像帧、严格CRC/长度/control解析、UDP多帧原子性、倍率payload、相机/功能反馈和视频模式payload。该结果只证明纯协议层；MT11真实固件ACK时序、双receiver长期播放、双机网络、热成像画面、本地媒体、Android性能和全工程Qt 6构建仍待目标设备验证。

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
- 新增 `FlyView/showHeadingCompassBar` 持久化 Fact，默认关闭、无需重启；开关位于 Application Settings -> Fly View -> Instrument Panel。缺少保存值时使用关闭默认值，用户已经保存的选择不被升级覆盖。
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

- 默认通信配置不再提供 `local/testlocal` 编译开关或强制管理多个配置表。仅当保存的通信链路总数 `LinkConfigurations/count` 为 `0` 时，创建一条 UDP 默认项 `local`：本地端口 `14550`、单一远端 `192.168.144.125:14550`、不自动连接且不标记为高延迟。
- 安装器在原生 LinkManager 读取 QSettings 之前运行，但 `count` 非零时立即返回，不读取、不删除、不改名、不去重也不补建任何配置。用户后续可将 `local` 改名为 `testlocal`，把本地端口改为 `14590`，或修改IP、远端端口、自动连接及高延迟属性，重启后都会原样保留；历史 `testlocal` 和重复名称也不再自动清理，由用户在通信链路界面自行管理。
- 固定本地端口用于消除断开重建 socket 时随机源端口变化这一风险；若 `.125` 图传路径透明转发到会保留地面站 UDP partner 的飞控实例或代理，保持地面站 IP 和本地端口稳定可避免其继续向旧随机端口回传。缓存具体位于图传、代理还是飞控仍需目标设备双侧抓包确认，不能仅凭程序侧改动承诺已经根治。
- QGC 原生动态 UDP AutoConnect 的默认监听端口也是本机 `14550`。`CustomPlugin` 只把 `AutoConnect/autoConnectUDP` 的缺省值设为 `false`；设置项保持可见，已有用户值不会被改写，用户可随时开启。若它与本地端口同为 `14550` 的 `local` 同时活动，两个 socket 可能共享端口并造成报文归属不确定，因此正常使用 `local` 时应保持该开关关闭，或先规划互不冲突并经过验证的端口。
- 任意已有配置都不会自动迁移，包括本地端口仍为 `0` 的历史 `local`。覆盖升级后如需固定端口，应在通信链路界面手动修改。只有删除全部通信链路使 `count=0` 并重启，安装器才会按新默认值重新创建 `local`；旧双配置逻辑的 `CustomCommunicationLinks/defaultsVersion` 标记已无读取者，但安装器也不再主动改写或删除它。
- 稳定UDP端点还要求遥控器/地面站 IP 与出接口在远端实例运行期间保持不变，并确认中间图传/NAT没有改写映射；否则需固定网络映射，或在远端实现链路超时后清除并重新学习 UDP partner。

### 1.10 PX4 飞控定制（已集成）

- 使用 custom PX4 Factory 替代原生 PX4 Factory，并关闭 APM Factory。Factory 的能力列表声明 PX4 + MultiRotor；当前 `firmwarePluginForAutopilot()` 只检查 `MAV_AUTOPILOT_PX4`、没有检查 `vehicleType`，所以运行时其他 PX4 机型也会进入 `CustomFirmwarePlugin`，不能把它描述成已经强制拒绝非多旋翼。
- 使用 `CustomFirmwarePlugin` 和 `CustomAutoPilotPlugin` 接入定制车辆能力、工具栏及车辆设置页。
- 普通模式只显示 Safety；高级模式显示 Airframe、Sensors、Radio、Flight Modes、Power、Motors、Safety 和 Tuning。
- 可由定制列表设置的飞行模式限制为 Loiter、RTL 和 Mission，并通过 `hasGimbal()` 静态声明 pitch/yaw 云台能力；该返回值不检测思翼设备、SDK 连接或云台实际响应状态。
- Fuel 指示器由 `CustomFirmwarePlugin::toolIndicators()` 插入 Battery 后，Proximity Radar插入GPS后；二者都是本项目custom工具栏功能，不是 `custom-example` 示例资源。

### 1.11 Proximity Radar 距离传感器告警（已集成）

- `CustomFirmwarePlugin` 在GPS指示器之后插入Proximity Radar工具栏入口；只要活动飞行器十个方向距离Fact中至少一个有效，入口即显示。
- 覆盖前、前右、右、后右、后、后左、左、前左、上、下十个方向；任一有效距离小于5.0 m时雷达图标变红并循环闪烁，恢复后立即回到普通颜色和不透明度。
- 点击图标打开详情页，只列出当前有效方向并显示Fact原生数值与单位；告警方向文字同步变红。该功能只读取 `Vehicle.distanceSensors`，不发送避障命令、不改变飞控参数，也不替代飞控自身的避障逻辑。

### 1.12 custom 架构、设置和翻译（已集成）

- 二次开发主体位于 `custom`，目录和命名参照 `src` 模块树；当前共 135 个文件。
- 仅保留 `src/CMakeLists.txt` 和 `src/Vehicle/VehicleSetup/VehicleSummary.qml` 两处 feature 必需的受控修改，其余功能通过 custom C++、QRC、独立custom QML模块、QML URL 拦截和 Android overlay 接入。
- General、Fly View 和 Video 设置页以及顶部 `GimbalIndicator.qml` 均按原生文件树使用同路径 custom 覆盖；Viewer3D、Gimbal、视频链路和航向罗盘条参数使用稳定 Fact/QSettings 分组持久化。General 页面继续绑定原生 `appFontPointSize`，Android 缺省值由 custom metadata hook 调整。
- Android 构建先在构建目录合并原生模板和 `custom/android` overlay，再只编译合并后的唯一 Java 源，避免原生/custom 同包同类冲突。
- 与 `src/Viewer3D` 完全相同的 C++、QML、qmldir 和 shader 由构建或 QRC 直接复用，不在 custom 保存重复副本；外部 WGS84 城镇样例只是源码树手动测试资产，不参与构建或 QRC 打包。
- 只从 `custom-example` 引入底部航向罗盘条；不引入其未使用的示例控件、自定义动作、圆形罗盘、姿态仪、品牌资源和全局配色，也不保存无必要的 `AppSettings.qml` 根页副本。
- custom 翻译加载、简体中文目录和 `lupdate` 更新脚本已经接入；本轮新增双云台相机选择、MT11第二视频/SDK/热成像和相关错误文本。当前英文模板与简体中文目录均为16个context、140条message：英文 `custom.ts` 的140条均按模板约定保持unfinished，`custom_zh_CN.ts` 的140条均已完成且unfinished/空译文均为0；Qt 5 `lrelease` 已验证通过，中文目录生成140条finished译文。最终提交前仍建议使用项目Qt 6 `lupdate`刷新源码location，并在刷新后复核context/source和译文状态；该步骤是位置维护要求，不改变当前目录已经完整对齐和翻译完成的事实。

## 2. 开发边界

1. 除 `src/CMakeLists.txt` 和 `src/Vehicle/VehicleSetup/VehicleSummary.qml` 两处 feature 必需改动外，不修改其他 `src` 文件。
2. custom 新增代码按 QGC 模块放置，例如 `FlightDisplay`、`FlightMap/Images`、`Settings`、`Gimbal`、`Comms`、`QmlControls`、`UI/AppSettings`、`VideoManager/VideoReceiver/GStreamer`；Android Java 同名覆盖按根目录 `android` 的文件树放在 `custom/android`。
3. Application Settings 的 General、Fly View、Video 页面和顶部工具栏 `GimbalIndicator.qml` 由项目在 custom 显式接管并保存同名覆盖；其他没有差异、也不需要项目接管的 QML 继续使用 `src`。
4. 与 `src/Viewer3D` 相同的公共实现由 `custom/CMakeLists.txt` 或 `custom.qrc` 直接引用，不在 custom 保存副本。
5. custom同名 QML 覆盖使用 `/Custom/qml` 前缀；新增的双视频复合类型由 `Custom.FlightDisplay`模块生成到 `/qml/Custom/FlightDisplay`；Viewer3D 独立模块仍使用 `/qml/Viewer3D`。
6. 设置 Fact 名和 QSettings 分组保持稳定，升级程序不会丢失已有 Viewer3D、Gimbal、Fly View 航向罗盘条和链路设置。
7. 复杂协议、坐标转换和跨模块行为使用中文注释；普通布局和赋值不增加无意义注释。
8. Android 构建先在构建目录合并原生 `android` 模板和 `custom/android` overlay，Gradle 只编译合并结果；不把两个 Java 源目录同时加入 source set，避免同包同类冲突。

## 3. custom 完整目录结构

当前共 135 个文件：

```text
custom/
  CMakeLists.txt
  custom.qrc
  cmake/
    CustomOverrides.cmake
  android/
    src/org/mavlink/qgroundcontrol/
      QGCCustomMediaLibrary.java
      QGCUsbSerialManager.java
  src/
    CustomPlugin.h
    CustomPlugin.cc
    Android/
      AndroidMediaLibrary.h
      AndroidMediaLibrary.cc
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
      DualPipView.qml
      FlyView.qml
      FlyViewCompassBar.qml
      FlyViewCustomLayer.qml
      FlyViewToolStripActionList.qml
      FlyViewTopRightColumnLayout.qml
      GeneratorBusVoltageAlert.qml
      GimbalCameraControl.qml
      GimbalZoomControl.qml
      MT11CameraControl.qml
      MT11Video.qml
    FlightMap/
      Images/compassPointer.svg
    Gimbal/
      A8MiniZoomPolicy.h
      A8MiniZoomPolicy.cc
      GimbalControl.SettingsGroup.json
      GimbalControlManager.h
      GimbalControlManager.cc
      GimbalMediaSessionPolicy.h
      GimbalMediaSessionPolicy.cc
      GimbalPhotoCapturePolicy.h
      GimbalPhotoCapturePolicy.cc
      GimbalControlSettings.h
      GimbalControlSettings.cc
      GimbalVideoStreamSupport.h
      GimbalVideoStreamSupport.cc
      SiyiProtocol.h
      SiyiProtocol.cc
      SiyiSdk.h
      SiyiSdk.cc
      Mt11Protocol.h
      Mt11Protocol.cc
      Mt11Sdk.h
      Mt11Sdk.cc
      Mt11ControlManager.h
      Mt11ControlManager.cc
    QmlControls/
      FuelStatusIndicatorPage.qml
      ProximityRadarIndicatorPage.qml
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
        GimbalIndicator.qml
        FuelStatusIndicator.qml
        ProximityRadarIndicator.qml
        Images/FuelIcon.svg
    VideoManager/
      DualVideoManager.h
      DualVideoManager.cc
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
      GimbalMediaSessionPolicyTest.cc
      GimbalPhotoCapturePolicyTest.cc
      Mt11ProtocolTest.cc
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
| `custom/CMakeLists.txt` | custom 构建总入口。向根工程注入 `QGC_CUSTOM_BUILD`、`CUSTOMHEADER=CustomPlugin.h` 和 `CUSTOMCLASS=CustomPlugin`，收集 AutoPilot/Firmware、Viewer3D、Gimbal、Comms、Settings、VideoManager、Android媒体库桥、GStreamer拉流尺寸探针和Android H.265等 custom C++，以及15个明确复用的原生 Viewer3D C++ 文件，并向根目标导出 include、library、resource 和 translation 列表；创建包含 Fuel 与 Proximity Radar 详情页的 `Custom.Widgets` 静态 QML 模块，以及包含 `DualPipView`和 `MT11Video`的 `Custom.FlightDisplay`静态 QML 模块。后者沿用原生 `FlightDisplayModule`的 `STATIC + RESOURCE_PREFIX /qml + NO_PLUGIN`模式，通过短 `QT_RESOURCE_ALIAS`生成模块内类型，并显式加入 `CUSTOM_LIBRARIES`确保Android主目标链接；不手写qmldir，也不修改原生FlightDisplay模块。桌面 `QGC_BUILD_TESTING` 构建创建独立 `SiyiProtocolTest`、`Mt11ProtocolTest`、`GimbalMediaSessionPolicyTest` 和 `GimbalPhotoCapturePolicyTest`，移动端不生成额外测试应用；MT11目标只链接Qt Core/Test和纯Protocol文件。它要求Quick3D/Quick3DAssetUtils，检测可选WebEngineQuick并定义Google 3D能力；翻译只导出 `custom_*.ts`，英文 `custom.ts`模板不编译。Android configure时把根 `android`模板复制到build目录，再用 `custom/android`同路径覆盖，同时校验USB与媒体库custom标记并让Gradle只使用唯一合并源目录。外部WGS84样例目录不参与构建或安装。 |
| `custom/custom.qrc` | custom RCC运行时资源清单，共67个 `<file>`；本轮继续注册同路径 `FlyView.qml`覆盖、`MT11CameraControl.qml`及共享 `GimbalCameraControl.qml/GimbalZoomControl.qml`。`DualPipView.qml`和 `MT11Video.qml`不再在本QRC重复打包，而由 `Custom.FlightDisplay`模块注册为可导入类型；`GimbalIndicator.qml` 与 `ProximityRadarIndicator.qml` 仍以 `QGroundControl/Toolbar/...` alias覆盖原生工具栏资源。URL拦截器只在 `/Custom/qml`候选实际存在时重定向；本文件只决定覆盖资源URL，不编译C++、不保存设置值。Fuel与Proximity Radar详情页由 `Custom.Widgets`注册，翻译 `.qm`由CMake生成，外部WGS84样例不在本QRC中。 |
| `custom/cmake/CustomOverrides.cmake` | 根工程配置阶段读取的产品能力开关。固定 `QGC_APP_NAME=Custom-QGroundControl` 以保持应用标识和既有 QSettings 路径；关闭原生 Viewer3D后端，防止它与 custom Viewer3D 类和设置产生重复符号；关闭APM dialect/plugin/factory，并关闭原生PX4 Factory，让 custom Factory成为PX4固件插件的唯一创建入口。它只决定编译内容和插件选择，不在这里检查具体 `MAV_TYPE`。 |

### 4.2 CustomPlugin 与通信链路

| 文件 | 详细作用 |
|---|---|
| `custom/src/CustomPlugin.h` | custom 功能的中央组合入口声明。继承 `QGCCorePlugin`，向QML暴露稳定的Viewer3D设置/管理器、FlyViewCustom设置、共享Gimbal设置、A8 Manager、`mt11ControlManager`和`dualVideoManager`；声明init/cleanup、Android字号metadata、MAVLink视频消息过滤、QML engine和视频sink覆盖。文件末尾的 `CustomOverrideInterceptor` 负责把原生QRC URL重定向到实际存在的 `/Custom/qml` 文件；本头文件只定义接口与所有权。 |
| `custom/src/CustomPlugin.cc` | 上述中央入口的实现。`init()`安装默认链路、翻译和各设置/Manager；按共享Gimbal设置创建独立A8、MT11控制器与DualVideoManager。`createVideoSink()`根据receiver父对象区分MT11第二receiver与原生主非thermal receiver：A8继续取得主渲染Item/receiver及尺寸探针，MT11只交给Mt11ControlManager，两个本地录像状态机不会操作对方receiver。DualVideoManager释放前以DirectConnection通知MT11 Manager停止owned本地录像并清空Item/receiver，再销毁接收器和sink；应用退出同时收尾两套本地媒体并清理第二视频。其他职责包括Android字号metadata、MAVLink视频消息过滤和QML URL拦截。 |
| `custom/src/Comms/DefaultCommunicationLinkInstaller.h` | 声明无状态的 `DefaultCommunicationLinkInstaller::ensureInstalled()` 静态接口。调用者只有 `CustomPlugin::init()`；头文件不创建或连接链路，目的是把“写入项目缺省通信配置”与 CustomPlugin生命周期代码分离。 |
| `custom/src/Comms/DefaultCommunicationLinkInstaller.cc` | `ensureInstalled()` 的启动前默认值实现。它只读取 `LinkConfigurations/count`：有效值为0时清理非活动的残留 `Link0` 槽位，写入默认 `local`（本地`14550`、远端 `192.168.144.125:14550`、`auto=false`、非高延迟）并把count设为1；count非零或值无效时不读取、不修改任何配置。它不再按名称处理 `local/testlocal`，不压缩索引，也不清理旧版本标记；后续编辑、连接、UDP会话、MAVLink和持久化仍由原生 LinkManager负责，日志类别为 `gcs.custom.communicationlink`。 |

#### 4.2.1 Android USB 串口管理器

| 文件 | 详细作用 |
|---|---|
| `custom/android/src/org/mavlink/qgroundcontrol/QGCUsbSerialManager.java` | Android USB串口生命周期的同名overlay实现，保持 `org.mavlink.qgroundcontrol` 包名、JNI类名和全部public static签名；CMake只把它覆盖到构建目录，不修改根 `android`。`QGCActivity`调用initialize/cleanup，Qt AndroidSerial/QSerialPortInfo经JNI调用枚举、open/close、读写和串口参数，Java listener再把数据/异常回调Qt。状态分为发现态 `drivers`、打开态 `deviceResourcesMap`、权限请求时间和本次attach拒绝集合，并由同一锁串行化；普通close只停I/O、关闭port/fd、失效listener并保留driver，所以不拔线可重开，任何重新扫描确认设备消失、detach或cleanup都会释放并移除陈旧状态。扫描先用默认prober，未匹配时仅对同时具备CDC COMM/ACM和CDC_DATA接口的设备创建保守 `CdcAcmSerialDriver`；只向Qt报告已匹配、有权限且至少有一个port的设备，当前每设备只打开 `ports.get(0)`。权限请求15秒内去重，明确拒绝后当前attach会话不再弹，detach/cleanup清除；打开失败的每一步都走幂等回滚，不强制中间9600波特率，真实参数由Qt随后下发。custom故意不在receiver线程直接用raw Qt指针通知断开，而让Qt工作线程通过端口列表消失完成close。日志标签 `QGCUsbSerial-Custom` 能证明overlay和定位枚举/权限/open状态；Java成功打开后仍必须经过USBBoardInfo/AutoConnect、SerialLink和MAVLink heartbeat才会出现Vehicle，飞控绿灯只表示VBUS供电。 |

#### 4.2.2 Android 本地媒体库

| 文件 | 详细作用 |
|---|---|
| `custom/src/Android/AndroidMediaLibrary.h` | 声明custom Android媒体库的稳定C++接口：`mediaStagingDirectory()`解析应用专属暂存目录，`existingMediaSourceDirectories()`返回所有已挂载卷上已经存在的V2 Staging与V1 `Android/media`源且不创建，`publishMediaFile()`接收已完成的暂存/旧文件并排队发布到公共Pictures或Movies，`cleanupPublishedVideos()`按当前安装的URI注册表清理公共录像，`waitForPendingPublications()`为正常退出有界等待已排队任务，`removeMediaFile()`仅删除暂存/旧源及其陈旧索引；非Android构建保持无副作用空实现。它不参与截图和录像状态机。 |
| `custom/src/Android/AndroidMediaLibrary.cc` | 以Qt 6 `QJniObject/QJniEnvironment`调用 `org.mavlink.qgroundcontrol.QGCCustomMediaLibrary` V2；用共用目录resolver连接 `getMediaStagingDirectory/getExistingMediaSourceDirectories`，并精确对应 `publishFile` 四个String参数、`cleanupPublishedVideos(jlong, String)`、`waitForPendingPublications(jlong)` 与 `deleteFile`。该层把Java换行分隔的全部现存源目录还原为去重 `QStringList`，检查Java类、参数、JNI异常和“任务已排队”同步结果，日志类别为 `gcs.custom.android.medialibrary`；排队成功不等于公共复制已完成，只有barrier成功或Java公开成功日志能证明队列已完成，录像ownership和容器完成边界仍由Manager判断。 |
| `custom/android/src/org/mavlink/qgroundcontrol/QGCCustomMediaLibrary.java` | additive custom Java V2类，不覆盖整份 `QGCActivity`。编码/封装暂存使用与AppSettings同 `StorageVolume` 的 `getExternalFilesDirs(null)/Custom-QGroundControl/Staging/{Photo,Video}`；API 29+按源卷选择具体MediaStore volume，把照片发布到 `MediaStore.Images + Pictures/Custom-QGroundControl/`、录像发布到 `MediaStore.Video + Movies/Custom-QGroundControl/`。发布使用 `IS_PENDING`、持久pending URI journal、`ParcelFileDescriptor` 复制、fsync与字节校验，公开且提交注册表后才删除源；已有目标只有同时属于本安装日志、路径/名称匹配且内容逐字节一致时才作为幂等成功，避免认领重装前或其他应用的同名媒体。失败会删除尚未公开的不完整行并保源。API 25–28改用公共Pictures/Movies隐藏partial、fsync、长度校验、rename和最多30秒非空MediaScanner URI确认。V1 `getExternalMediaDirs()`只用于覆盖升级迁移；`QGCCustomPublicMediaV2` SharedPreferences分别记录pending URI、当前安装的公共录像URI以及源待清理URI：正常删源后移除临时 `sourceCleanupUris`，录像URI继续留作当前安装的容量管理。公共录像名校验允许 `_local_NNN`后的provider同名后缀，但容量清理不触碰未公开Staging。`waitForPendingPublications()`向同一单线程executor追加Future barrier并有界等待，所以只覆盖调用前已经排队的任务；队列排空后如果 `FAILED_SOURCE_PATHS` 仍非空，barrier仍返回false并由C++记录退出告警。卸载时这些私有注册表消失但已公开的媒体保留。 |

V2注册表还与 `getNoBackupFilesDir()/qgc_custom_public_media_v2.install` 安装marker配对。根Manifest的 `allowBackup=true` 可能让SharedPreferences在重装时由云备份恢复，但no-backup marker不会恢复；因此marker缺失时Java会先清空恢复的V2 URI集并持久新marker，防止新安装错把旧媒体纳入自动删除。

### 4.3 PX4 FirmwarePlugin 与 AutoPilotPlugin

| 文件 | 详细作用 |
|---|---|
| `custom/src/FirmwarePlugin/CustomFirmwarePluginFactory.h` | 声明 `FirmwarePluginFactory` 子类及其全局注册实例，只向 QGC报告 `FirmwareClassPX4 + VehicleClassMultiRotor` 支持范围，并保存一个 `CustomFirmwarePlugin` 单例指针。该 Factory 是 HEARTBEAT识别到飞控后选择项目固件行为的入口，不处理具体飞行模式或UI。 |
| `custom/src/FirmwarePlugin/CustomFirmwarePluginFactory.cc` | 实现并在静态初始化阶段创建 `CustomFirmwarePluginFactoryImp`，使QGC Factory注册机制能发现它；`firmwarePluginForAutopilot()` 只对 `MAV_AUTOPILOT_PX4` 延迟创建并返回同一个 `CustomFirmwarePlugin`，其他autopilot返回空。该函数当前 `Q_UNUSED(vehicleType)`，因此能力列表虽只声明MultiRotor，运行选择阶段并不会拒绝其他PX4 `MAV_TYPE`；若产品必须强制仅多旋翼，需要在本函数增加vehicleType判断。 |
| `custom/src/FirmwarePlugin/CustomFirmwarePlugin.h` | 声明 `PX4FirmwarePlugin` 的项目行为覆盖接口：为每辆 Vehicle创建哪个 AutoPilotPlugin、顶部车辆指示器列表、云台轴能力和动态飞行模式属性。成员 `_toolIndicatorList` 缓存定制后的QML URL列表，避免每次查询重复构造。 |
| `custom/src/FirmwarePlugin/CustomFirmwarePlugin.cc` | 实现 PX4车辆级定制。为车辆创建 `CustomAutoPilotPlugin`；从原生工具栏列表移除 RC RSSI，把 custom Fuel 指示器稳定插入 Battery 后，并把 Proximity Radar 插入GPS之后（找不到GPS时追加）；`hasGimbal()`静态声明仅 pitch/yaw可用，但不检测思翼设备、UDP SDK连接或云台响应；构造及 `updateAvailableFlightModes()`重新标注机型适用性，并只让 Loiter、RTL、Mission 保持 `canBeSet=true`。它不发送模式切换命令，而是限制QGC向用户公开的可选模式。 |
| `custom/src/AutoPilotPlugin/CustomAutoPilotPlugin.h` | 声明 `PX4AutoPilotPlugin` 子类，覆盖 `vehicleComponents()` 返回车辆 Setup 页面模型，并提供高级模式变化槽；`_components` 缓存当前页面对象。该层控制“车辆设置页面有哪些”，不控制飞行界面工具条或实际PX4参数值。 |
| `custom/src/AutoPilotPlugin/CustomAutoPilotPlugin.cc` | 在参数准备完成后按需创建 Setup组件并调用各组件 `setupTriggerSignals()`：普通模式只创建 Safety；高级模式依次创建 Airframe、Sensors、Radio、Flight Modes、Power、Motors、Safety、Tuning。监听 `showAdvancedUIChanged` 后清空缓存并发出 `vehicleComponentsChanged()`，使UI立即重建；参数未就绪或版本错误时不生成页面。 |

### 4.4 FlightDisplay QML 与图像资源

| 文件 | 详细作用 |
|---|---|
| `custom/src/FlightDisplay/FlyView.qml` | 原生同路径Fly View的custom覆盖入口，在保留任务控制器、地图、原生A8视频、WidgetLayer、引导控制和Viewer3D容器的基础上，增加MT11视频与三路PIP编排。文件显式 `import Custom.FlightDisplay as CustomFlightDisplay`，并以限定名实例化 `MT11Video`和 `DualPipView`，避免被拦截的FlyView仍按原生模块类型表解析时出现 `is not a type`。Map、A8 Mini、MT11按 `item1/item2/item3` 接入 `DualPipView`；任一可用缩略图被点击后成为居中全尺寸项，原中心项回到左下缩略区。第二路禁用、URL为空或主视频不可用时对应项从候选中移除，若被移除项正在居中则回退Map；选择保存为 `MainFlyWindowView`，首次升级兼容旧 `MainFlyWindowIsMap`。A8或MT11进入视频全屏时统一隐藏工具栏、PIP、WidgetLayer和custom overlay，不让飞行叠加层盖住全屏画面。 |
| `custom/src/FlightDisplay/DualPipView.qml` | 三视图PIP状态管理器，沿用原生 `PipState` 的full/pip/window状态和PIP展开、弹窗、缩放交互，同时为三个视图提供各自adapter。可用项按Map、A8、MT11的稳定顺序排列；Map居中且两路视频均可用时，A8位于左下底层缩略框、MT11位于其上方，满足第二视频放在原视频上面的布局。点击任一缩略框只切换中心项并持久化选择，不创建或解码视频。 |
| `custom/src/FlightDisplay/MT11Video.qml` | MT11独立RTSP显示面，数据和状态只取自 `DualVideoManager`。外观与原生 `FlyViewVideo` 对齐：支持Video设置中的fit/grid、无视频占位、PIP/full/window三态、活动Vehicle在线时双击视频全屏、Proximity Radar和Obstacle Distance叠加；进入或退出独立PIP窗口时先停第二路视频，2秒后重启，避免渲染Item跨窗口切换。全屏时隐藏网格但保留视频专用遥测叠加，普通Fly View工具层由外层 `FlyView.qml` 统一隐藏。 |
| `custom/src/FlightDisplay/MT11CameraControl.qml` | MT11右栏薄封装，复用 `GimbalCameraControl.qml` 的缩放、拍照、录像、本地媒体和状态布局，注入 `mt11ControlManager` 并打开热成像控件。它不复制A8面板逻辑；热成像按钮实际命令、pending和RGB/IR反馈仍由Manager及共享面板处理。 |
| `custom/src/FlightDisplay/FlyViewCompassBar.qml` | 罗盘条本体和绘制算法。直接读取活动飞行器 `Vehicle.heading.rawValue`，验证并归一化到 `[0°, 360°)`，使用中心附近 11 个 45°相对标签计算 N/NE/E/SE/S/SW/W/NW 的横向位置，中央显示四舍五入的整数航向并用 `compassPointer.svg` 绘制固定指针。它不读取显示开关、不保存设置、不判断是否应被加载；外层 `FlyViewCustomLayer.qml` 负责生命周期和显示条件。组件没有鼠标拦截层，因此不会吞掉下方地图手势。 |
| `custom/src/FlightDisplay/FlyViewCustomLayer.qml` | Fly View custom overlay 的编排层，同时管理罗盘条与燃料电池母线告警。它从 `corePlugin.flyViewCustomSettings.showHeadingCompassBar` 读取用户意愿，再结合 overlay 可见、活动飞行器存在和 heading 有效四个条件，通过明确 QRC URL加载 `FlyViewCompassBar.qml`；罗盘条采用组件的 `implicitWidth`（与示例相同为 `50 × defaultFontPixelWidth`）并保持屏幕水平居中，只按 Fly View 总宽度和基础 margin 做最终屏幕边界钳制，不再使用 PIP/摇杆/仪表的角落 inset 压缩宽度。它把“罗盘条高度+底边 margin”的完整占用深度合并进 `bottomEdgeCenterInset`，关闭时透传原生 inset。同一文件还监听 `vehicle.generator.busVoltage`，低于20.0 V置告警、超过20.4 V清除，形成回差；`mapControl` 当前只是兼容接口，未参与逻辑。 |
| `custom/src/FlightDisplay/FlyViewToolStripActionList.qml` | Fly View 左侧工具条动作模型的同路径覆盖。保留检查单、起飞、降落、返航、暂停、附加动作和夹爪的原生顺序，在最前面新增仅当 `viewer3DSettings.enabled=true` 才可见的 2D/3D 切换动作；动作调用现有 `viewer3DWindow.open()/close()`，打开 3D 时用 PaperPlane 表示返回 Fly，关闭时用 custom 城市图标表示进入 3D。 |
| `custom/src/FlightDisplay/FlyViewTopRightColumnLayout.qml` | Fly View右侧中部控件容器的同路径覆盖，始终保留 `TerrainProgress`。A8或MT11任一启用时，无需活动Vehicle即可显示私有相机栏；两者同时启用时在栏顶增加 `A8 Mini/MT11`选择控件，选择哪个就向共享面板注入哪个Manager，某一路关闭后自动归一到仍可用的一路。`sdkResponding`只控制在线状态和相关按钮，不决定整栏可见性；只有两套私有相机都关闭且存在活动Vehicle时才回退原生 `PhotoVideoControl`。容器宽高跟随选择器与当前面板隐式尺寸，避免移动端缩放把控件压缩。 |
| `custom/src/FlightDisplay/GeneratorBusVoltageAlert.qml` | Fly View燃料电池母线低压提示本体。读取传入Vehicle的 `generator.busVoltage`，低于20.0 V显示告警、严格高于20.4 V清除，NaN或无Fact时隐藏；双阈值回差避免临界电压反复闪烁。它只绘制告警，加载位置和活动飞行器生命周期由 `FlyViewCustomLayer.qml` 管理。 |
| `custom/src/FlightDisplay/GimbalCameraControl.qml` | A8与MT11共用的相机面板，Manager可由外层注入，不依赖飞控、活动Vehicle或SDK在线状态决定可见性。单个半透明圆角面板纵向排列通用 `GimbalZoomControl`、可选热成像、拍照和录像；A8关闭热成像行，MT11通过wrapper打开并在拍照按钮上方显示RGB/IR切换，pending期间禁用重复点击，模式文案以SDK确认结果为准。空闲拍照/录像均使用同尺寸圆形图标，录像计时、pending或失败文字按内容展开。命令调用当前Manager的 `takePhoto()/toggleVideoRecording()`，拍照反馈观察机内和本地计数；录像读取组合会话available/active/capturing并分别显示 `SD`、`LOCAL` 状态，本地支路不被SD无卡覆盖。该栏不实例化原生 `PhotoVideoControl`。 |
| `custom/src/FlightDisplay/GimbalZoomControl.qml` | 合并栏顶部的思翼缩放子控件，使用单列GridLayout从上到下显示加号、当前目标倍率和减号，并把完整列高传给外层布局。从Manager读取 `zoomControlsUnlocked/currentZoom/zoomStatusKnown/zoomInAvailable/zoomOutAvailable`；只要在线且倍率已知就显示 `currentZoom`，不再因 `zoomValueUncertain`或命令pending隐藏数字。按钮不依赖 `sdkResponding`，方向availability只反映当前目标在唯一合法表中是否还有下一档。两侧MouseArea使用Idle/Pressed/Holding/Consumed状态：短按释放调用一次 `zoomIn/zoomOut`；长按420 ms成立后，把从最初按下起计算的总时长传给 `startZoomWithPressDuration(direction, totalMs)`，Manager通常只启动一次0x05并按 `qRound(totalMs / 600.0)` 推进显示目标；普通release调用 `stopZoom()`，取消、移出、隐藏、后台和销毁调用 `cancelZoom()`，都不会补发短按，活动0x05也都会停止。 |
| `custom/src/FlightMap/Images/compassPointer.svg` | 罗盘条中央固定三角指针的纯矢量资源，不含角度或交互逻辑。按原生 `src/FlightMap/Images` 资源分类保存，由 `custom.qrc` 注册为 `qrc:/custom/img/compassPointer.svg`，`FlyViewCompassBar.qml` 通过 `QGCColoredImage` 加载并按当前主题文本颜色着色。 |

本轮已在 custom 保存同路径 `FlyView.qml` 以接入三视图；无项目差异的 `FlyViewWidgetLayer.qml` 和 `FlyViewToolStrip.qml` 仍直接复用 `src`，工具条动作差异继续由上表 `FlyViewToolStripActionList.qml` 覆盖。

### 4.5 Gimbal 后端

| 文件 | 详细作用 |
|---|---|
| `custom/src/Gimbal/A8MiniZoomPolicy.h` | A8 Mini缩放策略的纯静态接口，将受支持拉流会话尺寸、卡录分辨率能力映射、唯一min锚合法目标表、相邻方向步进及按总按压时长计算单调hold目标从Manager状态机中分离，便于独立测试；不访问网络、QSettings或UI。 |
| `custom/src/Gimbal/A8MiniZoomPolicy.cc` | 拉流尺寸只判断1280×720或1920×1080会话是否受支持，不再产生倍率；卡录分辨率单独映射3840×2160或4096×2160→1.0x、2560×1440→3.5x、1920×1080→5.5x、1280×720→6.0x。倍率计算使用十分之一整数；合法目标从1.0x按配置步长递增，并把有效精确上限作为唯一末端目标，正反方向都遍历同一有序表。默认1.0x时2K为1/2/3/3.5，卡录1080P为1/2/3/4/5/5.5，卡录720P为1/2/3/4/5/6；4K只有1.0。hold按 `qRound(totalMs / 600.0)` 从起始目标计算档数并在端点钳制。 |
| `custom/src/Gimbal/GimbalControl.SettingsGroup.json` | `GimbalControl` 设置组的元数据源，不执行云台或视频操作。定义12个Fact：原有A8/本地媒体/视频集成7项，以及 `mt11Enabled=true`、`mt11SdkHost=192.168.144.24`、`mt11SdkPort=37260`、`mt11ZoomStep=1.0x`、`mt11RtspUrl=rtsp://192.168.144.24:8554/video1`。SDK host/port只用于相机命令，RTSP URL只用于第二视频；二者分开保存为 `[GimbalControl]/同名键`。 |
| `custom/src/Gimbal/GimbalControlSettings.h` | 声明 `GimbalControlSettings : SettingsGroup`，用12个 `DEFINE_SETTINGFACT` 生成惰性创建的 `Fact*` Q_PROPERTY。它是JSON/QSettings与QML、A8 Manager、MT11 Manager和DualVideoManager之间的设置入口，保证MT11 SDK与RTSP属性名称稳定；不创建socket或receiver。 |
| `custom/src/Gimbal/GimbalControlSettings.cc` | 通过 `DECLARE_SETTINGGROUP(GimbalControl, "GimbalControl")`确定元数据资源和QSettings分组，实现12个Fact getter并以reference-only类型注册。构造阶段执行两项受限MT11默认迁移：URL只在已有值精确等于旧默认 `rtsp://192.168.144.25:8554/video1` 时改为 `.24/video1`；SDK Host只在已有值精确等于旧默认 `192.168.144.25` 时改为 `.24`。键缺失时由JSON新默认建立，空字符串和任何其他用户自定义值均不改写，两项独立版本标记避免重复迁移。 |
| `custom/src/Gimbal/GimbalControlManager.h` | 思翼云台相机业务的QML门面和运行态声明。除缩放/SD拍照录像状态外，暴露 `localMediaStorageEnabled`、本地录像active/pending、组合会话active/capturing/available、本地照片计数和本地媒体错误。保存主视频渲染项的弱引用、已签发录像基名及对应完整输出路径，并声明本地截图、owned/external录像协调、启动/停止超时和退出清理接口；本地与SD状态不共用一个布尔值。截图保留覆盖grab与后台保存全过程的单次在途标记、未释放窗口holder计数、5秒grab Timer、专用单线程池以及指向当前holder的QPointer，避免Android连续点击或超时重试同时分配多份高分辨率FBO/QImage，也避免GUI取消路径提前销毁渲染线程对象。完整路径用于容器最终结束后发布Android媒体，不能只靠基名反推目录。 |
| `custom/src/Gimbal/GimbalControlManager.cc` | 由 `CustomPlugin`创建一次并持有 `SiyiSdk`、主视频渲染项和主（非thermal）`VideoReceiver`弱引用，没有修改 `src/VideoManager`。本地拍照在点击时快照新鲜合法0x20卡录尺寸、实际拉流尺寸、视频Item尺寸和窗口DPR，优先按卡录物理像素尺寸计算逻辑离屏target；0x0a无卡不改变尺寸选择，只有卡录参数不可用时才逐级回退拉流/Item信息。`QQuickItemGrabResult`的最后强引用由窗口子对象持有；5秒超时、等待 `ready` 阶段的视频Item替换以及Manager析构只退休业务generation，不跨线程直接析构抓图对象；已经进入worker后再替换Item，仍会保存已经抓到的帧。`ready`后把QImage移交最大并发1的专用线程池，照片策略在worker中修正分数DPR误差或不等宽高比留边，再用 `QSaveFile + QImageWriter` 以JPEG质量100原子提交；queued completion回到Manager线程后才累计计数并将Android暂存JPG交给公共媒体发布器。录像读取 `VideoSettings::recordingFormat`，桌面使用 `AppSettings::videoSavePath()`，Android使用与AppSettings同卷的 `getExternalFilesDirs(null)/Custom-QGroundControl/Staging/Video`，然后仅调用主receiver start/stop；thermal既不启动也不停止。只有confirmed-owned录像在实际 `recording=false` 后才用保留的完整暂存路径调用 `publishMediaFile()`，external、thermal、provisional和失败启动不发布。Android启用容量限制时另会调用 `cleanupPublishedVideos(maximumBytes)`，只统计当前安装 `SharedPreferences` URI注册表中可访问、命名匹配的公开录像，删除失败即停；Android路径会立即return，绝不以配额删除任何未公开Staging源，发布失败源保留重试且可能使实际占用高于上限。桌面仍按原生目录统计并只删除本功能旧分段。主receiver既有信号仍让VideoManager更新 `recording`和字幕。只有启动完成成功且输出completeBaseName命中已签发唯一基名时才确认ownership；失败/无效格式、不匹配、pending超时、断流分段和退出收尾均按独立状态处理。启动后会去重枚举所有已挂载卷上的V2 Staging与V1 `Android/media`，再加入当前AppSettings配置的旧Photo/Video目录，把本功能文件交给Java单线程发布器；Android正常 `aboutToQuit` 会等待照片worker并再次补扫，若录像3秒内仍未封装完成，只排除该精确活动输出路径，其他照片和历史失败源仍排队发布；最后用JNI barrier最多等待120秒完成已排队发布。 |
| `custom/src/Gimbal/GimbalMediaSessionPolicy.h` | 声明无QObject依赖的本地媒体纯状态转换策略。输入意图、设置、码流、实际录像、ownership、external、pending和blocked状态，输出StartOwned、StopOwned、AdoptExternal、ConfirmOwned或ReleaseExternal动作；另统一计算组合会话是否实际捕获及按钮可用性。 |
| `custom/src/Gimbal/GimbalMediaSessionPolicy.cc` | 实现上述确定性决策：本地启动条件不含SD或SDK状态；外部录像只能采用/释放，不能停止；owned/pending由Manager负责收尾；无流或本次启动已阻断时不重复启动。组合capturing排除尚未被0x0a确认的相机乐观状态，本地流可用时允许无SDK独立开始。 |
| `custom/src/Gimbal/GimbalPhotoCapturePolicy.h` | 声明本地照片纯尺寸/图像策略及 `CaptureGeometry`：卡录输出物理尺寸、完整画面内容尺寸和经DPR换算的Qt Quick逻辑抓取尺寸。该层不读取SDK、设置或文件路径。 |
| `custom/src/Gimbal/GimbalPhotoCapturePolicy.cc` | 根据卡录输出尺寸、实际解码源宽高和 `effectiveDevicePixelRatio()` 计算离屏抓取尺寸；结果回调时把分数DPR造成的少量像素误差平滑修正到精确输出。源流与卡录宽高比不同时使用保持比例完整显示和居中黑边，不裁剪、不拉伸；普通16:9精确命中时直接复用QImage，避免无意义的4K深拷贝。 |
| `custom/src/Gimbal/GimbalVideoStreamSupport.h` | 声明两个无对象状态的启动期适配接口：安装A8 Mini视频缺省设置，以及判断一条MAVLink消息是否应被过滤。它不创建 VideoReceiver、不连接RTSP，也不参与H.265解码。 |
| `custom/src/Gimbal/GimbalVideoStreamSupport.cc` | `CustomPlugin::init()` 每次启动调用的幂等迁移和消息策略实现。版本键 `[GimbalControl]/a8MiniVideoDefaultsVersion=4` 控制迁移：只在URL为空或旧拼写时设置A8 RTSP地址，只在A8 URL且timeout过小时提升到20秒，只在视频源为空/Disabled/No Video时选RTSP；Android仅在用户从未保存 `[Video]/lowLatencyMode` 且URL匹配时写true，已有选择不覆盖。过滤逻辑仅针对 `VIDEO_STREAM_INFORMATION`：Gimbal开启且 `mavlinkAutoVideoStream=false` 时阻止它进入原生自动视频配置，其余消息放行。 |
| `custom/src/Gimbal/SiyiProtocol.h` | 思翼私有协议的纯静态编解码接口，覆盖0x05/0x0a/0x0b/0x0c/0x0f/0x16/0x18/0x20及ACK帧判定；不继承QObject，不访问网络、QSettings或UI。 |
| `custom/src/Gimbal/SiyiProtocol.cc` | 实现 `55 66`帧头、control 0x01、小端payload长度、固定0的seq、command、payload和小端CRC16（多项式0x1021、初值0）。单帧解析保持严格长度，UDP报文拆帧路径可依次处理多个完整合法帧。0x05 ACK按小端uint16/10解析；0x16/0x18优先按官方“整数字节+一位小数字节”解析并兼容真机小端uint16/10。0x20请求携带 `stream_type`，录像流使用0；9字节ACK按type、codec、宽LE、高LE、码率LE和fps解析，宽高用于卡录能力映射。 |
| `custom/src/Gimbal/SiyiSdk.h` | `QUdpSocket`传输层接口声明，提供0x05原生连续变倍、0x0f绝对倍率、0x16当前支持范围、0x18当前倍率、0x20卡录编码参数、拍照、录像和状态查询；它不负责合法目标表、手势计时或设置持久化。当前tap由Manager调用0x0f，普通hold使用0x05开始/停止，起步即为端点的hold只使用单次0x0f。 |
| `custom/src/Gimbal/SiyiSdk.cc` | 将Protocol帧通过 `QUdpSocket::writeDatagram()`发到配置endpoint；接收侧按逻辑IP、逐帧长度、CRC和control 0x02过滤，并且只在对应业务payload解析成功后发出 `packetReceived`。一个UDP报文中的0x16、0x18、0x20或状态帧分别分发，不因相机合包而整体丢弃。`gcs.custom.gimbal.sdk`记录每个命令、原始payload、倍率编码和卡录参数；空包、短写或无效endpoint通过 `communicationError`交给Manager。 |
| `custom/src/Gimbal/Mt11Protocol.h` | 声明UniPod MT11 SDK V0.1.0纯协议接口、命令枚举、视频源枚举和payload结构，覆盖0x05手动变倍、0x0A相机状态、0x0B异步功能反馈、0x0C拍照/录像、0x0F绝对倍率、0x10查询视频模式、0x11设置视频模式、0x16最大倍率、0x18当前倍率。还声明单帧/UDP合包解析、ACK判定以及状态、倍率、功能反馈、RGB/热成像模式解析；不访问网络或UI。 |
| `custom/src/Gimbal/Mt11Protocol.cc` | 按SDK构造并严格解析 `55 66 + control + payload length LE + sequence LE + command + payload + CRC16 LE`。生产请求sequence固定0；CRC覆盖CRC字段之前的完整帧，多项式 `0x1021`、初值0、逐位高位优先。解析拒绝错误头、非法control、长度尾随/截断和坏CRC；UDP合包采用原子语义，任一子帧非法则整批不输出。0x10/0x11把RGB主流/热成像副流编码为 `[0,2]`，热成像主流/RGB副流编码为 `[2,0]`。 |
| `custom/src/Gimbal/Mt11Sdk.h` | 声明MT11独立 `QUdpSocket`传输对象，默认endpoint为 `192.168.144.24:37260`，提供协议九类命令的发送接口和状态信号。该endpoint只代表私有SDK控制链路，不决定第二路RTSP URL，也不创建视频receiver。 |
| `custom/src/Gimbal/Mt11Sdk.cc` | 发送Protocol帧并同时按配置逻辑IP和配置端口过滤回包；IPv4与其IPv4-mapped IPv6表示可视为同一逻辑地址，但回包源端口必须精确等于 `mt11SdkPort`。0x0B允许作为文档规定的相机异步通知，其余普通响应必须为ACK control且匹配同command最近1.5秒请求窗口。由于生产sequence固定0，按command维护有界代次是线协议可提供的最强关联；无匹配或过期ACK忽略，payload非法时恢复剩余请求窗口等待可能到达的合法帧。Manager设置重置时显式清除旧请求窗口，即使仅Enabled变化且endpoint文本不变，迟到ACK也不能恢复已清状态。一个UDP datagram只有全部帧合法才分发。 |
| `custom/src/Gimbal/Mt11ControlManager.h` | 声明MT11的QML业务门面和运行状态，暴露启用/在线、1.0–30.0x缩放、相机录像、拍照、本地媒体、RGB/热成像模式及错误反馈；保存独立MT11视频Item和receiver弱引用，并声明receiver替换、录像启动结果、退出收尾接口。接口形状与共享相机面板兼容，但状态不与A8 Manager共用。 |
| `custom/src/Gimbal/Mt11ControlManager.cc` | 持有 `Mt11Sdk`，每2秒轮询0x16/0x18/0x0A/0x10并以6秒最后响应watchdog判断在线；按MT11步长和设备上限处理tap/hold缩放，0x05连续变焦启动后另启60秒single-shot安全watchdog，若用户侧停止事件未到达则超时主动发送0停止并报告安全超时。拍照、录像与热成像均使用SDK命令和2–2.5秒pending保护。热成像切换发送0x11，直到0x10/0x11模式payload确认后才更新RGB/IR。MT11本地拍照只有在第二receiver已进入decoding且视频Item尺寸有效时才抓取当前帧；拍照/录像同时可按共享本地媒体开关使用MT11自己的视频Item/receiver，绝不操作A8主receiver。录像命令是无ACK的toggle：命令pending期间修改MT11 Enabled/SDK Host/Port会把Fact原子恢复为已应用值并提示稍后重试，避免旧设备被重复toggle；非pending设置变化会在切换endpoint前停止旧端点已确认的相机录像并结束对应本地会话。receiver被替换、第二视频释放或应用退出时停止并收尾自身owned录像。 |
| `custom/test/Gimbal/SiyiProtocolTest.cc` | custom独立QtTest协议与策略回归用例。覆盖0x0f封包量化、ACK control、坏CRC、严格单帧长度、多帧UDP拆分、0x16/0x18双格式倍率payload、0x20录像流请求与9字节ACK、拉流会话白名单、4K/2K/1080P/720P卡录能力映射、唯一min锚目标表、精确上限追加、正反同表序列，以及420 ms长按成立后按 `qRound(total/600)` 计算档数和端点钳制。Manager级测试还需覆盖能力交叉校验、tap成功即显示、快速替换、hold只启动一次0x05、普通release与cancel分流及不产生释放后反向0x0f。 |
| `custom/test/Gimbal/Mt11ProtocolTest.cc` | MT11纯协议QtTest，共7个业务slot，覆盖SDK文档0x05/0x0A/0x0C/0x0F/0x16/0x18精确命令帧，0x10/0x11热成像帧，严格control/长度/CRC/sequence解析，UDP多帧原子性，以及倍率、相机状态、0x0B功能反馈和视频模式payload。当前运行结果为9 passed、0 failed（含init/cleanup）；它不覆盖QUdpSocket真实网络、Manager计时、VideoReceiver或真机固件。 |
| `custom/test/Gimbal/GimbalMediaSessionPolicyTest.cc` | 独立QtTest状态策略回归。覆盖无SD/SDK依赖的本地启动、external录像只释放不停止、confirmed owned关闭、未确认provisional取消等待、迟到确认后的补偿停止、expected stop不重启、pending幂等、码流与blocked门控、采用/确认已有实际录像、排除相机乐观状态的capturing，以及无SDK时只凭本地码流即可启用录像按钮。它不替代真实文件系统、GStreamer和Android设备测试。 |
| `custom/test/Gimbal/GimbalPhotoCapturePolicyTest.cc` | 独立QtTest照片尺寸策略回归。覆盖720P/1080P/2K/4K与DPR 1/1.5/2/2.625换算、4096×2160输出对16:9实时帧的四色角完整性和左右各128像素黑边、分数DPR实际grab偏差修正为精确输出、普通16:9路径不触发QImage深拷贝，以及无效尺寸/DPR拒绝。它不替代Qt Quick真实离屏渲染、5秒Timer/generation/窗口生命周期、线程池、JPEG/QSaveFile失败、Android GPU内存和MediaStore真机测试。 |

云台相机完整调用链为：`VideoReceiver`解码当前视频 -> 真实首帧CAPS或最终 `GstVideoInfo` 隐式尺寸 -> 无直接结果时使用稳定1秒的 `VideoManager::videoSize` -> 受支持拉流只建立视频会话门控 -> 0x20查询 `stream_type=0`卡录分辨率并映射基础上限 -> 合法0x16只以较小值安全收紧 -> `A8MiniZoomPolicy`生成1.0x起始的唯一min锚网格并追加有效精确上限 -> tap立即发送同表相邻一档0x0f并在成功后显示目标 -> hold在420 ms成立后通常只发送一次0x05方向命令，按总按压时长每600 ms更新同方向合法显示目标并在端点立即停止 -> 普通release调用 `stopZoom()`，取消、移出、隐藏、后台和销毁调用 `cancelZoom()`；活动0x05路径都会发送0停止，且hold结束不发送0x0f反向归整。若hold起步目标已是端点，则只发一次同方向端点0x0f而不进入0x05。0x18实际反馈独立保存，不覆盖当前目标倍率。

### 4.6 GStreamer 拉流分辨率与 Android 视频解码策略

| 文件 | 详细作用 |
|---|---|
| `custom/src/VideoManager/DualVideoManager.h` | 声明MT11第二路视频的独立生命周期对象，向QML暴露enabled/hasVideo/streaming/decoding/fullScreen、尺寸、receiver和videoItem；提供init/start/stop/cleanup，并在释放对象前发出 `videoObjectsAboutToBeReleased`。它不复用或修改原生 `VideoManager` 的主receiver。 |
| `custom/src/VideoManager/DualVideoManager.cc` | 监听 `mt11Enabled/mt11RtspUrl`，在 `MT11Video.qml` 的 `mt11VideoContent` Item就绪后由core plugin创建独立 `VideoReceiver`和sink，应用全局低延迟与RTSP timeout并维护启动、停止、1秒重试、设置变化和主动pause。无URL、关闭MT11或cleanup时先停再释放；释放前通知MT11 Manager停止自身owned本地录像，随后按receiver先销毁、sink后释放的顺序清理，避免跨receiver录像和悬空渲染Item。SDK控制是否在线不参与RTSP启动判定。 |
| `custom/src/VideoManager/VideoReceiver/GStreamer/PulledVideoResolutionProbe.h` | 声明无QObject状态的主拉流协商尺寸探针安装接口及 `ResolutionHandler` 回调。由 `CustomPlugin::createVideoSink()` 调用；非GStreamer构建、空sink、非 `VideoReceiver` parent或thermal receiver返回false且不改变原生视频路径。回调由GStreamer流线程触发，调用方必须排队切回Manager线程。 |
| `custom/src/VideoManager/VideoReceiver/GStreamer/PulledVideoResolutionProbe.cc` | 只在 `QGC_GST_STREAMING` 下对主视频 `qgcvideosinkbin` 的 `sink` ghost pad安装downstream CAPS、BUFFER和BUFFER_LIST探针。只有真实帧到达才发布尺寸；宽高直接读取CAPS structure，不把成功条件绑死在完整format的 `gst_video_info_from_caps()`。若外层ghost pad没有current CAPS，则继续读取已连接解码器peer和ghost target的current CAPS，覆盖不同平台的caps存放差异。得到正宽高后既通过回调直达Manager，也经既有 `VideoReceiver::videoSizeChanged` 修正 `VideoManager`，从而覆盖原生 `GstVideoReceiver::_addVideoSink()` 在管线刚拼接时用 `gst_pad_query_caps()` 得到的暂态/无效值；该原生信号同时可触发Manager受控的1秒稳定兜底。不修改 `src`、不猜测卡录分辨率，也不影响thermal流。首个真实帧仍无法取得宽高时会明确告警。 |
| `custom/src/VideoManager/VideoReceiver/GStreamer/AndroidH265HardwareDecoderAdapter.h` | 声明进程级 custom H.265 decoder factory接口：固定factory名、注册函数、已选内部厂商MediaCodec factory查询，以及policy与adapter共用的厂商名称过滤函数。它只是适配器注册API，不实现H.265算法，也不调用Android `MediaCodecInfo.isHardwareAccelerated()`做系统级硬件认证。 |
| `custom/src/VideoManager/VideoReceiver/GStreamer/AndroidH265HardwareDecoderAdapter.cc` | 在启动阶段枚举能接受 `video/x-h265,stream-format=byte-stream,alignment=au` 的 `amcviddec-*`，排除secure、software/FFmpeg、`*.sw.dec`、Qualcomm `*swvdec`、OMX Google及C2 Android/Google/Goldfish，优先名称含low-latency变体的组件，再按原rank/名称排序；预检只证明元素可创建、静态链可链接且bin能进READY，不证明真实profile/level已解码。选中后缓存厂商factory并以rank `PRIMARY+100=356` 注册 `qgcandroidh265hwdec`。每个实例内部为：外部hvc1 ghost sink -> `h265parse(config-interval=-1)` -> Annex-B byte-stream/AU capsfilter -> 厂商MediaCodec -> downstream-leaky raw queue（最多2 buffer）-> `video/x-raw(ANY)` ghost src；probe记录真实输入caps和首个raw buffer的caps/PTS/bytes/GLMemory。首帧日志只证明经过名称筛选的factory已产出raw frame，不等同Android API硬件认证，也不保证画面已到QML sink。 |
| `custom/src/VideoManager/VideoReceiver/GStreamer/AndroidVideoDecoderPolicy.h` | 声明一次性的Android H.265 factory rank/适配策略入口。正确调用窗口是 `VideoManager`构造已完成 `GStreamer::initialize()` 之后、`VideoManager::init()` 创建VideoReceiver和 `decodebin3`之前；过早无法枚举插件，过晚则已建管线不会重新选择decoder。 |
| `custom/src/VideoManager/VideoReceiver/GStreamer/AndroidVideoDecoderPolicy.cc` | `CustomPlugin::init()` 从 `forceAndroidH265HardwareDecoder` 读取一次并调用的策略实现，因此开关要求重启。仅Android+`QGC_GST_STREAMING`且GStreamer已初始化时生效：先尝试注册上述hvc1适配器，再枚举H.265 decoder并按静态sink caps判断hvc1/byte-stream兼容；适配器至少rank356，经过厂商名称筛选且直接接受hvc1的MediaCodec至少提升到 `PRIMARY+2=258`，高于原生Force Software将 `avdec_h265`设成的257。其他软件factory rank不删除也不置0，只保留回退资格，不能承诺所有真机运行期协商失败都一定无黑屏回退。逐候选日志记录分类、caps兼容和rank变化；H.264及非Android不受影响。 |

Android H.265选择链为：`CustomPlugin::init()`读取重启后生效的Fact -> `AndroidVideoDecoderPolicy` 在管线创建前调整factory候选/rank -> `decodebin3`依据caps与rank选择decoder。直接兼容hvc1的厂商MediaCodec可直接入选；只兼容Annex-B的厂商decoder由 `qgcandroidh265hwdec` 包装，实际数据路径为 `hvc1 -> h265parse -> video/x-h265,stream-format=byte-stream,alignment=au -> 厂商MediaCodec -> 最多2帧的leaky raw队列 -> video/x-raw -> 原生QGC显示链`。rank只能影响选择优先级；确认运行链路必须看实际factory和首个raw buffer日志，不能只看READY预检。

### 4.7 Application Settings、通用默认值、Fly View custom Settings、顶部云台栏、Fuel 和 qmldir

General -> UI Scaling 使用 custom 同路径覆盖页，但仍绑定原生整数 Fact；Android 12 pt 缺省值由 4.2 节的 `CustomPlugin` metadata hook 在 Fact 创建时提供。页面不负责写入缺省值，也不新增 SettingsGroup 或 JSON，避免只有打开 General 页面后设置才生效。

| 文件 | 详细作用 |
|---|---|
| `custom/src/QmlControls/FuelStatusIndicatorPage.qml` | Fuel 顶部指示器点击后创建的详情页。输入为活动飞行器 `fuelStatus` Fact，按燃料类型选择 ml 或 MPa，显示剩余比例、剩余量、最大量、已消耗量、流量和温度；它只负责详情展示，不决定工具栏图标是否出现。该类型由精简的 `Custom.Widgets` QML 模块注册，创建入口在 `FuelStatusIndicator.qml`。 |
| `custom/src/QmlControls/ProximityRadarIndicatorPage.qml` | Proximity Radar工具栏入口点击后的详情页。通过required `radarData`接收十方向Fact及5.0 m判断函数，只Repeater显示有效方向、原生值与单位，告警行文字变红；它不计算飞行器避障动作，也不保存阈值设置。 |
| `custom/src/QmlControls/Viewer3D/Models3D/qmldir` | 声明 `Viewer3D.Models3D` QML 模块，并把 `CameraLightModel`、`Line3D`、`External3DMap`、`Viewer3DModel`、`Viewer3DVehicleItems`、`Waypoint3DModel` 六个类型映射到对应 QML。`CameraLightModel`、`Line3D`、`Waypoint3DModel` 继续由 QRC 引用原生源码，另外三个带项目差异的场景类型映射到 custom 文件。它只解决 `import Viewer3D.Models3D` 后的类型发现，不创建场景、不加载模型，也不保存设置；`QGroundControl.Viewer3D` 是 C++ 类型模块，不能与本模块名混用。 |
| `custom/src/Settings/FlyViewCustom.SettingsGroup.json` | 只定义航向罗盘条显示开关 `showHeadingCompassBar` 的 Fact 元数据：类型为 bool、缺省为 `false`、无需重启。它不绘制罗盘条，也不保存当前用户值；`FlyViewCustomSettings.cc` 根据资源名加载它，实际选择保存为 `FlyView/showHeadingCompassBar`。 |
| `custom/src/Settings/FlyViewCustomSettings.h` | 声明 `FlyViewCustomSettings : SettingsGroup`，并通过 `DEFINE_SETTINGFACT(showHeadingCompassBar)` 生成稳定的 `Fact*` Q_PROPERTY、延迟创建指针和访问器。该类是 C++/QML 之间的设置接口层，只表达“用户是否允许显示罗盘条”，不包含航向计算或绘制代码。 |
| `custom/src/Settings/FlyViewCustomSettings.cc` | 实现上述 SettingsGroup：`DECLARE_SETTINGGROUP(FlyViewCustom, "FlyView")` 使用独立元数据 `:/json/FlyViewCustom.SettingsGroup.json`，但把用户值写入原生 `FlyView` QSettings 分组；注册 reference-only QML 类型并实现 `showHeadingCompassBar()` 的延迟 Fact 创建。实例由 `CustomPlugin` 创建并暴露为 `QGroundControl.corePlugin.flyViewCustomSettings`。 |
| `custom/src/UI/AppSettings/GeneralSettings.qml` | Application Settings -> General 的同路径 custom 覆盖页。完整保留原生 Language、Color Scheme、GCS位置流、音频、Android SD Card、清除设置、数据路径、Units和Brand Image。UI Scaling直接绑定原生整数 `appFontPointSize`，按 `appFontPointSize / ScreenTools.platformFontPointSize × 100` 四舍五入显示，`-`/`+` 每次修改1 pt并由原生 `SettingsFact` 保存；页面本身不写缺省值。`SettingsFact` 构造期间先调用 `CustomPlugin::adjustSettingMetaData()` 把 Android raw default改为12 pt，再读取已有QSettings或该缺省值，因此未打开本页面也会生效；非Android默认仍为100%。 |
| `custom/src/UI/AppSettings/FlyViewSettings.qml` | Application Settings -> Fly View 的同路径覆盖页。保留全部原生 Fly View 设置，从 `corePlugin.flyViewCustomSettings.showHeadingCompassBar` 取得 Fact，在 Instrument Panel 中用 `FactCheckBoxSlider` 提供“显示航向罗盘条”开关；切换会由 `SettingsFact` 自动持久化并被 `FlyViewCustomLayer.qml` 立即观察。页面底部还装载 Viewer3D 与思翼云台设置组，本文件不绘制罗盘条。三个处于原生 `SettingsPage/ColumnLayout` 中的Loader都把已加载项的 `implicitHeight` 显式提供给 `Layout.preferredHeight/minimumHeight`，避免零高槽位造成内容溢出、顶部裁切、分组拉伸及滚动范围不足；不再手工反复写 `item.width`。 |
| `custom/src/UI/AppSettings/VideoSettings.qml` | Application Settings -> Video 的同路径覆盖页。保留原生Video Source、Connection、播放设置和Local Video Storage；新增独立且常驻的 `MT11 Second Video`组，绑定 `mt11RtspUrl`，即使MAVLink自动流使原生Source/Connection禁用仍可编辑。Local Video Storage继续提供共享 `localMediaStorageEnabled`，只控制A8/MT11各自本地附加支路，不关闭相机SD动作；页面还保留MAVLink自动视频与Android H.265设置。实际双路receiver、相机命令、录像、截图和解码策略由对应Manager执行。 |
| `custom/src/UI/AppSettings/Viewer3DSettingsGroup.qml` | 由 `FlyViewSettings.qml` 显式加载的 Viewer3D 设置面板。根Loader将内容的 `implicitWidth/implicitHeight` 向外透传，供设置页正确计算分组高度和滚动范围；不再用 `onLoaded/onWidthChanged` 手工写子项宽度。只有设置对象及14个所需Fact可用时才创建内容；Google与外部模型两个开关相互排斥，两者都关闭时隐式进入本地OSM模式。页面编辑API Key、外部模型文件、WGS84原点、单位换算、比例、yaw、OSM路径、建筑层高和车辆高度偏移；外部文件选择交给 `External3DMapManager.importModelFile()` 检查/转换并返回状态，本文件不创建或渲染三维场景。页面的 Clear只修改保存的路径值，不删除磁盘文件。 |
| `custom/src/UI/AppSettings/GimbalControlSettingsGroup.qml` | Fly View设置页中的双相机私有UDP面板。A8组绑定 `enabled/sdkHost/sdkPort/zoomStep`；UniPod MT11组独立绑定 `mt11Enabled/mt11SdkHost/mt11SdkPort/mt11ZoomStep`，并提示第二视频地址应在Video -> MT11 Second Video配置。根Loader透传隐式尺寸；本文件只编辑Fact，不测试设备在线，也不把SDK地址同步成RTSP URL。 |
| `custom/src/UI/toolbar/GimbalIndicator.qml` | 原生 `src/UI/toolbar/GimbalIndicator.qml` 的同路径custom覆盖，保留遥测、设置、多云台选择和显式Acquire/Release界面。`Yaw Lock/Follow`、`Center`、`Tilt 90`、`Retract` 在控制权不足时建立一个带Vehicle/Controller/Gimbal身份和代次token的待执行动作，只发送一次Configure；同一上下文后续点击只替换动作内容。任一控制权属性表明QGC失权时还会按 `Vehicle id + manager compid + device id` 记住该具体云台的Center需要重新激活，因此切换活动Vehicle/云台再返回，或点击前状态已经恢复成QGC持权，都不会误清其他云台的标记或绕过修复。待两个控制权属性经下一事件循环共同确认后，普通姿态动作在受保护窗口中重放一次；需要重新激活的Center先缓存当前pitch，在已验证合法的 `[-90°,0°]` 内计算一个与钳制值相差1°、严格非0的目标，再调用与最终Center相同坐标系和flags的 `sendPitchBodyYaw(primerPitch, 0, false)` 并停止原生速率Timer。它监听同一Vehicle的 `mavCommandResult`，同时核对代次、Vehicle id、manager component、命令1000和原生命令结果类型，只在预激活ACK Accepted后等待400 ms，再复核上下文/控制权并执行最新动作；最终Center的Accepted ACK才清除对应云台的重新激活标记，失败或4秒无结果则保留。10秒Timer、ACK失败、上下文变化、对象销毁、显式Acquire/Release和Point Home都会使旧待执行动作失效；pending或姿态执行窗口内由原生 `_tryGetGimbalControl()` 产生的确认信号只被静默抑制，不触发重试；自动流程不活跃时，其他调用来源仍保留原生确认框。Point Home继续直发Vehicle ROI；本文件不实现RC输入、不修改MAVLink协议、也不调用思翼UDP SDK。 |
| `custom/src/UI/toolbar/FuelStatusIndicator.qml` | `CustomFirmwarePlugin::toolIndicators()` 插入 Battery 后的顶部工具栏组件。监听活动飞行器 `fuelStatus.telemetryAvailable`，无 `FUEL_STATUS` 数据时隐藏且不占可见空间，有数据时显示 `FuelIcon.svg` 与剩余百分比；点击后通过主窗口弹出 `FuelStatusIndicatorPage.qml`。它不生成 Fuel 遥测，也不负责母线低电压告警。 |
| `custom/src/UI/toolbar/ProximityRadarIndicator.qml` | `CustomFirmwarePlugin::toolIndicators()` 插入GPS之后的工具栏组件。读取活动飞行器 `distanceSensors` 的前/前右/右/后右/后/后左/左/前左/上/下十个方向，任一Fact有效即显示；任一有效距离小于固定5.0 m阈值时图标变红并以400 ms淡入淡出闪烁。点击打开详情页；无遥测时隐藏。它只做告警呈现，不发送避障指令。 |
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
| `custom/translations/custom.ts` | `lupdate` 生成的英文源字符串模板，供各语言目录对齐context/source，CMake不把它编译为运行时 `.qm`。本轮新增双云台选择、MT11设置、第二视频、SDK错误、RGB/热成像、本地媒体和连续变焦安全超时文本；当前为16个context、140条message，140条均按英文模板约定保持unfinished。Qt 5 `lrelease` 已通过并按预期忽略这些未翻译源文本；最终提交前仍建议用项目Qt 6 `lupdate`刷新location并复核context/source。 |
| `custom/translations/custom_zh_CN.ts` | 简体中文locale目录；CMake将其编译成 `custom_zh_CN.qm`放入 `:/i18n`，`CustomPlugin`匹配中文locale时安装translator。当前与英文模板保持相同的16个context、140条message，140条均已翻译，unfinished和空译文均为0；Qt 5 `lrelease` 已成功生成140条finished译文。最终提交前使用项目Qt 6 `lupdate`刷新location后，仍需确认本轮译文未被新增或重置。 |
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

当前分支 `SecDev/ft/gimbal` 沿用的二次开发 `src` 差异只有以下两处；它们是 custom PX4 模块正常链接所需的受控例外：

| 文件 | 修改原因 |
|---|---|
| `src/CMakeLists.txt` | 原生 PX4 Factory 被关闭时仍链接 `AutoPilotPluginsPX4Module`，保证 VehicleSummary 和 CustomAutoPilotPlugin 使用的 PX4 QML 页面存在。 |
| `src/Vehicle/VehicleSetup/VehicleSummary.qml` | 注释 APM QML import；当前构建关闭 APM 模块，继续导入会造成运行时 `module QGroundControl.AutoPilotPlugins.APM is not installed`。 |

除这两处外，当前二次开发功能没有其他 `src` 差异。顶部云台栏自动接管、底部航向罗盘条、Android H.265 与 USB 飞控连接修复都完全位于 `custom`；根目录 `android/src` 保持原样，Android APK 通过构建目录 overlay 使用 custom Java 实现，未新增任何 `src` 修改。

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
| `showHeadingCompassBar` | bool / `false` | 显示飞行界面底部中央的航向罗盘条；保存于 `FlyView` QSettings 分组，切换后立即生效，无需重启。 |

使用流程：

1. 打开 Application Settings -> Fly View -> Instrument Panel。
2. 该开关首次默认关闭；测试时使用 `Show Heading Compass Bar` 手动开启，简体中文界面对应“显示航向罗盘条”。
3. 返回 Fly View 并连接飞控。只有活动飞行器的 `heading` 有效时才显示罗盘条，未连接飞控不会以 0° 伪造航向。
4. 中央数值和固定三角指针表示当前机头航向，N/NE/E/SE/S/SW/W/NW 方位随航向连续移动；该数据不包含航点方向和航线偏差。

实现从 `custom-example/FlyViewCustomLayer.qml` 中选择性提取横向航向条。示例通过 720 个 `QGCLabel` 切换可见性模拟滚动，本实现使用以当前 45° 区间为基准的 11 个相对方位 Label，保持 359°/0° 连续过渡并降低 Android QML 更新开销；标签精简只改变绘制数量，不负责组件宽度。罗盘条通过 `FlyViewCustomLayer` 的 Loader 显式加载并贴近 Fly View 底边，首选宽度直接使用组件与示例一致的 `50 × defaultFontPixelWidth`。此前外层取 `max(leftEdgeBottomInset, rightEdgeBottomInset)` 后从左右各扣一次，PIP 或仪表任一侧变宽都会受到双倍扣减，极端时宽度变为 0；当前只用 Fly View 总宽度减去两侧基础 margin 作为屏幕边界，角落 inset 的变化不会再挤压罗盘条。这样在 86% 缩放和 PIP 从最小到最大拖动时仍保持示例宽度；极端放大的角落控件可能与罗盘条发生视觉层叠，这是优先保证航向可读性的明确取舍。显示时只扩展 `bottomEdgeCenterInset`，关闭时恢复原生 inset。组件不放置 `DeadMouseArea`，因此不会吞掉其覆盖区域的地图拖动、滚轮缩放、PIP 调整或 Android 触摸手势。

custom同路径 `FlyView.qml` 延续原生全屏语义：A8或MT11视频全屏时都隐藏工具栏、三视图PIP、WidgetLayer和custom overlay，所以罗盘条与母线告警均不显示；退出全屏后恢复。普通Map/A8/MT11居中切换和Viewer3D不受影响。

### 8.3 Gimbal 与视频参数

#### 8.3.1 思翼私有SDK相机控制与视频

| Fact | 范围/默认值 | 说明 |
|---|---|---|
| `enabled` | bool / `true` | 启用私有SDK云台相机后端和合并的缩放/拍照/录像控制栏；关闭后才回退原生相机控件。 |
| `localMediaStorageEnabled` | bool / `true` | Application Settings -> Video -> Local Video Storage中的“在本机保存照片和录像”；未保存过该项时默认开启，已有用户显式选择继续保留；切换即时生效、无需重启，只控制本地附加支路，不控制云台SD卡。 |
| `sdkHost` | `192.168.144.25` | A8 Mini SDK IP。 |
| `sdkPort` | 1-65535 / `37260` | A8 Mini 私有 UDP SDK 端口。 |
| `zoomStep` | 0.1-4.5 / `1.0x` | tap的绝对步进和hold目标分档。合法目标从1.0x按步长递增并追加卡录能力的有效精确上限，正反方向共用同一表；默认1.0x时2K卡录为1.0/2.0/3.0/3.5，1080P卡录为1.0/2.0/3.0/4.0/5.0/5.5，720P卡录为1.0/2.0/3.0/4.0/5.0/6.0，4K只有1.0。 |
| `mavlinkAutoVideoStream` | bool / `false` | 是否接受 MAVLink 相机流 URI 并允许其锁定视频源。修改后重启 QGC。 |
| `forceAndroidH265HardwareDecoder` | bool / `true` | 仅 Android 生效。注册 `hvc1 -> byte-stream/au` 适配器并优先真实厂商 MediaCodec H.265 硬解；无可用硬解时保留软件回退。修改后重启 QGC。 |

Gimbal启用后，Manager即使不在Fly View也会持续运行，并每2秒向 `sdkHost:sdkPort`探测：GStreamer构建优先采用主显示sink直接报告的最终协商尺寸，仅在没有任何有效直接结果时用 `VideoManager::videoSizeChanged/decodingChanged` 启动1秒稳定兜底；非GStreamer构建直接使用这两个原生状态。拉流结果只决定视频会话门控。每轮查询0x20录像流编码参数、0x16设备上限和0x0a状态；0x20卡录分辨率给出基础能力，合法0x16只以较小值安全收紧。新视频会话和能力变化后以0x18建立目标参考；此后任意合法0x0f在本地发送成功后立即更新并显示 `currentZoom`，实际0x18反馈独立核对且不覆盖当前目标。Fly View右侧纵向合并栏不等待探测结果，无论是否连接飞控或云台都立即显示；缩放按钮需要受支持视频会话及卡录能力均已确认，且不把 `sdkResponding`作为单独门控。

本地媒体的运行语义如下：

1. 开关关闭时保持原有思翼SD行为；开关开启后，控制栏每次拍照或开始/停止录像都会同时发起SD与本地动作。两条支路并行、状态独立、失败互不回滚。SDK离线、相机状态未知、无云台SD卡或0x0a返回状态2/3时，只影响 `SD` 徽标；只要主视频条件满足，本地照片/录像仍可工作。
2. 本地照片是当前主视频渲染项的解码帧截图，不是SD卡原片。Manager要求正在解码、主 `QQuickItem` 有有效尺寸且 `Photo` 暂存目录可写；点击时快照0x20卡录尺寸、实际解码源尺寸、Item尺寸和窗口 `effectiveDevicePixelRatio()`。输出物理像素优先等于新鲜合法的0x20卡录尺寸，DPR只用于反算 `grabToImage(targetSize)` 的逻辑target，不能把卡录像素尺寸直接当逻辑尺寸再次放大；0x0a无卡不清除此配置，也不作为本地门槛，只有0x20从未确认、超时失效或尺寸不支持时才依次回退协商拉流、VideoManager、Item隐式源尺寸和Item物理显示尺寸。源与卡录宽高比不同时完整居中并补黑边，分数DPR误差在保存前修正为精确输出；卡录4K而拉流1080P只能得到1080P细节的4K上采样文件。等待 `ready`阶段有5秒超时，业务取消只退休generation，窗口托管的holder继续保护可能正在渲染线程执行的grab并在安全边界释放；退休holder释放前新的本地grab仍被拒绝，避免超时重试叠加4K FBO。拿到QImage后，尺寸修正、质量100 JPEG编码和QSaveFile提交都在最大并发1的专用worker执行，保存阶段没有5秒超时。一次只允许一个grab/worker在途，重复点击仍独立尝试思翼SD拍照，但本地支路提示上一张仍在处理。文件以 `yyyy-MM-dd_hh.mm.ss.zzz_local_NNN.jpg` 命名，`NNN`来自请求generation，校验失败/超时可造成跳号；原子提交到Android暂存目录成功即递增 `_localPhotoCount`，再异步请求公共MediaStore发布，发布失败不回滚计数且暂存源留待下次启动重试。因此“JPG暂存已落盘”“LOCAL成功计数”和“公共图库已发布”是不同口径，卸载保留只对第三个阶段成立。
3. 本地录像仍用 `VideoManager::streaming()/recording()`观察全局主视频会话，但不调用VideoManager的start/stop。`CustomPlugin::createVideoSink()`只把主（非thermal）receiver保存到Manager；开始时读取并校验 `VideoSettings::recordingFormat`，用实际平台目录、`yyyy-MM-dd_hh.mm.ss.zzz_local_NNN`和对应mkv/mov/mp4扩展组成完整路径，然后直接调用主receiver `startRecording(outputFile, fileFormat)`。停止同样只调用该receiver。这样复用主压缩码流，不重新编码显示帧，也不会启动/停止thermal录像。Manager保存完整输出路径，仅当主receiver启动结果确认ownership且实际状态最终变为false后才把Android暂存文件排入公共发布器；录制中、external、thermal、provisional或失败启动都不发布半成品。录像复制到公共Movies期间源与目标并存，空间峰值约增加一份完整录像；发布任务只被排队、尚未完成时强制卸载，该暂存录像不承诺保留。
4. 桌面端因为绕过了VideoManager入口，custom在开始前保留对Video目录的受限配额清理：只处理可读写、非符号链接且命名符合 `*_local_NNN.mkv/.mov/.mp4` 的本功能旧分段，不碰其他入口或thermal文件。Android不走这段文件系统清理，而只读取当前安装 `QGCCustomPublicMediaV2` SharedPreferences中的 `publishedVideoUris`；核验URI位于公共Movies、文件名含 `_local_NNN`锚点（允许MediaStore/legacy冲突后缀），对可访问项求和并按 `DATE_ADDED`最旧优先删除，直到总量低于 `maxVideoSize`。provider暂时不可访问的URI保留在注册表并跳过本轮统计，实际删除失败才停止继续删；范围不扩大到注册表或公开Movies之外，更不会删除任何未公开Staging源。成功发布自行删源，失败源必须保留重试，因此暂存占用可使设备实际媒体空间高于用户上限。卸载/重装会形成新安装边界，历史公共媒体不计入新注册表也不自动删除；格式和限额仍来自原生VideoSettings，`recordingFormat`越界时不调用receiver并显示格式无效。
5. Manager只停止自己发起的owned录像。`VideoManager::recordingChanged`来自主receiver既有连接，继续更新全局状态和字幕，但只能证明“有录像”；主receiver的 `onStartRecordingComplete`还必须报告成功，且 `recordingOutput()`完整基名命中本次签发列表，Manager才确认ownership。失败结束pending；仍有本地意图时显示启动失败，用户已取消或关闭开关时不残留红色错误。输出不匹配视为其他入口录像，不能认领或停止。start pending为3秒；stop pending为5秒，超时只再调用一次主receiver stop。若断流重连或重新开启本地开关时仍有旧generation未决，重试意图会保留到该generation成功/失败后再消费，不能提前丢失，也不能并发发出第二次start。
6. 录像时断流会使当前本地文件结束；若同一用户会话和开关仍有效，Manager保留resume意图，码流恢复后以新时间戳/段号启动新文件。应用进入 `aboutToQuit` 时停止owned主receiver，并最多等待3秒直到VideoManager经既有主receiver信号把 `recording`更新为false，让后端写完容器尾部。Android随后等待照片worker结束，补扫所有已挂载卷中的V2 Staging与V1 `Android/media`源、再加当前AppSettings配置的旧源；若录像尚未封装完成，只排除它的精确输出路径，不跳过其他照片或历史失败源。最后调用 `waitForPendingPublications(120000)`，以Java单线程executor的Future barrier等待此前任务。任一发布已执行但失败时barrier也返回false；录像3秒未完成或公共发布120秒内未全部成功都只告警并继续退出。external录像和thermal录像不由本功能停止，强杀进程/直接卸载也不会执行这条正常退出链路。
7. 桌面端目录来自 `AppSettings::savePath()`下的 `Photo`、`Video`。Android先用该路径确定暂存所在 `StorageVolume`，并把JPEG编码成品和尚待封装的录像写入同卷 `Context.getExternalFilesDirs(null)/Custom-QGroundControl/Staging/Photo` 与 `Video`；这是应用专属暂存，不是最终图库位置。API 29+最终照片位于暂存源所对应可写MediaStore volume的 `Pictures/Custom-QGroundControl/`，最终录像位于 `Movies/Custom-QGroundControl/`；API 30+用 `StorageVolume.getMediaStoreVolumeName()`，API 29以primary或可用volume名与UUID交叉匹配，无法匹配时明确记录并回退主卷。API 25–28只能把成品发布到主共享存储的公共Pictures/Movies并等待MediaScanner确认。已完成公开发布的媒体不属于应用专属目录，卸载/重装QGC后物理文件和图库条目继续保留；在API 29+，图库可见意味着该行已经清除 `IS_PENDING`并成为公共持久文件。发布完成前强制卸载会丢失暂存源，不做保留承诺。每次启动会枚举所有已挂载卷上已存在的V2 Staging与V1 `getExternalMediaDirs()/Custom-QGroundControl/{Photo,Video}`，再加入当前AppSettings配置的Photo/Video旧目录，既可重试未完成发布，也避免切换本机存储卷后遗留媒体；不会猜测或扫描每个卷上的其他旧AppSettings路径。旧版若已先卸载并导致应用专属文件被删，程序无法恢复，因此必须先覆盖安装迁移版并等待公共发布成功后再测试卸载。这里的“Android本机SD卡”与云台相机SD卡仍是两个设备、两套状态。

8. 公共发布成功后，卸载只会删除应用专属Staging、SharedPreferences和no-backup marker，不删公共Pictures/Movies媒体。由于Manifest允许备份，重装时如果云备份恢复了 `QGCCustomPublicMediaV2` 但 `getNoBackupFilesDir()` 中没有当次安装marker，Java必须清空旧pending、录像管理与照片源清理URI集后才建立新marker。这一边界故意让历史公共媒体交还用户和系统图库管理，新QGC不在未授权的情况下读取、收编或静默删除它们。

缩放物理能力取卡录分辨率：4K→1.0x且不可变倍、2K→3.5x、1080P→5.5x、720P→6.0x。该能力来自0x20录像流参数，不从QGC拉流尺寸猜测；0x16若报告更小值则采用更小的有效上限。GStreamer主视频在真实首帧到达时从sink、解码器peer或ghost target的current CAPS读取宽高，同时以主 `GstGLQt6VideoItem` 根据最终 `GstVideoInfo` 设置的隐式尺寸作为第二条直接来源，仅用于确认视频会话可用。若两条直接路径都没有报告有效尺寸，Manager仅在原生视频状态已解码、尺寸属于会话白名单且连续稳定1秒时以 `VideoManager`兜底。`decoding=false`清除本次UI解锁和活动手势；同分辨率重连重新建立视频门控，但不能据此改变卡录能力。

tap以 `currentZoom`为基准，在唯一合法目标表中前进一档并立即发送0x0f；本地发送成功即显示目标，已有在途目标由新tap当场替换，没有方向FIFO或延迟派发。hold在420 ms成立时锁存起点、方向和总按压计时，并通常只发送一次0x05方向命令；目标档数为 `qRound(totalMs / 600.0)`，Manager只在计算结果比上次多一档时更新显示，并在1.0x与有效精确上限处钳制，首次到端点立即停止。默认步长1.0x时，2K卡录沿1/2/3/3.5，1080P卡录沿1/2/3/4/5/5.5，720P卡录沿1/2/3/4/5/6。普通release调用 `stopZoom()`完成最后一次时间计算并发送0x05停止；取消、移出、隐藏、断流、应用后台或销毁调用 `cancelZoom()`，不推进目标但同样发送停止。成功停止后约80 ms再发送一份安全停止副本；停止后不发送0x0f归整，断流/重连和设置变化也不能重新触发归整。只有起步第一目标已经是端点时，才以一次同方向0x0f替代极短0x05。0x18只记录实际位置，不覆盖当前目标显示。

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
2. Application Settings -> Fly View -> SIYI Gimbal Camera 中确认IP、端口、缩放分度值和Enabled。页面标题说明保持简短，以免撑坏设置页布局；tap/hold、唯一min锚目标表、卡录分辨率有效端点和成功发送即显示目标等完整要求以本节和Fact元数据为准。
3. Application Settings -> Video -> Local Video Storage 按需开启 `Save photos and videos locally`，并确认原生录像格式、最大本地视频存储和应用数据位置；该开关即时生效。Video Stream Integration 中选择是否使用 MAVLink 自动视频流；Android H.265硬解设置修改后重启QGC。
4. 返回Fly View。只要Gimbal Enabled，右侧单个纵向合并栏就应立即显示，不要求飞控或云台已连接；从上到下依次为 `+`、当前目标倍率、`-`、拍照/录像图标按钮及 `SD`/`LOCAL` 状态徽标。空闲时两个相机按钮应同为圆形且图标等大，录像按钮不应出现“录像/REC”文字；计时、pending或失败文字只在对应状态下显示。尚未确认受支持视频流或卡录能力时倍率显示 `--`且缩放按钮禁用，但开启本地存储且视频正在流式传输时录像按钮仍允许开始本地独立录像。视频第一帧只建立拉流会话门控；随后应看到卡录分辨率、映射上限及最终有效上限的能力摘要。最终上限按卡录4K=1.0、2K=3.5、1080P=5.5、720P=6.0确定，0x16只允许收紧。
5. 短按 `+`/`-` 每次立即发送同一合法目标表中的相邻一档，发送成功即显示target。默认步长1.0x时，2K卡录严格沿1→2→3→3.5往返，1080P卡录沿1→2→3→4→5→5.5往返，720P卡录沿1→2→3→4→5→6往返；拉流设为1080P但卡录为2K时仍必须使用3.5上限。快速点击 `+++`应立即依次发出并显示合法目标，后一次现场替换前一次目标。按住420 ms后进入hold，普通路径抓包应只出现一次0x05 `+1/-1`开始命令、release/cancel或目标到端点时的0x05 `0`停止及一份有界安全重复，不得周期性出现0x0f；若成立时第一目标就是端点，则只出现一次同方向端点0x0f而不出现0x05。显示目标档数必须等于 `qRound(totalMs / 600.0)`并沿按下方向单调；普通release完成最后一次时间计算，取消路径不推进显示目标。0x18运动中raw只更新独立实际值，不得覆盖当前目标显示或触发释放、断流重连后的0x0f纠偏。
   应用日志不再逐包打印SDK发送/接收、周期0x16/0x18/0x20回包、未变化能力确认或长按120 ms目标推进。正常测试只保留拉流会话与卡录能力就绪、单击目标发送/实际确认、长按开始/停止及停止后一次目标—实际倍率核对；超时、非法业务payload、分辨率不支持和安全上限冲突仍使用告警日志。来源IP不匹配、错误帧头/长度/CRC及非ACK帧会静默丢弃，协议级逐包检查应使用抓包工具，不依赖应用控制台。
6. 使用纵向栏下部的相机图标拍照：开启本地开关时应同时得到SD反馈（若相机/卡可用）和 `Photo/*.jpg` 当前帧截图。点击录像后分别观察 `SD` 与 `LOCAL`；相机toggle约400 ms后查询0x0a并以2.5秒确认超时保护，本地支路按实际 `VideoManager::recording()`进入红色状态。组合计时只要任一支路实际捕获即显示，停止按钮在另一支路离线后仍可用。
7. 分别在“有卡+SDK在线”“无卡”“SDK离线但RTSP正常”“RTSP断流但SDK在线”场景验证支路独立性；随后连接或断开飞控，控制栏不应消失或切换后端。只有关闭Gimbal设置时，有活动飞行器才恢复原生 `PhotoVideoControl`。

#### 8.3.2 UniPod MT11私有SDK、第二视频与双云台界面

| Fact | 范围/默认值 | 说明 |
|---|---|---|
| `mt11Enabled` | bool / `true` | 启用MT11私有SDK控制、独立第二路视频和右栏MT11选项；关闭或把URL清空会移除MT11三视图候选并释放其receiver。 |
| `mt11SdkHost` | `192.168.144.24` | MT11私有UDP SDK主机，只用于0x05/0x0A/0x0B/0x0C/0x0F/0x10/0x11/0x16/0x18相机命令和反馈。 |
| `mt11SdkPort` | 1–65535 / `37260` | MT11私有UDP SDK端口；与第二视频RTSP端口、连接状态和故障状态相互独立。MT11回包除来源逻辑IP必须匹配 `mt11SdkHost` 外，源端口也必须精确等于本值。 |
| `mt11ZoomStep` | 0.1–29.0 / `1.0x` | MT11绝对倍率tap步长；合法范围为1.0x到设备0x16确认且不超过30.0x的上限。hold使用0x05原生连续变倍并在停止时发送0；连续运动最多保持60秒，安全watchdog超时会主动停止并提示错误。 |
| `mt11RtspUrl` | string / `rtsp://192.168.144.24:8554/video1` | 只供 `DualVideoManager` 创建MT11第二路receiver；位于Application Settings -> Video -> MT11 Second Video，在MAVLink自动视频开启时仍可单独编辑。 |

SDK控制endpoint与RTSP视频URL在程序职责上保持分离，但现在默认都指向已改IP的同一台MT11：控制发往 `192.168.144.24:37260`，第二视频拉取 `rtsp://192.168.144.24:8554/video1`。SDK在线不证明第二视频能解码，第二视频有画面也不证明缩放、拍照、录像或热成像命令可用。A8 `sdkHost/sdkPort`默认仍为 `.25:37260`，MT11默认已为 `.24:37260`，因此两台实体设备的默认目的地址互不冲突。

上一开发版本曾把MT11 RTSP缺省写成 `rtsp://192.168.144.25:8554/video1`，SDK Host缺省写成 `192.168.144.25`。覆盖升级时分别检查 `[GimbalControl]/mt11RtspDefaultMigrationVersion`和 `mt11SdkHostDefaultMigrationVersion`：只有对应键已经存在且字面值精确等于旧默认时才改为 `.24`；键缺失时使用JSON当前默认，空字符串和任何其他自定义值均原样保留。各迁移完成后写入版本1，后续启动不重复覆盖。

界面和控制流程：

1. Application Settings -> Fly View分别配置 `SIYI A8 Mini Gimbal Camera`与 `UniPod MT11 Gimbal Camera` 的Enabled、SDK Host、SDK Port和Zoom Step；Application Settings -> Video -> MT11 Second Video单独配置MT11 RTSP URL。修改URL或Enabled时 `DualVideoManager`有序停止并释放旧receiver，再按新值创建/启动；不需要修改QGC主Video Source。
2. Fly View同时提供Map、A8、MT11三个可选视图，同一时刻仅一个居中全尺寸显示，其余可用项在左下垂直排列。Map居中时A8在下、MT11在上；点击任一缩略框后它立即居中，原中心项回到缩略区。选择保存为 `MainFlyWindowView`，首次升级兼容 `MainFlyWindowIsMap`。某路失效或禁用且正居中时回退Map。
3. 第二路由 `DualVideoManager`拥有独立 `VideoReceiver`、sink、解码/重试状态和MT11视频Item。A8 Manager只保存主非thermal receiver，MT11 Manager只保存第二receiver；任一录像按钮只开始/停止本Manager确认owned的录像。第二receiver释放前发出 `videoObjectsAboutToBeReleased`，MT11 Manager先停止本地录像并清空Item/receiver，应用cleanup也执行同一路径。
4. 右栏在A8与MT11同时启用时显示相机选择器；切换只更换当前控制Manager，不改变居中视频。两套面板保持相同缩放、拍照、录像、SD/LOCAL徽标和布局；选择MT11时在拍照控件上方额外显示RGB/IR圆形按钮。点击后发送0x11 `[2,0]`切到热成像主流，或 `[0,2]`切回RGB主流；pending期间禁止重复点击，只有0x10/0x11合法模式反馈才更新显示。
5. A8或MT11居中视频均保留原生风格的双击全屏；全屏会统一隐藏Fly View工具栏、PIP、WidgetLayer及custom overlay，退出后恢复。MT11视频继续使用Video设置中的fit/grid与Proximity/Obstacle视频叠加；独立PIP弹窗开关前后暂停第二路并延迟2秒重启，必须在桌面窗口切换和Android前后台真机验证。

MT11帧格式为 `55 66 | control | payload length LE | sequence LE | command | payload | CRC16 LE`；请求control为0x01、普通ACK为0x02，生产请求sequence固定0。CRC多项式 `0x1021`、初值0，覆盖CRC字段前的全部字节。当前命令职责为：0x05手动变倍/停止、0x0A相机系统/录像状态、0x0B异步功能反馈、0x0C拍照/录像切换、0x0F绝对倍率、0x10读取视频模式、0x11设置RGB/热成像模式、0x16最大倍率、0x18当前倍率。除文档规定可异步到达的0x0B外，接收侧要求来源逻辑IP等价于 `mt11SdkHost`且源端口精确等于 `mt11SdkPort`、整批UDP子帧长度与CRC全部合法、普通回包control为ACK且命中同command最近1.5秒请求窗口。

当前 `Mt11ProtocolTest` 报告9 passed、0 failed，覆盖7个协议业务slot及QtTest init/cleanup；这只证明纯Protocol构帧和解析。MT11 SDK真实UDP往返、固件ACK时序、0x0B异步反馈、0x10/0x11热成像画面、双路长期解码/重连、三视图/全屏、双receiver录像隔离、本地媒体和双设备网络仍需实机验收，不能据此宣称整机或全工程Qt 6构建通过。

#### 8.3.3 原生顶部云台姿态栏自动接管

该功能处理的是QGC、RC与飞控Gimbal Manager之间的MAVLink控制权，不经过A8 Mini私有UDP SDK。PX4的 `MNT_MODE_IN=Auto (0)` 会按最近输入在RC和MAVLink之间切换；较大的RC摇杆动作可把输入切回RC。本分支在custom顶部栏中把自动接管和原始按钮动作合并为一次用户操作。

源码对比和真机结果同时表明，控制权问题与Center专属问题是两个阶段：QGC的Center和Tilt 90都无条件调用 `MAV_CMD_DO_GIMBAL_MANAGER_PITCHYAW(1000)`，相同target、NaN角速度和相同flags下，主要差别只有pitch为 `0` 或 `-90`。MAVLink规范用NaN表示未设置，`0°`是合法角度；标准PX4输入实现也不会过滤 `0,0`。关键对照是：RC前最后MAVLink目标非0时，RC后Center可用；RC前最后目标为 `0,0` 时，RC只改变物理姿态却没有刷新飞控输出桥保存的MAVLink目标，随后相同的Center会被下游change-detection当成旧值；Tilt 90或Yaw模式命令改变目标/模式后，Center才重新成为变化。上一版“发送当前实际姿态”也不能保证变化，因为它可能等于RC正在输出的实际姿态。因此修复不能只清QGC缓存或重发同一个 `0,0`，而要先发送一个既不同于旧 `0,0`、也不同于钳制后上报pitch的受限小偏移目标。若失败Center的命令1000 ACK确认为Accepted，可基本把问题定位到Gimbal Manager之后的输出/设备链；若ACK为Denied，则仍是控制权被RC抢回；无ACK则先处理链路。

1. 点击 `Yaw Lock/Follow`、`Center`、`Tilt 90` 或 `Retract`。如果同一活动云台已经满足 `gimbalHaveControl && !gimbalOthersHaveControl`，普通姿态动作直接执行且不额外发送Configure；如果此前任何控制权通知曾表明QGC失权，则按 `Vehicle id + manager compid + device id` 保存该云台的“需要预激活”标记，即使点击时状态已经恢复为QGC持权也进入预激活流程，但不再发送Configure。标记按云台身份隔离，切换活动云台或Vehicle不会用新对象的当前状态覆盖旧对象记录。
2. 如果控制权属于RC/其他控制端，QGC静默调用一次 `acquireGimbalControl()`，发送 `MAV_CMD_DO_GIMBAL_MANAGER_CONFIGURE`，不显示“是否接管”确认框，也不立即盲发姿态目标。待执行事务记录本次是否已经发送Configure；若点击瞬间仍显示QGC持权、但延迟到下一事件循环做稳定复核前RC恰好夺权，也会在复核处补发唯一一次Configure，而不是无命令地等到10秒超时。
3. 只有同一Vehicle、同一GimbalController和同一活动Gimbal的 `GIMBAL_MANAGER_STATUS` 确认QGC成为primary后，才进入动作阶段。`gimbalHaveControlChanged` 比 `gimbalOthersHaveControlChanged` 先发出的当前原生更新顺序由 `Qt.callLater` 合并复核，不能只看到第一个布尔值就提前发送。
4. 如果最新待执行动作不是Center，直接重放一次。如果仍为Center，则先缓存当前遥测pitch。QGC当前没有向QML暴露设备上报的pitch min/max，但本项目原生Center和Tilt 90已真机验证 `0°`、`-90°` 两端合法，因此先把pitch钳制到 `[-90°,0°]`：钳制值不高于 `-2°` 时向0方向增加1°，否则减小1°。所得目标始终在该区间、严格非0，并与钳制值相差1°。随后调用原生 `sendPitchBodyYaw(primerPitch, 0, false)`，使预激活和最终Center采用相同的body-yaw坐标系、yaw=0和flags，只有pitch由非0变为0；该接口同时停止原生500 ms速率Timer，防止它并发发送同一个命令1000。该目标相对钳制后的上报pitch差1°，但遥测陈旧时不等于对实际物理姿态只移动1°，因此不能再把它描述为无位移命令。
5. 预激活和真正Center都是命令1000。必须监听同一Vehicle的 `mavCommandResult`，严格匹配manager component、命令号、当前代次和 `failureCode=MavCmdResultCommandResultOnly`；Vehicle在发出正常最终结果信号前已经移除在途项。只有预激活ACK Accepted才启动400 ms稳定窗口，因为ACK仅代表Gimbal Manager接受命令，不代表较慢的飞控到厂商云台输出桥已经锁存。窗口结束后再次确认对象和控制权，再调用一次原生 `centerGimbal()`。最终Center另建独立结果门控：同Vehicle、manager component和命令1000的ACK必须是Accepted且failureCode为原生命令结果，才删除该身份键的预激活标记；Duplicate、Denied、Failed、NoResponse或4秒没有结果都只结束结果等待并保留标记，下次Center仍会重新预激活。预激活ACK失败、无响应、QGC本地Duplicate或RC重抢都立即取消，不能在首条ACK前盲发第二条。
6. 等待中的多次姿态点击只保留最后一次，例如 `Center -> Tilt 90 -> Retract` 最终只执行Retract；整个等待过程仍只有一次Configure。10秒没有完成、切换车辆/控制器/活动云台、对象销毁、点击Point Home或手动Acquire/Release都会取消，不允许迟到状态或ACK把命令发到新对象。
7. 持续摇动RC时不会循环Configure或后台抢权。若RC在重放瞬间再次取得控制，原生确认信号被静默抑制，本次动作结束且不自动重试；松开/回中摇杆后由用户再次点击。摇杆速率、屏幕拖动等连续控制也不进入pending队列。
8. `Point Home` 保持 `Vehicle.guidedModeROI(homePosition)` 直发，因为它是飞控ROI行为而不是同一Gimbal Manager姿态控制接口；点击它会先取消未完成的姿态重放。

推荐 PX4 TELEM2 参数：

| 参数 | 值 |
|---|---|
| `MAV_1_CONFIG` | `TELEM 2`，修改后重启飞控 |
| `SER_TEL2_BAUD` | `115200` |
| `MAV_1_MODE` | `Gimbal` |
| `MAV_1_FLOW_CTRL` | `Off` |
| `MAV_1_FORWARD` | `Enabled` |
| `MNT_MODE_IN` | `Auto (0)`，允许RC与MAVLink Gimbal Protocol v2按最近输入自动切换 |
| `MNT_MODE_OUT` | `MAVLink Gimbal Protocol v2` |

需要同时使用RC通道和顶部MAVLink姿态栏时使用 `MNT_MODE_IN=Auto (0)`；若只允许地面站控制可改为 `MAVLink Gimbal Protocol v2 (4)`，若只允许RC则改为 `RC (1)`，修改后按PX4要求重启。PX4官方说明见 [Gimbal Configuration](https://docs.px4.io/v1.15/en/advanced/gimbal_control)。TELEM2参数只负责飞控与云台的MAVLink集成，是飞行任务/姿态控制场景的推荐配置，不是思翼私有合并栏的前置条件。custom的tap 0x0f、hold 0x05、拍照、录像和状态查询全部由电脑或遥控器直接发往 `192.168.144.25:37260/UDP`；纯云台无飞控时仍可使用，RTSP播放、私有SDK控制和飞控MAVLink是彼此独立的三条链路。

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

通信链路列表为空（`count=0`）时，项目创建以下默认链路：

| 名称 | 类型 | 本地端口 | 单一服务器 | 开始时自动连接 | 高延迟 |
|---|---|---:|---|---|---|
| `local` | UDP | `14550` | `192.168.144.125:14550` | 关闭 | 关闭 |

双默认表的自动安装方案已取消。只要已经保存至少一条通信链路，安装器就完全不干预：历史 `testlocal`、重复 `local`、其他名称及用户修改值都会保留，需要时由用户在界面删除或编辑。原生动态 UDP AutoConnect 默认关闭但设置项可见、可开启；它与本地端口同为 `14550` 的链路不应同时活动。

## 11. 关键运行链路

```text
PX4 HEARTBEAT
  -> CustomFirmwarePluginFactory
     -> 能力列表声明 PX4 + MultiRotor
     -> 当前运行选择只检查 MAV_AUTOPILOT_PX4，未检查 MAV_TYPE
  -> CustomFirmwarePlugin
     -> CustomAutoPilotPlugin 控制车辆设置页
     -> toolIndicators 移除 RC RSSI、插入 Fuel，并在GPS后插入Proximity Radar
        -> Vehicle.distanceSensors十方向任一有效时显示
        -> 小于5.0 m时图标与详情行变红、图标闪烁
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
     -> 将原生UDP AutoConnect缺省值设为false，保留可见设置和用户值
     -> DefaultCommunicationLinkInstaller
        -> count非零或无效时完全不修改现有通信链路
        -> 仅在count=0时创建默认local：本地14550 -> 192.168.144.125:14550
     -> Viewer3DSettings / External3DMapManager / CustomViewer3DManager
     -> FlyViewCustomSettings（FlyView/showHeadingCompassBar）
      -> GimbalControlSettings / GimbalControlManager
         -> enabled时立即启动后台2秒探测，不依赖activeVehicle或QML可见
         -> localMediaStorageEnabled默认true且即时生效，独立于思翼SD卡/SDK状态
         -> GStreamer优先接收主显示sink最终协商尺寸；无直接结果时稳定1秒的VideoManager尺寸兜底
         -> 非GStreamer直接使用VideoManager解码状态和尺寸；拉流只建立会话可用门控
         -> 每2秒以0x20查询stream_type=0卡录参数：4K→1.0、2K→3.5、1080P→5.5、720P→6.0
         -> 同轮0x16作设备安全交叉校验，只能以较小值收紧卡录能力
         -> 合法目标为1.0起算的唯一min锚网格，并追加有效精确上限
         -> 每轮发送0x0a状态；取得视频门控和能力后，缩放空闲时发送0x18
         -> 受支持视频会话和卡录能力均确认后解锁；真实断流撤销UI门控，同尺寸重连立即重建0x18倍率参考
         -> 来源逻辑IP、精确帧长、CRC、control=0x02及payload均合法才置sdkResponding
         -> 0x0f本地发送成功即更新并显示currentZoom目标；0x18实际值独立核对
         -> tap立即发送/替换同表相邻一档0x0f；不维护方向队列，也不延迟重放
         -> hold在420 ms成立，通常只发送一次0x05方向命令；按qRound(totalMs/600.0)更新单调显示目标
         -> release/cancel/端点发送0x05停止和有界安全重复；起步即端点时只发一次同方向0x0f
         -> 拍照并行：0x20卡录像素尺寸 + 解码源宽高 + 窗口DPR计算逻辑target
            -> 主渲染纹理grabToImage(target)，窗口holder保护渲染线程生命周期；等待ready最多5秒
            -> QImage交给单线程worker完成精确尺寸/完整比例修正、质量100编码和暂存原子提交
            -> 同时独立发送思翼0x0c拍照；JPG暂存成功后回主线程计数并排队公共MediaStore发布
         -> 录像并行：主非thermal VideoReceiver压缩码流写Video + 思翼0x0c toggle
            -> Android只在confirmed-owned且recording=false完成容器收尾后排队公共MediaStore发布
         -> Android按AppSettings所在StorageVolume选择getExternalFilesDirs暂存Photo/Video目录
            -> API29+以IS_PENDING复制/fsync/字节校验，照片公开到Pictures，录像公开到Movies，完成后删暂存
            -> API25-28以公共Pictures/Movies partial复制、rename和非空MediaScanner URI确认发布
         -> 启动时遍历全部已挂载卷的V2 Staging、V1 Android/media，加上当前AppSettings旧目录，失败保源
         -> Android公共录像仅统计当前安装SharedPreferences URI注册表
            -> 按DATE_ADDED从旧到新删除；重装前历史公共媒体不自动删除
         -> 本地录像区分owned/external及start/stop pending；断流恢复创建新段
         -> ownership只由主receiver启动成功且recordingOutput匹配本次基名确认
     -> aboutToQuit（DirectConnection）
        -> shutdownLocalMedia(true)停止owned录像
        -> 最多等待3秒至recording=false完成容器收尾
        -> Android等待照片worker，补扫遗漏源；封装超时时只排除活动录像路径
        -> JNI executor barrier最多等待120秒完成已排队公共发布；任一超时告警后退出
     -> AndroidVideoDecoderPolicy::apply()
        -> 枚举并预检 byte-stream/au 厂商 amcviddec-*
        -> 注册高 rank qgcandroidh265hwdec
     -> GimbalVideoStreamSupport 安装 A8 Mini 默认值
     -> Mt11ControlManager
        -> MT11 SDK独立endpoint 192.168.144.24:37260，不读取主视频URL
        -> 每2秒轮询0x16/0x18/0x0A/0x10，6秒无合法响应判为离线
        -> 0x05/0x0F缩放、0x0C拍照/录像、0x11 RGB/热成像切换
        -> 只保存MT11第二视频Item/receiver；本地媒体不操作A8主receiver
     -> DualVideoManager
        -> 读取独立mt11RtspUrl，默认rtsp://192.168.144.24:8554/video1
        -> 为MT11创建独立VideoReceiver、sink、启动/停止和1秒重试状态
        -> 禁用、URL为空、设置变化和cleanup时有序停止并释放
        -> videoObjectsAboutToBeReleased先通知MT11 Manager收尾owned本地录像
  -> VideoManager::init()
     -> 创建 VideoReceiver / decodebin3
        -> CustomPlugin::createVideoSink()复用原生qgcvideosinkbin
           -> receiver父对象为DualVideoManager时，只接入MT11 Manager与第二视频，不进入A8探针/主receiver路径
           -> PulledVideoResolutionProbe在真实首帧读取sink/peer/ghost-target current CAPS
           -> 同时观察GstGLQt6VideoItem由最终GstVideoInfo写入的隐式尺寸
           -> 保存主非thermal receiver；本地录像只直调该receiver start/stop
           -> 主receiver onStartRecordingComplete + recordingOutput回传Manager
           -> receiver既有信号继续由VideoManager更新recording与字幕
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
     -> MT11 Second Video
        -> mt11RtspUrl（默认rtsp://192.168.144.24:8554/video1）
        -> 独立于主Video Source、MAVLink自动流和mt11SdkHost/mt11SdkPort
     -> localMediaStorageEnabled（默认true，即时控制本地照片与录像附加支路）
     -> recordingFormat / maxVideoSize继续决定格式与总量门限
     -> 桌面使用AppSettings Photo/Video；Android使用同卷getExternalFilesDirs暂存后发布到公共Pictures/Movies
     -> Android公共清理只管理当前安装注册且含_local_NNN锚点的Movies录像，兼容同名后缀
     -> 不以配额删除未公开Staging；失败源额外占用空间直至重试成功/用户处理
  -> Video Stream Integration
     -> mavlinkAutoVideoStream
     -> forceAndroidH265HardwareDecoder（所有平台显示，仅 Android 生效）
```

```text
Fly View
  -> custom同路径 FlyView.qml；继续复用原生 FlyViewWidgetLayer.qml / FlyViewToolStrip.qml
  -> DualPipView.qml
     -> item1 Map / item2 A8 / item3 MT11
     -> 点击任一左下缩略框即切为居中全尺寸，原中心项回到缩略区
     -> Map居中时A8在下、MT11在上；选择持久化为MainFlyWindowView
  -> MT11Video.qml
     -> 独立receiver画面、原生fit/grid、Proximity/Obstacle视频叠加
     -> PIP窗口切换前后停流并延迟2秒重启
  -> A8或MT11视频全屏时统一隐藏工具栏、PIP、WidgetLayer和custom overlay
  -> custom FlyViewToolStripActionList.qml 增加 3D 入口
  -> custom FlyViewTopRightColumnLayout.qml
     -> A8或MT11任一enabled
        -> 不检查activeVehicle；两者同时启用时显示A8 Mini/MT11选择器
        -> A8加载GimbalCameraControl，MT11经MT11CameraControl复用同一UI并注入MT11 Manager
        -> 合并栏始终显示，sdkResponding不决定可见性；SDK超时会失效卡录能力并锁缩放，待新0x20恢复
         -> 顶部 GimbalZoomControl.qml（+ / 倍率 / -，调用当前选择的Manager）
            -> tap：唯一min锚表的相邻目标 -> 立即0x0f -> 成功即显示currentZoom
            -> hold 420 ms成立：一次0x05连续运动，按总按压时长每600 ms更新同方向显示目标
            -> 目标表取决于卡录：2K为1/2/3/3.5，1080P为1/2/3/4/5/5.5，720P为1/2/3/4/5/6
            -> 普通release调用stopZoom；取消/移出/离线/销毁调用cancelZoom，不补短按
        -> 下部双目标相机控制
           -> MT11选择下在拍照上方增加RGB/IR按钮，0x11发送、0x10/0x11确认
           -> QGC拍照/录像图标，无原生缩放滑块
           -> SD：0x0c拍照/录像 + 0x0b功能反馈 + 0x0a录像状态校正
           -> LOCAL：当前主渲染帧JPG + 主非thermal VideoReceiver压缩码流录像
           -> VideoManager只经receiver既有信号维护状态/字幕，不由custom调用全局start/stop
           -> SD/LOCAL独立徽标；任一实际捕获即可显示组合计时
           -> 无云台卡或SDK离线不阻断LOCAL；无流/本地写盘失败不回滚SD
        -> 不依赖Vehicle、飞控或MAVLink相机管理器
     -> A8和MT11均disabled && activeVehicle存在
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
grep 'QGC_CUSTOM_ANDROID_MEDIA_LIBRARY_V2' \
  ../build-qgc-android-arm64/custom/android/src/org/mavlink/qgroundcontrol/QGCCustomMediaLibrary.java
```

第一条必须指向当前 build 下的 `custom/android`，后两条必须分别命中USB和媒体库custom标记；否则安装的 APK 可能仍缺少对应Java实现。

重点验证：

1. Application Settings -> Fly View -> Instrument Panel显示 `Show Heading Compass Bar`，页面同时保留Viewer3D、`SIYI A8 Mini Gimbal Camera`和 `UniPod MT11 Gimbal Camera`；两套SDK host/port/zoom step各自保存，MT11组提示第二视频URL位于Video页。
2. Application Settings -> Video保留全部原生设置组；新增常驻 `MT11 Second Video`组，默认URL必须为 `rtsp://192.168.144.24:8554/video1`，且在MAVLink自动流启用后仍可编辑。Local Video Storage新增本地照片/录像开关；Video Stream Integration在所有平台显示两个开关，H.265强制硬解仅Android生效。
3. Viewer3D Enabled 持久化，重启后图标状态正确。
4. 3D 图标白色，2D/3D 可往返切换。
5. 本地 OSM、外部 OBJ/glTF/GLB 和可选 Google 3D 正常加载。
6. 验证云台在线发现与飞控解耦：
   - Gimbal Enabled但飞控和云台都未接入时，合并栏仍必须立即显示并占用完整布局尺寸；Manager继续在后台探测，状态点灰显、倍率显示 `--`，两个缩放按钮禁用，tap不得发送0x0f且hold不得发送0x05。开关关闭时保持原有SDK按钮门控；开关开启且RTSP主流可用时，即使SDK状态未知也必须允许本地录像，拍照必须尝试当前帧截图。错误来源逻辑IP、短包、错误帧头/长度/CRC、control不是0x02或业务payload非法都不能把 `sdkResponding`置true。
   - A8 `SiyiSdk`用测试socket分别回送原生IPv4来源和IPv4-mapped IPv6来源，二者表示同一配置IP时都必须通过来源检查；同IP但回包源端口不同且CRC合法时也必须接受。真正不同的来源IP仍须静默丢弃，具体来源应通过抓包工具验证，不再输出逐包SDK debug日志。本项仅描述A8接收策略，不适用于MT11；MT11严格端点验收见下方双云台协议矩阵。
   - 只连接A8 Mini网络、正常拉流并接通私有SDK、完全不连接飞控时，合法回包必须把状态点切为绿色；受支持拉流只确认视频会话，合法0x20录像流ACK确认卡录能力，二者齐备后缩放才解锁，随后0x18建立起始目标倍率，0x0a恢复机内录像状态。单独0x16、单独拉流尺寸或SDK绿色状态都不能代替0x20卡录能力。连接或断开飞控不得影响控制栏可见性和后端选择。
   - 空闲探测时私有SDK超过1.5秒未响应但视频仍在正常解码，合并栏不得消失，状态点可变灰；为避免沿用旧卡录设置，已确认能力失效并锁定缩放，直到新的0x20录像流参数恢复能力。真实断流同样取消活动手势并锁定视频门控；同分辨率恢复解码且SDK恢复后无需进入设置页即可重新查询0x20/0x16并发起0x18倍率同步。
7. 验证合并控制栏的输入、安全、相机状态和布局：
   - 验证拉流会话来源与能力来源分离：拉流白名单仍只有1280×720和1920×1080，但二者都只建立视频可用门控，不产生6.0/5.5上限。任一直接观察器报告尺寸时应立即取消 `VideoManager`兜底；若两个直接观察器都未回调，则要求 `decoding=true`且同一白名单尺寸连续稳定1秒后才采用。`decoding=false`撤销UI门控并停止活动0x05；重连同一尺寸必须重新建立门控。
   - 抓包应看到Manager每2秒发送0x20录像流请求和0x16。0x20请求payload必须为 `00`，ACK中的 `stream_type`也必须为0；4K/2K/1080P/720P分别映射1.0/3.5/5.5/6.0。0x16是设备安全交叉校验：小于映射值时立即采用较小值，大于映射值时不得扩展能力。拉流1080P＋卡录2K＋0x16=3.5时最终必须为3.5；拉流1080P＋卡录1080P＋0x16=5.5时最终为5.5。未知卡录尺寸、非法0x20或超过4.5秒没有新的有效0x20时必须锁定缩放，即使0x16、0x0a等其他回包仍正常也不能保留旧能力。
   - 验证0x16/0x18双格式解析。新版：`01 00`→1.0x、`01 08`→1.8x、`02 08`→2.8x、`03 05`→3.5x、`05 05`→5.5x；真机旧版：`0A 00`→1.0x、`12 00`→1.8x、`1C 00`→2.8x、`23 00`→3.5x、`37 00`→5.5x。必须逐包先尝试新版，只有新版候选越过当前A8范围时才计算完整小端uint16/10旧版候选并再次校验；两种成功路径由协议单元测试和最终倍率结果验证，不再逐包打印encoding。`00 00`、0.9x、超过当前上限、`FF FF`、非法小数字节、短/长payload及带尾随字节的完整帧均不得改变倍率或编码输出参数。
   - `-`/`+`每次tap只调用一次方向接口并立即发送同表相邻一档0x0f目标，本地发送成功后中心数字必须立即显示target。默认步长1.0x时，2K卡录严格覆盖1→2→3→3.5并反向返回，1080P卡录覆盖1→2→3→4→5→5.5，720P卡录覆盖1→2→3→4→5→6；4K两方向均不可用。快速 `+++`应依次发送并显示合法目标，后一个目标现场替换前一个目标，不存在等待、FIFO或停止点击后再派发。
   - 按住420 ms后进入hold，普通路径只允许发送一次0x05 `+1/-1`开始命令。以手势按下起点为时间零点，显示目标档数必须等于 `qRound(totalMs / 600.0)`；只在计算档数增加时更新显示，并始终从hold起始目标沿同一方向表计算，不能因定时器抖动、反馈raw或快速回调倒退。覆盖420/600/900/1200 ms附近边界、正反方向、端点钳制和释放竞争；显示目标到1.0或卡录有效上限时必须主动停止0x05。普通release调用一次 `stopZoom()`并完成最后一次时间计算；取消、移出、控件隐藏/销毁、应用后台或断流调用 `cancelZoom()`且不推进目标。活动0x05路径必须发送0x05 `0`停止及有界安全重复，结束后、断流重连或设置变化后都不得出现0x0f归整。仅当hold成立时第一目标已是端点，才允许以一次同方向端点0x0f替代0x05。
   - 验证目标显示与实际反馈解耦：0x0f本地发送成功即显示target；运动中0x18只更新内部实际值，不能把中心数字从target改回旧档。发送失败必须保持原目标；新tap或hold目标成功后立即替换显示。合法0x16若收紧上限、0x20卡录模式变化、SDK能力失效或真实 `decoding=false`可以触发安全重同步和锁定；普通中间0x18不得如此。取消/隐藏/后台后不得在稍后重放旧hold目标或发送反向0x0f。
   - 拍照测试矩阵：开关关闭只验证0x0c/0x0b；开关开启时验证“SDK+有卡”“SDK无卡/拍照失败”“SDK离线”“无主渲染帧”“Photo暂存目录不可写”。前3类只要有主帧和可写路径就必须生成JPG；无卡且0x20仍新鲜时继续按该卡录尺寸输出，无卡且0x20从未确认/已失效时回退拉流尺寸，二者都不能破坏本地独立语义；后2类只报LOCAL错误且仍尝试SDK。分别组合720P/1080P拉流与720P/1080P/2K/3840×2160/4096×2160卡录，JPG物理尺寸必须精确等于已确认卡录尺寸；4K文件的细节仍以实时拉流为上限。4096×2160配16:9流必须保留四角并在左右各补128像素黑边，不能裁剪或拉伸。Android地图主画面+视频PIP、视频主画面，以及Ubuntu两种状态输出尺寸必须一致；覆盖DPR 1、1.5、2、2.625、Android 86%/100%界面缩放和横竖屏，日志中的逻辑target乘DPR应接近期望content，最终output必须完全命中卡录尺寸。
   - 拍照失败与发布边界：Manager级覆盖5秒内ready、永不ready、超时后迟到ready、grab期间替换Item、空QImage、worker期间快速点击和编码/open/commit失败。业务超时/替换不得提前销毁渲染线程grab，抓帧或暂存提交失败不得计数/发布；暂存成功但公共发布失败仍只计一次且必须保留源供重试。文件必须来自当前解码帧、以 `*_local_NNN.jpg` 命名且JPEG质量100，不能误称相机原片。
   - Android照片发布矩阵：API 29+分别用内置卷和遥控器本机可移动SD验证暂存位于对应 `Android/data/org.mavlink.qgroundcontrol/files/Custom-QGroundControl/Staging/Photo`，发布完成后暂存源消失，公共目标位于同卷 `Pictures/Custom-QGroundControl/`；MediaStore的width/height/_size必须与实际JPG一致，日志出现非空 `content://media/...` URI且图库显示。API 25–28另验证公共Pictures最终文件与非空MediaScanner URI；只有暂存文件、没有公共URI不算卸载保留验收通过。
   - 录像测试矩阵：分别覆盖“有卡+本地”“无卡+本地”“SDK离线+本地”“本地路径不可写但SD可用”“RTSP无流但SD可用”“开始时断流/恢复”“录像中断流/恢复”“开关中途关闭/再开启”。SD与LOCAL必须独立显示，任何一边失败不得停止另一边；无卡时0x0a状态2结束SD pending但LOCAL继续绿色录制；本地恢复后必须产生新分段。验证开关关闭只停止owned本地录像且SD保持原目标，退出也只停止owned会话。
   - receiver边界矩阵：同时配置主流和thermal流，按思翼栏开始/停止时只允许主（`isThermal()==false`）receiver各收到一次start/stop，thermal不得收到调用或生成/关闭文件。确认VideoManager仍从主receiver既有信号更新 `recording`、录像开始/停止状态和字幕；custom不得调用会遍历所有receiver的VideoManager start/stop接口。主流断开重建receiver后，Manager弱引用必须更新，不能调用已销毁对象。
   - ownership/pending矩阵：先由其他入口启动主receiver录像，再点击/停止思翼栏，Manager只能adopt/release而不能调用stop；分别注入主receiver启动完成的“成功+匹配输出完整基名”“成功+不匹配基名”“失败”“3秒无回调”，只有第一种允许confirmed owned；仍有本地意图的失败应显示 `FAILED`/“启动本地视频录像失败”，已取消的迟到失败/超时不得残留红色错误。自有停止5秒无完成时只重试一次主receiver stop。快速连点、`recordingChanged`与启动完成回调先后顺序、迟到回调不能造成双start、双stop或把external误标owned。另覆盖旧generation未决期间断流重连和开关OFF→ON：不得并发start，旧generation失败后必须消费一次延迟重试，旧generation成功则直接接管且不得再start。
   - 格式/发布矩阵：mkv、mov、mp4三种 `recordingFormat`分别确认传给主receiver的完整路径、枚举值和实际文件可回放；越界格式必须报错且不调用start。桌面完整路径来自 `AppSettings::videoSavePath()`；Android录制路径必须来自AppSettings所在卷的 `Android/data/org.mavlink.qgroundcontrol/files/Custom-QGroundControl/Staging/Video`，容器完成后最终发布到同卷 `Movies/Custom-QGroundControl/`。录制中不能公开半成品，confirmed-owned停止并收到 `recording=false` 后才允许排队发布；external、thermal、provisional和失败启动不能由本功能发布。分别注入insert空URI、pending journal写入失败、复制中断、fsync/字节数不匹配、清除pending失败和删暂存失败：不完整公共行必须删除，发布未成功时保留源，公开已成功但源清理失败不得删公共成品。预置同名不同内容公共目标，确认MediaStore/legacy生成碰撞后缀而不覆盖或认领旧文件，带后缀的新录像仍可由本安装URI注册表管理。Android图库以MP4为基线，MKV/MOV若已有MediaStore记录但厂商图库不支持不判为发布失败。
   - 容量/重装矩阵：关闭 `enableStorageLimit`不得删暂存或公共文件；开启后在当前安装URI注册表混合多个原名及带provider同名后缀的 `_local_NNN` Movies URI、图库已手动删除URI、非本功能文件名与provider暂时失败，只对可访问且命名匹配的注册录像求和，按 `DATE_ADDED`最旧优先删除，任一删除失败即停。另构造总量已超限但发布失败的Staging录像，反复触发清理后源仍必须保留重试；成功发布自行删源，失败暂存造成的额外占用允许使物理空间继续高于上限。先发布多张照片和多段录像，记录URI与文件hash/尺寸，再卸载QGC、重装同包名APK：图库项和物理内容必须原样保留，新安装的空URI注册表不得自动删历史媒体。再模拟 `allowBackup=true` 将V2 SharedPreferences恢复而 `getNoBackupFilesDir()` 安装marker不存在，必须先清空恢复的pending、录像管理和照片源清理URI注册表并创建、fsync marker，不得把卸载前媒体重新纳入自动清理。
   - 升级/卸载矩阵：在旧V1 APK尚未卸载时，分别向多个已挂载卷的V2 Staging、`getExternalMediaDirs()/Custom-QGroundControl/{Photo,Video}` 和旧AppSettings Photo/Video放入合法、空文件、同名同内容、同名同大小但内容不同与不匹配命名文件，然后以同签名覆盖安装V2。只有合法本功能文件被发布；已有公共目标只有“本安装URI日志已知、名称/relative path匹配且内容逐字节一致”时才幂等复用，不能仅按大小认领，也不能把重装前历史媒体加入新安装的录像清理注册表。再分别在复制中强杀进程、公开后删源前强杀进程、公开前强制卸载和公开完成后卸载：pending、`sourceCleanupUris`与仍存在的源应支持本安装内恢复且不产生重复公共成品；公开前卸载不承诺暂存媒体保留，公开完成后卸载必须保留。已经先卸载V1而丢失的旧文件必须明确判定为不可恢复，不得声称程序能找回。
   - 退出收尾矩阵：owned主receiver录像中触发正常退出，确认 `aboutToQuit`只向主receiver调用stop并在3秒内等到VideoManager recording=false，生成文件可回放且容器时长/索引正常；随后必须等待照片worker、补扫全部已挂载卷的V2/V1源与当前AppSettings旧目录，并让JNI Future barrier在最多120秒内等完调用前已排队的公共发布，退出后图库项应已公开且Staging源已按成功规则删除。注入录像3秒不结束时，补扫必须仅排除该精确活动输出，与它无关的照片和历史失败源仍要发布；另分别注入公共复制超过120秒和Java barrier异常，必须记录对应告警并继续退出，不能死锁。external主录像、thermal录像、未录像和已停止场景不得被停止或无条件等待3秒；普通cleanup重复调用必须幂等。另以强杀进程/直接卸载对照，确认不会误称这两种路径享有正常退出保证。
   - SD支路继续覆盖0x0c功能2、约400 ms状态查询、2.5秒确认超时、旧状态忽略、0x0a状态2/3和0x0b功能4失败；0x0b大于4的未知值必须在Protocol层拒绝。组合capturing/计时不得把本次0x0c发送后的乐观SD状态当成已捕获，只有0x0a确认或LOCAL actual为true才开始。
   - 合并栏必须保持纵向单栏结构，按 `+ -> 当前倍率 -> - -> 分隔线 -> 拍照 -> 录像 -> SD/LOCAL徽标` 排列，使用QGC相机图标且不出现原生缩放滑块。空闲拍照和录像按钮必须同为 `actionSize` 圆形触控区，录像图标固定为 `actionSize * 0.48`并与拍照图标等大，空闲录像按钮不得显示“录像/REC”；计时、pending和 `FAILED` 仅在相应状态下出现。验证SD/LOCAL绿色实际录制、黄色pending、红色失败、灰色不可用/未录制及错误提示互不覆盖。Gimbal启用时右侧Column宽度和Loader高度必须始终随控制栏完整隐式尺寸扩展；在Android 86%/100%、桌面、横竖屏、地图/视频主窗口、PIP、Viewer3D和小屏触摸场景检查不裁切、不重叠、按钮达到移动端最小触控尺寸。
   - Gimbal Disabled且存在活动飞行器时恢复原生 `PhotoVideoControl`；关闭时没有活动飞行器则不加载原生控件。关闭后回送关闭前轮询产生的迟到0x18/0x20/0x0a/0x0b或任意0x16，不能重新把私有SDK标记在线、恢复能力、改变已清空的状态或发送0x0f。Gimbal Enabled但离线时仍显示私有合并栏，以灰色状态明确离线，不得用原生控件替换或把整栏隐藏。
   - 双云台网络和设置迁移：确认A8既有配置保持 `.25`不变；MT11 SDK实际发包目的为 `192.168.144.24:37260`，第二receiver实际请求 `.24:8554/video1`，两条链路分别断开时只影响各自状态。对SDK Host和RTSP分别构造“无旧键”“旧键精确为旧 `.25`默认”“旧键为空”“旧键为其他用户值”“迁移版本已为1”，只有精确旧默认首次启动改为 `.24`，自定义值不得覆盖。
   - 三视图矩阵：在Map、A8、MT11都可用时逐一点击三个左下缩略框，点击项必须居中、旧中心回到PIP，且Map居中时MT11缩略框位于A8上方；重启保持最后选择。分别关闭A8流、清空MT11 URL、关闭MT11 Enabled，候选应移除且正在居中的失效项回退Map。桌面PIP独立窗口打开/关闭后MT11应停流并约2秒后恢复，不得遗留旧receiver/sink。
   - 双视频全屏和叠加层：A8、MT11居中时分别双击进入/退出全屏；全屏期间工具栏、三视图PIP、WidgetLayer、罗盘条和母线告警均隐藏，退出后恢复。MT11普通显示继续验证fit/grid、Proximity Radar和Obstacle Distance；全屏不应出现普通Fly View工具层覆盖，Vehicle断链或MT11失效应退出MT11全屏。
   - 右栏与热成像：A8、MT11同时启用时选择器必须出现并可往返切换，切换控制栏不能隐式切换中心视频；两套缩放/拍照/录像保持同一UI。MT11选择下RGB/IR按钮位于拍照上方，抓包应看到0x11 `[02 00]`与 `[00 02]`往返；pending期间不重复发送，0x10/0x11合法反馈后才改变显示，2.5秒无确认时恢复可操作并报错。A8选择下不得显示热成像控件。
   - MT11协议与receiver隔离：抓包核对0x05/0x0A/0x0B/0x0C/0x0F/0x10/0x11/0x16/0x18、production sequence 0和小端CRC；原生IPv4与等价IPv4-mapped IPv6来源在源端口精确等于 `mt11SdkPort` 时应接受，来源IP不同或同IP但源端口不同，以及control、长度、CRC、过期/无匹配ACK错误时均不得推进状态，0x0B异步反馈除外；其中拍照0/1反馈只有本应用拍照pending时才计数/报错，外部控制器反馈不得污染LOCAL UI。保持MT11 0x05连续变焦且不发送用户停止事件时，60秒安全watchdog必须主动发送0停止并显示安全超时。A8和MT11分别开始/停止本地录像，确认只调用对应receiver；录像toggle等待0x0A/0x0B确认时修改MT11 Enabled/SDK Host/Port必须恢复旧值并提示稍后重试，不得切换endpoint或补发第二次toggle；命令确认后再修改时，应在旧endpoint停止已确认相机录像并结束本地会话。禁用MT11、改URL、cleanup和应用退出必须先收尾MT11 owned录像再释放第二receiver，不能停止A8录像或留下悬空Item。
8. 使用 `MNT_MODE_IN=Auto (0)` 验证顶部原生云台姿态栏自动接管：
   - 从未发生失权且QGC已拥有控制权时，Center/Tilt 90/Yaw Lock/Follow/Retract各点击一次只产生一次原生动作，不额外发送Configure；发生过失权但点击Center前状态已恢复为QGC持权时，不发送Configure，但仍必须出现预激活和最终Center两条命令。
   - 先用RC大幅移动云台使控制权切到RC，再分别点击Tilt 90、Yaw Lock/Follow和Retract；抓包应看到一次 `MAV_CMD_DO_GIMBAL_MANAGER_CONFIGURE`，状态确认QGC成为primary后只出现最后一次目标命令，全程无接管确认框。
   - 分别把RC前最后MAVLink目标设为 `0,0` 和非 `0,0`，再用RC移动后单独点击Center，两种场景都必须成功。需要预激活时，状态确认后先出现一条命令1000：body yaw为0，pitch在 `[-90°,0°]` 内、严格非0，并与钳制后的当时上报pitch相差1°；其ACK Accepted后约400 ms出现第二条同坐标系、同flags的命令1000，pitch/yaw必须为 `0,0`。记录第一条实际物理动作；遥测新鲜时额外俯仰通常约1°，遥测陈旧时允许更大但目标仍须在上述区间，第二条必须最终使云台居中。
   - 分别注入预激活ACK Accepted、Denied和无ACK：Accepted才允许第二条Center；Denied立即取消；无ACK由10秒事务超时取消。再分别给最终Center注入Accepted、Denied、Duplicate和无ACK：只有Accepted清除身份键；其余情况4秒内结束结果等待但保留标记，下一次Center仍必须先发预激活。两条命令不能在首条ACK前同时进入Vehicle pending列表。
   - 在等待状态期间快速点击 `Center -> Tilt 90 -> Retract`，Configure仍只能有一次，最终只能执行Retract；切换活动云台或Vehicle后旧动作不得发送到新对象。
   - 在“点击Center时QGC仍持权、下一事件循环复核前切到RC”的窄竞态中，必须补发且只补发一次Configure；不能直接发Center，也不能无Configure空等10秒。
   - 多Vehicle或多云台时，让云台A失权后切到B再切回A；A的预激活标记必须仍在，B的正常Center不能误清A的标记，只有A的最终Center收到Accepted ACK后才清除A对应身份键。
   - 阻断 `GIMBAL_MANAGER_STATUS` 超过10秒后恢复，迟到状态不得触发运动；持续推动RC时不得周期性出现Configure，松杆后由用户重新点击。
   - Point Home继续直发ROI且会取消旧pending；显式Acquire/Release保持原生按钮语义。私有UDP缩放/拍照/录像抓包不应因本测试出现额外数据。
9. Ubuntu 24.04 播放同一路 H.265 RTSP 保持正常；Ubuntu/虚拟机代理需将A8的 `192.168.144.25`和MT11的 `192.168.144.24`同时加入忽略列表。
10. Android 使用云台 H.264 编码回归测试，画面、延迟和断流重连均不退化。
11. Android 使用云台 H.265 编码连续播放至少 10 分钟，分别记录开始、5 分钟和 10 分钟的端到端延迟，确认延迟不持续增长；同时测试应用前后台切换和断流重连。
12. 真机日志中确认 `qgcandroidh265hwdec` 使用厂商 `amcviddec-*`，其输入 caps 为 `stream-format=byte-stream,alignment=au`，并出现首个 raw frame 的 `hardware confirmed` 日志；不能再用 rank 单独判断硬解是否成功。
13. H.265 播放期间开始和停止录像，确认录像文件仍可正常回放；这验证播放支路转换没有影响原生 `hvc1` 录像支路。
14. 在没有兼容 H.265 硬解的 Android 设备上，适配器不应注册，日志应告警未找到厂商 MediaCodec，原生软件 ranks 保持原值且不应直接黑屏。
15. Fuel遥测存在时顶部显示Fuel、无数据时隐藏；Proximity Radar在十方向任一距离Fact有效时显示，逐方向检查详情值/单位，4.99 m时图标变红闪烁、5.0 m及以上不告警，全部Fact为NaN时隐藏；它不得发送任何飞控命令。
16. 默认通信链路验收：`count=0` 时启动只生成一条 `local`，默认必须为 UDP、本地端口`14550`、单一远端 `192.168.144.125:14550`、不自动连接且非高延迟，残留在非活动Link0中的附加键不得混入新默认项。`count>0` 时分别构造改名为 `testlocal` 的唯一配置、重复local、仅Serial/TCP且没有local、本地端口`0/14590`、旧端点、`auto=true`、高延迟、附加键和旧 `defaultsVersion` 标记，启动前后所有配置和count必须完全不变，不得清理、去重、补建或迁移；删除local但仍有其他链路时不得补建，删除全部链路使count=0后重启才重新生成默认local。清除 `AutoConnect/autoConnectUDP` 保存键后启动，UDP自动连接开关必须可见且缺省关闭；预置为true后重启仍应保持true，用户在界面切换后应正常持久化。进行local重连验收时须关闭UDP AutoConnect，或确保其监听端口与local不同且方案已经验证；两个socket同绑14550即使成功也不得当作支持场景。最后在地面站IP和图传映射稳定的条件下抓包确认QGC出包源端口与远端回包目标，并完成主动连接/断开/重连及完整退出/启动各至少20轮。
17. Factory能力列表应声明PX4 + MultiRotor，APM不出现在支持列表中；同时用一个非多旋翼PX4 heartbeat确认当前边界：由于 `firmwarePluginForAutopilot()` 尚未检查 `vehicleType`，它仍会取得CustomFirmwarePlugin，不能把“支持列表只声明多旋翼”误当成运行时硬拒绝。
18. 普通模式只显示 Safety 设置页，高级模式显示完整定制 PX4 设置页。
19. 飞行模式仅 Loiter、RTL、Mission 可由该列表设置，RC RSSI 不显示，Fuel 紧随 Battery。
20. Android 冷启动前已插入飞控，以及 QGC 启动后再插入飞控，两种顺序均可自动连接；无权限时只请求一次，当前 attach 会话已有权限时不重复弹窗。
21. 同一根 USB 线不拔，QGC 主动断开/重新连接至少 20 次；不得出现 `Attempt to open unknown device` 或重复端口，每次 close 日志回到 `openResources=0`，下一次 open 为 `openResources=1`，driver 和 pending permission 数量不持续增长。
22. 保持 MAVLink 已连接时拔出/插回至少 20 次，并覆盖飞控 bootloader 到 application 的重枚举；每轮都先释放旧端口再创建新端口。
23. 拔出最后一个串口设备后再插入，旧 driver 不得残留；拒绝权限后拔插并改为允许，应能恢复枚举和连接。
24. QGC 前后台切换和 Activity 重建后 receiver 仍能收到新拔插事件；思翼内置视频 USB 与飞控同时存在时，只有串口设备进入 QGC 端口列表。
25. 先由思翼地面站或串口工具独占飞控端口，确认 QGC 明确记录 open 失败；关闭占用方后重新连接，QGC 无需杀进程即可成功。
26. 清除 `FlyView/showHeadingCompassBar` 保存键后启动，`Show Heading Compass Bar` 必须默认关闭；手动开启后在航向有效时立即显示，关闭后立即隐藏，重启QGC保持用户选择；没有活动飞行器或 `heading` 为NaN时不显示伪造的0°/N。
27. 使用模拟或真机航向覆盖 N、NE、E、SE、S、SW、W、NW，并重点检查 359° -> 0° -> 1° 连续过渡，中央数值、固定指针和移动方位必须一致。
28. 在地图主窗口、视频主窗口、地图/视频 PIP 互换、虚拟摇杆、右下仪表盘、Viewer3D、横竖屏和小屏布局下检查罗盘条底边位置、宽度及 `bottomEdgeCenterInset`；重点在目标遥控器 86% 缩放下把左下 PIP 从 10% 连续拖到 75%，以及改变右下仪表宽度，罗盘条必须保持示例的首选宽度，不能缩成点、短条或消失。常规尺寸下检查无不必要遮挡；极端放大的角落控件允许与罗盘条视觉层叠，但 PIP 调整和罗盘条区域的地图拖动、缩放必须仍然有效；全屏视频模式按原生语义隐藏。
29. Android H.265 连续播放测试期间同时保持罗盘条开启并改变航向，确认 11 个方位 Label 的更新不造成新增卡顿或持续帧率下降。
30. 在采用 14 pt 平台基准的目标 Android 遥控器上清除应用数据或净安装 APK，首次进入 Application Settings -> General 时 UI Scaling 应显示 86%，运行中的 `appFontPointSize` Fact 应为整数 12 pt。
31. 在 Android 上使用原生 `-`/`+` 修改整数点数并重启 QGC、覆盖安装保留数据的新 APK，必须保持用户值而不是恢复 86%；执行“清除全部设置”后才恢复 12 pt 缺省值。分别净安装 Ubuntu、Windows、macOS/iOS 构建，默认应保持原生 100%。
32. 物理宽度小于 120 mm 的极小 Android 设备单独确认平台基准和页面显示值；其 11 pt 基准无法用整数点数精确表示 86%，不得把固定 12 pt 一概描述为所有 Android 屏幕的 86%。

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

Android本地照片/录像与图库调试使用：

```bash
adb logcat -v threadtime | grep -Ei \
  "QGCCustomMedia-Custom|gcs.custom.android.medialibrary|Using app-private media staging|Queued durable public-media|Published durable public media|Public media publication failed|Failed to publish media|Removed stale pending|Deleted managed public video|Timed out publishing local media|Starting local camera-frame capture|Saved local camera frame|local video"
adb shell find /storage -type f \( -path '*/Pictures/Custom-QGroundControl/*' -o -path '*/Movies/Custom-QGroundControl/*' \) -name '*_local_*'
adb shell find /storage -type f -path '*/Android/data/org.mavlink.qgroundcontrol/files/Custom-QGroundControl/Staging/*' -name '*_local_*'
adb shell find /storage -type f -path '*/Android/media/org.mavlink.qgroundcontrol/Custom-QGroundControl/*' -name '*_local_*'
adb shell content query --uri content://media/external/images/media --projection _id:_display_name:relative_path:volume_name:is_pending:width:height:_size:mime_type | grep '_local_'
adb shell content query --uri content://media/external/video/media --projection _id:_display_name:relative_path:volume_name:is_pending:width:height:_size:mime_type:date_added | grep '_local_'
```

`Using app-private media staging directory`只证明APK选中了编码/封装暂存卷，不表示图库已保存。`Queued durable public-media publication`也只表示任务已进入单线程队列；只有 `Published durable public media: <source> -> content://... relativePath=Pictures/...` 或 `Movies/...` 才表示pending已清除、URI journal已提交且公共成品可见。API 25–28的对应成功日志为 `Published durable legacy public media`，必须同时看到非空URI。`Public media publication failed; preserving staging source`说明未完成公开发布，应在应用专属Staging中找到保留的源并于下次启动重试；MediaStore已有记录而厂商图库不显示MKV/MOV时，先改用MP4复测。上面第一条find查公共成品，第二条查当前安装暂存，第三条 `Android/media` 只用于核对V1覆盖升级迁移，不再是新文件的最终路径。V1和旧AppSettings文件同样记录 `Queued durable public-media publication`；只有公开成功后才删旧源，必须覆盖安装迁移版，若先卸载旧APK导致 `Android/data`/`Android/media` 文件已删则无法恢复。

运行日志出现 `GimbalCameraControl is not a type`、`GimbalZoomControl is not a type` 或 `Gimbal camera control failed to load`，首先检查 `custom.qrc` 是否同时注册 `QGroundControl/FlightDisplay/GimbalCameraControl.qml` 和 `GimbalZoomControl.qml`。顶层由 `FlyViewTopRightColumnLayout.qml`使用完整 `qrc:/Custom/qml/QGroundControl/FlightDisplay/GimbalCameraControl.qml`地址显式加载，缩放子控件再从同一资源目录解析；它们不加入原生FlightDisplay qmldir。新增QRC文件后必须重新构建资源，若仍命中旧缓存，应新建构建目录后重新configure，而不是修改 `src` qmldir。

Android启动日志出现 `Type FlyView unavailable`和 `DualPipView is not a type`时，说明运行包仍未包含或未导入 `Custom.FlightDisplay`模块：只把新QML加入 `custom.qrc`并不能把它注册成原生 `QGroundControl.FlightDisplay`中的类型。当前规范接线是在 `custom/CMakeLists.txt`创建并链接 `CustomFlightDisplayModule`，由Qt自动生成qmldir和 `/qml/Custom/FlightDisplay`资源，`FlyView.qml`再用限定模块名实例化 `DualPipView/MT11Video`。修改QML模块或CMake后必须重新configure并干净构建Android包；若改后出现 `module "Custom.FlightDisplay" is not installed`，先检查主目标是否链接 `CustomFlightDisplayModule`及生成目录的qmldir，不要把文件加入原生 `src/FlightDisplay/CMakeLists.txt`。原故障中随后出现的 `QObject::property -> QGCApplication::event`空指针是根窗口创建失败后的二次退出崩溃，不是MT11 SDK、RTSP或H.265解码故障。

RC控制后顶部Center仍在第一次点击弹确认框或无动作时，先确认APK/桌面程序已重新编译 `custom.qrc`，其中存在 `QGroundControl/Toolbar/GimbalIndicator.qml` alias；该URL由拦截器从原生 `qrc:/qml/QGroundControl/Toolbar/GimbalIndicator.qml` 重定向，旧资源缓存仍会运行原生逻辑。随后抓取 `MAV_CMD_DO_GIMBAL_MANAGER_CONFIGURE`、`GIMBAL_MANAGER_STATUS`、两条 `MAV_CMD_DO_GIMBAL_MANAGER_PITCHYAW` 及各自 `COMMAND_ACK`：Configure只有一次但10秒内始终没有状态确认，应检查MAVLink转发和Gimbal Manager状态上报；状态确认后立刻又回到RC或命令1000返回Denied，说明摇杆仍在持续产生输入，本实现按安全边界不循环争抢。需要预激活的Center应先出现一条body yaw为0、pitch为受限非零1°偏移的命令；若第一条仍等于钳制后的上报pitch或仍为 `0,0`，说明运行的还是上一版“当前姿态预激活”资源。第一条ACK Accepted约400 ms后才应出现 `0,0,NaN,NaN` 的真正Center；只有一条1000说明预激活ACK未匹配或超时。最终Center的ACK若不是Accepted，身份标记会保留供下次重试。两条均ACK Accepted但第二条仍无物理动作时，应检查 `MNT_MODE_OUT`、飞控到云台的下行MAVLink及厂商固件，不再归因于QGC按钮或控制权弹窗。

Gimbal Enabled但合并栏不显示时，不要检查飞控、云台回包或 `activeVehicle`：当前可见性已完全与连接状态解耦，只要Enabled为true就必须显示。优先检查设置值、custom QRC命中、Loader错误及资源是否重新构建。若A8控制栏显示但状态点持续灰色，再确认A8 Mini供电和网络、`sdkHost/sdkPort`、本机路由及2秒轮询；RTSP视频与私有UDP SDK是独立链路。A8 `SiyiSdk`接受逻辑等价的IPv4/IPv4-mapped IPv6来源且不强制回包源端口为37260，但要求来源逻辑IP、帧头、精确长度、CRC、control=0x02及业务payload全部合法。若MT11状态点持续灰色，则另检查 `mt11SdkHost/mt11SdkPort`及设备回包端点；MT11 `Mt11Sdk`同样接受等价IP表示，但回包源端口必须精确等于配置的 `mt11SdkPort`，同IP不同源端口的合法CRC帧也会被静默丢弃。

视频有画面但缩放按钮仍灰色时，要分别检查视频门控和卡录能力。直接路径应出现 `Installed main pulled-video resolution observers`，随后出现 negotiated/observed主拉流尺寸；当前只有1920×1080和1280×720属于会话白名单。视频门控成立后还必须持续收到合法0x20录像流参数，能力首次确认或发生变化时日志应出现 `Updated SIYI recording-stream capability`；仅有 `sdkResponding`、0x16或拉流尺寸都不能替代0x20。若超过4.5秒没有有效0x20，会出现 `recording-stream parameters timed out`并主动锁定缩放。若两路视频直接观察器均未报告，则检查约1秒后的 `stable VideoManager fallback`。能力确认后以0x18建立起始目标；之后0x0f本地发送成功即更新显示，不等待实际回读。

拉流尺寸白名单只有1280×720和1920×1080，但不再对应倍率。上限取卡录分辨率映射并受0x16较小值约束：4K=1.0、2K=3.5、1080P=5.5、720P=6.0。合法目标从1.0x按步长递增并追加有效精确上限；默认1.0x时2K为1/2/3/3.5，卡录1080P为1/2/3/4/5/5.5，卡录720P为1/2/3/4/5/6。1.0x时减号灰显、有效上限时加号灰显是正确边界；4K时两方向都灰显。

初始镜头1.0x却显示错误、缩放后数字不更新或缩放按钮始终灰色时，先区分“协议actual raw”和“UI当前目标倍率”。新版0x18 `01 00`表示1.0x、`01 08`表示1.8x；A8真机旧版会返回 `0A 00`表示1.0x、`10 00`表示1.6x、`14 00`表示2.0x。日志若持续出现 `Rejected invalid SIYI current zoom payload "0a 00"`，说明运行的仍是未加入兼容解析的旧构建。0x0f出站仍使用官方“整数byte+小数byte”，2.0x为 `02 00`；成功发送后UI立即显示2.0x，运动中的0x18 raw保存在独立实际值状态中。

目标2.0期间出现raw 1.6，表示设备报告了尚未到达目标的物理运动位置，不是2.0被误解码。由于UI显示的是当前目标，0x0f成功后应继续显示2.0；后续raw 1.8或其他中间值只更新内部actual，不能把中心数字退回1.0或改成任意小数。目标3.0、4.0、5.0或5.5时遵循同一规则；发送失败才保持原目标，取消后的旧hold目标不得稍后重放。

再次tap应立即看到新的绝对target发送日志，并且中心数字在本地发送成功后同步更新；不应出现方向队列、延迟派发或停止点击后继续发包。默认步长1.0x且卡录1080P时，正向必须为1→2→3→4→5→5.5，反向必须沿同一表5.5→5→4→3→2→1；卡录2K必须在3.5终止，卡录720P正反同表为1→2→3→4→5→6。改变拉流分辨率但不改变卡录分辨率，不得改变这张目标表。

hold应在按下420 ms后成立，普通路径只发送一次0x05方向命令，随后保持相机原生连续运动。显示目标档数按从最初按下开始的总时长 `qRound(totalMs / 600.0)` 计算；例如默认1.0x从1.0正向长按时，档数增加才依次显示2.0、3.0、4.0，任何回调或actual raw都不能让目标倒退，到卡录能力的有效端点必须立即停止。普通release调用 `stopZoom()`并完成最后一次时间计算；取消、移出、控件隐藏/销毁、应用后台、SDK能力失效或断流调用 `cancelZoom()`且不推进目标。活动0x05路径发送停止及一份有界安全重复，不补tap、不发送0x0f归整；断流重连和设置变化也不能复活归整。若成立时第一目标已经是端点，则只发送一次同方向端点0x0f，不启动0x05。

按下拍照后SD有反馈但找不到本地JPG时，先确认 `localMediaStorageEnabled=true`，再区分“无解码帧”“主视频渲染项未安装/尺寸为0”“Photo暂存目录不存在或不可写”“离屏grab返回空图”“JPEG编码/暂存原子提交失败”和“暂存已写入但公共MediaStore发布失败”；本地照片不会从相机SD下载，所以不能在思翼卡目录中寻找。日志 `Starting local camera-frame capture` 应同时给出output及来源、decoded/negotiated/VideoManager/Item尺寸、DPR、content和逻辑target；`Timed out waiting for the local camera-frame grab`表示仅等待ready阶段超过5秒，SD命令仍独立；有Starting、没有该超时但界面提示仍在处理，通常表示已进入没有独立超时的worker编码/写盘阶段；`Failed to save local camera frame`表示worker失败，`Saved local camera frame`给出raw grab、最终output和暂存文件字节，之后还必须继续观察 `Queued durable public-media publication` 与 `Published durable public media`。1920宽实体屏若仍得到约384×216，说明运行的仍是按20% PIP无参截图旧构建；若日志output已是卡录1920×1080但图库观感模糊，先核对MediaStore width/height、拉取公共JPG查看而不要用缩略图判断。卡录4K配1080P拉流时4K像素尺寸正确但不可能产生超过1080P源纹理的新细节。LOCAL成功而SD失败或无卡属于设计允许状态；无卡本身不会清除仍新鲜的0x20尺寸。Android暂存文件位于AppSettings所在卷的 `Android/data/org.mavlink.qgroundcontrol/files/Custom-QGroundControl/Staging/Photo`，公开成品位于对应MediaStore volume的 `Pictures/Custom-QGroundControl/`；选择遥控器本机可移动SD但该卡缺失/只读时，暂存选择和公共发布会记录卷回退。这与云台SD状态2没有关系。

按下录像后LOCAL一直黄色时，检查 `VideoManager::streaming()`、主非thermal receiver、启动完成回调、输出基名和3秒超时；格式无效应检查 `recordingFormat`枚举。若thermal也产生或被停止，说明运行版本仍调用全局VideoManager start/stop。LOCAL结束但图库迟迟不出现时，先确认已收到owned `recording=false`并看到 `Queued durable public-media publication`，再检查是否因剩余空间不足以同时容纳暂存源和公共目标而发布失败。Android容量清理后仍超过用户感知总量不一定是故障：自动上限只管理当前安装SharedPreferences注册、名称含 `_local_NNN`锚点的公共Movies URI，兼容provider同名后缀；重装前历史公共媒体、其他入口、thermal文件以及所有未公开Staging都不会被新安装静默删除，发布失败源会额外占用空间直到重试成功或用户处理。正常退出时还应观察录像3秒封装与最长120秒公共发布barrier的告警；强杀进程不经过这些保证，发布完成前直接卸载也不承诺保留暂存录像。

Proximity Radar不显示时，先检查活动Vehicle的 `distanceSensors` 十方向Fact是否至少一个非NaN，再确认 `QGroundControl/Toolbar/ProximityRadarIndicator.qml` QRC alias与 `Custom.Widgets` 中的详情页已进入构建。5.0 m是严格边界：只有 `< 5.0`告警；没有任何有效方向时隐藏是预期行为。

同分辨率断流重连应立即发起0x18查询，不能等2秒后才接受第一次操作；该查询不再打印应用逐包日志，需通过抓包或重新建立的倍率状态验证。紧邻应用退出出现的 `PhotoVideoControl.qml`中 `cameraManager/currentCameraInstance`空对象警告来自QGC原生相机面板销毁时序，不参与custom缩放状态机，也不是本次锁定原因。

`FlyViewCompassBar.qml` 同样不加入原生 FlightDisplay qmldir，而由 `FlyViewCustomLayer.qml` 使用 `qrc:/Custom/qml/QGroundControl/FlightDisplay/FlyViewCompassBar.qml` 显式加载。若设置开关存在但界面不显示，先检查 Application Messages 中的 `Fly View compass bar failed to load`，再确认 `custom.qrc` 已重新编译、存在活动飞行器且 `Vehicle.heading` 不是 NaN。

常用静态检查：

```powershell
rg --files custom
rg -n "DefaultCommunicationLinkInstaller|192\.168\.144\.125|14550|autoConnectUDP|adjustSettingMetaData|appFontPointSize|FlyViewCompassBar|ProximityRadar|localMediaStorageEnabled|GimbalMediaSessionPolicy|GimbalPhotoCapturePolicy|effectiveDevicePixelRatio|grabLogicalSize|localRecording|grabToImage|startRecording|stopRecording|GimbalIndicator|GimbalCameraControl|A8MiniZoomPolicy|Mt11Protocol|Mt11Sdk|Mt11ControlManager|DualVideoManager|mt11RtspUrl|MT11Video|DualPipView|thermal|takePhoto|toggleVideoRecording|CommandPhotoAndRecord|mediaStagingDirectory|existingMediaSourceDirectories|publishMediaFile|cleanupPublishedVideos|waitForPendingPublications|QGC_CUSTOM_ANDROID_MEDIA_LIBRARY_V2|IS_PENDING|sourceCleanupUris|getNoBackupFilesDir" custom
rg -n "CustomIconButton|CustomOnOffSwitch|CustomVehicleButton|CustomAttitudeWidget" custom
git diff --check
```

桌面测试构建启用 `QGC_BUILD_TESTING` 后，至少运行：

```powershell
cmake --build <desktop-build> --target check_siyi_protocol
cmake --build <desktop-build> --target check_mt11_protocol
cmake --build <desktop-build> --target check_gimbal_media_session_policy
cmake --build <desktop-build> --target check_gimbal_photo_capture_policy
cmake --build <desktop-build> --target CustomFlightDisplayModule
cmake --build <desktop-build> --target CustomFlightDisplayModule_qmllint
ctest --test-dir <desktop-build>/custom -R '^(SiyiProtocolTest|Mt11ProtocolTest|GimbalMediaSessionPolicyTest|GimbalPhotoCapturePolicyTest)$' --output-on-failure
```

截至2026-08-14，本轮已独立编译并运行 `Mt11ProtocolTest`，QtTest报告9 passed、0 failed：7个业务slot加框架init/cleanup，覆盖SDK文档命令帧、RGB/热成像模式帧、严格CRC/长度/control解析、UDP合包原子性和业务payload。该结果仅属于纯 `Mt11Protocol`，不覆盖 `Mt11Sdk`的真实UDP、Manager计时、QML、GStreamer、双receiver或真机固件。Android首次启动日志已定位到未注册的 `DualPipView`导致根QML创建失败，本轮已在custom中改为独立 `Custom.FlightDisplay`静态模块并限定名导入，同时移除两个重复QRC条目；CMake接线、QRC XML和关键引用已完成静态复核。当前开发环境仍未完成修复后全工程Qt 6桌面/Android干净构建，因此不能宣称全工程编译通过；PATH中的Qt 5.14 qmllint不能验证Qt 6 inline component或新模块，必须以项目Qt 6构建的 `CustomFlightDisplayModule_qmllint`、生成qmldir以及目标遥控器启动结果为准。当前两份TS均为16个context、140条message，英文模板140条保持unfinished，简体中文140条全部finished且unfinished/空译文均为0，Qt 5 `lrelease`验证通过。最终提交前仍建议用项目Qt 6 `lupdate`刷新location并再次复核目录，但不能再把当前翻译描述为待完成。A8+MT11双机地址/路由、真实ACK时序、RGB/热成像往返、双路长期解码/重连、三视图/全屏与叠加层、两套本地媒体receiver隔离、Android性能及媒体发布、SDK/RTSP各自故障和退出收尾仍须按本章矩阵在目标设备验收。
