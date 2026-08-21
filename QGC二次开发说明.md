# QGC 二次开发说明

适用工程：`F:\qgroundcontrol_viewer3d`

当前分支：`SecDev/ft/gimbal`

最后更新：2026-08-21

## 1. 当前开发进度

### 1.1 总体进度

当前开发分支为 `SecDev/ft/gimbal`，二次开发已形成十一个面向用户的功能模块和一套 `custom` 工程化集成架构：

1. Viewer3D 三维飞行视图。
2. 思翼 A8 Mini 云台控制与独立本地照片/录像。
3. RTSP 视频流集成与 Android H.264/H.265 低延迟硬件解码优先策略。
4. 飞行界面底部航向罗盘条。
5. Android 遥控器默认界面缩放。
6. Android USB 飞控连接。
7. Fuel 燃料状态与低电压告警。
8. 默认通信链路安装。
9. PX4 FirmwarePlugin/AutoPilotPlugin 定制。
10. Proximity Radar 距离传感器告警。
11. 通用 Video 1 + Video 2 独立双视频、Map/Video 1/Video 2 三视图切换，以及 A8 Mini + UniPod MT11 双相机控制。

各模块当前所处阶段如下：

- **已集成**：Viewer3D、思翼云台、Fuel、Proximity Radar、默认通信链路和 PX4 定制均已接入 `custom` 构建、资源及运行链路。
- **代码已集成，待目标遥控器真机回归验收**：本轮A8 Mini缩放即时单击/原生连续长按状态机、SD卡与本地媒体双支路、UniPod MT11私有SDK、通用独立第二路RTSP、Map/Video 1/Video 2三视图、双相机右栏、底部航向罗盘条、Android 86%界面缩放缺省值、Android H.264/H.265厂商MediaCodec优先策略和Android USB串口管理器已经进入当前工作树。H.264只提升经过 `androidmedia` 插件与厂商名称筛选且原生接受 `avc` 的decoder；H.265兼容原生 `hvc1` 的厂商decoder可直接提rank，只有H.265 Annex-B厂商decoder需要独立adapter。软件decoder rank均保留。双路是否能各自创建并持续驱动独立MediaCodec实例，仍须按第12章在目标设备验证，不能仅凭协议测试、静态接线、候选rank或adapter注册判定整机验收通过。
- **已有实测基础与最新日志边界**：Ubuntu 24.04 下 A8 Mini 云台控制及 H.265 RTSP 播放曾正常；Android 下同一云台使用 H.264 编码曾正常。MT11 `rtsp://192.168.144.24:8554/video1` 的SDP已由外部工具确认为标准RTSP、H.265 Main、1920×1080、30 fps，用户也已在官方QGC验证同一URL可播放。14:28 Desktop附件中MT11 16次start、16次均未到SETUP；0e44随后证明同机GStreamer/GIO会把私网RTSP误送到 `192.168.163.1:7897` 系统代理。最新295d Desktop附件是在Ubuntu系统代理仍开启的条件下运行，但进程先打印GIO direct resolver；MT11 URL 2从标准OPTIONS依次进入DESCRIBE、SETUP、PLAY，随后取得source媒体首帧、实例化 `libav/avdec_h265`、输出1920×1080 I420 25 fps解码首帧并到达sink，证明本轮Desktop单路MT11画面已恢复，也把系统代理误路由闭环为这次黑屏的程序根因。该次A8未连接且持续失败；MT11在60.986秒因21秒无帧被watchdog重启，之后连接失败。因此它不是A8+MT11双路、长期断流重连或Android双MediaCodec验收，后者仍须目标遥控器实测。
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

### 1.3.1 通用双视频与 A8 Mini + UniPod MT11 双相机控制（代码已集成，待双机真机验收）

- MT11 SDK V0.1.0已在 `custom/src/Gimbal` 转换为 `Mt11Protocol`、`Mt11Sdk` 和 `Mt11ControlManager` 三层：Protocol只做严格帧/payload编解码，Sdk持有独立UDP socket并校验来源IP、ACK和1.5秒命令窗口，Manager负责2秒常规轮询、缩放、拍照、录像、热成像以及当前产品映射下 Video 2 的本地媒体协调。新增通用 `ZoomStepPolicy` 统一十分之一倍率、1.0x最小值锚定网格和精确末端档位，`Mt11ZoomPolicy`在其上实现MT11的显示目标、短按协议域和反馈驱动长按推进；A8只把共用网格接口薄包装到该通用策略，同时保留自己的卡录分辨率能力与按时长hold策略。连续变倍活动期间另以350 ms轮询0x18实测倍率；0x16能力和0x18位置各有独立6.5秒新鲜度门禁。由于production sequence固定为0，0x05停止与方向ACK不能可靠关联到具体代次，其内嵌倍率不再推进UI或边界；只有主动查询得到的0x18是权威位置来源。两套Manager并存，MT11不使用A8的卡录分辨率倍率映射。
- MT11帧使用 `55 66`头、control、payload length LE、production请求固定sequence 0、command、payload和末尾CRC16 LE；CRC多项式为 `0x1021`、初值为0。当前接入命令为0x05手动变倍/停止、0x0A相机系统状态、0x0B异步功能反馈、0x0C拍照/录像切换、0x0F绝对倍率、0x10查询视频模式、0x11设置视频模式、0x16最大倍率和0x18当前倍率。SDK PDF规定0x0F绝对命令只覆盖1.0～30.0x；0x05倍率反馈为十分之一倍率的 `uint16 LE`，0x16/0x18则为“整数byte + 一位小数byte”。因此0x16/0x18 payload `a5 01`是合法165.1x，不是非法165.1样式值。Protocol/Sdk线格式层允许反馈解析至255.9x，Manager再按当前MT11产品策略把可操作混合变倍上限封顶165.1x（标称约165x）。0x10/0x11以主可见光+副热成像 `[0,2]` 和主热成像+副可见光 `[2,0]`实现RGB/热成像往返。
- MT11短按和长按使用不同命令域，实测倍率始终保存在私有状态，QML的 `currentZoom`只发布从1.0x锚定、且不超过物理上限的完整步长档位。MT11不采用A8“追加精确非整步末端”的例外：默认步长1.0x且设备/产品物理上限为165.1x时，165.1x只供Manager边界判断，UI最高显示165.0x；步长0.1x时165.1x本身才是合法显示档位。短按按独立 `mt11ZoomStep` 从上一已显示目标计算0x0F目标，私有实测倍率和命令目标都必须位于1.0～30.0x；发送成功即显示新目标，快速连点直接替换上一在途目标，不按 `actual + step`重新建立网格。生产帧固定sequence且ACK只标识command，无法可靠判断reject属于哪次点击，因此所有0x0F reject一律只触发0x18查询、不直接回滚当前显示目标；只有0x18精确命中最新目标或独立10秒确认超时才结束pending。默认步长1.0x，设置页与A8步长并排但不共享Fact。实测倍率大于30.0x时，两方向tap availability都关闭；QML短点会安静消费且不调用Manager，Manager仍对直接调用作防御性拒绝并提示仅支持长按，避免0x0F把混合倍率突然拉回30x以内。长按420 ms成立后使用0x05原生连续运动，可在1.0x到 `min(设备0x16上限, 165.1x)` 之间双向运行；活动期间以350 ms查询0x18，只有实测反馈到达或越过下一合法显示档位才推进中心数字，到设备物理边界或60秒安全watchdog时停止。若前一次0x0F短按还在pending，新长按不再被该pending锁住：Manager先发一次0x05 `0`退出绝对控制器，等待150 ms后发送0x05方向；指针仍按住时再等待150 ms仅重发一次同方向安全副本。该交接保留最新短按合法目标作为显示起点，不会被较旧的0x18样本拉回；release/cancel会取消尚未发出的方向/副本并立即发送停止，不会在松手后迟到启动。普通长按停止仍先立即发送0x05 `0`，150 ms后只发送一次安全重复并查询最终0x18；在该停止序列之后的有效0x18 settled反馈到达前tap/hold继续锁定且保留上次合法显示档位，不发送A8式时间目标或释放后0x0F归整。Manager分别暴露tap/hold方向可用性；共享QML在hold成立时先消费手势，即使启动失败，release也不会补发一次tap。
- `mt11ZoomStep=1.0x`表示绝对目标严格沿1、2、3、4…线性增加，不表示每一档的画面体感相等。在同一成像链路中，从 `z`到 `z+1`的线性尺寸比为 `(z+1)/z`：1→2是2.0倍（100%增量）、2→3是1.5倍（50%）、3→4约1.33倍（33%）、4→5是1.25倍（25%），因此越往后固定 `+1.0x`看起来越小是数学上的必然结果，不是QGC改了步长。《UniPod MT11 v1.2》p16另列出广角4.5 mm/等效24 mm和变焦镜头15～50 mm/等效81～270 mm，`15/4.5 ≈ 3.33`；p37的8K拍照逻辑也以3.3x和11x分段。由此可推断设备双镜头焦距段在约3.3x附近还会形成一个额外体感拐点，但这是设备光学/成像链路的手册参数推断，不是SDK重映射或程序在4x后换了步长。
- `VideoCustomSettings` 只把第二路地址作为通用 `[Video]/secondaryRtspUrl` Fact，与原生 `[Video]/rtspUrl` 在 Application Settings -> Video -> Connection 中分别标记为 `RTSP URL 1` 和 `RTSP URL 2`。产品新安装默认值分别为 URL 1 `rtsp://192.168.144.25:8554/main.264`、URL 2 `rtsp://192.168.144.24:8554/video1`。两路RTSP都固定使用QGC/GStreamer原生Auto：程序不设置 `rtspsrc.protocols`，由GStreamer自行协商UDP、TCP等允许的下层传输；Auto并不等于禁用TCP。已发布版本可能在QSettings中留下 `[Video]/primaryRtspTcpOnly` 和 `[Video]/secondaryRtspTcpOnly`，当前程序不再注册、读取、迁移或删除这两个旧键，使它们对本版运行无影响，同时保留降级到旧版时的用户值。为兼容已发布版本，仅当新URL键不存在时读取旧 `[GimbalControl]/mt11RtspUrl`：旧值精确等于历史出厂默认 `rtsp://192.168.144.25:8554/video1` 时写入新 `.24/video1`，其他自定义值及空字符串原样复制，旧键不删除。
- `DualVideoManager`为 Video 2 单独创建 `VideoReceiver`、QML视频背景Item、原生视频sink和重启/停止状态，不复用 Video 1 receiver，因此两路独立并行拉流与解码，不存在两路帧同步前置条件。Loader就绪或 `Window.window` 变化后直接把实际 `secondaryVideoContent` Item和当前window交给Manager，不再仅依赖根window的 `findChild()`；Item换代时才有序释放并重建receiver。动态创建的 `QGCVideoBackground` 会持续留在场景图中；Manager经过 `QQuickWindow::BeforeSynchronizingStage` 后还必须确认其真实 `itemInitialized=true`，再启动receiver，避免qml6glsink在OpenGL上下文建立前进入READY。Manager同时处理 `onStartDecodingComplete`，并在RTSP已streaming但规定时间内没有首个解码帧时完整停止/重启，避免永久停在WAITING。URL为空时禁用 Video 2；重复源判断同时覆盖配置URL 1、主receiver当前URI、本轮冻结的starting URI、成功start后的active URI和stop后的releasing URI。主流stop完成或receiver销毁后，active URI仍以精确Timer保留至少1000 ms；同URI新主流成功start会取消待清除Timer。这样从Video 1向Video 2交接同一endpoint时，旧主管线释放前不会先打开第二receiver。cleanup进入永久终止态并断开主receiver连接、停止Timer，退出过程中不会重新创建第二receiver。用户修改非空URL时stop/start同一receiver以重建该路GStreamer管线，不销毁/新建receiver；第二路不再读取或维护自定义RTSP传输偏好。
- custom同路径覆盖 `FlyView.qml`并引入 `DualPipView.qml`、`FlyViewSecondaryVideo.qml` 和 `FlightDisplayViewSecondaryVideo.qml`。新增类型位于与原生一致的 `custom/src/FlightDisplay`，由独立静态模块 `Custom.FlightDisplay`注册；不修改原生 `QGroundControl.FlightDisplay`模块。Map、Video 1、Video 2各自保留原生 `PipState` 的 full/pip/window 语义；左下角是固定下槽和上槽，点击哪个槽，该视图进入主视图，原主视图精确回到被点击的同一槽位，未点击槽不移动。点击层位于重挂的视频/地图Item之上，切换后仍可继续点击。
- 两个缩略框均复用原生 `PipView.qml` 的左下布局、默认宽度比例、显示/隐藏、独立窗口和右上拖拽缩放交互；Video 1 继续使用原生 `FlyViewVideo`，Video 2 的wrapper/surface按原生 `FlyViewVideo.qml` 和 `FlightDisplayViewVideo.qml` 拆分，并共享Video设置中的fit/grid、无视频占位、Proximity Radar和Obstacle Distance叠加。任一视频主视图双击可进入/退出全屏，全屏时统一隐藏toolbar、双PIP、WidgetLayer和custom overlay。
- Fly View右侧栏在A8和MT11都启用时显示 `A8 Mini / MT11`切换按钮，只启用一路时自动归一到该后端。两栏共用 `GimbalCameraControl.qml`的缩放、拍照、录像、SD/LOCAL徽标和触控尺寸；`MT11CameraControl.qml`只负责注入MT11 Manager并在拍照上方开启热成像按钮。热成像按钮在0x10确认模式前不可用，点击后以0x11发送相反模式，匹配回包前保持pending，超时后重新查询而不以本地乐观值冒充切换成功。
- MT11设备IP已由 `192.168.144.25`改为 `192.168.144.24`，因此程序默认把SDK控制endpoint同步为 `192.168.144.24:37260`，而通用 Video 2 的新安装缺省URL为 `rtsp://192.168.144.24:8554/video1`。SDK和RTSP仍使用独立Fact、socket和故障状态：默认产品配置将Video 1本地媒体receiver映射给A8 Manager、Video 2映射给MT11 Manager，但视频布局和URL名称本身不再绑定设备型号。SDK在线不能证明RTSP可解码，RTSP有画面也不能证明相机命令可用。
- 当前代码已完成扩展后的两套独立QtTest编译运行：`Mt11ProtocolTest`为11 passed、0 failed（9个业务槽加init/cleanup），`SiyiProtocolTest`为42 passed、0 failed（40个业务槽加init/cleanup）；`Mt11Sdk.cc`也已独立编译通过。MT11测试除SDK文档命令帧、热成像帧、严格CRC/长度/control、UDP多帧原子性、相机/功能反馈和165.1/255.9线边界外，还覆盖通用网格的精确maximum能力、MT11完整步长显示上限、1～30x短按目标、30x以上双向tap拒绝、raw反馈对齐、长按仅越过合法档位才推进显示，以及“在途短按目标作为长按交接显示起点”和“实测位置/最新目标联合决定hold方向可用性”；默认1.0步长明确拒绝把物理165.1发布为显示目标，0.1步长则允许165.1。A8测试继续覆盖协议、能力映射、通用网格薄包装和既有hold策略。该结果只证明纯Protocol/Policy与独立Sdk编译范围，不覆盖Manager的150 ms交接/副本时序、完整QGC Qt 6构建或真机控制。295d中的 `Rejected invalid MT11 ACK 16 "a5 01"` 属于修正前旧构建：当前代码会合法解析165.1x并由Manager接受为产品上限。MT11真实固件下的短按→长按交接、边界停止、RGB/热成像往返、双receiver长期播放、双机网络、本地媒体和Android性能仍待目标设备验证。

### 1.4 RTSP 与 Android H.264/H.265 视频链路（代码已集成，待真机验收）

- 为 A8 Mini 安装 RTSP 默认地址 `rtsp://192.168.144.25:8554/main.264`、20 秒超时和 Android 低延迟默认值；实际 H.264/H.265 编码类型仍由 RTSP SDP 协商确定，不由 URL 后缀强制指定。
- MT11实机SDP确认 `video1` 为H.265 Main、1920×1080、30 fps。11:58附件先把此前笼统的RTSP控制/读取EOF定位到初始OPTIONS；14:28附件进一步证明跳过OPTIONS后能够真正外发DESCRIBE，但当时16轮仍未到SETUP。295d在GIO direct resolver生效后，MT11使用标准兼容级别0即依次完成OPTIONS、DESCRIBE、SETUP、PLAY及完整首帧显示，不需要触发basic-header或skip-OPTIONS，证明先前握手失败并非已证实的MT11 OPTIONS/头部不兼容。Connection组现在只配置两路URL，不再提供RTSP传输开关；两个receiver都不写 `rtspsrc.protocols`，固定保留原生Auto协商。具体会话仍可能由GStreamer在SETUP时选中interleaved TCP，这属于Auto协商结果，不是应用强制TCP，也不改变URI、SDP、编码格式、RTSP请求兼容状态或解码策略。
- 0e44附件证明系统代理是必须先消除的根因：GStreamer 1.24.2的 `rtspsrc` 对私网A8 URL报“无法连接到代理服务器192.168.163.1”，`strace`实际connect为 `192.168.163.1:7897`；同一时刻 `ffprobe`直接连接 `192.168.144.25:8554`并取得LIVE555 SDP与H.265帧。`QGC_GST_STREAMING`构建下，`CustomPlugin`构造函数在VideoManager/GStreamer初始化及GIO默认resolver创建之前检查 `QGC_GST_USE_SYSTEM_PROXY`；只有其值忽略大小写和首尾空白后为 `1`、`true`、`yes` 或 `on` 时才保留environment/system策略，否则设置 `GIO_USE_PROXY_RESOLVER=dummy`并打印 `GStreamer GIO proxy policy: direct resolver "dummy"`。该resolver是进程级选择，会使其后所有GIO/GStreamer网络源（包括未来可能接入的HTTP/HLS源）默认直连；确需这些源使用系统代理时必须通过上述truthy opt-in恢复。QtNetwork不使用该GIO resolver，因此QGC地图、下载等QtNetwork流量仍遵循其原有代理配置。
- `GstVideoReceiver` 对RTSP的设置超时值和运行时watchdog值分开处理：设置Fact不被回写，原生Auto管线的实际超时下限为8秒。RTSP source始终不设置 `rtspsrc.protocols`，不再存在应用层TCP首轮、5秒TCP专用下限或TCP到Auto回退状态；source EOS、timeout和资源错误统一进入既有停止与退避重启路径。
- `GstVideoReceiver` 以成功启动代次记录stop completion：每个成功start最多完成一次stop；即使URI已清空也会释放旧pipeline，重复stop在没有活动pipeline和待完成代次时被忽略，从而避免同一次停止重复启动重连Timer。通用 `VideoManager` 为原生主/thermal receiver保存唯一生命周期状态，冷启动不再由初始化和render job各发一次start；异常停止后的RTSP重试改为1/2/4/8/15秒有上限退避。每个延迟回调携带generation，URI、期望运行状态或新启动代次变化后，旧generation不得再次启动管线；只有首个解码/sink帧成立时才清零退避，单有source pad但无媒体buffer不会把每轮重试重置回1秒。Video 2继续使用自己的1～15秒有上限退避；11:58日志发现被动的主receiver active/releasing通知会绕过该Timer，14:28附件中的时间序列证明门禁修正已进入实际二进制。当前只由URI、启用状态或重复源判定等真实配置变化取消旧延迟；被动通知保留原deadline。为减少正常离线设备的控制台噪声，VideoManager调度、Video 2重试/剩余时间、主路handoff和一般生命周期过程日志已删除，后续回归以抓取的连接时间、状态和画面结果验证，不再要求应用日志逐次打印Timer。
- OPTIONS级别0保持 `rtspsrc` 标准请求；精确的首帧前OPTIONS EOF使同一URI下一轮进入级别1，设置 `short-header=true`并仍然外发OPTIONS；同样EOF再进入级别2，由 `before-send` 抑制OPTIONS、关闭RTSP keepalive并继续正常DESCRIBE。持久级别与每轮active快照分离，compare-exchange保证同一attempt的重复bus error最多推进一次；状态跟随URI，URI变化或转为非RTSP时复位。逐attempt启动、source配置、逐method/header计数、PAUSE/TEARDOWN以及skip-OPTIONS过程日志现已删除；协议深查使用GStreamer `GST_DEBUG`、`strace`和pcap/Wireshark，不依赖常规Application Messages。
- 上述所有兼容级别均保留 `rtspsrc` 原生 `udp-reconnect=true`，避免回归成功UDP媒体会话后的RTSP控制连接恢复；级别1的一次失败attempt仍可能由GStreamer内部重发一次请求。GIO默认直连是本轮有系统调用证据支持的首要修复；basic-header与skip-OPTIONS只是直连后仍精确出现OPTIONS EOF时的有界次级兜底。14:28所有Real扩展计数为0，不能再称Real扩展、某个User-Agent值或某一具体header字节为已证明根因。
- `before-send` 仍在内部更新 `lastRtspMethod`、抑制teardown期间的PAUSE并在兼容级别2跳过OPTIONS，但不再逐请求输出URI、method、header计数或抑制结果。停止进入NULL期间TEARDOWN仍允许发送并受1秒 `teardown-timeout` 约束；teardown标志从请求NULL起一直保持到下一轮start，pipeline引用清除后的迟到PAUSE也必须被抑制。对RTSP(S) URI、消息源factory为 `rtspsrc` 且domain为 `GST_RESOURCE_ERROR` 的bus error，`(error code, lastRtspMethod)`与上一条已报告签名不同时输出warning，紧接着重复同一签名降为debug。URI变化、切到非RTSP或重新收到source媒体帧时复位。其他GStreamer error继续输出critical。两类结构化错误都保留 `uri/source/domain/code/lastRtspMethod/message/debug`。
- 295d日志在系统代理开启时先确认进程使用direct resolver，随后MT11依次到SETUP、PLAY、source媒体首帧、decoder首输出和sink首帧，已完成本轮Desktop单路播放恢复闭环。该日志仍未用 `strace`直接记录peer，但结合direct resolver启动时序、请求URI及完整媒体链，足以确认程序修正有效；它不覆盖A8同时在线、60秒后的持续播放/重连、Android硬解或双路性能。常规日志现在只保留proxy策略、首个source媒体帧、实际decoder实例、decoder首输出和sink首帧等低频里程碑，以及必要warning/critical。
- 支持手动视频源与 MAVLink 相机流信息两种接入方式；`Use MAVLink automatic video stream` 和沿用旧Fact键 `forceAndroidH265HardwareDecoder` 的 `Prefer Android H.264/H.265 hardware decoding` 已统一放入 Application Settings -> Video -> Video Stream Integration，并在所有平台显示。
- Android策略只优先经过 `androidmedia` 插件归属与厂商名称筛选的MediaCodec候选。H.264厂商decoder原生接受 `avc`、或H.265厂商decoder原生接受 `hvc1` 时，direct候选都提升到 `GST_RANK_PRIMARY + 3`；H.264不注册格式适配器。
- H.265兼容adapter仍注册为回退候选，rank为 `GST_RANK_PRIMARY + 2`。它接收播放支路的 `hvc1`，通过GStreamer `h265parse`转换为 `video/x-h265,stream-format=byte-stream,alignment=au` 后送入只接受Annex-B的厂商MediaCodec；因此direct hvc1优先于adapter，二者都高于原生Force Software的 `GST_RANK_PRIMARY + 1`。
- 排除 Google/Android/Goldfish、secure、软件、FFmpeg 及厂商软件变体，优先选择厂商单独提供的 `lowlatency`/`low_latency` 解码组件。
- 硬解输出队列采用 downstream-leaky 且最多保留 2 帧，显示端反压时主动丢弃旧帧，防止延迟随播放时间持续累积。
- decodebin记录每个实际decoder的创建及其src首个buffer，字段包括receiver、element name、URI、plugin/factory、instance和caps；还需看到对应sink首帧，才能证明该receiver从实际decoder输出走通到显示端。候选rank或adapter的 `vendor MediaCodec candidate` 首帧字样都不是系统级硬件确认；必要时仍以Android API 29 `isHardwareAccelerated()`核实硬件属性。
- 没有兼容厂商硬解时，H.265适配器不注册，且H.264/H.265原生软件decoder rank始终保留为安全回退，避免设备直接黑屏；因此该开关表示优先兼容候选，不是对所有设备无条件保证硬解。
- 非Android平台不受该策略影响；H.265播放支路的格式转换不修改原生 `hvc1` 录像支路。

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

- 二次开发主体位于 `custom`，目录和命名参照 `src` 模块树；当前共 143 个文件。
- 仅保留 `src/CMakeLists.txt`、`src/Vehicle/VehicleSetup/VehicleSummary.qml` 两处feature必需例外，`VideoManager.h/.cc` 的通用串行生命周期与退避，`VideoReceiver.h`/`GstVideoReceiver.h/.cc`/`QtMultimediaReceiver.cc` 的通用启动URI快照、OPTIONS EOF兼容、teardown与解码诊断扩展，`QGCLogging.cc` 的通用日志级别过滤修复，以及 `SimulatedCameraControl.cc` 对原生VideoManager通知信号名的修正；GIO直连策略位于 `custom/src/CustomPlugin.cc`，其余功能通过custom C++、QRC、独立custom QML模块、QML URL拦截和Android overlay接入。RTSP始终不设置 `rtspsrc.protocols`，完整使用原生Auto协商，不保留应用层传输枚举、强制TCP或TCP到Auto回退；请求兼容状态只依据通用RTSP错误、method与URI推进，不引入相机型号、产品IP或custom设置依赖。
- General、Fly View 和 Video 设置页以及顶部 `GimbalIndicator.qml` 均按原生文件树使用同路径 custom 覆盖；Viewer3D、Gimbal、视频链路和航向罗盘条参数使用稳定 Fact/QSettings 分组持久化。Gimbal设置组位于Fly View的Instrument Panel正下方、Viewer3D之前，不依赖云台在线状态。此前 `FlyViewSettings.qml` 外层Loader加载的组件根节点又是一个条件Loader，设置对象初始化瞬间内层 `active=false`会使 `item=null/implicitHeight=0`，并经外层 `Layout.preferredHeight/minimumHeight` 把整组静默折叠；这是“仪表板下方完全空白”的根因，不是排序或QRC缺文件。现在只保留Fly View一层Loader：父页缓存 `gimbalControlSettings`，使用 `Qt.resolvedUrl()` 加载同目录组件并在 `onLoaded` 注入设置对象；`GimbalControlSettingsGroup.qml` 根节点直接为 `ColumnLayout`，不再二次决定可见性或高度。组内仍先显示桌面并排/窄屏堆叠的A8 Mini与MT11 Zoom Step，再显示两套SDK设置。General 页面继续绑定原生 `appFontPointSize`，Android 缺省值由 custom metadata hook 调整。
- Android 构建先在构建目录合并原生模板和 `custom/android` overlay，再只编译合并后的唯一 Java 源；合并时排除 `.gradle`、`build` 和 `local.properties`，并仅在生成副本中关闭 Gradle configuration cache，避免跨构建残留的AGP插桩状态阻断APK打包。
- 与 `src/Viewer3D` 完全相同的 C++、QML、qmldir 和 shader 由构建或 QRC 直接复用，不在 custom 保存重复副本；外部 WGS84 城镇样例只是源码树手动测试资产，不参与构建或 QRC 打包。
- 只从 `custom-example` 引入底部航向罗盘条；不引入其未使用的示例控件、自定义动作、圆形罗盘、姿态仪、品牌资源和全局配色，也不保存无必要的 `AppSettings.qml` 根页副本。
- custom 翻译加载、简体中文目录和 `lupdate` 更新脚本已经接入；视频层文案保留通用 Video 1/Video 2、第二路独立receiver/留空禁用提示，以及沿用旧Fact键但显示为Android H.264/H.265硬解优先的开关。两路RTSP-over-TCP开关及其元数据说明已经从英文模板和简体中文目录同时删除；原生Auto属于程序行为与开发约束，不额外作为设置页文案。两份TS的最终统计与完成状态见第4.12节。
- 原生 `translations/qgc_json_zh_CN.ts` 另有一处受控翻译数据修正：按元数据注释改用ASCII逗号分隔 `ChibiOS,NuttX`，并把 `apmVehicleType` 精确保持为五项 `多旋翼,直升机,固定翼,地面车辆,水下航行器`。旧译文只有四个中文逗号分隔片段，和英文五项enum不等长，导致295d中的FactMetaData enum mismatch；该修正不改变custom TS的context/source对应关系。

## 2. 开发边界

1. 二次开发业务仍位于 `custom`。`src` 只允许已登记的受控例外：`src/CMakeLists.txt`、`src/Vehicle/VehicleSetup/VehicleSummary.qml`；为所有原生VideoReceiver提供唯一start/stop代次、generation取消和有上限RTSP退避的 `src/VideoManager/VideoManager.h`、`VideoManager.cc`；提供冻结启动URI的 `onStartAttempt`、RTSP安全超时、同URI OPTIONS EOF两级兼容、stop代次去重、内部before-send方法跟踪及实际decoder输出诊断的 `src/VideoManager/VideoReceiver/VideoReceiver.h`、`GStreamer/GstVideoReceiver.h`、`GStreamer/GstVideoReceiver.cc`、`QtMultimedia/QtMultimediaReceiver.cc`；按实际 `QtMsgType` 保留info/warning/critical的 `src/Utilities/QGCLogging.cc`；以及把模拟相机错误连接的 `VideoManager::hasVideo` getter改为真实通知信号 `hasVideoChanged` 的 `src/Camera/SimulatedCameraControl.cc`。这些改动保持通用，不识别A8/MT11或产品地址；RTSP不写 `rtspsrc.protocols`，固定使用原生Auto协商；URL分路、产品默认值和设置UI仍全部在 `custom`。未登记的新 `src` 改动不允许并入。
2. custom 新增代码按 QGC 模块放置，例如 `FlightDisplay`、`FlightMap/Images`、`Settings`、`Gimbal`、`Comms`、`QmlControls`、`UI/AppSettings`、`VideoManager/VideoReceiver/GStreamer`；Android Java 同名覆盖按根目录 `android` 的文件树放在 `custom/android`。
3. Application Settings 的 General、Fly View、Video 页面和顶部工具栏 `GimbalIndicator.qml` 由项目在 custom 显式接管并保存同名覆盖；其他没有差异、也不需要项目接管的 QML 继续使用 `src`。
4. 与 `src/Viewer3D` 相同的公共实现由 `custom/CMakeLists.txt` 或 `custom.qrc` 直接引用，不在 custom 保存副本。
5. custom同名 QML 覆盖使用 `/Custom/qml` 前缀；新增的双视频复合类型由 `Custom.FlightDisplay`模块生成到 `/qml/Custom/FlightDisplay`；Viewer3D 独立模块仍使用 `/qml/Viewer3D`。
6. 设置 Fact 名和 QSettings 分组保持稳定，升级程序不会丢失已有 Viewer3D、Gimbal、Fly View 航向罗盘条和链路设置。
7. 复杂协议、坐标转换和跨模块行为使用中文注释；普通布局和赋值不增加无意义注释。
8. Android 构建先在构建目录合并原生 `android` 模板和 `custom/android` overlay，Gradle 只编译合并结果；不把两个 Java 源目录同时加入 source set，避免同包同类冲突。源码树和Git均不得保存 `.gradle`生成缓存；custom构建关闭configuration cache但保留普通build cache。
9. 根目录 `translations/qgc_json_zh_CN.ts` 仅保留本次已登记的元数据枚举翻译修正；枚举项必须使用ASCII逗号并与source项数一一对应。除该项外，项目新增或覆盖文案继续进入 `custom/translations`，不得借翻译修正扩大原生目录改动范围。

## 3. custom 完整目录结构

当前共 143 个文件：

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
      FlyViewSecondaryVideo.qml
      FlightDisplayViewSecondaryVideo.qml
    FlightMap/
      Images/compassPointer.svg
    Gimbal/
      A8MiniZoomPolicy.h
      A8MiniZoomPolicy.cc
      ZoomStepPolicy.h
      ZoomStepPolicy.cc
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
      Mt11ZoomPolicy.h
      Mt11ZoomPolicy.cc
    QmlControls/
      FuelStatusIndicatorPage.qml
      ProximityRadarIndicatorPage.qml
      Viewer3D/Models3D/qmldir
    Settings/
      FlyViewCustom.SettingsGroup.json
      FlyViewCustomSettings.h
      FlyViewCustomSettings.cc
      VideoCustom.SettingsGroup.json
      VideoCustomSettings.h
      VideoCustomSettings.cc
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
| `custom/CMakeLists.txt` | custom 构建总入口。向根工程注入 `QGC_CUSTOM_BUILD`、`CUSTOMHEADER=CustomPlugin.h` 和 `CUSTOMCLASS=CustomPlugin`，收集 AutoPilot/Firmware、Viewer3D、Gimbal、Comms、Settings、VideoManager、Android媒体库桥、GStreamer拉流尺寸探针和Android H.265等 custom C++，以及15个明确复用的原生 Viewer3D C++ 文件，并向根目标导出 include、library、resource 和 translation 列表；创建包含 Fuel 与 Proximity Radar 详情页的 `Custom.Widgets` 静态 QML 模块，以及包含 `DualPipView`、`FlyViewSecondaryVideo` 和 `FlightDisplayViewSecondaryVideo`的 `Custom.FlightDisplay`静态 QML 模块。后者沿用原生 `FlightDisplayModule`的 `STATIC + RESOURCE_PREFIX /qml + NO_PLUGIN`模式，通过短 `QT_RESOURCE_ALIAS`生成模块内类型，并显式加入 `CUSTOM_LIBRARIES`确保Android主目标链接；不手写qmldir，也不修改原生FlightDisplay模块。桌面 `QGC_BUILD_TESTING` 构建创建独立 `SiyiProtocolTest`、`Mt11ProtocolTest`、`GimbalMediaSessionPolicyTest` 和 `GimbalPhotoCapturePolicyTest`，移动端不生成额外测试应用；Siyi目标加入通用 `ZoomStepPolicy`，MT11目标只链接Qt Core/Test以及纯Protocol、`ZoomStepPolicy`、`Mt11ZoomPolicy`文件。它要求Quick3D/Quick3DAssetUtils，检测可选WebEngineQuick并定义Google 3D能力；翻译只导出 `custom_*.ts`，英文 `custom.ts`模板不编译。Android configure时把根 `android`模板复制到build目录，再用 `custom/android`同路径覆盖，复制过程排除Gradle生成缓存，并把合并副本的 `org.gradle.configuration-cache`固定为`false`、保留普通build cache；同时校验USB与媒体库custom标记并让Gradle只使用唯一合并源目录。外部WGS84样例目录不参与构建或安装。 |
| `custom/custom.qrc` | custom RCC运行时资源清单，共68个 `<file>`；本轮继续注册同路径 `FlyView.qml`覆盖、`MT11CameraControl.qml`及共享 `GimbalCameraControl.qml/GimbalZoomControl.qml`，并打包 `VideoCustom.SettingsGroup.json`。`DualPipView.qml`、`FlyViewSecondaryVideo.qml` 和 `FlightDisplayViewSecondaryVideo.qml`不在本QRC重复打包，而由 `Custom.FlightDisplay`模块注册为可导入类型；`GimbalIndicator.qml` 与 `ProximityRadarIndicator.qml` 仍以 `QGroundControl/Toolbar/...` alias覆盖原生工具栏资源。URL拦截器只在 `/Custom/qml`候选实际存在时重定向；本文件只决定覆盖资源URL，不编译C++、不保存设置值。Fuel与Proximity Radar详情页由 `Custom.Widgets`注册，翻译 `.qm`由CMake生成，外部WGS84样例不在本QRC中。 |
| `custom/cmake/CustomOverrides.cmake` | 根工程配置阶段读取的产品能力开关。固定 `QGC_APP_NAME=Custom-QGroundControl` 以保持应用标识和既有 QSettings 路径；关闭原生 Viewer3D后端，防止它与 custom Viewer3D 类和设置产生重复符号；关闭APM dialect/plugin/factory，并关闭原生PX4 Factory，让 custom Factory成为PX4固件插件的唯一创建入口。它只决定编译内容和插件选择，不在这里检查具体 `MAV_TYPE`。 |

### 4.2 CustomPlugin 与通信链路

| 文件 | 详细作用 |
|---|---|
| `custom/src/CustomPlugin.h` | custom 功能的中央组合入口声明。继承 `QGCCorePlugin`，向QML暴露稳定的Viewer3D设置/管理器、FlyViewCustom设置、共享Gimbal设置、通用 `videoCustomSettings`、A8 Manager、`mt11ControlManager`和`dualVideoManager`；声明init/cleanup、Android字号metadata、MAVLink视频消息过滤、QML engine和视频sink覆盖。文件末尾的 `CustomOverrideInterceptor` 负责把原生QRC URL重定向到实际存在的 `/Custom/qml` 文件；本头文件只定义接口与所有权。 |
| `custom/src/CustomPlugin.cc` | 上述中央入口的实现。`QGC_GST_STREAMING`构建下，构造函数在VideoManager/GStreamer初始化及GIO默认resolver创建之前调用匿名 `configureGStreamerNetworkPolicy()`：`QGC_GST_USE_SYSTEM_PROXY`经trim/lower后只有 `1/true/yes/on` 保留environment/system resolver，其余值均把 `GIO_USE_PROXY_RESOLVER`设为`dummy`，失败时输出critical。该进程级选择使后续所有GIO/GStreamer网络源（包括未来HTTP/HLS）默认直连；确需GIO代理时使用上述truthy opt-in，QtNetwork地图/下载代理不受影响。`init()`安装默认链路、翻译和各设置/Manager；创建独立A8、MT11控制器、`VideoCustomSettings`与DualVideoManager。`createVideoSink()`根据receiver父对象区分通用 Video 2 receiver与原生 Video 1 非thermal receiver；视频层不以设备型号命名。它把主receiver注入DualVideoManager，使重复源检测覆盖configured/current/starting/active/releasing各阶段URI；主路唯一start、generation和退避由通用 `VideoManager` 串行化，不在custom复制第二套主路Timer。当前产品本地媒体映射仍为 Video 1 -> A8 Manager、Video 2 -> MT11 Manager，两个本地录像状态机不会操作对方receiver。DualVideoManager释放前以DirectConnection通知MT11 Manager停止owned本地录像并清空Item/receiver，再销毁接收器和sink；应用退出同时收尾两套本地媒体并清理 Video 2。其他职责包括Android字号metadata、MAVLink视频消息过滤和QML URL拦截。RTSP传输不在CustomPlugin编排：两路receiver均由core保持GStreamer原生Auto。 |
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
| `custom/src/FlightDisplay/FlyView.qml` | 原生同路径Fly View的custom覆盖入口，保留任务控制器、地图、原生 Video 1 `FlyViewVideo`、WidgetLayer、引导控制和Viewer3D容器，增加通用 Video 2 与三路PIP编排。Video 1不再叠加 `visible: videoManager.hasVideo` 的custom门控，恢复原生 `FlyViewVideo` 始终存在的surface生命周期，避免启流前surface被隐藏而形成自依赖。文件显式 `import Custom.FlightDisplay as CustomFlightDisplay`，并以限定名实例化 `FlyViewSecondaryVideo`和 `DualPipView`。Map/Video 1/Video 2按 `item1/item2/item3` 接入；任一缩略框被点击后成为居中全尺寸项，原中心项精确回到被点击槽位。URL为空、重复或主视频不可用时对应项从候选中移除，正在居中的失效项回退Map；选择保存为 `MainFlyWindowView`并兼容旧 `MainFlyWindowIsMap`。任一视频全屏时统一隐藏工具栏、PIP、WidgetLayer和custom overlay。 |
| `custom/src/FlightDisplay/DualPipView.qml` | Map/Video 1/Video 2 三视图PIP状态管理器，沿用原生 `PipState` 的full/pip/window状态和原生 `PipView.qml` 的展开/隐藏、独立窗口、右上拖拽缩放交互。左下定义固定下槽与上槽；点击某槽只交换该槽和主视图，原主视图回到同一槽、另一槽保持不动。可点击层的 `z` 高于重挂内容，避免首次切换后视频/地图Item遮住点击区。本文件只管布局和状态，不创建或解码视频。 |
| `custom/src/FlightDisplay/FlyViewSecondaryVideo.qml` | Video 2 的通用Fly View wrapper，结构对齐原生 `FlyViewVideo.qml`：持有 `PipState`，进入/退出独立PIP窗口时暂停receiver并延迟2秒重启，只在full状态接受双击全屏，并叠加Proximity Radar和Obstacle Distance。它不包含MT11 SDK或设备地址。 |
| `custom/src/FlightDisplay/FlightDisplayViewSecondaryVideo.qml` | Video 2 的通用解码显示surface，结构对齐原生 `FlightDisplayViewVideo.qml`。从 `DualVideoManager` 取得decoding/尺寸/全屏状态，复用Video设置的fit/grid和原生无视频背景；Loader创建objectName为 `secondaryVideoContent` 的 `QGCVideoBackground`，就绪后通过 `initVideoItem(window, videoLoader.item)` 把实际渲染Item直接交给Manager，并在 `Window.window` 变化时用 `Qt.callLater` 重试；这避免Loader跨窗口/重挂载后仅在根window搜索而找不到真实Item。启动等待期间GL Item仍保持在场景图中，由更高z值的原生无视频背景覆盖，首个解码帧到达后再显示画面，避免“因为尚未decoding而隐藏GL Item、又因为GL Item未初始化而无法decoding”的循环依赖。它不调用任何相机SDK。 |
| `custom/src/FlightDisplay/MT11CameraControl.qml` | MT11右栏薄封装，复用 `GimbalCameraControl.qml` 的缩放、拍照、录像、本地媒体和状态布局，注入 `mt11ControlManager` 并打开热成像控件。它不复制A8面板逻辑；热成像按钮实际命令、pending和RGB/IR反馈仍由Manager及共享面板处理。 |
| `custom/src/FlightDisplay/FlyViewCompassBar.qml` | 罗盘条本体和绘制算法。直接读取活动飞行器 `Vehicle.heading.rawValue`，验证并归一化到 `[0°, 360°)`，使用中心附近 11 个 45°相对标签计算 N/NE/E/SE/S/SW/W/NW 的横向位置，中央显示四舍五入的整数航向并用 `compassPointer.svg` 绘制固定指针。它不读取显示开关、不保存设置、不判断是否应被加载；外层 `FlyViewCustomLayer.qml` 负责生命周期和显示条件。组件没有鼠标拦截层，因此不会吞掉下方地图手势。 |
| `custom/src/FlightDisplay/FlyViewCustomLayer.qml` | Fly View custom overlay 的编排层，同时管理罗盘条与燃料电池母线告警。它从 `corePlugin.flyViewCustomSettings.showHeadingCompassBar` 读取用户意愿，再结合 overlay 可见、活动飞行器存在和 heading 有效四个条件，通过明确 QRC URL加载 `FlyViewCompassBar.qml`；罗盘条采用组件的 `implicitWidth`（与示例相同为 `50 × defaultFontPixelWidth`）并保持屏幕水平居中，只按 Fly View 总宽度和基础 margin 做最终屏幕边界钳制，不再使用 PIP/摇杆/仪表的角落 inset 压缩宽度。它把“罗盘条高度+底边 margin”的完整占用深度合并进 `bottomEdgeCenterInset`，关闭时透传原生 inset。同一文件还监听 `vehicle.generator.busVoltage`，低于20.0 V置告警、超过20.4 V清除，形成回差；`mapControl` 当前只是兼容接口，未参与逻辑。 |
| `custom/src/FlightDisplay/FlyViewToolStripActionList.qml` | Fly View 左侧工具条动作模型的同路径覆盖。保留检查单、起飞、降落、返航、暂停、附加动作和夹爪的原生顺序，在最前面新增仅当 `viewer3DSettings.enabled=true` 才可见的 2D/3D 切换动作；动作调用现有 `viewer3DWindow.open()/close()`，打开 3D 时用 PaperPlane 表示返回 Fly，关闭时用 custom 城市图标表示进入 3D。 |
| `custom/src/FlightDisplay/FlyViewTopRightColumnLayout.qml` | Fly View右侧中部控件容器的同路径覆盖，始终保留 `TerrainProgress`。A8或MT11任一启用时，无需活动Vehicle即可显示私有相机栏；两者同时启用时在栏顶增加 `A8 Mini/MT11`选择控件，选择哪个就向共享面板注入哪个Manager，某一路关闭后自动归一到仍可用的一路。`sdkResponding`只控制在线状态和相关按钮，不决定整栏可见性；只有两套私有相机都关闭且存在活动Vehicle时才回退原生 `PhotoVideoControl`。容器宽高跟随选择器与当前面板隐式尺寸，避免移动端缩放把控件压缩。 |
| `custom/src/FlightDisplay/GeneratorBusVoltageAlert.qml` | Fly View燃料电池母线低压提示本体。读取传入Vehicle的 `generator.busVoltage`，低于20.0 V显示告警、严格高于20.4 V清除，NaN或无Fact时隐藏；双阈值回差避免临界电压反复闪烁。它只绘制告警，加载位置和活动飞行器生命周期由 `FlyViewCustomLayer.qml` 管理。 |
| `custom/src/FlightDisplay/GimbalCameraControl.qml` | A8与MT11共用的相机面板，Manager可由外层注入，不依赖飞控、活动Vehicle或SDK在线状态决定可见性。单个半透明圆角面板纵向排列通用 `GimbalZoomControl`、可选热成像、拍照和录像；A8关闭热成像行，MT11通过wrapper打开并在拍照按钮上方显示RGB/IR切换，pending期间禁用重复点击，模式文案以SDK确认结果为准。空闲拍照/录像均使用同尺寸圆形图标，录像计时、pending或失败文字按内容展开。命令调用当前Manager的 `takePhoto()/toggleVideoRecording()`，拍照反馈观察机内和本地计数；录像读取组合会话available/active/capturing并分别显示 `SD`、`LOCAL` 状态，本地支路不被SD无卡覆盖。该栏不实例化原生 `PhotoVideoControl`。 |
| `custom/src/FlightDisplay/GimbalZoomControl.qml` | 合并栏顶部的A8/MT11共享手势壳，使用单列GridLayout从上到下显示加号、当前倍率和减号，并把完整列高传给外层布局。从当前Manager读取 `zoomControlsUnlocked/currentZoom/zoomStatusKnown`以及分开的 `zoomIn/OutTapAvailable`、`zoomIn/OutHoldAvailable`；按钮只要对应方向的tap或hold任一种可用便可按下。两侧MouseArea统一实现Idle/Pressed/Holding/Consumed状态；短按只有在release时对应 `canTap`仍为true才调用一次 `zoomIn/zoomOut`，所以MT11大于30x的短点在UI安静消费，不调用Manager。长按420 ms成立时先把手势置为Consumed再调用 `startZoomWithPressDuration(direction, totalMs)`，因此hold不可用、Manager拒绝或启动失败后的release都不会补发tap。成功hold的普通release调用 `stopZoom()`，取消、移出、隐藏、后台和销毁调用 `cancelZoom()`。具体倍率策略留在各Manager：A8使用卡录分辨率能力表和按时长显示目标；MT11短按使用1.0～30.0x的0x0F精确目标，长按使用0x05原生连续运动和0x18反馈驱动合法显示档位，30.0x以上双向tap availability均关闭。QML不把A8的 `qRound(totalMs / 600.0)` 目标、卡录分辨率上限或80 ms停止时序复制给MT11。 |
| `custom/src/FlightMap/Images/compassPointer.svg` | 罗盘条中央固定三角指针的纯矢量资源，不含角度或交互逻辑。按原生 `src/FlightMap/Images` 资源分类保存，由 `custom.qrc` 注册为 `qrc:/custom/img/compassPointer.svg`，`FlyViewCompassBar.qml` 通过 `QGCColoredImage` 加载并按当前主题文本颜色着色。 |

本轮已在 custom 保存同路径 `FlyView.qml` 以接入三视图；无项目差异的 `FlyViewWidgetLayer.qml` 和 `FlyViewToolStrip.qml` 仍直接复用 `src`，工具条动作差异继续由上表 `FlyViewToolStripActionList.qml` 覆盖。

### 4.5 Gimbal 后端

| 文件 | 详细作用 |
|---|---|
| `custom/src/Gimbal/A8MiniZoomPolicy.h` | A8 Mini缩放策略的纯静态接口，保留受支持拉流会话尺寸、卡录分辨率能力映射、反馈确认、端点交接及按总按压时长计算hold目标等A8专属契约；对合法档位判断、最近档位对齐和相邻步进保留兼容接口，但实现已作为 `ZoomStepPolicy` 的薄包装。它不访问网络、QSettings或UI。 |
| `custom/src/Gimbal/A8MiniZoomPolicy.cc` | 拉流尺寸只判断1280×720或1920×1080会话是否受支持，不再产生倍率；卡录分辨率单独映射3840×2160或4096×2160→1.0x、2560×1440→3.5x、1920×1080→5.5x、1280×720→6.0x。合法档位、对齐和相邻步进直接委托通用 `ZoomStepPolicy`，A8仍把能力的精确非整步上限作为唯一末端目标：默认1.0x时2K为1/2/3/3.5，卡录1080P为1/2/3/4/5/5.5，卡录720P为1/2/3/4/5/6；4K只有1.0。hold继续按 `qRound(totalMs / 600.0)` 从起始目标计算档数并在端点钳制。 |
| `custom/src/Gimbal/ZoomStepPolicy.h` | 声明A8与MT11共用的无状态定步长网格接口：判断合法档位、把观测值对齐到最近档位、以及沿指定方向取得下一档。调用者显式传入最小值、最大值和步长；本层不识别相机型号、协议命令、卡录分辨率或QML。 |
| `custom/src/Gimbal/ZoomStepPolicy.cc` | 使用十分之一倍率整数实现最小值锚定网格、方向中点选择和严格单调步进，避免浮点漂移；通用接口允许调用者把传入的精确maximum作为末端合法档位。A8利用该例外保留3.5/5.5等精确端点，MT11则先由自己的策略把物理上限收敛到最后一个完整步长显示上限，不能直接把165.1x例外套进默认1.0x的MT11显示。 |
| `custom/src/Gimbal/Mt11ZoomPolicy.h` | 声明MT11专用的显示目标与手势策略：根据1.0x锚点、配置步长和设备物理上限取得显示上限，将私有实测值对齐为合法显示档位，按上一显示目标规划1～30x短按，以及只在反馈真正越过下一档时推进长按显示。它不发送UDP包，也不使用A8卡录能力表。 |
| `custom/src/Gimbal/Mt11ZoomPolicy.cc` | 在通用网格之上增加MT11协议与产品约束。显示上限取不超过物理/0x16上限的最后一个完整步长：默认1.0x时物理165.1x只在Manager内部用于停止，UI最高发布165.0x；步长0.1x时165.1x本身才是合法显示档位。短按使用已经合法的显示目标继续步进，并以私有实测值执行30x协议域门禁，快速连点不会用raw重新锚定；长按根据方向逐档检查350 ms的0x18反馈，只发布已经到达或越过的合法档位，中间raw值不对QML公开。新增交接策略在0x0F目标仍在pending时保留该最新合法显示目标作为hold起点；hold方向可用性取最新0x18实测位置与在途目标的可行方向并集，因此绝对运动期间仍能用长按明确抢占。 |
| `custom/src/Gimbal/GimbalControl.SettingsGroup.json` | `GimbalControl` 设置组的元数据源，不执行云台或视频操作。定义11个Fact：A8、本地媒体、视频集成开关，以及 `mt11Enabled=true`、`mt11SdkHost=192.168.144.24`、`mt11SdkPort=37260`、`mt11ZoomStep=1.0x`。MT11步长允许0.1～29.0x，用于1.0～30.0x的0x0F短按目标，也用于0x05长按反馈跨越时发布合法显示档位；它不会把连续0x05物理运动拆成按步长发送的多条绝对命令，也不改变设备/产品物理上限。它不再定义RTSP URL；通用 Video 2 地址已迁入 `[Video]/secondaryRtspUrl`。 |
| `custom/src/Gimbal/GimbalControlSettings.h` | 声明 `GimbalControlSettings : SettingsGroup`，用11个 `DEFINE_SETTINGFACT` 生成惰性创建的 `Fact*` Q_PROPERTY。它是JSON/QSettings与QML、A8 Manager和MT11 Manager之间的相机控制设置入口；不保存通用Video URL，不创建socket或receiver。 |
| `custom/src/Gimbal/GimbalControlSettings.cc` | 通过 `DECLARE_SETTINGGROUP(GimbalControl, "GimbalControl")`确定元数据资源和QSettings分组，实现11个Fact getter并以reference-only类型注册。构造阶段仅保留MT11 SDK Host的受限默认迁移：已有值精确等于旧默认 `192.168.144.25` 时首次改为 `.24`，键缺失、空值或其他用户值不改写。旧RTSP键的兼容复制已由 `VideoCustomSettings.cc` 接管。 |
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
| `custom/src/Gimbal/Mt11Protocol.h` | 声明UniPod MT11 SDK V0.1.0纯协议接口、命令枚举、视频源枚举和payload结构，覆盖0x05手动变倍、0x0A相机状态、0x0B异步功能反馈、0x0C拍照/录像、0x0F绝对倍率、0x10查询视频模式、0x11设置视频模式、0x16最大倍率、0x18当前倍率。倍率常量分开表达 `MinimumZoom=1.0`、0x0F命令上限 `MaximumAbsoluteZoom=30.0` 与线反馈上限 `MaximumFeedbackZoom=255.9`；不把产品165.1x策略或A8能力映射放进协议层。 |
| `custom/src/Gimbal/Mt11Protocol.cc` | 按SDK构造并严格解析 `55 66 + control + payload length LE + sequence LE + command + payload + CRC16 LE`。生产请求sequence固定0；CRC覆盖CRC字段之前的完整帧，多项式 `0x1021`、初值0、逐位高位优先。0x0F出站只允许1.0～30.0x并编码为“整数byte + 一位小数byte”；0x05 ACK按十分之一倍率的 `uint16 LE`解析，`73 06`为165.1x；0x16/0x18按“整数byte + 一位小数byte”解析，`a5 01`为165.1x。两类反馈线格式都可校验到255.9x，产品上限由Manager另行收紧。0x10/0x11把RGB主流/热成像副流编码为 `[0,2]`，热成像主流/RGB副流编码为 `[2,0]`。 |
| `custom/src/Gimbal/Mt11Sdk.h` | 声明MT11独立 `QUdpSocket`传输对象，默认endpoint为 `192.168.144.24:37260`，提供协议九类命令的发送接口和状态信号。`setZoomRange()`只约束0x0F绝对命令，`setFeedbackZoomRange()`独立约束0x05/0x16/0x18反馈，默认反馈范围为1.0～255.9x；该endpoint只代表私有SDK控制链路，不决定RTSP URL 1/2，也不创建视频receiver。 |
| `custom/src/Gimbal/Mt11Sdk.cc` | 发送Protocol帧并同时按配置逻辑IP和配置端口过滤回包；IPv4与其IPv4-mapped IPv6表示可视为同一逻辑地址，但回包源端口必须精确等于 `mt11SdkPort`。0x0B允许作为文档规定的相机异步通知，其余普通响应必须为ACK control且匹配同command最近1.5秒请求窗口。由于生产sequence固定0，按command维护有界代次是线协议可提供的最强关联；无匹配或过期ACK忽略，payload非法时恢复剩余请求窗口等待合法帧。绝对命令与设备反馈使用独立范围，因此0x16 `a5 01`现在会作为合法165.1x分发；295d中的拒绝日志只属于修正前旧构建。Manager设置重置时显式清除旧请求窗口，一个UDP datagram仍须全部帧合法才分发。 |
| `custom/src/Gimbal/Mt11ControlManager.h` | 声明MT11的QML业务门面和运行状态，向QML暴露启用/在线、合法显示目标 `currentZoom`、设备0x16最大倍率、独立短按步长、分开的tap/hold方向availability、缩放门禁、相机录像、拍照、本地媒体、RGB/热成像模式及错误反馈；0x05/0x18实测倍率只保存在私有状态。常量明确区分0x0F绝对上限30.0x、当前产品混合倍率物理上限165.1x和协议反馈上限255.9x；接口形状与共享相机面板兼容，但状态不与A8 Manager共用。 |
| `custom/src/Gimbal/Mt11ControlManager.cc` | 持有 `Mt11Sdk`。短按由 `Mt11ZoomPolicy`从上一合法显示目标按 `mt11ZoomStep`计算0x0F目标，私有实测值负责1.0～30.0x协议域门禁；发送成功立即发布新显示目标，快速连点现场替换在途目标，不以raw重新锚定。生产帧固定sequence且0x0F ACK只标识command，因此reject一律只触发0x18查询、不直接回滚可能已被更新的目标；只有0x18精确命中或独立10秒超时才结束pending，6.5秒反馈新鲜度失效不会取消该绝对确认窗口。实测大于30.0x时两方向tap availability关闭，QML短点不调用Manager，Manager直接调用仍防御性拒绝。长按用0x05原生连续变倍，可运行到 `min(0x16设备上限, 165.1x)`；活动时每350 ms查询0x18，只有raw到达或越过下一完整合法档位才更新显示，到物理边界或60秒watchdog时停止。在途0x0F不再阻断新hold：Manager保留最新合法显示目标，先发0x05 `0`，150 ms后发方向，仍按住时再过150 ms最多发一份同方向副本；release/cancel取消未发Timer并立即停止，避免松手后迟到启动。固定sequence下的0x05停止/方向ACK无法按代次关联，故不用其倍率推进UI或边界，位置只信0x18。普通停止立即发0x05 `0`，150 ms后只安全重复一次并查询最终0x18；此后tap/hold仍锁定，必须收到该停止序列之后的有效0x18 settled反馈才重新开放，期间保留上次合法显示档位。默认1.0步长且物理上限165.1时中心最高165.0，0.1步长才可显示165.1。常规每2秒查询0x16/0x18/0x0A/0x10；6秒无任意合法SDK包判离线，0x16能力和0x18位置分别超过6.5秒未刷新就锁定控制并停止活动运动。视频模式切换前停止连续变倍并立即使旧镜头倍率失效，pending期间不采纳旧镜头0x16/0x18；变化按原始 `mainStream` 枚举判断，因此Zoom/Wide/composite或RGB/IR切换都会重查新主镜头能力/位置。若外部控制器先改变mainStream，Manager会立即退休旧0x0F generation、清旧请求和状态；活动0x05只向旧镜头立即停止一次，并取消其尚未触发的150 ms副本，避免跨镜头补发。0x10/0x11确认后立即重查，2.5秒超时也清状态并重查。析构进入可能运行nested event loop的本地媒体收尾前，先停止全部SDK/control Timer、退休缩放/命令状态并断开SDK到Manager的回调；若此前仍有绝对/连续/待重试缩放，再同步发送两份0x05停止，然后才等待照片worker和本地媒体结束，避免半析构对象被迟到ACK或Timer重入。拍照、录像与当前Video 2本地媒体映射仍不操作Video 1/A8 receiver。 |
| `custom/test/Gimbal/SiyiProtocolTest.cc` | custom独立QtTest协议与策略回归用例，共40个业务slot；当前独立运行结果为42 passed、0 failed（含init/cleanup）。覆盖0x0f封包量化、ACK control、坏CRC、严格单帧长度、多帧UDP拆分、0x16/0x18双格式倍率payload、0x20录像流请求与9字节ACK、拉流会话白名单、4K/2K/1080P/720P卡录能力映射、通用 `ZoomStepPolicy`薄包装、唯一min锚目标表、A8精确上限追加、正反同表序列，以及420 ms长按成立后按 `qRound(total/600)` 计算档数和端点钳制。Manager级测试还需覆盖能力交叉校验、tap成功即显示、快速替换、hold只启动一次0x05、普通release与cancel分流及不产生释放后反向0x0f。 |
| `custom/test/Gimbal/Mt11ProtocolTest.cc` | MT11纯协议与策略QtTest，共9个业务slot，覆盖SDK文档0x05/0x0A/0x0C/0x0F/0x16/0x18精确命令帧、0x10/0x11热成像帧、严格control/长度/CRC/sequence解析、UDP多帧原子性和状态payload。倍率边界明确覆盖0x05 `73 06`→165.1x、`ff 09`→255.9x，0x16/0x18 `a5 01`→165.1x、`ff 09`→255.9x，并拒绝0x0F 30.1x；策略覆盖通用1.0锚定网格、从显示目标tap、30x以上双向tap门禁、反馈对齐/越档推进、tap pending时的hold显示起点保留及方向可行性并集，以及默认1.0步长将物理165.1收敛为显示165.0、0.1步长允许显示165.1。当前独立运行结果为11 passed、0 failed（含init/cleanup），`Mt11Sdk.cc`也已独立编译通过；它不覆盖Manager的150 ms交接/方向副本时序、完整QGC Qt 6构建、真实UDP往返或真机镜头运动。 |
| `custom/test/Gimbal/GimbalMediaSessionPolicyTest.cc` | 独立QtTest状态策略回归。覆盖无SD/SDK依赖的本地启动、external录像只释放不停止、confirmed owned关闭、未确认provisional取消等待、迟到确认后的补偿停止、expected stop不重启、pending幂等、码流与blocked门控、采用/确认已有实际录像、排除相机乐观状态的capturing，以及无SDK时只凭本地码流即可启用录像按钮。它不替代真实文件系统、GStreamer和Android设备测试。 |
| `custom/test/Gimbal/GimbalPhotoCapturePolicyTest.cc` | 独立QtTest照片尺寸策略回归。覆盖720P/1080P/2K/4K与DPR 1/1.5/2/2.625换算、4096×2160输出对16:9实时帧的四色角完整性和左右各128像素黑边、分数DPR实际grab偏差修正为精确输出、普通16:9路径不触发QImage深拷贝，以及无效尺寸/DPR拒绝。它不替代Qt Quick真实离屏渲染、5秒Timer/generation/窗口生命周期、线程池、JPEG/QSaveFile失败、Android GPU内存和MediaStore真机测试。 |

A8 Mini完整缩放调用链为：`VideoReceiver`解码当前视频 -> 真实首帧CAPS或最终 `GstVideoInfo` 隐式尺寸 -> 无直接结果时使用稳定1秒的 `VideoManager::videoSize` -> 受支持拉流只建立A8视频会话门控 -> 0x20查询 `stream_type=0`卡录分辨率并映射基础上限 -> 合法0x16只以较小值安全收紧 -> `A8MiniZoomPolicy`以薄包装委托通用 `ZoomStepPolicy`生成1.0x起始的唯一min锚网格，并保留A8专属的有效精确上限追加 -> tap立即发送同表相邻一档0x0f并在成功后显示目标 -> hold在420 ms成立后通常只发送一次0x05方向命令，按总按压时长每600 ms更新同方向合法显示目标并在端点立即停止 -> 普通release调用 `stopZoom()`，取消、移出、隐藏、后台和销毁调用 `cancelZoom()`；活动0x05路径都会发送0停止，且hold结束不发送0x0f反向归整。若hold起步目标已是端点，则只发一次同方向端点0x0f而不进入0x05。0x18实际反馈独立保存，不覆盖A8当前目标倍率；整条卡录分辨率/时间目标链只属于A8。

MT11完整缩放调用链为：任意合法SDK包维持6秒在线状态 -> 0x16设备能力与0x18私有实测位置分别维持6.5秒新鲜度 -> `Mt11ZoomPolicy`把实测位置对齐为1.0x锚定、完整步长的显示档位 -> 两者已知且无模式切换pending时解锁 -> tap从上一显示目标按独立 `mt11ZoomStep`生成1.0～30.0x的0x0F精确目标，发送成功立即显示并以250 ms查询0x18，快速连点替换在途目标 -> 大于30.0x时QML关闭两方向tap并安静消费短点 -> 0x0F ACK因固定sequence/command-only关联而不直接回滚目标，精确0x18或独立10秒超时结束pending -> hold在420 ms后用0x05连续运动；若此时0x0F仍在pending，先以0x05 `0`抢占，150 ms后发方向，持续按住再过150 ms最多补一份同方向，显示起点保留最新tap合法目标 -> 以350 ms查询0x18，只有越过合法显示档位才推进中心数字，物理停止边界为1.0x或 `min(设备0x16, 165.1x)` -> release/cancel/边界/超时取消尚未发出的交接Timer并立即停止，普通停止的150 ms安全副本后查询最终0x18 -> 收到停止序列之后的有效0x18 settled反馈前tap/hold继续锁定，上次合法显示档位保持不变 -> 模式原始 `mainStream` 变化或切换pending时清旧能力/位置并锁定，确认或超时后重查0x16/0x18。0x05 ACK只维持可达性，不用其内嵌倍率更新状态；0x18是唯一权威位置来源。默认1.0步长时物理165.1只供边界停止，最后显示档位为165.0；0.1步长才显示165.1。外部mainStream切换会退休旧绝对命令generation，活动连续运动只立即停止一次并取消旧镜头尚未触发的150 ms副本；退出则在nested媒体收尾前停Timer、退休状态、断SDK回调，并在需要时同步双发停止。该链不读取VideoReceiver尺寸、0x20卡录参数或 `A8MiniZoomPolicy`。

### 4.6 GStreamer RTSP传输、拉流分辨率与 Android 视频解码策略

| 文件 | 详细作用 |
|---|---|
| `custom/src/VideoManager/DualVideoManager.h` | 声明通用 Video 2 的独立生命周期对象，向QML暴露enabled/hasVideo/duplicateSource/streaming/decoding/fullScreen、尺寸、receiver和videoItem；提供init/start/stop/cleanup及接收真实Loader Item的 `initVideoItem(window, item)`，并在释放对象前发出 `videoObjectsAboutToBeReleased`。另保存主receiver的start-attempt/start/stop/destroy安全连接、primary starting/active/releasing URI、精确延迟清除Timer和terminal cleanup门，用于主/副endpoint交接及退出保护。它不复用或修改原生 Video 1 `VideoManager` receiver，也不暴露任何设备型号。 |
| `custom/src/VideoManager/DualVideoManager.cc` | 监听通用 `[Video]/secondaryRtspUrl` 以及原生Video Source/`rtspUrl`/`streamEnabled`，在 `FlightDisplayViewSecondaryVideo.qml` 把实际 `secondaryVideoContent` Item和window交给 `initVideoItem()` 后，由core plugin创建独立 `VideoReceiver`和sink；`findChild()`只作兼容回退。指定Item换代时会清空旧widget并有序重建receiver。创建对象后先经过 `QQuickWindow::BeforeSynchronizingStage`，再读取动态qgcvideosink Item的 `itemInitialized`；属性存在时只有其为true才启流，并监听 `itemInitializedChanged`和跨窗口变化，防止OpenGL上下文未完成时过早启动。它独立维护启动、停止、1–15秒有上限退避重试、URL变化和主动pause；处理 `onStartDecodingComplete`失败，并对“streaming=true但迟迟没有decoding首帧”设置10–30秒有界watchdog后重建管线。只有secondary URI、启用状态或重复源判定等真实配置变化才取消已有退避；主receiver的active/releasing被动通知即使触发 `_refreshSettings()`，也只能保留Timer而不能提前start。URL为空，或经QUrl比较后与配置URL 1、主receiver当前URI、本轮 `onStartAttempt` 冻结的starting URI、成功start后的active URI、stop后的releasing URI任一相同时，不启动第二receiver。主stop完成或receiver销毁后，active URI由 `Qt::PreciseTimer` 至少保留1000 ms；同URI新start会取消清除，防止旧主管线释放前Video 2打开同一endpoint。所有回调校验当前QPointer receiver，替换/销毁时断开并清理连接；cleanup永久门控settings、render job和primary late signal，停止全部Timer后再释放receiver，退出阶段不得重建。停止/释放路径不受render-ready门闩限制。动态视频Item被销毁时先清除receiver中的裸widget指针并有序释放旧sink，后续新surface加载时重新创建receiver，避免沿用失效GL对象。释放前仍通知当前映射的MT11 Manager收尾owned本地录像，随后按receiver先销毁、sink后释放的顺序清理。相机SDK是否在线不参与通用RTSP启动判定。正常的surface、streaming、decoding、尺寸、handoff、重试和成功回调不再逐项打印；仅保留初始化/创建、start/startDecoding失败和首帧watchdog超时等warning/critical。Video 2不读取传输Fact，创建的RTSP source与Video 1一致使用原生Auto。 |
| `custom/src/VideoManager/VideoReceiver/GStreamer/PulledVideoResolutionProbe.h` | 声明无QObject状态的主拉流协商尺寸探针安装接口及 `ResolutionHandler` 回调。由 `CustomPlugin::createVideoSink()` 调用；非GStreamer构建、空sink、非 `VideoReceiver` parent或thermal receiver返回false且不改变原生视频路径。回调由GStreamer流线程触发，调用方必须排队切回Manager线程。 |
| `custom/src/VideoManager/VideoReceiver/GStreamer/PulledVideoResolutionProbe.cc` | 只在 `QGC_GST_STREAMING` 下对主视频 `qgcvideosinkbin` 的 `sink` ghost pad安装downstream CAPS、BUFFER和BUFFER_LIST探针。只有真实帧到达才发布尺寸；宽高直接读取CAPS structure，不把成功条件绑死在完整format的 `gst_video_info_from_caps()`。若外层ghost pad没有current CAPS，则继续读取已连接解码器peer和ghost target的current CAPS，覆盖不同平台的caps存放差异。得到正宽高后既通过回调直达Manager，也经既有 `VideoReceiver::videoSizeChanged` 修正 `VideoManager`，从而覆盖原生 `GstVideoReceiver::_addVideoSink()` 在管线刚拼接时用 `gst_pad_query_caps()` 得到的暂态/无效值；该原生信号同时可触发Manager受控的1秒稳定兜底。不修改 `src`、不猜测卡录分辨率，也不影响thermal流。正常安装探针的debug日志已删除，首个真实帧仍无法取得宽高时保留明确告警。 |
| `custom/src/VideoManager/VideoReceiver/GStreamer/AndroidH265HardwareDecoderAdapter.h` | 声明进程级 custom H.265 decoder factory接口：固定factory名、注册函数、已选内部厂商MediaCodec factory查询，以及policy与adapter共用的厂商名称过滤函数。它只是适配器注册API，不实现H.265算法，也不调用Android `MediaCodecInfo.isHardwareAccelerated()`做系统级硬件认证。 |
| `custom/src/VideoManager/VideoReceiver/GStreamer/AndroidH265HardwareDecoderAdapter.cc` | 在启动阶段枚举实际属于 `androidmedia` 插件、能接受 `video/x-h265,stream-format=byte-stream,alignment=au` 且通过厂商名称筛选的MediaCodec候选，排除secure、software/FFmpeg、`*.sw.dec`、Qualcomm `*swvdec`、OMX Google及C2 Android/Google/Goldfish，优先名称含low-latency变体的组件，再按原rank/名称排序；预检只证明元素可创建、静态链可链接且bin能进READY，不证明真实profile/level已解码。选中后缓存厂商factory并以rank `PRIMARY+2=258` 注册 `qgcandroidh265hwdec`，作为direct hvc1候选之后的兼容回退。每个播放管线都会创建独立adapter实例，实例各自拥有parser、capsfilter、厂商MediaCodec element和downstream-leaky raw queue（最多2 buffer）；共享项仅为启动时选定后只读的factory名称、类型注册和rank，不共享decoder element、buffer或逐流状态。单实例内部为：外部hvc1 ghost sink -> `h265parse(config-interval=-1)` -> Annex-B byte-stream/AU capsfilter -> 厂商MediaCodec -> raw queue -> `video/x-raw(ANY)` ghost src；probe记录真实输入caps和首个raw buffer的caps/PTS/bytes/GLMemory，首帧使用 `vendor MediaCodec candidate` 术语。它只是插件/名称启发式候选证据，不等同Android API硬件认证，也不保证画面已到QML sink。 |
| `custom/src/VideoManager/VideoReceiver/GStreamer/AndroidVideoDecoderPolicy.h` | 声明一次性的Android H.264/H.265 factory rank与H.265格式适配策略入口。正确调用窗口是 `VideoManager`构造已完成 `GStreamer::initialize()` 之后、`VideoManager::init()` 创建VideoReceiver和 `decodebin3`之前；过早无法枚举插件，过晚则已建管线不会重新选择decoder。 |
| `custom/src/VideoManager/VideoReceiver/GStreamer/AndroidVideoDecoderPolicy.cc` | `CustomPlugin::init()` 从兼容保留的 `forceAndroidH265HardwareDecoder` Fact读取一次并调用策略，因此开关要求重启；UI语义已扩展为优先Android H.264/H.265硬解。仅Android+`QGC_GST_STREAMING`且GStreamer已初始化时生效。候选必须来自 `androidmedia` 插件并通过厂商名称筛选：原生接受H.264 `stream-format=avc` 或H.265 `hvc1` 的direct候选都提升到 `PRIMARY+3=259`，H.264不注册adapter；H.265 adapter仍以 `PRIMARY+2=258` 注册作格式兼容回退。direct候选与adapter都高于原生Force Software的257，且direct优先于adapter。其他软件factory rank不删除也不置0，保留回退资格；该优先策略不能承诺所有真机都选中硬件组件或运行期协商失败后一定无黑屏。逐候选日志记录plugin/factory、caps兼容和rank变化，非Android不受影响。 |

Android选择链为：`CustomPlugin::init()`读取重启后生效的兼容Fact -> `AndroidVideoDecoderPolicy` 在管线创建前调整候选/rank并注册可用H.265 adapter -> `decodebin3`依据caps与rank选择decoder。H.264 `avc` 只走rank259的direct厂商MediaCodec候选；H.265优先rank259的direct hvc1厂商候选，rank258的 `qgcandroidh265hwdec` 作为Annex-B格式兼容回退，数据路径为 `hvc1 -> h265parse -> video/x-h265,stream-format=byte-stream,alignment=au -> 厂商MediaCodec -> 最多2帧的leaky raw队列 -> video/x-raw -> 原生QGC显示链`。rank只影响优先级；必须结合decodebin实际创建的factory、该decoder的首个输出buffer和对应sink首帧判断运行链路，不能只看rank、READY预检或adapter候选日志。

为避免离线相机持续重试时淹没Application Messages，Video 2的逐次启动、成功回调、surface、streaming/decoding、尺寸、handoff和退避过程日志，以及主路observer安装日志均已删除；失败回调和首帧watchdog超时仍可见。core `GstVideoReceiver` 保留四个低频成功里程碑：source媒体首帧、decodebin实际decoder实例、该decoder首个输出buffer和sink首帧。它们带receiver/URI/factory/instance/caps等必要字段，可用于区分两路真实链路；旧日志中的 `STATUS_MIN/STATUS_OK=0`只代表异步请求受理，本轮不再把该过程值当作播放证据。

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
| `custom/src/Settings/VideoCustom.SettingsGroup.json` | 只定义通用第二路URL Fact `secondaryRtspUrl`：缺省 `rtsp://192.168.144.24:8554/video1`，空值禁用Video 2。元数据不含MT11型号或传输偏好语义，不创建receiver，也不把标准 `rtsp://` 改写为其他scheme。 |
| `custom/src/Settings/VideoCustomSettings.h` | 声明 `VideoCustomSettings : SettingsGroup` 及唯一的 `secondaryRtspUrl` `Fact*` Q_PROPERTY，作为QML、QSettings、`CustomPlugin`与 `DualVideoManager` 之间的稳定通用第二路视频设置接口。 |
| `custom/src/Settings/VideoCustomSettings.cc` | 使用 `DECLARE_SETTINGGROUP(VideoCustom, "Video")` 把 `secondaryRtspUrl` 写入原生 `[Video]` 分组。新键不存在时才读取旧 `[GimbalControl]/mt11RtspUrl`：旧值精确为历史出厂默认 `rtsp://192.168.144.25:8554/video1` 时转成 `rtsp://192.168.144.24:8554/video1`，其他自定义值和空字符串原样复制。已有新值绝不覆盖，旧键也不删除，保持升降级安全。旧版本遗留的 `[Video]/primaryRtspTcpOnly` 与 `[Video]/secondaryRtspTcpOnly` 不在本类注册或读取，也不主动迁移、覆盖或删除。 |
| `custom/src/UI/AppSettings/GeneralSettings.qml` | Application Settings -> General 的同路径 custom 覆盖页。完整保留原生 Language、Color Scheme、GCS位置流、音频、Android SD Card、清除设置、数据路径、Units和Brand Image。UI Scaling直接绑定原生整数 `appFontPointSize`，按 `appFontPointSize / ScreenTools.platformFontPointSize × 100` 四舍五入显示，`-`/`+` 每次修改1 pt并由原生 `SettingsFact` 保存；页面本身不写缺省值。`SettingsFact` 构造期间先调用 `CustomPlugin::adjustSettingMetaData()` 把 Android raw default改为12 pt，再读取已有QSettings或该缺省值，因此未打开本页面也会生效；非Android默认仍为100%。 |
| `custom/src/UI/AppSettings/FlyViewSettings.qml` | Application Settings -> Fly View 的同路径覆盖页。保留全部原生 Fly View 设置，从 `corePlugin.flyViewCustomSettings.showHeadingCompassBar` 取得 Fact，在 Instrument Panel 中用 `FactCheckBoxSlider` 提供“显示航向罗盘条”开关；切换会由 `SettingsFact` 自动持久化并被 `FlyViewCustomLayer.qml` 立即观察。父页缓存 `corePlugin.gimbalControlSettings`，仅用一层Loader在Instrument Panel正下方以 `Qt.resolvedUrl("GimbalControlSettingsGroup.qml")` 加载相机组，并在 `onLoaded` 注入设置对象；该入口不依赖云台或SDK在线状态。这取代“外层Loader→组件根Loader”的双层隐式高度链，避免设置对象初始化时内层 `item=null/implicitHeight=0` 导致整组静默折叠。Viewer3D组随后加载；各Loader都把已加载项的 `implicitHeight` 显式提供给 `Layout.preferredHeight/minimumHeight`，本文件不绘制罗盘条。 |
| `custom/src/UI/AppSettings/VideoSettings.qml` | Application Settings -> Video 的同路径覆盖页。保留原生Video Source、Connection、播放设置和Local Video Storage；当选择RTSP源时，在同一 `Connection` 组内将原生 `[Video]/rtspUrl` 标记为 `RTSP URL 1`，将 `[Video]/secondaryRtspUrl` 标记为 `RTSP URL 2`。界面不再显示传输开关；URL 2提示只说明它使用独立receiver且留空会禁用，原生Auto策略由程序行为和本说明记录。程序始终保留标准 `rtsp://` 地址且不设置 `rtspsrc.protocols`。第二路为空时不显示Video 2；URL 2与配置中的URL 1或主receiver当前实际URI相同时显示警告并由Manager禁用重复接收器。Local Video Storage继续提供共享 `localMediaStorageEnabled`，只控制A8/MT11各自本地附加支路，不关闭相机SD动作；实际双路receiver、相机命令、录像、截图和解码策略由对应Manager执行。 |
| `custom/src/UI/AppSettings/Viewer3DSettingsGroup.qml` | 由 `FlyViewSettings.qml` 显式加载的 Viewer3D 设置面板。根Loader将内容的 `implicitWidth/implicitHeight` 向外透传，供设置页正确计算分组高度和滚动范围；不再用 `onLoaded/onWidthChanged` 手工写子项宽度。只有设置对象及14个所需Fact可用时才创建内容；Google与外部模型两个开关相互排斥，两者都关闭时隐式进入本地OSM模式。页面编辑API Key、外部模型文件、WGS84原点、单位换算、比例、yaw、OSM路径、建筑层高和车辆高度偏移；外部文件选择交给 `External3DMapManager.importModelFile()` 检查/转换并返回状态，本文件不创建或渲染三维场景。页面的 Clear只修改保存的路径值，不删除磁盘文件。 |
| `custom/src/UI/AppSettings/GimbalControlSettingsGroup.qml` | Fly View设置页中的双相机私有UDP面板。根节点现在直接是 `ColumnLayout`，不再用第二层Loader决定active/visible/隐式高度；父页注入的 `gimbalControlSettings` 作为唯一Fact来源。组内顺序固定为Zoom Step、A8 SDK、MT11 SDK：A8与MT11两套独立步长Fact首先在同一个 `Zoom Step` 分组中桌面并排、窄屏上下堆叠，之后两套SDK组各自绑定Enabled/SDK Host/SDK Port。A8继续绑定 `zoomStep`；MT11绑定默认1.0x的 `mt11ZoomStep`，它决定1.0x锚定合法显示档位和1.0～30.0x的0x0F短按目标，0x05物理连续运动本身不按该步长离散。页面仅提示视频地址统一在Video -> Connection的RTSP URL 1/2配置；本文件不测试设备在线，也不把SDK地址自动同步成RTSP URL。 |
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
| `custom/translations/custom.ts` | `lupdate` 生成的英文源字符串模板，供各语言目录对齐context/source，CMake不把它编译为运行时 `.qm`。它已包含通用RTSP URL 1/2、第二路独立receiver/留空禁用提示、重复URL警告、双相机选择、并列Zoom Step中的A8 Mini/MT11标签、MT11 SDK、30x以上仅长按提示、非网格MT11目标提示、RGB/热成像、本地媒体和连续变焦安全超时文本；已删除不可靠的0x0F reject回滚文案，以及两路RTSP-over-TCP开关及相关回退说明的6条message。本轮单层Loader与MT11交接状态机修正没有新增、删除或修改可翻译字符串，因此TS文件不制造仅location行号变化的diff。当前按UTF-8 XML实测为18个context、146条message和139个唯一source，英文模板146条保持unfinished，与简体中文目录的context/source集合差异为0。 |
| `custom/translations/custom_zh_CN.ts` | 简体中文locale目录；CMake将其编译成 `custom_zh_CN.qm`放入 `:/i18n`，`CustomPlugin`匹配中文locale时安装translator。本轮可翻译字符串集合未变，当前按UTF-8 XML实测与英文模板保持相同的18个context、146条message和139个唯一source，146条全部finished，unfinished和空译文均为0，context/source集合差异为0；本轮只完成TS结构/译文统计，未把完整QGC构建或真机验收写成已通过。 |
| `custom/translations/custom-lupdate.sh` | Bash 翻译维护脚本：优先使用 `LUPDATE` 环境变量指定的工具，否则从 `PATH` 查找 Qt 6 `lupdate`；先扫描 `custom/src` 更新 `custom.ts`，再更新所有 `custom_*.ts`，并用 `-no-obsolete` 清理失效条目。它只更新 TS，不生成 `.qm`，新增/unfinished 条目仍需人工翻译和复核。 |
| `translations/qgc_json_zh_CN.ts` | 原生JSON元数据简体中文目录的受控数据修正。`ChibiOS,NuttX`和 `apmVehicleType` 五项都使用ASCII逗号，后者精确翻译为 `多旋翼,直升机,固定翼,地面车辆,水下航行器`，使译文拆分数与source一致并消除FactMetaData enum mismatch；不改变Fact值、固件筛选或custom翻译统计。 |

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

当前分支 `SecDev/ft/gimbal` 的二次开发业务仍位于 `custom`。`src` 差异登记为以下十个文件：前两个是custom PX4模块正常链接所需的受控例外，中间两个为所有原生VideoReceiver提供通用串行生命周期与RTSP退避，随后四个提供通用启动URI快照、OPTIONS请求兼容、teardown诊断与无媒体恢复扩展，另一个修正通用日志级别过滤，最后一个修正模拟相机对VideoManager通知信号的连接。GStreamer/GIO默认直连策略已置于 `custom/src/CustomPlugin.cc`，不新增该项core受控例外：

| 文件 | 修改原因 |
|---|---|
| `src/CMakeLists.txt` | 原生 PX4 Factory 被关闭时仍链接 `AutoPilotPluginsPX4Module`，保证 VehicleSummary 和 CustomAutoPilotPlugin 使用的 PX4 QML 页面存在。 |
| `src/Vehicle/VehicleSetup/VehicleSummary.qml` | 注释 APM QML import；当前构建关闭 APM 模块，继续导入会造成运行时 `module QGroundControl.AutoPilotPlugins.APM is not installed`。 |
| `src/VideoManager/VideoManager.h` | 为每个原生主/thermal `VideoReceiver` 保存通用生命周期状态：唯一restart Timer、generation、RTSP retry index、期望运行状态、start/stop pending、stop后是否重启、URI快照及terminal cleanup门。该状态不读取custom设置、不识别Video 1/2产品映射或设备地址。 |
| `src/VideoManager/VideoManager.cc` | 初始化receiver时只登记状态，不再立即start；render初始化完成后通过GUI线程唯一入口启动，消除冷启动的第二个首次start。start/stop/restart串行化并防止重复pending；RTSP异常停止按1/2/4/8/15秒精确Timer退避，只有首个解码/sink帧后清零。每次取消或URI/期望状态变化递增generation，回调还复核URI和期望运行状态，旧Timer不能跨设置或新代次复活。空URI的pending start完成后会立即停止，cleanup后排队的render job/start/timer也不得复活receiver。非RTSP保持1秒失败重试，不包含相机型号或产品URL。逐次restart调度日志已删除，退避验证使用时间戳/连接抓包，不改变Timer行为。 |
| `src/VideoManager/VideoReceiver/VideoReceiver.h` | 在通用receiver基类中增加携带本轮冻结URI的 `onStartAttempt(uri)` 信号；异步完成观察者不得再从可变 `uri` 属性推断旧管线实际endpoint。该基类不再暴露应用自定义的RTSP传输枚举或状态，也不读QSettings、不识别Video 1/2、不包含相机型号或IP。 |
| `src/VideoManager/VideoReceiver/GStreamer/GstVideoReceiver.h` | 保存上次RTSP URI、本attempt是否收到source frame，以及同URI持久OPTIONS兼容状态与active快照：0为标准、1为基本头部、2为跳过OPTIONS。另以成功start代次保存待完成stop标志，使用原子状态记录“正在进入NULL的teardown”和最后一次允许发送的RTSP method，以互斥快照保存实际pipeline URI，并用原子整数保存最近一次已报告的RTSP resource-error `(code, lastMethod)`签名。URI变化或转为非RTSP会复位兼容级别，URI变化或重新收到媒体帧还会复位错误签名；头文件不再保存任何自定义传输快照或回退状态。 |
| `src/VideoManager/VideoReceiver/GStreamer/GstVideoReceiver.cc` | GUI调用start时先冻结URI和low-latency并随任务传入私有worker，worker不再读取可能由设置线程并发修改的URI，也不会把旧attempt误记为新地址；实际pipeline URI以互斥副本供bus、watchdog和before-send线程读取。创建标准 `rtsp://` 的 `rtspsrc` 时始终不设置 `protocols`，完整保留GStreamer原生Auto协商；Auto可能协商出UDP或interleaved TCP，但程序不再维护强制TCP、5秒TCP专用超时或TCP到Auto回退。首帧前精确 `RESOURCE_READ + OPTIONS + Received end-of-file` 以active快照和CAS推进OPTIONS兼容：级别1启用 `short-header`并仍发OPTIONS；再失败进入级别2，跳过OPTIONS并关闭keepalive后继续DESCRIBE。各级均保留 `rtspsrc` 原生 `udp-reconnect=true`。逐attempt启动、source配置、逐method/header、PAUSE/TEARDOWN和skip过程日志已删除；`before-send`只保留内部method跟踪与抑制语义。`rtspsrc + RTSP(S) URI + GST_RESOURCE_ERROR`的 `(code,lastMethod)`与上一条已报告签名不同时输出warning，紧接着重复相同签名降为debug；URI变化或source帧恢复时复位。其他GStreamer error仍为critical；结构化错误均含 `uri/source/domain/code/lastRtspMethod/message/debug`。成功start代次最多一次stop completion；常规info只保留source首帧、实际decoder实例/首输出和sink首帧。RTSP watchdog下限为8秒。该通用hook不识别MT11/A8，不改URI、caps、延迟或decoder选择。 |
| `src/VideoManager/VideoReceiver/QtMultimedia/QtMultimediaReceiver.cc` | 与GStreamer实现保持通用生命周期信号契约，在实际设置媒体源前发送 `onStartAttempt(_uri)`；不改变QtMultimedia播放、解码或停止逻辑。 |
| `src/Utilities/QGCLogging.cc` | 自定义消息处理器按收到的 `QtMsgType` 调用 `QLoggingCategory::isEnabled(type)`。旧实现无论消息级别都检查 `isDebugEnabled()`，当 `qgc.*.debug=false` 时会错误吞掉同类别的info、warning和critical；修复不改变各类别自身的过滤规则。当前RTSP resource error会把紧接着重复的相同签名降为debug，签名首次出现或发生变化时仍为warning；其他GStreamer error保持critical。因此关闭debug仍能看到首次/变化后的RTSP结构化warning和全部非资源型关键错误。 |
| `src/Camera/SimulatedCameraControl.cc` | 把构造函数中错误连接的 `&VideoManager::hasVideo` getter改为真实通知信号 `&VideoManager::hasVideoChanged`。该修复消除295d启动时三条 `QObject::connect: signal not found in VideoManager`，使模拟相机信息在视频可用状态变化时正常更新；不改变真实A8/MT11 receiver、RTSP或解码状态机。 |

除上述十个已登记文件外，当前二次开发功能没有其他 `src` 差异。两路URL、默认策略、设置UI、GIO默认直连、receiver识别及第二路编排仍位于 `custom`；核心只提供与产品无关的原生receiver串行生命周期、退避、启动URI快照、原生Auto RTSP source、OPTIONS兼容及诊断，不知道A8/MT11或任何产品地址。顶部云台栏自动接管、底部航向罗盘条、Android H.264/H.265优先策略与 USB 飞控连接修复都完全位于 `custom`；根目录 `android/src` 保持原样，Android APK 通过构建目录 overlay 使用 custom Java 实现。

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

custom同路径 `FlyView.qml` 延续原生全屏语义：Video 1或Video 2全屏时都隐藏工具栏、三视图PIP、WidgetLayer和custom overlay，所以罗盘条与母线告警均不显示；退出全屏后恢复。普通Map/Video 1/Video 2居中切换和Viewer3D不受影响。

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
| `forceAndroidH265HardwareDecoder` | bool / `true` | 兼容保留的旧QSettings键，仅Android生效；UI显示为“优先Android H.264/H.265硬件解码”。缺省开启后，H.264优先经过筛选且接受 `avc` 的厂商androidmedia decoder；H.265同时支持直接兼容 `hvc1` 的候选和 `hvc1 -> byte-stream/au` adapter。软件rank保留。修改后必须重启QGC。双receiver能否同时获得独立厂商MediaCodec实例必须真机验证；失败时可临时关闭做软件/自动解码A/B，已有用户选择不被metadata缺省覆盖。 |

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

RTSP URL 的 `.264` 后缀只是 A8 Mini 的固定路径名，不代表当前一定为 H.264；QGC 依据 RTSP SDP 中的 `H264`/`H265` 编码声明组建管线。Android策略同时处理两种编码，但路径不同：H.264只提升经过androidmedia插件与厂商名称筛选、静态sink caps接受 `avc` 的直接decoder候选，不注册H.264 adapter；H.265另有 `hvc1` 直接候选和Annex-B adapter路径。

开启硬解优先后，策略在 GStreamer 初始化后、`decodebin3` 创建播放管线前执行：

- 原生 Stable V5.0 在 `parsebin` 阶段把 H.265 强制为 `hvc1`；部分 Android 厂商 MediaCodec 只声明接受 Annex-B `byte-stream,alignment=au`，因此仅修改原厂 decoder rank 无法让它进入候选集合。
- custom只把实际所属插件为 `androidmedia` 且名称通过厂商筛选的decoder当作MediaCodec候选，排除 Google OMX、C2 Android、C2 Google、C2 Goldfish、secure、`*.sw.dec`、Qualcomm `*swvdec`、software/FFmpeg decoder；若系统同时暴露名称带 `lowlatency`、`low_latency` 或 `low-latency` 的专用H.265组件则优先尝试，再按原 rank 逐个执行“元素可创建、静态管线可链接且 decoder 可进入 READY”预检。
- H.264候选若静态sink caps直接接受 `video/x-h264,stream-format=avc`，rank提升到 `GST_RANK_PRIMARY + 3`（259）；不直接接受avc的H.264候选只记录兼容性，不注册转换adapter，也不改变软件decoder rank。
- 当前项目固定的 GStreamer 1.22.12 `amcvideodec` 没有暴露可由应用设置的 low-latency 属性，因此 custom 不能通过 `g_object_set` 伪造 Android `KEY_LOW_LATENCY`；本实现使用厂商专用低延迟组件（存在时）、真实硬解、GL-compatible raw caps 和限长输出队列控制延迟。
- 找到可用Annex-B候选后注册 `qgcandroidh265hwdec`，它的外部 sink 接受 `hvc1`，内部执行 `h265parse(config-interval=-1) -> byte-stream/au -> 厂商 MediaCodec`。适配器rank为 `GST_RANK_PRIMARY + 2`（258），高于原生 `Force software decoder` 的257，但低于direct hvc1候选。
- 若设备厂商H.265解码器本身直接接受 `hvc1`，不经过适配器即可使用；该direct候选提升到 `GST_RANK_PRIMARY + 3`（259），因此decodebin优先走更简单的direct路径，adapter作为次级格式兼容回退。
- 适配器内部不创建软件解码器。所有原生软件 ranks 保留；适配器无法注册或在自动建链阶段不能使用时，`decodebin3` 仍可选择外层软件 decoder，避免无兼容硬解设备直接黑屏。MediaCodec 收到具体 profile/level 后才发生的运行期配置失败不保证自动切换，需依据首帧日志和完整 logcat 判断。
- 厂商 decoder 后的 raw queue 设为 downstream-leaky、最多 2 帧、字节/时间不设上限；显示端阻塞时丢弃旧 raw frame，不丢压缩 H.265 AU，不破坏参考帧链。
- `hvc1 -> byte-stream` 只发生在 tee 后的播放解码支路，录像支路继续使用原生 `hvc1`，不会因本次适配器改变封装格式。
- Android 首次安装默认值时，仅当 A8 Mini URL 匹配且用户从未保存 `Video/lowLatencyMode` 才将它设为 `true`；用户已有的开关选择始终保留。

H.265 adapter首帧日志使用 `vendor MediaCodec candidate`，只表示经过插件与名称筛选的候选已输出raw frame，不能表述为硬件已确认。两种编码都必须在同一receiver上依次核对decodebin实际创建的plugin/factory/instance、该decoder src首个输出buffer的caps，以及 `First decoded video frame reached the sink`。仅看到候选、rank、adapter注册或输入caps均不等于画面链路成功；系统级硬件属性仍须在API 29+结合 `MediaCodecInfo.isHardwareAccelerated()`确认。

使用流程：

1. 电脑或遥控器网口连接A8 Mini，确认可访问 `192.168.144.25`；私有相机控制无需连接飞控，也无需等待QGC出现活动Vehicle。
2. Application Settings -> Fly View -> SIYI Gimbal Camera 中确认IP、端口、缩放分度值和Enabled。页面标题说明保持简短，以免撑坏设置页布局；tap/hold、唯一min锚目标表、卡录分辨率有效端点和成功发送即显示目标等完整要求以本节和Fact元数据为准。
3. Application Settings -> Video -> Local Video Storage 按需开启 `Save photos and videos locally`，并确认原生录像格式、最大本地视频存储和应用数据位置；该开关即时生效。Video Stream Integration 中选择是否使用 MAVLink 自动视频流；双路Android验收时保持缺省的H.264/H.265硬解优先开启，修改后重启QGC。只有在候选实例创建或协商失败的A/B排障中才临时关闭。
4. 返回Fly View。只要Gimbal Enabled，右侧单个纵向合并栏就应立即显示，不要求飞控或云台已连接；从上到下依次为 `+`、当前目标倍率、`-`、拍照/录像图标按钮及 `SD`/`LOCAL` 状态徽标。空闲时两个相机按钮应同为圆形且图标等大，录像按钮不应出现“录像/REC”文字；计时、pending或失败文字只在对应状态下显示。尚未确认受支持视频流或卡录能力时倍率显示 `--`且缩放按钮禁用，但开启本地存储且视频正在流式传输时录像按钮仍允许开始本地独立录像。视频第一帧只建立拉流会话门控；随后应看到卡录分辨率、映射上限及最终有效上限的能力摘要。最终上限按卡录4K=1.0、2K=3.5、1080P=5.5、720P=6.0确定，0x16只允许收紧。
5. 短按 `+`/`-` 每次立即发送同一合法目标表中的相邻一档，发送成功即显示target。默认步长1.0x时，2K卡录严格沿1→2→3→3.5往返，1080P卡录沿1→2→3→4→5→5.5往返，720P卡录沿1→2→3→4→5→6往返；拉流设为1080P但卡录为2K时仍必须使用3.5上限。快速点击 `+++`应立即依次发出并显示合法目标，后一次现场替换前一次目标。按住420 ms后进入hold，普通路径抓包应只出现一次0x05 `+1/-1`开始命令、release/cancel或目标到端点时的0x05 `0`停止及一份有界安全重复，不得周期性出现0x0f；若成立时第一目标就是端点，则只出现一次同方向端点0x0f而不出现0x05。显示目标档数必须等于 `qRound(totalMs / 600.0)`并沿按下方向单调；普通release完成最后一次时间计算，取消路径不推进显示目标。0x18运动中raw只更新独立实际值，不得覆盖当前目标显示或触发释放、断流重连后的0x0f纠偏。
   应用日志不再逐包打印SDK发送/接收、周期0x16/0x18/0x20回包、未变化能力确认或长按120 ms目标推进。正常测试只保留拉流会话与卡录能力就绪、单击目标发送/实际确认、长按开始/停止及停止后一次目标—实际倍率核对；超时、非法业务payload、分辨率不支持和安全上限冲突仍使用告警日志。来源IP不匹配、错误帧头/长度/CRC及非ACK帧会静默丢弃，协议级逐包检查应使用抓包工具，不依赖应用控制台。
6. 使用纵向栏下部的相机图标拍照：开启本地开关时应同时得到SD反馈（若相机/卡可用）和 `Photo/*.jpg` 当前帧截图。点击录像后分别观察 `SD` 与 `LOCAL`；相机toggle约400 ms后查询0x0a并以2.5秒确认超时保护，本地支路按实际 `VideoManager::recording()`进入红色状态。组合计时只要任一支路实际捕获即显示，停止按钮在另一支路离线后仍可用。
7. 分别在“有卡+SDK在线”“无卡”“SDK离线但RTSP正常”“RTSP断流但SDK在线”场景验证支路独立性；随后连接或断开飞控，控制栏不应消失或切换后端。只有关闭Gimbal设置时，有活动飞行器才恢复原生 `PhotoVideoControl`。

#### 8.3.2 通用双视频、UniPod MT11私有SDK与双相机控制

| Fact | 范围/默认值 | 说明 |
|---|---|---|
| 原生 `[Video]/rtspUrl` | string / `rtsp://192.168.144.25:8554/main.264` | Application Settings -> Video -> Connection 中的 `RTSP URL 1`，供原生 `VideoManager` 创建 Video 1 receiver；由custom部署默认安装器仅在未配置或命中旧 `rtspt` 默认时写入，不覆盖其他用户地址。 |
| `[Video]/secondaryRtspUrl` | string / `rtsp://192.168.144.24:8554/video1` | 同一Connection组中的 `RTSP URL 2`，供 `DualVideoManager` 创建通用 Video 2 receiver；空值禁用Video 2，与配置URL 1或主receiver实际生效URI相同时为防止重复拉流而不启动。 |
| `mt11Enabled` | bool / `true` | 只启用MT11私有SDK控制和右栏MT11选项；不再决定Video 2是否出现或是否拉流。 |
| `mt11SdkHost` | `192.168.144.24` | MT11私有UDP SDK主机，只用于0x05/0x0A/0x0B/0x0C/0x0F/0x10/0x11/0x16/0x18相机命令和反馈。 |
| `mt11SdkPort` | 1–65535 / `37260` | MT11私有UDP SDK端口；与两路RTSP端口、连接状态和故障状态相互独立。MT11回包除来源逻辑IP必须匹配 `mt11SdkHost` 外，源端口也必须精确等于本值。 |
| `mt11ZoomStep` | 0.1–29.0 / `1.0x` | MT11短按0x0F精确倍率步长；设置页与A8步长并排显示，两项Fact及保存值相互独立，但两种设备策略都复用通用 `ZoomStepPolicy`网格算法。只有私有实测倍率和从上一显示档位计算的目标都在1.0～30.0x时允许短按；实测大于30.0x时两方向tap availability均关闭，QML短点不调用Manager。hold改用0x05原生连续变倍，可覆盖1.0x到设备0x16上限且产品策略最多165.1x；步长仍决定反馈越过哪些合法显示档位，但不把0x05运动离散成多条绝对命令。在1～30x内，tap发出的0x0F还在pending时可立即转入hold；Manager使用有序停止/方向交接而不等待0x0F确认超时。MT11显示上限是从1.0锚定且不超过物理上限的最后完整步长：默认1.0时物理165.1对应UI 165.0，步长0.1时才可显示165.1。 |

SDK控制endpoint与RTSP视频URL在程序职责上保持分离。两个URL只表示Video 1/Video 2，不表示A8/MT11型号；当前产品本地拍照/录像映射仍明确为 Video 1 receiver -> A8 Manager、Video 2 receiver -> MT11 Manager，右侧控制栏也仍依用户选择调用A8或MT11 SDK。这一产品映射不会将通用视频UI重新命名为设备型号。SDK在线不证明对应RTSP能解码，RTSP有画面也不证明缩放、拍照、录像或热成像命令可用。两路RTSP receiver都固定不设置 `rtspsrc.protocols`，由GStreamer原生Auto协商实际下层传输；协商结果可以是TCP，不能把“删除强制TCP开关”误解为禁用TCP。

默认步长1.0x时，QGC实际生成的绝对目标是严格的1.0、2.0、3.0、4.0…，中心数字不应出现非网格值。但固定 `+1.0x` 的相对画面增量按 `(z+1)/z` 递减：1→2为100%、2→3为50%、3→4约33%、4→5为25%，所以低倍档位视觉跨度明显大于4x之后并不能证明步长失效。《UniPod MT11 v1.2》p16给出广角4.5 mm和变焦镜头15～50 mm，`15/4.5 ≈ 3.33`；p37的8K拍照逻辑以3.3x/11x分段，因而约3.3x还可能存在由双镜头焦距段造成的额外体感拐点。后一点是依手册参数得出的设备成像链路推断，不是QGC在4x后改变步长或对0x0F重映射。

升级兼容分为两条。MT11 SDK Host仍使用 `[GimbalControl]/mt11SdkHostDefaultMigrationVersion`：只有已存在的Host精确等于旧默认 `192.168.144.25` 时首次改为 `.24`。`VideoCustomSettings` 在 `[Video]/secondaryRtspUrl` 尚不存在时读取旧 `[GimbalControl]/mt11RtspUrl`：只把精确历史出厂默认 `.25/video1` 转成 `.24/video1`，其他用户自定义值和空字符串原样复制；新键已存在则不覆盖，旧键也不删除。新安装没有旧键时才使用 `.24/video1` 的新JSON缺省。旧版本写入的 `[Video]/primaryRtspTcpOnly`、`[Video]/secondaryRtspTcpOnly` 不再注册或读取，也不迁移、覆盖或删除；本版运行忽略它们，原值留在QSettings中供必要降级使用。

界面和控制流程：

1. Application Settings -> Video先选择RTSP源，再在同一Connection组配置 `RTSP URL 1` 与 `RTSP URL 2`。产品默认分别为 `rtsp://192.168.144.25:8554/main.264` 与 `rtsp://192.168.144.24:8554/video1`；URL 1严格对应Video 1，URL 2严格对应Video 2，两者都不使用相机型号作为设置名。界面不再提供RTSP传输开关，两路都使用原生Auto；URL仍填 `rtsp://...`，不填 `rtspt://...`。修改URL 2时 `DualVideoManager`有序停止并重启同一receiver，以按新地址重建播放管线；URL 2与配置URL 1或主receiver实际生效URI相同时显示明确警告并禁用Video 2。Application Settings -> Fly View的仪表板正下方固定显示单层加载的相机设置组，它不依赖云台在线；分别配置A8与MT11的Enabled/SDK Host/SDK Port，两套独立Zoom Step位于组内第一项，桌面并排、窄屏堆叠。MT11默认1.0x，决定1～30x短按目标和hold反馈跨越时的合法显示网格；0x05长按物理范围仍由0x16设备能力和165.1x产品封顶决定。
2. Fly View同时提供Map、Video 1、Video 2三个可选视图，同一时刻仅一个居中全尺寸显示，其余可用项位于左下的固定下槽/上槽。点击任一槽后，该视图立即居中，原主视图精确回到这个被点击槽位，另一槽不移动。选择保存为 `MainFlyWindowView`并兼容 `MainFlyWindowIsMap`；失效视图若正居中则回退Map。
3. Video 1继续由原生 `VideoManager`拥有receiver，Video 2由 `DualVideoManager`拥有第二个独立 `VideoReceiver`、sink、解码/重试状态和通用videoItem，两路独立并行启动并可同时显示，不等待另一流时间戳或首帧。两个receiver创建 `rtspsrc` 时都不设置 `protocols`，各自独立完成原生Auto协商；一路断流或重启不共享也不停止另一路。第二receiver由Loader直接传入实际GL Item与window，再经过 `BeforeSynchronizingStage` 并确认 `itemInitialized=true` 后才启流；解码启动失败或RTSP已streaming但首帧超时时重建自身管线，不影响Video 1。当前本地媒体映射下，A8 Manager只保存Video 1非thermal receiver，MT11 Manager只保存Video 2 receiver；任一录像按钮只开始/停止本Manager确认owned的录像。
4. 右栏在A8与MT11同时启用时显示相机选择器；切换只更换当前控制Manager，不改变居中视频。MT11在1～30x内可交替使用短按与长按：短按一次发送一个步长网格目标；随后即使0x0F仍在pending，按住420 ms也应进入有序的0x05停止→方向交接，不得无反应，也不得把一次release补解释为tap。两套面板保持相同缩放、拍照、录像、SD/LOCAL徽标和布局；选择MT11时在拍照控件上方额外显示RGB/IR圆形按钮。点击后发送0x11 `[2,0]`切到热成像主流，或 `[0,2]`切回RGB主流；pending期间禁止重复点击，只有0x10/0x11合法模式反馈才更新显示。
5. Video 1或Video 2居中时均保留原生风格的双击全屏；两个PIP均保留原生显示/隐藏、独立窗口和右上拖拽缩放。全屏会统一隐藏Fly View工具栏、PIP、WidgetLayer及custom overlay，退出后恢复。Video 2使用Video设置中的fit/grid与Proximity/Obstacle视频叠加；独立PIP弹窗开关前后暂停第二路并延迟2秒重启。

已知运行边界：清空URL 2、把Video Source切离RTSP、关闭全局stream或URL 1/2触发判重时，`DualVideoManager` 会同步停止并释放Video 2 receiver。当前产品映射下，释放前会通过DirectConnection调用MT11 `shutdownLocalMedia(true)`；如果此时恰在Video 2本地录像封装或Android媒体发布路径中，设置操作可能短时阻塞UI。这一点尚未改成异步释放状态机；真机验收和实际操作中应先停止Video 2本地录像、等待媒体收尾，再修改URL或Video Source。

MT11帧格式为 `55 66 | control | payload length LE | sequence LE | command | payload | CRC16 LE`；请求control为0x01、普通ACK为0x02，生产请求sequence固定0。CRC多项式 `0x1021`、初值0，覆盖CRC字段前的全部字节。当前命令职责为：0x05手动变倍/停止、0x0A相机系统/录像状态、0x0B异步功能反馈、0x0C拍照/录像切换、0x0F绝对倍率、0x10读取视频模式、0x11设置RGB/热成像模式、0x16最大倍率、0x18当前倍率。PDF规定0x0F只支持1.0～30.0x，payload为“整数byte + 一位小数byte”；0x05 ACK是十分之一倍率的 `uint16 LE`，所以 `73 06`为165.1x；0x16/0x18是“整数byte + 一位小数byte”，所以 `a5 01`同样为165.1x。Protocol/Sdk仍严格解析0x05 ACK以校验线格式，但固定sequence下停止与方向请求的同command ACK无法可靠归属，因此Manager只用0x18作为倍率位置真值。Protocol/Sdk只按线格式验证1.0～255.9x，Manager才将本产品可操作上限封顶165.1x；A8的卡录分辨率映射不参与MT11。除文档规定可异步到达的0x0B外，接收侧要求来源逻辑IP等价于 `mt11SdkHost`且源端口精确等于 `mt11SdkPort`、整批UDP子帧长度与CRC全部合法、普通回包control为ACK且命中同command最近1.5秒请求窗口。

当前 `Mt11ProtocolTest` 独立运行报告11 passed、0 failed，覆盖9个协议/策略业务slot及QtTest init/cleanup，包括0x05的165.1/255.9边界、0x16/0x18的 `a5 01`/`ff 09` 边界、0x0F 30.1x拒绝、通用步长网格、MT11显示目标/tap/hold反馈策略，以及tap pending→hold时的显示起点与方向可行性。`SiyiProtocolTest`独立运行报告42 passed、0 failed（40个业务slot及init/cleanup），`Mt11Sdk.cc`也已独立编译通过；Manager头文件moc检查0错误，diff-check通过。这只证明纯Protocol/Policy、独立Sdk编译与Manager接口静态范围。Manager的250/350 ms轮询、150 ms交接/方向副本/停止计时、停止后settled反馈门禁、6.5秒新鲜度、真实UDP往返、固件ACK、镜头运动/边界停止、模式切换、完整QGC Qt 6构建、双路长期解码、本地媒体和双设备网络仍需后续验收。

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
  -> CustomPlugin 构造（QGC_GST_STREAMING）
     -> GStreamer/GIO初始化前设置proxy policy：缺省dummy直连；QGC_GST_USE_SYSTEM_PROXY仅取1/true/yes/on时保留environment/system resolver
     -> 只影响GStreamer/GIO，不改变QtNetwork地图和下载代理
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
        -> 仅枚举androidmedia插件且通过厂商名称筛选的MediaCodec候选
        -> H.264：直接接受avc的候选提升到PRIMARY+3，不注册adapter
        -> H.265：直接接受hvc1的候选提升到PRIMARY+3；qgcandroidh265hwdec以PRIMARY+2注册作格式兼容回退
        -> 原生软件decoder rank不删除，保留fallback资格
     -> GimbalVideoStreamSupport 安装 A8 Mini 默认值
      -> Mt11ControlManager
         -> MT11 SDK独立endpoint 192.168.144.24:37260，不读取主视频URL
         -> 每2秒常规轮询0x16/0x18/0x0A/0x10；6秒无合法包判离线，0x16能力/0x18倍率位置各自6.5秒失效
         -> 私有raw由Mt11ZoomPolicy对齐为1.0锚定完整步长显示档位；默认step 1.0、物理165.1时UI末档165.0
         -> 短按：从上一显示目标按mt11ZoomStep生成0x0F精确目标，仅1～30x；发送成功立即显示，快速连点替换pending
         -> 30x以上两方向tap availability关闭，QML安静消费短点；Manager直接调用仍防御性提示仅长按
         -> 0x0F确认期间250 ms轮询0x18、最长10秒；固定sequence/command-only ACK不直接回滚，只由精确0x18或超时收敛
         -> 普通长按：0x05连续运动至min(设备0x16, 165.1x)，350 ms轮询0x18且仅越过合法档位才更新显示
         -> tap pending后长按：0x05(0)抢占 -> 150 ms后方向 -> 仍按住再150 ms最多一份同方向副本；两个等待段release/cancel都取消后续发送并立即停止
         -> 0x05 ACK不按代次更新倍率，只有主动查询的0x18是位置真值
         -> 停止立即发0x05(0)，150 ms只安全重复一次；其后的有效0x18 settled反馈到达前tap/hold保持锁定
         -> 0x0C拍照/录像；0x11切换前停止缩放并锁定旧镜头能力，0x10/0x11确认或超时后重查0x16/0x18
         -> 外部mainStream变化退休旧generation并取消旧150 ms停止副本；析构先停Timer/断SDK回调，按需同步双发停止后再等媒体收尾
         -> 当前产品映射下只保存Video 2 Item/receiver；本地媒体不操作Video 1/A8 receiver
     -> VideoCustomSettings
        -> 通用 `[Video]/secondaryRtspUrl`，默认rtsp://192.168.144.24:8554/video1
        -> 两路都保留标准rtsp:// URI；不注册自定义传输Fact，固定使用GStreamer原生Auto
        -> 旧版两个TCP偏好键不读取、不迁移、不删除，本版忽略而保留降级数据
        -> 新键缺失时读取旧 `[GimbalControl]/mt11RtspUrl`：精确历史 `.25/video1` 默认转为 `.24/video1`，其他值/空值原样复制，不删旧键
     -> DualVideoManager
        -> 读取通用secondaryRtspUrl；重复源同时比较主流configured/current/starting/active/releasing URI
         -> 为Video 2创建独立VideoReceiver、sink、启动/停止和1–15秒有上限退避重试状态；主路被动通知保留现有deadline，只有真实配置变化取消Timer
        -> `onStartAttempt`冻结本轮starting URI，成功后转为active；stop完成后至少保留1000ms releasing guard
        -> QML Loader把secondaryVideoContent和当前window直接交给initVideoItem，findChild只作兼容回退
        -> secondaryVideoContent保持在场景图；等待BeforeSynchronizingStage及itemInitialized=true后与Video 1并行启流
        -> 创建receiver后沿用core原生Auto；URL变化stop/start同一receiver、只重建Video 2 GStreamer管线
        -> startDecoding失败或streaming后10–30秒无首个解码帧时停止并重建自身管线
        -> URL为空/重复、设置变化和cleanup时有序停止并释放；terminal cleanup禁止late Timer/signal重新创建receiver
        -> videoObjectsAboutToBeReleased先通知MT11 Manager收尾owned本地录像
   -> VideoManager::init()
      -> receiver初始化只登记生命周期；render同步后经GUI线程唯一start入口启动
      -> 原生receiver start/stop pending串行化；RTSP失败按1/2/4/8/15秒退避，generation淘汰旧Timer
      -> 创建 VideoReceiver / decodebin3
         -> deep-element-added记录每个实际decoder的receiver/name/URI/plugin/factory/instance
         -> 每个decoder src首buffer记录同一身份、bytes与caps；随后sink另记首帧
        -> CustomPlugin::createVideoSink()复用原生qgcvideosinkbin
           -> receiver父对象为DualVideoManager时识别为通用Video 2；当前本地媒体映射中只接入MT11 Manager，不进入A8探针/Video 1 receiver路径
           -> 不注入RTSP传输偏好；Video 1/2各自使用core原生Auto
           -> PulledVideoResolutionProbe在真实首帧读取sink/peer/ghost-target current CAPS
           -> 同时观察GstGLQt6VideoItem由最终GstVideoInfo写入的隐式尺寸
           -> 保存主非thermal receiver；本地录像只直调该receiver start/stop
           -> 主receiver onStartRecordingComplete + recordingOutput回传Manager
           -> receiver既有信号继续由VideoManager更新recording与字幕
           -> 两路尺寸直达Manager；pad路径另发布到VideoManager，thermal流不参与
         -> H.264 avc可由被提rank的直接厂商androidmedia decoder处理
         -> 原生 parsebin 输出 H.265 hvc1
         -> qgcandroidh265hwdec（仅H.265 adapter路径）
           -> h265parse -> byte-stream/au
           -> 厂商 amcviddec-* -> raw leaky queue（2 帧）
        -> qgcvideosinkbin / qml6glsink
     -> GstVideoReceiver::_makeSource(rtsp://...)
        -> 始终不写protocols，保留原生rtspsrc自动协商；GStreamer仍可自行选中TCP
         -> 同URI OPTIONS EOF按0标准、1基本头、2跳过OPTIONS逐级兼容
         -> 各级保留原生udp-reconnect，跳过OPTIONS级关闭RTSP keepalive；before-send仅在内部跟踪method并执行抑制
         -> 进入NULL后teardown flag保持到下一start，仅抑制PAUSE、允许TEARDOWN，teardown-timeout=1s
         -> 每个成功start代次最多一次stop completion；URI已清空仍释放旧pipeline
        -> URL、SDP、parsebin/decodebin3和解码器选择不变
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
  -> GimbalControlSettingsGroup.qml
     -> Zoom Step（A8 Mini / MT11桌面并排、窄屏堆叠）
     -> SIYI A8 Mini SDK
     -> UniPod MT11 SDK
  -> Viewer3DSettingsGroup.qml
```

```text
Application Settings / Video
  -> 原生 AppSettings.qml 请求 VideoSettings.qml
  -> CustomOverrideInterceptor 映射到 custom VideoSettings.qml
  -> 保留原生 Video Source / Connection / Settings / Local Video Storage
     -> Connection（选择RTSP源时）
        -> RTSP URL 1 = 原生 `[Video]/rtspUrl` -> Video 1，产品默认rtsp://192.168.144.25:8554/main.264
        -> RTSP URL 2 = `[Video]/secondaryRtspUrl` -> Video 2，默认rtsp://192.168.144.24:8554/video1
        -> 两路固定使用原生Auto：URL保持rtsp://，应用不设置rtspsrc.protocols
        -> URL 2空值禁用Video 2；与URL 1设置值或主receiver实际URI相同时警告并禁用第二receiver
     -> localMediaStorageEnabled（默认true，即时控制本地照片与录像附加支路）
     -> recordingFormat / maxVideoSize继续决定格式与总量门限
     -> 桌面使用AppSettings Photo/Video；Android使用同卷getExternalFilesDirs暂存后发布到公共Pictures/Movies
     -> Android公共清理只管理当前安装注册且含_local_NNN锚点的Movies录像，兼容同名后缀
     -> 不以配额删除未公开Staging；失败源额外占用空间直至重试成功/用户处理
  -> Video Stream Integration
     -> mavlinkAutoVideoStream
     -> forceAndroidH265HardwareDecoder（兼容旧键；UI为H.264/H.265硬解优先，默认true；仅Android生效且修改后重启）
```

```text
Fly View
  -> custom同路径 FlyView.qml；继续复用原生 FlyViewWidgetLayer.qml / FlyViewToolStrip.qml
  -> DualPipView.qml
     -> item1 Map / item2 Video 1 / item3 Video 2
     -> 左下固定下槽/上槽；点击槽与主视图精确交换，另一槽不移动
     -> 原生显示/隐藏、独立窗口、右上拖拽resize；选择持久化为MainFlyWindowView
  -> FlyViewSecondaryVideo.qml + FlightDisplayViewSecondaryVideo.qml
     -> 通用Video 2独立receiver画面、原生fit/grid、Proximity/Obstacle视频叠加
     -> PIP窗口切换前后停流并延迟2秒重启
  -> Video 1或Video 2全屏时统一隐藏工具栏、PIP、WidgetLayer和custom overlay
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

Android Gradle缓存规范：源码目录 `android/.gradle` 已从Git索引移除并由根 `.gitignore`忽略；`android/gradle`是必须保留的Gradle Wrapper目录，两者不能混淆。custom configure会在合并后的构建副本中写入 `org.gradle.configuration-cache=false`，普通 `org.gradle.caching`仍按原生配置保留。首次使用该修复时，应只删除或重命名目标Android生成工程的 `android-build-Custom-QGroundControl/.gradle/configuration-cache` 后重新configure/打包，不自动删除整个 `~/.gradle`。若仍出现 `pending instrumentation exception`，先执行一次 `./gradlew --stop`，再用 `--no-configuration-cache --no-build-cache --no-daemon --stacktrace`复核；该命令成功说明源码、Qt/JDK/AGP/Gradle组合可构建，异常位于Gradle状态复用链，不能据此把QML/C++业务代码判为编译失败。

重点验证：

1. Application Settings -> Fly View -> Instrument Panel显示 `Show Heading Compass Bar`；其正下方必须立即显示Gimbal设置组，再往下才是Viewer3D。Gimbal组不是条件隐藏，组内第一项是A8 Mini与MT11独立Zoom Step（桌面并排、窄屏堆叠），随后依次显示 `SIYI A8 Mini Gimbal Camera`和 `UniPod MT11 Gimbal Camera`两套SDK设置；host/port及两个步长Fact各自保存。MT11默认1.0x，修改后决定1～30x短按0x0F步进及长按反馈跨越时发布的合法显示档位，但不能改变0x05物理运动上限；MT11组只提示两路视频URL统一位于Video -> Connection，不再显示MT11专用URL名称。
2. Application Settings -> Video保留全部原生设置组；选择RTSP源后，Connection同组只显示 `RTSP URL 1` 和 `RTSP URL 2` 两个视频地址，不再出现 `Prefer RTSP-over-TCP` 开关。新安装默认必须分别为 URL 1 `rtsp://192.168.144.25:8554/main.264`、URL 2 `rtsp://192.168.144.24:8554/video1`；两个地址都保持标准 `rtsp://`。用GST_DEBUG/pcap确认两路 `rtspsrc` 均未由应用设置 `protocols`，实际SETUP传输由GStreamer原生Auto协商，协商为TCP仍属于正确结果。分别预置旧版两个TCP偏好QSettings键为true/false并重启，本版界面、source属性和协商策略都不得受它们影响，且旧值不得被迁移、覆盖或删除。清空URL 2后Video 2移除；URL 2与URL 1设置值或主receiver实际URI相同时，页面必须告警且第二receiver不得启动。Local Video Storage保留本地照片/录像开关；Video Stream Integration在所有平台显示两个原有开关，兼容Fact键不改，但UI必须显示H.264/H.265硬解优先，缺省true且仅Android生效。
3. Viewer3D Enabled 持久化，重启后图标状态正确。
4. 3D 图标白色，2D/3D 可往返切换。
5. 本地 OSM、外部 OBJ/glTF/GLB 和可选 Google 3D 正常加载。
6. 验证云台在线发现与飞控解耦：
   - Gimbal Enabled但飞控和云台都未接入时，合并栏仍必须立即显示并占用完整布局尺寸；Manager继续在后台探测，状态点灰显、倍率显示 `--`，两个缩放按钮禁用，tap不得发送0x0f且hold不得发送0x05。开关关闭时保持原有SDK按钮门控；开关开启且RTSP主流可用时，即使SDK状态未知也必须允许本地录像，拍照必须尝试当前帧截图。错误来源逻辑IP、短包、错误帧头/长度/CRC、control不是0x02或业务payload非法都不能把 `sdkResponding`置true。
   - A8 `SiyiSdk`用测试socket分别回送原生IPv4来源和IPv4-mapped IPv6来源，二者表示同一配置IP时都必须通过来源检查；同IP但回包源端口不同且CRC合法时也必须接受。真正不同的来源IP仍须静默丢弃，具体来源应通过抓包工具验证，不再输出逐包SDK debug日志。本项仅描述A8接收策略，不适用于MT11；MT11严格端点验收见下方双云台协议矩阵。
   - 只连接A8 Mini网络、正常拉流并接通私有SDK、完全不连接飞控时，合法回包必须把状态点切为绿色；受支持拉流只确认视频会话，合法0x20录像流ACK确认卡录能力，二者齐备后缩放才解锁，随后0x18建立起始目标倍率，0x0a恢复机内录像状态。单独0x16、单独拉流尺寸或SDK绿色状态都不能代替0x20卡录能力。连接或断开飞控不得影响控制栏可见性和后端选择。
   - 空闲探测时私有SDK超过1.5秒未响应但视频仍在正常解码，合并栏不得消失，状态点可变灰；为避免沿用旧卡录设置，已确认能力失效并锁定缩放，直到新的0x20录像流参数恢复能力。真实断流同样取消活动手势并锁定视频门控；同分辨率恢复解码且SDK恢复后无需进入设置页即可重新查询0x20/0x16并发起0x18倍率同步。
7. 验证合并控制栏的输入、安全、相机状态和布局：
   - A8拉流会话来源与能力来源分离：拉流白名单仍只有1280×720和1920×1080，但二者都只建立视频可用门控，不产生6.0/5.5上限。任一直接观察器报告尺寸时应立即取消 `VideoManager`兜底；若两个直接观察器都未回调，则要求 `decoding=true`且同一白名单尺寸连续稳定1秒后才采用。`decoding=false`撤销A8 UI门控并停止活动0x05；重连同一尺寸必须重新建立门控。这套视频/卡录门禁不用于MT11。
   - A8抓包应看到Manager每2秒发送0x20录像流请求和0x16。0x20请求payload必须为 `00`，ACK中的 `stream_type`也必须为0；4K/2K/1080P/720P分别映射1.0/3.5/5.5/6.0。0x16是设备安全交叉校验：小于映射值时立即采用较小值，大于映射值时不得扩展能力。拉流1080P＋卡录2K＋0x16=3.5时最终必须为3.5；拉流1080P＋卡录1080P＋0x16=5.5时最终为5.5。未知卡录尺寸、非法0x20或超过4.5秒没有新的有效0x20时必须锁定A8缩放，即使0x16、0x0a等其他回包仍正常也不能保留旧能力。
   - A8验证0x16/0x18双格式解析。新版：`01 00`→1.0x、`01 08`→1.8x、`02 08`→2.8x、`03 05`→3.5x、`05 05`→5.5x；真机旧版：`0A 00`→1.0x、`12 00`→1.8x、`1C 00`→2.8x、`23 00`→3.5x、`37 00`→5.5x。必须逐包先尝试新版，只有新版候选越过当前A8范围时才计算完整小端uint16/10旧版候选并再次校验；两种成功路径由协议单元测试和最终倍率结果验证，不再逐包打印encoding。`00 00`、0.9x、超过当前上限、`FF FF`、非法小数字节、短/长payload及带尾随字节的完整帧均不得改变倍率或编码输出参数；这些A8兼容编码不能套到MT11。
   - A8 `-`/`+`每次tap只调用一次方向接口并立即发送同表相邻一档0x0f目标，本地发送成功后中心数字必须立即显示target。默认步长1.0x时，2K卡录严格覆盖1→2→3→3.5并反向返回，1080P卡录覆盖1→2→3→4→5→5.5，720P卡录覆盖1→2→3→4→5→6；4K两方向均不可用。快速 `+++`应依次发送并显示合法目标，后一个目标现场替换前一个目标，不存在等待、FIFO或停止点击后再派发。
   - A8按住420 ms后进入hold，普通路径只允许发送一次0x05 `+1/-1`开始命令。以手势按下起点为时间零点，显示目标档数必须等于 `qRound(totalMs / 600.0)`；只在计算档数增加时更新显示，并始终从hold起始目标沿同一方向表计算。覆盖420/600/900/1200 ms附近边界、正反方向、端点钳制和释放竞争；显示目标到1.0或卡录有效上限时必须主动停止0x05。普通release调用一次 `stopZoom()`并完成最后一次时间计算；取消路径不推进目标。活动0x05路径必须发送停止及80 ms有界安全重复，且不得出现0x0f归整。仅当hold成立时第一目标已是端点，才允许以一次同方向端点0x0f替代0x05；这套按时长目标和80 ms时序不用于MT11。
   - A8验证目标显示与实际反馈解耦：0x0f本地发送成功即显示target；运动中0x18只更新内部实际值，不能把中心数字从target改回旧档。发送失败必须保持原目标；新tap或hold目标成功后立即替换显示。合法0x16若收紧上限、0x20卡录模式变化、SDK能力失效或真实 `decoding=false`可以触发安全重同步和锁定；普通中间0x18不得如此。取消/隐藏/后台后不得在稍后重放旧hold目标或发送反向0x0f。
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
   - 双相机网络和视频设置迁移：确认A8既有SDK配置保持 `.25`不变，MT11 SDK实际发包目的为 `192.168.144.24:37260`。通用URL则按用户配置分别由Video 1/Video 2 receiver请求，SDK断开不得使通用视频框消失。为旧版 `[Video]/primaryRtspTcpOnly`、`[Video]/secondaryRtspTcpOnly` 分别预置true/false并重启，本版必须不注册、不读取、不迁移、不覆盖也不删除它们，界面和两路source均保持原生Auto；必要时降级旧版仍应读到原值。对Video 2 URL迁移构造“新键缺失+旧键精确为历史 `.25/video1` 出厂默认”“新键缺失+旧键为其他用户值”“新键缺失+旧键为空”“新旧键都不存在”“新键已存在”：第一种必须写入 `.24/video1`，第二/三种必须原样复制，第四种使用新JSON缺省，第五种绝不覆盖，且所有路径都不删旧URL键。
   - MT11后续RTSP测试前置：295d已证明系统代理开启但QGC进程使用GIO direct resolver时，URL 2 `.24/video1`可从标准OPTIONS走到sink首帧。复测仍应先彻底退出QGC及其他RTSP客户端，再断电重启MT11并等待视频服务启动；逐字符核对URL 2不得保留14:01日志中短暂出现的 `192.168.144.2`，URL 1必须与URL 2不同，产品双路基线保持URL 1 `.25/main.264`、URL 2 `.24/video1`且两路均使用原生Auto。保存后冷启动干净重编译程序，采集期间不编辑URL；普通相机环境清除 `QGC_GST_USE_SYSTEM_PROXY`，或至少不设为 `1/true/yes/on`。VideoManager和DualVideoManager已删除逐次调度、deadline及handoff日志，退避/门禁回归应使用带时间戳的socket连接抓包、状态采样和实际画面验证，不能再要求 `Keeping scheduled ... remainingMs` 等过程文本。
   - 三视图与精确槽位交换矩阵：在Map、Video 1、Video 2都可用时，记录左下固定下槽/上槽内容。点击下槽后只能是下槽与主视图交换，上槽不动；再点击上槽后只能是上槽与主视图交换，下槽不动。连续往返至少20次，每次都必须仍可点击；重启保持最后主视图选择。清空URL 2或使其与URL 1重复时Video 2候选移除，正在居中则回退Map；关闭MT11 SDK Enabled不得移除Video 2。另验证两个PIP的隐藏/恢复、桌面独立窗口以及右上角拖拽resize与原生一致。
   - 双路并行解码、全屏和叠加层：URL 1/2配置为两个不同且可用的RTSP endpoint，保持两路原生Auto和兼容Fact `forceAndroidH265HardwareDecoder=true` 并冷启动。两路都必须依次出现各自 `First source media frame reached the receiver`、`Decoder element instantiated`、`Decoder produced its first output frame`和 `First decoded video frame reached the sink`；decoder日志中的receiver与instance必须各自不同，plugin/factory必须与该路SDP编码及目标Android候选相符。H.264直接兼容avc时不会出现adapter；H.265只有走转换路径时才出现独立 `qgcandroidh265hwdec`。这些低频里程碑与两个缩略框持续实时画面共同验收双实例，不能把rank或候选日志当成功。Video 2的render-ready、Starting、成功status、streaming/decoding布尔过程日志已删除；surface顺序通过QML Item/window状态、首帧里程碑和实际画面核对。WAITING期间Loader/GL Item必须仍为active/visible。若source首帧始终没有出现，10–30秒watchdog必须停止并以1–15秒退避重建Video 2管线；正常sink首帧必须取消watchdog，不能永久停在WAITING或每秒无限重建。若反复重建，使用GST_DEBUG/pcap保留Auto协商和RTP证据；必要时只在外部 `gst-launch-1.0` 中强制UDP/TCP做诊断A/B，不把外部命令当成本程序设置。只有source媒体帧已进入但decoder没有输出时，才进入编码/硬解A/B。Video 1、Video 2居中时分别双击进入/退出全屏；全屏期间工具栏、三视图PIP、WidgetLayer、罗盘条和母线告警均隐藏。Video 2继续验证fit/grid、Proximity Radar和Obstacle Distance；断流后退出对应全屏，不得影响另一路receiver。桌面独立PIP窗口开关和Fly View surface销毁/重建后必须重新看到sink首帧并恢复画面，不能沿用旧sink或永久黑屏。
   - URL变化与停止生命周期验收：在两个非空、非重复URL间修改URL 2时，上述“停止/重建”必须保持同一 `VideoReceiver`身份并重建其GStreamer管线，不应触发 `videoObjectsAboutToBeReleased`或新建receiver。只有URL变空/重复、Video Source/全局stream关闭、cleanup或实际surface Item换代才进入receiver释放/新建路径。每个成功start代次最多只产生一次stop completion；清空URI时仍须释放旧pipeline，重复stop不得触发新的完成或重启。冷启动时同一主URI在首个错误/stop前只能有一次连接attempt，不得重现14:01双start。主路连续无媒体失败按1/2/4/8/15秒封顶，Video 2按自己的1～15秒Timer；11:58的8次提前绕过和14:28的修正时间序列保留为历史证据。当前逐次调度、remainingMs、active/releasing和1000 ms hold日志已删除，使用socket连接时间戳、receiver对象身份采样和实际endpoint占用验证deadline、generation取消与交接hold。修改URI或stream状态后，旧generation Timer不得start；把活动主URI从 `.24/video1` 交给Video 2时，旧主管线释放及至少1000 ms hold完成前Video 2不得连接同一URI，新主同URI恢复则取消清除。
   - RTSP运行包与日志验收：修改 `custom/src/CustomPlugin.cc`或 `GstVideoReceiver.cc` 后删除旧桌面构建产物或至少强制重新configure并全量重编译，确认最终可执行文件时间戳来自本轮构建。任何receiver启动前必须先看到 `GStreamer GIO proxy policy: direct resolver "dummy"`；用 `strace -f -e trace=network`复核AF_INET connect目标为 `.24:8554`/`.25:8554`而不是0e44的 `192.168.163.1:7897`。只有专门的代理opt-in回归才把 `QGC_GST_USE_SYSTEM_PROXY`设为truthy并期待 `environment/system` 日志；测试后清除变量。另验证地图瓦片和QtNetwork下载仍按原Qt代理策略工作。常规日志不再输出逐attempt `Starting/Configured`、source属性、OPTIONS兼容属性、逐method/header或PAUSE/TEARDOWN；使用 `GST_DEBUG=rtspsrc:6`、`strace`及pcap/Wireshark确认应用没有设置 `protocols`，并验证原生Auto的实际SETUP结果、OPTIONS 0→1→2、keepalive、udp-reconnect和teardown时序。RTSP(S) `rtspsrc` resource error的 `(code,lastMethod)`与上一条已报告签名不同时为warning，紧接着重复相同签名为debug；URI变化或source媒体恢复时复位；其他GStreamer error仍为critical。结构化warning/critical均须含 `uri/source/domain/code/lastRtspMethod/message/debug`。分别把 `rtspTimeout` 设为0、1、5、20，以GST_DEBUG属性或实际超时证明前三者的RTSP运行下限均为8秒、20秒不降低。播放成功以直连、source首帧、decoder首输出、sink首帧和实际画面判定；proxy policy或接口status不算成功。
   - 同步释放已知风险验收：正常操作基线必须先停止Video 2本地录像并等待Android媒体收尾，再清空URL 2、切换Video Source或制造URL 1/2重复。另做故障注入：在Video 2 owned录像/发布期间执行上述设置变更，记录DirectConnection调用 `shutdownLocalMedia(true)` 到UI恢复响应的最长时间、文件完整性和receiver释放结果。此项用于量化未解决风险，不得因为最终可恢复就判定为“已异步化”或“不阻塞UI”。
   - 右栏与热成像：A8、MT11同时启用时选择器必须出现并可往返切换，切换控制栏不能隐式切换中心视频；两套缩放/拍照/录像保持同一UI。MT11选择下RGB/IR按钮位于拍照上方，抓包应看到0x11 `[02 00]`与 `[00 02]`往返；pending期间不重复发送，0x10/0x11合法反馈后才改变显示，2.5秒无确认时恢复可操作并报错。A8选择下不得显示热成像控件。
   - MT11协议与策略边界：抓包核对0x05/0x0A/0x0B/0x0C/0x0F/0x10/0x11/0x16/0x18、production sequence 0和小端CRC。0x05 ACK `73 06`必须解析为165.1x、`ff 09`为255.9x；0x16/0x18 `a5 01`必须解析为165.1x、`ff 09`为255.9x。Protocol/Sdk接受至255.9x只代表线格式能力，Manager必须把当前产品物理操作上限钳制到 `min(设备0x16, 165.1x)`。显示上限另取从1.0锚定且不超过物理上限的最后完整步长：默认step 1.0时165.1只在私有raw/物理停止判断中使用，中心数字最高165.0；step 0.1时165.1才是合法显示档位。0x0F只允许1.0～30.0x，并拒绝30.1x；不得使用A8的精确非整步末端、卡录分辨率上限或旧版小端兼容回退解释MT11。当前 `Mt11ProtocolTest`为11 passed、0 failed（9个业务slot加init/cleanup），`SiyiProtocolTest`为42 passed、0 failed（40个业务slot加init/cleanup），`Mt11Sdk.cc`独立编译也已通过，但此项的Manager/QGC全工程及真机运动仍未验收。
   - MT11短按矩阵：默认 `mt11ZoomStep=1.0`，另覆盖0.1与29.0边界；1～30x内每次短按从上一合法显示目标计算、只发送一个量化到0.1x的精确0x0F目标，发送成功立即发布该目标。快速 `+++`必须依显示目标连续步进并现场替换在途目标，不能按 `actual + step`重建网格或排FIFO。私有实测倍率严格大于30.0x时，`+`和`-`的tap availability都为false；QML短点安静消费、不调用Manager，Manager直接调用仍防御性拒绝并提示仅支持长按。恰好30.0x时加号tap不可用，减号仍可按步长回到30x以下。0x0F生产帧固定sequence且ACK只标识command，无法证明reject属于哪个点击，因此accepted/rejected ACK都只触发0x18查询，不能直接覆盖或回滚中心显示；只有0x18精确命中最新目标或独立10秒超时才结束pending。该确认窗口不能被0x18恰好跨过6.5秒新鲜度而提前取消。
   - MT11 tap→hold交接矩阵：在1～30x内先短按发0x0F，不等待0x18确认就再按住420 ms。hold必须保留该最新合法tap目标作为显示起点，立即发0x05 `0`退出绝对控制器，150 ms后发一次0x05方向；此后仍按住时再过150 ms最多发一份同方向安全副本，不得发第三份。分别在第一个150 ms等待内和第二个150 ms等待内做release/cancel/移出/转后台：都必须取消未发出的方向或副本并立即发停止，松手后不得有迟到方向包启动镜头。正/反两方向以及实测位置处在端点而最新tap目标已离开端点的场景都必须覆盖。
   - MT11长按与新鲜度矩阵：无绝对命令pending的普通hold在420 ms成立后只以0x05开始原生连续运动，活动期间每350 ms查询0x18；0x05 ACK只维持SDK可达性，不用其内嵌倍率更新位置或边界。raw只保存在Manager内部，向上反馈到达/越过下一完整合法档位、向下反馈到达/越过上一完整合法档位时才更新中心数字，未越档的5.6等中间值不得发布。物理上向上到 `min(设备0x16, 165.1x)`、向下到1.0x时主动停止；显示仍遵守完整步长上限。release、cancel、隐藏、后台、能力/状态失效、离线和60秒watchdog都必须立即发送0x05 `0`，150 ms后恰好再发一次安全停止并请求最终0x18，不得采用A8的80 ms、`qRound(total/600)`目标或释放后0x0F归整。150 ms副本结束也不能立即开放操作；在其后的有效0x18 settled反馈到达前tap/hold availability必须保持false并保留上次合法显示档位，避免设备实际已到30.1x而缓存仍为30.0x时误发0x0F。0x16能力与0x18位置分别超过6.5秒未刷新时须锁定缩放并停止活动运动；任意合法SDK包超过6秒未到则判离线，不能由RTSP画面代替。
   - MT11模式/endpoint锁定矩阵：本地模式切换前先停止活动0x05、处理待发安全停止并立即清除旧镜头0x16/0x18状态；pending期间缩放锁定且旧镜头反馈不能解锁。模式变化按0x10/0x11反馈中的原始 `mainStream` 枚举判断，不只比较thermal布尔值，因此Zoom/Wide/composite等主路变化也必须重新查询0x16/0x18；确认后立即重查，2.5秒超时也清除旧状态、查询实际模式并重查倍率。另模拟外部控制器改变mainStream：未确认的旧0x0F generation和旧0x16/0x18请求必须立即退休；若旧0x05仍活动，只向旧镜头立即发一次停止，随后取消其150 ms Timer，切换后不得把安全副本发往新镜头。原生IPv4与等价IPv4-mapped IPv6来源仅在源端口精确等于 `mt11SdkPort` 时接受，来源IP/端口不符以及control、长度、CRC、过期/无匹配ACK错误均不得推进状态，0x0B异步反馈除外。
   - MT11析构安全矩阵：分别在0x0F确认pending、0x05活动、150 ms停止副本pending以及录像/照片收尾时销毁Manager。进入本地媒体可能运行的nested event loop前，全部SDK/control Timer必须已停止，缩放与命令generation已退休，SDK到Manager回调已断开；只要销毁前存在上述任一缩放状态，就同步发出两份0x05 `0`，不得等待已经取消的Timer。随后迟到UDP ACK、Timer事件和媒体回调都不能重启轮询、恢复倍率或重入半析构对象。
   - MT11 receiver隔离：拍照0/1反馈只有本应用拍照pending时才计数/报错，外部控制器反馈不得污染LOCAL UI。A8和MT11分别开始/停止本地录像，确认只调用对应receiver；录像toggle等待0x0A/0x0B确认时修改MT11 Enabled/SDK Host/Port必须恢复旧值并提示稍后重试，不得切换endpoint或补发第二次toggle；命令确认后再修改时，应在旧endpoint停止已确认相机录像并结束对应本地会话。关闭MT11 SDK不得释放通用Video 2 receiver；清空或修改URL 2、切换Video Source、触发重复URL保护、cleanup和应用退出需要释放第二receiver时，必须先收尾MT11 owned录像，不能停止A8录像或留下悬空Item。
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
9. Ubuntu 24.04下对MT11做RTSP分层验收：295d已完成URL 2原生Auto单路首帧基线，但60.986秒后断流且没有A8同时在线，因此仍需先确认QGC direct policy和系统调用直连 `.24:8554`，再用SDP/discoverer确认endpoint参数；分别在URL 1和URL 2长期播放，并通过GST_DEBUG/pcap记录Auto实际协商的SETUP传输。保留A8 URL 1独立及A8+MT11同时在线回归。若需隔离UDP/TCP差异，只在外部GStreamer对照命令中显式限制 `protocols`，本程序不提供对应开关。QGC本身不应依赖桌面代理忽略列表才能直连相机；单独执行系统 `gst-launch-1.0` 时仍需清理其代理环境或将 `.24/.25`加入忽略列表，因为外部进程不经过QGC `CustomPlugin`构造阶段的网络策略。
10. Android使用云台H.264编码回归：确认实际decoder来自经过筛选、接受avc的androidmedia厂商候选，并依次看到该decoder首输出和sink首帧；画面、延迟和断流重连不得退化。不得期待H.264出现 `qgcandroidh265hwdec`。
11. Android保持兼容Fact `forceAndroidH265HardwareDecoder=true`，分别覆盖H.264+H.264、H.265+H.265及产品实际A8+MT11编码组合，同时连续播放至少10分钟。每路必须有不同receiver/decoder instance的实例化日志、各自decoder首输出与sink首帧；H.265 adapter路径才要求两个独立 `qgcandroidh265hwdec`。分别记录开始、5分钟和10分钟的端到端延迟、CPU占用和丢帧，确认两路持续有画面且延迟不增长，并测试前后台切换和断流重连。
12. H.265 adapter路径的真机日志应确认各 `qgcandroidh265hwdec` 实例选中厂商 `amcviddec-*`，输入caps为 `stream-format=byte-stream,alignment=au`，首个raw frame使用 `vendor MediaCodec candidate` 字样；随后还必须看到外层实际decoder输出日志和对应sink首帧。若只有一路首帧或出现MediaCodec实例/资源错误，临时关闭兼容Fact并重启完成软件/自动解码A/B；不能只用rank、READY预检或候选字样判断双路硬解成功。
13. 硬解优先开启时，在双路H.265播放期间分别开始和停止录像，确认录像文件仍可正常回放；这验证播放支路转换没有影响原生 `hvc1` 录像支路。
14. 在没有兼容厂商H.264/H.265 decoder的Android设备上，候选不应提rank、H.265适配器不应注册，原生软件ranks保持原值且应保留fallback资格；若仍黑屏须按实际decoder与caps日志排障。完成A/B后恢复默认true并再次重启。
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
33. 使用简体中文启动并进入固件升级相关设置，确认日志不再出现 `FactMetaData: enum strings/values count mismatch`；`apmVehicleType`必须显示五项 `多旋翼/直升机/固定翼/地面车辆/水下航行器`，`apmChibiOS`必须拆分为 `ChibiOS/NuttX` 两项。验收的是ASCII逗号分隔和项数一致，不改变对应Fact值或筛选逻辑。
34. 覆盖会创建 `SimulatedCameraControl` 的无相机与模拟相机路径，确认启动日志不再出现295d中的三条 `QObject::connect: signal not found in VideoManager`；再改变VideoManager的 `hasVideo` 状态，确认 `hasVideoChanged` 能触发模拟相机信息刷新。该项不作为真实A8/MT11 RTSP或解码验收。

RTSP黑屏必须先做“URL/SDP -> transport/RTP -> decode -> render item”分层，不得一开始就归因于H.265或双路同步。Ubuntu对照命令为：

```bash
gst-discoverer-1.0 rtsp://192.168.144.24:8554/video1
gst-launch-1.0 -v rtspsrc location=rtsp://192.168.144.24:8554/video1 protocols=udp latency=25 ! decodebin ! videoconvert ! autovideosink
gst-launch-1.0 -v rtspsrc location=rtsp://192.168.144.24:8554/video1 protocols=tcp latency=25 ! decodebin ! videoconvert ! autovideosink
```

当前可确认的基线已经从“外部工具/官方QGC可播”推进到“本分支Desktop单路可播”。2026-08-19 14:28附件仍未包含随后加入的GIO默认直连；0e44又证明同机外部GStreamer会把私网RTSP送到系统代理。最新295d附件在Ubuntu系统代理开启时由本分支先选择GIO direct resolver，MT11 URL 2随后完整走通到sink首帧，闭环了这次Desktop黑屏修正。它没有连接成功A8，且MT11约60秒后断流并重连失败，所以仍不是双路、长期稳定或Android硬解验收。常规Application Messages只保留proxy策略、结构化错误与source/decoder/sink低频里程碑；transport、逐method及teardown深查改用GST_DEBUG/pcap。

作为历史对照，14:01附件中MT11精确 `.24/video1` 共22次start、20次资源错误code 9，全部仍落在 `gstrtspsrc.c(6888): gst_rtspsrc_try_send()`，内容为 `Could not receive message. (Received end-of-file)`。其中21轮effective Auto/`0x7`，只有一轮为effective TCP/`0x4`；误配置 `.2/video1` 另有2次start/2次相同EOF，未连接A8的 `.25/main.264` 有13次start/12次connect失败，还短暂出现主/副同时请求 `.24/video1`。该附件只能把黑屏限定在媒体pad之前，无法知道失败的是OPTIONS、DESCRIBE、SETUP还是PLAY。

作为旧版本历史证据，11:58附件首次给出完整 `before-send/lastRtspMethod` 定位。MT11副receiver共9次start，其中前8轮完整失败，第9轮在程序退出时中止；8轮完整失败全部是：第一次外发 `OPTIONS`，约0.4～0.5秒后第二次外发同一 `OPTIONS`，随后 `GST_RESOURCE_ERROR_READ code 9`、`lastRtspMethod "OPTIONS"`和 `Received end-of-file`。9轮合计17条OPTIONS，`DESCRIBE/SETUP/PLAY/PAUSE/TEARDOWN`均为0，source pad/媒体buffer、decoder实例/输出和sink首帧也均为0。该旧版首轮记录为effective TCP/`0x4`，其余为当时自定义回退后的effective Auto/`0x7`；两者在OPTIONS阶段完全相同，因为 `protocols` 只决定取得SDP后SETUP所允许的RTP/RTCP下层传输，不能改变RTSP控制连接的初始OPTIONS。每轮两条OPTIONS也不是两个receiver：GStreamer `gst_rtspsrc_try_send()` 源码在第一次响应读取为EEOF、尚未interleaved且 `udp-reconnect=true` 时会内部重连并重发同一请求一次，第二次仍EEOF才向bus报告。该日志只能确定当时实际连接到的peer在初始OPTIONS阶段关闭了控制连接；它没有记录socket目标，结合后来发现的GIO系统代理路径，不能再把该peer直接等同于MT11，也不能单凭此判断相机拒绝了method或某个请求头。现行代码已整体删除该传输偏好与回退状态，旧日志中的0x4/0x7不能作为当前功能说明。

14:28附件给出对上一轮method定位的扩展。MT11共16次receiver start、16次全部失败：OPTIONS级别0和1各有两条真正外发请求；进入级别2后，12条OPTIONS回调均为 `suppressed true`，并实际产生23条DESCRIBE。12个到达DESCRIBE的attempt中，11个在等响应时收到EOF，另1个等待20秒超时并由GStreamer重复上报两条错误；还有2个attempt在外发method前连接失败。因此总错误日志为17条，但对应的仍是16个失败attempt。MT11的SETUP、PLAY、`Streaming started`、source媒体buffer、decoder实例/首输出和sink首帧均为0；16条 `startDecoding statusCode 0` 只表示异步命令受理。所有已记录MT11 OPTIONS/DESCRIBE均为 `userAgentHeaders=1`、`realExtensionHeaders=0`，故本次失败不能归因于Real扩展头。skip-OPTIONS按设计越过了第一个method，但DESCRIBE响应前仍由当时连接路径上的peer关闭或超时；由于应用日志没有实际TCP peer，这同样不能证明请求已经直达MT11。

同一14:28进程中的A8提供了控制组：共15次start，前10次连接失败后，第11次在141.320秒依次外发OPTIONS、DESCRIBE、SETUP和PLAY，随后出现source pad、首个媒体buffer、`libav/avdec_h265`实例、1920×1080@25首输出和sink首帧；之后断流watchdog还证明PAUSE被抑制而TEARDOWN获准外发。MT11在A8成功播放前、播放期间和断流后都未越过DESCRIBE，因此不能把MT11黑屏归因于同一进程不具备RTSP能力、双路decoder资源竞争、H.265、MediaCodec、QML或render item。14:28被测旧版本在MT11首次DESCRIBE EOF后曾错误触发自定义TCP到Auto回退；该行为只属于历史构建，现行代码已删除传输枚举、强制TCP和整套回退逻辑，所有RTSP attempt都保持GStreamer原生Auto。

0e44附件给出当前最强的网络层证据。`ffprobe`第5～14行对A8 `.25:8554/main.264`直接连接成功，第15行起取得LIVE555 SDP并随后解析H.265；相同URL的GStreamer 1.24.2在第114/152行明确报无法连接代理 `192.168.163.1`，第167～174行的 `strace`显示唯一AF_INET connect目标为 `192.168.163.1:7897`且没有相机8554连接。它直接证明该环境的GStreamer/GIO会把私网RTSP送往桌面代理，是本轮有系统调用证据支持的首要根因；但因抓的是外部A8 `gst-launch`而非MT11/QGC旧进程，把它映射到历史MT11 EOF仍属于高置信推断。最终修正因此由 `CustomPlugin`在GStreamer初始化前默认设置dummy GIO resolver；保留的OPTIONS 0/1基本头/2跳过路径只是直连后仍精确出现OPTIONS EOF时的次级兼容。

295d附件给出当时Desktop视频路径的实测闭环。用户保持Ubuntu系统代理开启，进程在0.128秒打印 `GStreamer GIO proxy policy: direct resolver "dummy"`；MT11 URL 2在1.603/1.609/1.750/1.788秒依次发出OPTIONS、DESCRIBE、SETUP和PLAY，1.842秒出现source媒体首帧，2.057秒实例化 `libav/avdec_h265`，2.086秒输出1920×1080、I420、25 fps首帧并到达sink。该轮标准兼容级别0即可成功，没有触发basic-header或skip-OPTIONS，因此系统代理误路由可确认为本次MT11黑屏根因，OPTIONS兼容只保留为次级兜底。A8 `.25/main.264`未连接且持续失败，不能用该附件宣称双路；60.986秒MT11因21秒无帧触发watchdog，64.320和70.464秒的重启连接失败，不能宣称长期重连稳定。附件中的hex命令0x16和 `Rejected invalid MT11 ACK 16 "a5 01"` 是修正前旧版控制代码的历史事实：当前版本按0x16“整数byte + 一位小数byte”把 `a5 01`合法解析为165.1x，并由Manager作为产品上限处理。旧日志不代表当前预期行为，也始终与已成功的RTSP链路相互独立。

较早附件缺少精确GStreamer错误，是因为旧 `QGCLogging::msgHandler` 对debug、info、warning、critical一律调用 `isDebugEnabled()`；当前代码已改为 `isEnabled(type)`。11:58/14:28曾用临时详细日志证明before-send、OPTIONS兼容、deadline门禁及teardown语义已运行；295d完成根因闭环后，逐attempt/source配置、逐method/header、PAUSE/TEARDOWN、Dual生命周期/handoff/retry、VideoManager调度及observer/probe安装日志均已移除。常规运行保留proxy策略和source/decoder/sink一次性里程碑。RTSP `rtspsrc` resource error按最近 `(code,lastMethod)`签名降噪：签名首次出现或变化时warning，紧接着重复时debug；URI变化或source媒体恢复时复位。其他GStreamer error仍critical。停止进入NULL时内部仍只抑制PAUSE、允许TEARDOWN。MT11 SDK对剩余真正非法的同签名payload仍只在首次warning；`a5 01`现已按PDF规则合法解析，旧版invalid ACK日志不能用于判断当前倍率实现，更不能用于判断RTSP是否到达SDP。

11:58附件同时暴露Video 2退避绕过：8次失败分别计划2、4、8、15、15、15、15、15秒重启，但实际约0.548、2.322、0.171、1.452、2.119、0.835、6.120、2.621秒后就再次start，没有一次等到deadline。触发点均与离线A8主路的start/error或1000 ms releasing guard通知重合。当前修正使 `_applyDesiredState()` 在 `_restartTimer` 活动时直接返回；只有secondary URI、启用状态或重复判定等真实配置变化才显式取消Timer。该修正控制请求压力和生命周期确定性，但无法纠正GIO把RTSP送往代理的连接路径，因此不能把退避绕过当作黑屏根因。剩余时间过程日志现已删除，后续用连接时间戳验证。

```bash
GST_DEBUG_NO_COLOR=1 GST_DEBUG="rtspsrc:6" \
  ./Custom-QGroundControl --logging:full 2>&1 | tee qgc-mt11-rtsp-deep.log
sudo strace -f -e trace=network -o qgc-mt11-network.strace ./Custom-QGroundControl
```

常规应用日志不再提供逐attempt transport、source属性或RTSP method；上述 `GST_DEBUG`用于GStreamer内部协商，`strace`/pcap用于实际peer和时序。应用侧 `qgc.videomanager.videoreceiver.gstreamer.gstvideoreceiver`仍提供source timeout、首次resource warning、其他critical及首帧里程碑，warning/critical不依赖debug开关。修改本轮core文件后必须重新configure并干净重编译，不能只重建custom QML/C++对象，否则旧 `GstVideoReceiver.cc` 或 `QGCLogging.cc` 对象仍可能被链接进可执行文件。

以下顺序用于定位：

1. 设置层：复测前先退出全部RTSP客户端并断电重启MT11，等待服务启动后核对URL 2精确为 `rtsp://192.168.144.24:8554/video1`，不能是14:01附件中的 `.2/video1`；URL 1/2必须不同且两路使用原生Auto。界面不应再出现TCP开关；旧版偏好键即使残留也不影响运行且不被本版改写。`Starting secondary video`、`Keeping scheduled...`等过程日志已删除；以URL、socket连接时间戳、实际peer、失败warning和画面变化验证generation及退避，不把缺少这些文本判为失败。
2. source/RTSP层：任何receiver启动前先确认应用日志为 `proxy policy: direct resolver "dummy"`，并用 `strace`确认AF_INET peer为相机 `.24:8554`/`.25:8554`而非代理。冷启动首个终止事件前同一主URI只能有一个连接attempt；连续失败后的主路间隔按1/2/4/8/15秒封顶，URL或运行状态变化后旧generation不能重连。应用不再打印 `Starting RTSP receiver`、`Configured RTSP source`、source属性、OPTIONS属性或逐method；使用 `GST_DEBUG=rtspsrc:6`和pcap确认没有应用层 `protocols` 设置，记录原生Auto实际协商的UDP或TCP SETUP，并验证OPTIONS 0→1→2、keepalive、`udp-reconnect=true`及TEARDOWN/session时序。若直连后仍握手异常，用Wireshark过滤 `ip.addr == 192.168.144.24 && (rtsp || rtp || rtcp || tcp.port == 8554)` 对比本程序与官方QGC工作会话。
3. Video 2 surface层：render-ready和Starting过程日志已删除。检查Loader传入的实际Item、window和 `itemInitialized`运行值，确认Item就绪前没有socket连接；最终以同一receiver的source/decoder/sink里程碑和实际画面证明surface链，不与RTSP握手或下层传输协商混为一个问题。
4. receiver/decode/render层：成功status、`Secondary video streaming/decoding`过程日志已删除。`First source media frame reached the receiver`证明媒体buffer进入receiver；随后必须看到同一receiver的 `Decoder element instantiated`、`Decoder produced its first output frame`和 `First decoded video frame reached the sink`。没有source首帧时结合GST_DEBUG/pcap查RTSP/RTP/depay/parser；有source首帧但没有decoder实例/输出时查codec、caps与实例资源；decoder有输出但无sink首帧再查显示链、window和可见性。start/startDecoding真正失败仍由DualVideoManager warning报告。

14:28 QGC日志证明当时MT11从OPTIONS推进到DESCRIBE但仍失败，0e44又证明同环境GStreamer代理误路由；295d最终在QGC direct resolver生效后让MT11 URL 2标准OPTIONS一路到source/decoder/sink首帧，已完成Desktop单路恢复闭环。下一轮重点不再是“首次证明能播放”，而是补齐URL 1单路MT11、A8+MT11双路、60秒以上长期播放与断流重连、原生Auto在实际SETUP中的协商结果、停止释放及PIP跨窗口。常规应用日志只用于proxy策略、结构化错误和首帧里程碑；OPTIONS、RTP传输和teardown用GST_DEBUG/strace/pcap。295d不能扩大为Android双硬解或长期稳定验收。

Android 调试时同时关注 `gcs.custom.video.androidvideodecoderpolicy` 和 `gcs.custom.video.androidh265hardwaredecoderadapter`。可用 QGC Application Messages 或 `adb logcat` 查看：

```bash
adb logcat -v threadtime | grep -Ei \
  "androidvideodecoderpolicy|androidh265hardwaredecoderadapter|qgcandroidh265hwdec|androidmedia|amcviddec|avdec_h264|avdec_h265|stream-format=(avc|byte-stream|hvc1)|Decoder element instantiated|Decoder produced its first output frame|First decoded video frame|not-negotiated|configure codec"
```

关键日志按候选、实际decoder与显示链分层判断：

1. policy候选：H.264日志应显示实际plugin为androidmedia、厂商factory接受 `avc`且rank提升到259；H.265 adapter路径可见 `Registered qgcandroidh265hwdec ... rank 258 ... hvc1 -> byte-stream/au conversion`。这层只证明候选或adapter已准备。
2. 实际选择：每路都要看到 `Decoder element instantiated`，并核对receiver、URI、plugin/factory和instance；H.264没有adapter，H.265 adapter内部的 `selected actual decoder ... byte-stream ... alignment=au`只说明选中了候选并收到输入caps。
3. decoder输出：每路必须看到 `Decoder produced its first output frame`及正确caps。H.265 adapter自己的首raw日志写作 `vendor MediaCodec candidate`，不是硬件认证；`glMemoryOutput true`只说明其输出协商为GLMemory。
4. 显示到达：同一URI随后必须出现 `First decoded video frame reached the sink`。双路验收要求上述实际decoder输出与sink首帧分别在两个receiver上成立，并且decoder instance不同。

若只有候选/实例化而没有decoder首输出，或出现 `not-negotiated`、`Failed to configure codec`、profile/level不支持等错误，说明实际链路未跑通；若已有decoder输出但无sink首帧则转查显示端。任何 `vendor MediaCodec candidate` 都只代表筛选结果，系统级硬件属性仍以Android API 29 `isHardwareAccelerated()`为准，不能把rank或日志命名误判成硬解已确认。

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

Android启动日志出现 `Type FlyView unavailable`、`DualPipView is not a type`、`FlyViewSecondaryVideo is not a type` 或 `FlightDisplayViewSecondaryVideo is not a type`时，说明运行包未包含或未导入 `Custom.FlightDisplay`模块：只把新QML加入 `custom.qrc`并不能把它注册成原生 `QGroundControl.FlightDisplay`中的类型。当前规范接线是在 `custom/CMakeLists.txt`创建并链接 `CustomFlightDisplayModule`，由Qt自动生成qmldir和 `/qml/Custom/FlightDisplay`资源，`FlyView.qml`再用限定模块名实例化 `DualPipView/FlyViewSecondaryVideo`，后wrapper再实例化 `FlightDisplayViewSecondaryVideo`。修改QML模块或CMake后必须重新configure并干净构建Android包；若改后出现 `module "Custom.FlightDisplay" is not installed`，先检查主目标是否链接 `CustomFlightDisplayModule`及生成目录的qmldir，不要把文件加入原生 `src/FlightDisplay/CMakeLists.txt`。原故障中随后出现的 `QObject::property -> QGCApplication::event`空指针是根窗口创建失败后的二次退出崩溃，不是相机SDK、RTSP或H.265解码故障。

RC控制后顶部Center仍在第一次点击弹确认框或无动作时，先确认APK/桌面程序已重新编译 `custom.qrc`，其中存在 `QGroundControl/Toolbar/GimbalIndicator.qml` alias；该URL由拦截器从原生 `qrc:/qml/QGroundControl/Toolbar/GimbalIndicator.qml` 重定向，旧资源缓存仍会运行原生逻辑。随后抓取 `MAV_CMD_DO_GIMBAL_MANAGER_CONFIGURE`、`GIMBAL_MANAGER_STATUS`、两条 `MAV_CMD_DO_GIMBAL_MANAGER_PITCHYAW` 及各自 `COMMAND_ACK`：Configure只有一次但10秒内始终没有状态确认，应检查MAVLink转发和Gimbal Manager状态上报；状态确认后立刻又回到RC或命令1000返回Denied，说明摇杆仍在持续产生输入，本实现按安全边界不循环争抢。需要预激活的Center应先出现一条body yaw为0、pitch为受限非零1°偏移的命令；若第一条仍等于钳制后的上报pitch或仍为 `0,0`，说明运行的还是上一版“当前姿态预激活”资源。第一条ACK Accepted约400 ms后才应出现 `0,0,NaN,NaN` 的真正Center；只有一条1000说明预激活ACK未匹配或超时。最终Center的ACK若不是Accepted，身份标记会保留供下次重试。两条均ACK Accepted但第二条仍无物理动作时，应检查 `MNT_MODE_OUT`、飞控到云台的下行MAVLink及厂商固件，不再归因于QGC按钮或控制权弹窗。

Gimbal Enabled但合并栏不显示时，不要检查飞控、云台回包或 `activeVehicle`：当前可见性已完全与连接状态解耦，只要Enabled为true就必须显示。优先检查设置值、custom QRC命中、Loader错误及资源是否重新构建。若A8控制栏显示但状态点持续灰色，再确认A8 Mini供电和网络、`sdkHost/sdkPort`、本机路由及2秒轮询；RTSP视频与私有UDP SDK是独立链路。A8 `SiyiSdk`接受逻辑等价的IPv4/IPv4-mapped IPv6来源且不强制回包源端口为37260，但要求来源逻辑IP、帧头、精确长度、CRC、control=0x02及业务payload全部合法。若MT11状态点持续灰色，则另检查 `mt11SdkHost/mt11SdkPort`及设备回包端点；MT11 `Mt11Sdk`同样接受等价IP表示，但回包源端口必须精确等于配置的 `mt11SdkPort`，同IP不同源端口的合法CRC帧也会被静默丢弃。

选择A8时视频有画面但缩放按钮仍灰色，要分别检查视频门控和卡录能力。observer/probe安装成功日志已删除；直接路径以 `Negotiated main pulled-video resolution: W x H`证明真实首帧CAPS到达，当前只有1920×1080和1280×720属于A8会话白名单；若没有该结果，则检查约1秒后的 `stable VideoManager fallback`。A8视频门控成立后还必须持续收到合法0x20录像流参数，能力首次确认或发生变化时日志应出现 `Updated SIYI recording-stream capability`；仅有 `sdkResponding`、0x16或拉流尺寸都不能替代0x20。若超过4.5秒没有有效0x20，会出现 `recording-stream parameters timed out`并主动锁定A8缩放。能力确认后以0x18建立起始目标；之后0x0f本地发送成功即更新显示，不等待实际回读。这些分辨率/0x20条件不适用于MT11。

选择MT11时视频有画面但缩放按钮仍灰色，应独立检查私有SDK是否在线、0x16能力与0x18位置是否都在6.5秒新鲜度内、停止0x05后的settled反馈是否已经到达，以及0x10/0x11模式切换是否仍pending；RTSP画面、A8的0x20和卡录分辨率都不能解锁MT11。若日志仍出现 `Rejected invalid MT11 ACK 16 "a5 01"`，说明运行的是旧构建；当前代码必须把该0x16 payload解析为165.1x。私有实测倍率大于30x时两方向tap availability关闭是预期保护：QML短点安静消费且不调用Manager，应按住420 ms启动0x05，再通过每350 ms的0x18反馈确认运动；Manager直接调用仍会给出仅长按提示。停止后150 ms安全副本结束仍灰显时，应等待其后的有效0x18，防止用停止前缓存误判0x0F协议域。若长按仍无动作，抓包核对0x05开始/停止、0x16设备上限、0x18反馈、6秒在线watchdog和6.5秒能力/状态新鲜度，不要用A8分辨率表推断MT11上限。默认step 1.0时物理165.1对应最后显示档位165.0并非丢精度；只有step 0.1时中心数字才可显示165.1。

A8拉流尺寸白名单只有1280×720和1920×1080，但不再对应倍率。A8上限取卡录分辨率映射并受0x16较小值约束：4K=1.0、2K=3.5、1080P=5.5、720P=6.0。A8合法目标从1.0x按步长递增并追加有效精确上限；默认1.0x时2K为1/2/3/3.5，卡录1080P为1/2/3/4/5/5.5，卡录720P为1/2/3/4/5/6。1.0x时减号灰显、有效上限时加号灰显是正确边界；4K时两方向都灰显。MT11不读取该白名单或卡录映射。

A8初始镜头1.0x却显示错误、缩放后数字不更新或缩放按钮始终灰色时，先区分“协议actual raw”和“UI当前目标倍率”。新版0x18 `01 00`表示1.0x、`01 08`表示1.8x；A8真机旧版会返回 `0A 00`表示1.0x、`10 00`表示1.6x、`14 00`表示2.0x。日志若持续出现 `Rejected invalid SIYI current zoom payload "0a 00"`，说明运行的仍是未加入兼容解析的旧构建。A8 0x0f出站仍使用官方“整数byte+小数byte”，2.0x为 `02 00`；成功发送后UI立即显示2.0x，运动中的0x18 raw保存在独立实际值状态中。

A8目标2.0期间出现raw 1.6，表示设备报告了尚未到达目标的物理运动位置，不是2.0被误解码。由于A8 UI显示的是当前目标，0x0f成功后应继续显示2.0；后续raw 1.8或其他中间值只更新内部actual，不能把中心数字退回1.0或改成任意小数。目标3.0、4.0、5.0或5.5时遵循同一规则；发送失败才保持原目标，取消后的旧hold目标不得稍后重放。MT11同样不直接发布raw：tap成功立即发布合法显示目标，hold则只在350 ms反馈到达或越过下一合法档位时推进；MT11不采用A8按时间推进或追加精确非整步末端的规则。

A8再次tap应立即看到新的绝对target发送日志，并且中心数字在本地发送成功后同步更新；不应出现方向队列、延迟派发或停止点击后继续发包。默认步长1.0x且卡录1080P时，正向必须为1→2→3→4→5→5.5，反向必须沿同一表5.5→5→4→3→2→1；卡录2K必须在3.5终止，卡录720P正反同表为1→2→3→4→5→6。改变拉流分辨率但不改变卡录分辨率，不得改变这张A8目标表。

A8 hold应在按下420 ms后成立，普通路径只发送一次0x05方向命令，随后保持相机原生连续运动。A8显示目标档数按从最初按下开始的总时长 `qRound(totalMs / 600.0)` 计算；例如默认1.0x从1.0正向长按时，档数增加才依次显示2.0、3.0、4.0，任何回调或actual raw都不能让目标倒退，到卡录能力的有效端点必须立即停止。普通release调用 `stopZoom()`并完成最后一次时间计算；取消、移出、控件隐藏/销毁、应用后台、SDK能力失效或断流调用 `cancelZoom()`且不推进目标。活动0x05路径发送停止及一份80 ms有界安全重复，不补tap、不发送0x0f归整；断流重连和设置变化也不能复活归整。若成立时第一目标已经是端点，则只发送一次同方向端点0x0f，不启动0x05。MT11不使用该时间目标或80 ms时序，其350 ms反馈轮询和150 ms单次安全停止见上文。

按下拍照后SD有反馈但找不到本地JPG时，先确认 `localMediaStorageEnabled=true`，再区分“无解码帧”“主视频渲染项未安装/尺寸为0”“Photo暂存目录不存在或不可写”“离屏grab返回空图”“JPEG编码/暂存原子提交失败”和“暂存已写入但公共MediaStore发布失败”；本地照片不会从相机SD下载，所以不能在思翼卡目录中寻找。日志 `Starting local camera-frame capture` 应同时给出output及来源、decoded/negotiated/VideoManager/Item尺寸、DPR、content和逻辑target；`Timed out waiting for the local camera-frame grab`表示仅等待ready阶段超过5秒，SD命令仍独立；有Starting、没有该超时但界面提示仍在处理，通常表示已进入没有独立超时的worker编码/写盘阶段；`Failed to save local camera frame`表示worker失败，`Saved local camera frame`给出raw grab、最终output和暂存文件字节，之后还必须继续观察 `Queued durable public-media publication` 与 `Published durable public media`。1920宽实体屏若仍得到约384×216，说明运行的仍是按20% PIP无参截图旧构建；若日志output已是卡录1920×1080但图库观感模糊，先核对MediaStore width/height、拉取公共JPG查看而不要用缩略图判断。卡录4K配1080P拉流时4K像素尺寸正确但不可能产生超过1080P源纹理的新细节。LOCAL成功而SD失败或无卡属于设计允许状态；无卡本身不会清除仍新鲜的0x20尺寸。Android暂存文件位于AppSettings所在卷的 `Android/data/org.mavlink.qgroundcontrol/files/Custom-QGroundControl/Staging/Photo`，公开成品位于对应MediaStore volume的 `Pictures/Custom-QGroundControl/`；选择遥控器本机可移动SD但该卡缺失/只读时，暂存选择和公共发布会记录卷回退。这与云台SD状态2没有关系。

按下录像后LOCAL一直黄色时，检查 `VideoManager::streaming()`、主非thermal receiver、启动完成回调、输出基名和3秒超时；格式无效应检查 `recordingFormat`枚举。若thermal也产生或被停止，说明运行版本仍调用全局VideoManager start/stop。LOCAL结束但图库迟迟不出现时，先确认已收到owned `recording=false`并看到 `Queued durable public-media publication`，再检查是否因剩余空间不足以同时容纳暂存源和公共目标而发布失败。Android容量清理后仍超过用户感知总量不一定是故障：自动上限只管理当前安装SharedPreferences注册、名称含 `_local_NNN`锚点的公共Movies URI，兼容provider同名后缀；重装前历史公共媒体、其他入口、thermal文件以及所有未公开Staging都不会被新安装静默删除，发布失败源会额外占用空间直到重试成功或用户处理。正常退出时还应观察录像3秒封装与最长120秒公共发布barrier的告警；强杀进程不经过这些保证，发布完成前直接卸载也不承诺保留暂存录像。

Proximity Radar不显示时，先检查活动Vehicle的 `distanceSensors` 十方向Fact是否至少一个非NaN，再确认 `QGroundControl/Toolbar/ProximityRadarIndicator.qml` QRC alias与 `Custom.Widgets` 中的详情页已进入构建。5.0 m是严格边界：只有 `< 5.0`告警；没有任何有效方向时隐藏是预期行为。

同分辨率断流重连应立即发起0x18查询，不能等2秒后才接受第一次操作；该查询不再打印应用逐包日志，需通过抓包或重新建立的倍率状态验证。紧邻应用退出出现的 `PhotoVideoControl.qml`中 `cameraManager/currentCameraInstance`空对象警告来自QGC原生相机面板销毁时序，不参与custom缩放状态机，也不是本次锁定原因。

`FlyViewCompassBar.qml` 同样不加入原生 FlightDisplay qmldir，而由 `FlyViewCustomLayer.qml` 使用 `qrc:/Custom/qml/QGroundControl/FlightDisplay/FlyViewCompassBar.qml` 显式加载。若设置开关存在但界面不显示，先检查 Application Messages 中的 `Fly View compass bar failed to load`，再确认 `custom.qrc` 已重新编译、存在活动飞行器且 `Vehicle.heading` 不是 NaN。

常用静态检查：

```powershell
rg --files custom
rg -n "DefaultCommunicationLinkInstaller|192\.168\.144\.125|14550|autoConnectUDP|adjustSettingMetaData|appFontPointSize|FlyViewCompassBar|ProximityRadar|localMediaStorageEnabled|GimbalMediaSessionPolicy|GimbalPhotoCapturePolicy|effectiveDevicePixelRatio|grabLogicalSize|localRecording|grabToImage|startRecording|stopRecording|GimbalIndicator|GimbalCameraControl|ZoomStepPolicy|Mt11ZoomPolicy|A8MiniZoomPolicy|Mt11Protocol|Mt11Sdk|Mt11ControlManager|VideoCustomSettings|secondaryRtspUrl|DualVideoManager|duplicateSource|BeforeSynchronizingStage|FlyViewSecondaryVideo|FlightDisplayViewSecondaryVideo|DualPipView|thermal|takePhoto|toggleVideoRecording|CommandPhotoAndRecord|mediaStagingDirectory|existingMediaSourceDirectories|publishMediaFile|cleanupPublishedVideos|waitForPendingPublications|QGC_CUSTOM_ANDROID_MEDIA_LIBRARY_V2|IS_PENDING|sourceCleanupUris|getNoBackupFilesDir" custom
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

截至2026-08-21，本轮已独立编译并运行两套协议/策略测试：`Mt11ProtocolTest`为11 passed、0 failed（9个业务slot加框架init/cleanup），`SiyiProtocolTest`为42 passed、0 failed（40个业务slot加框架init/cleanup）；`Mt11Sdk.cc`也已独立编译通过。协议层已区分0x0F的1～30x绝对命令范围与0x05/0x16/0x18最高255.9x的线反馈范围，`a5 01`现按0x16/0x18格式合法解析为165.1x；Manager以165.1x封顶当前产品物理操作范围，私有raw不直接发布。`ZoomStepPolicy`提供共用1.0锚定网格，A8薄包装保留精确非整步末端，MT11策略只发布完整步长档位：默认step 1.0且物理165.1时UI最高165.0，step 0.1时才显示165.1。MT11 tap只在1～30x从上一显示目标步进，发送成功立即显示并支持快速替换；固定sequence/command-only 0x0F ACK不直接回滚，精确0x18或独立10秒超时收敛pending。tap pending后的hold以0x05(0)→150 ms后方向→再150 ms最多一份同方向副本抢占，两个延时段都可由release/cancel取消；0x05 ACK不作为位置真值，只有0x18推进倍率与边界。普通长按以0x05连续运动、350 ms反馈越档推进和150 ms单次安全停止覆盖设备范围；停止后的tap/hold还必须等随后有效0x18 settled反馈，且6.5秒能力/状态新鲜度及模式切换重查会锁定陈旧状态。外部mainStream变化会退休旧缩放generation并取消跨镜头的150 ms副本；析构也会在nested媒体收尾前停止控制Timer、退休状态、断开SDK回调并按需同步双发停止。上述结果仍不代表Manager时序、完整QGC Qt 6构建或MT11真机控制通过。当前Video 1/Video 2分别使用 `[Video]/rtspUrl` 与 `[Video]/secondaryRtspUrl`，两路固定不设置 `rtspsrc.protocols`、使用GStreamer原生Auto；Auto仍可自行协商TCP。`CustomPlugin`在GStreamer初始化前缺省设置 `GIO_USE_PROXY_RESOLVER=dummy`，truthy `QGC_GST_USE_SYSTEM_PROXY`才恢复environment/system，QtNetwork不受影响。core保留stop代次、generation退避、OPTIONS 0/1/2兼容和teardown语义，已删除应用RTSP传输枚举、强制TCP、TCP到Auto回退和5秒TCP专用超时；常规日志不再逐attempt打印source属性、OPTIONS属性、method或 `teardownTimeoutMs`，只保留proxy策略、结构化错误、source首帧、实际decoder实例/首输出和sink首帧，深度协议属性由GST_DEBUG/strace/pcap验证。RTSP resource error在签名首次出现或变化时warning，紧接着相同签名重复时debug；其他GStreamer error仍critical。Android H.264 direct avc与H.265 direct hvc1候选rank为259，H.265 adapter为258，软件rank保留。`DualPipView`保留固定槽交换、原生PIP显隐、独立窗口与拖拽resize。`SimulatedCameraControl`已连接真实 `hasVideoChanged`信号，295d中的三条signal-not-found应消失。`qgc_json_zh_CN.ts`已用ASCII逗号修复ChibiOS/NuttX和五项 `apmVehicleType`，消除enum mismatch。本轮可翻译字符串集合未变；两份custom TS均为18个context、146条message，英文146条unfinished、中文146条finished且空译文为0。

11:58历史附件的确定结论是：它来自Desktop而非Android；物理上只连接MT11，但应用仍同时运行未连接A8主receiver和MT11副receiver。MT11当时共9次start，完整失败都停在OPTIONS，DESCRIBE/SETUP/PLAY及媒体/decoder/sink首帧为0；每轮第二条OPTIONS是GStreamer `udp-reconnect`内部重发。该附件还暴露Video 2退避被主路通知绕过，但没有记录实际socket peer，不能把EOF直接归因于MT11。基本头部/skip-OPTIONS、副路deadline门禁、teardown flag延长及GIO direct resolver均在其后加入；“尚无真机成功日志”只适用于11:58当时。295d随后已证明本分支Desktop URL 2从标准OPTIONS走通SETUP/PLAY、source、decoder和sink首帧。

295d已经完成GIO直连修正后的Desktop URL 2单路MT11播放恢复，不能再描述为“桌面待首次恢复”。但该次A8未连接，MT11在60.986秒断流后两次重连失败，且没有Android构建/MediaCodec证据，因此仍不能扩大为QGC双路、长期稳定或Android双硬解验收。下一轮矩阵包括：MT11在URL 1原生Auto、A8 URL 1 + MT11 URL 2同时显示、60秒以上长期播放和断流重连、两路Auto实际SETUP协商、stop/TEARDOWN释放、Video 2 Item/window重挂载、连续PIP交换/拖拽，以及Android两个不同receiver/decoder instance各自decoder输出和sink首帧；不再包含TCP开关或回退测试。逐Timer/handoff及RTSP method日志已删除，使用连接时间戳、对象状态、GST_DEBUG/strace/pcap和实际画面验收。当前本地媒体仍映射Video 1 -> A8、Video 2 -> MT11；同步DirectConnection释放可能短时阻塞UI的已知风险未改。`a5 01`的协议解析已经修正；仍待验证的是MT11真机对1～30x短按、30x以上长按、165.1x边界停止、Zoom/Wide/composite及RGB/热成像切换后的能力重查和真实反馈。A8+MT11真实ACK时序、两套本地媒体隔离、Android性能及媒体发布、SDK/RTSP各自故障和退出收尾仍须按矩阵验证。
