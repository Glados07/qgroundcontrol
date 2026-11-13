# Rouke 日志抓取说明

## 一、抓取流程

**连接 USB 启动 → 拔线测试 → 接回 USB 导出。** USB 会打断遥控器通信链路，功能测试必须在拔线、链路恢复后进行。

**1. 准备设备**

- 无人机、AI 盒子、负载全程保持上电，在地面静止状态测试。
- 遥控器安装带 `schema=5`、`reservePolicy=ignored` 标记的 APK，开启 USB 调试，剩余空间至少 1 GiB。脚本会检查原始 53022 诊断和接收策略，未满足时无法进入正式测试。
- 保持 Rouke 控制启用，记录 AI 盒子 IP、UDP 端口和 `payload_addr`（当前默认 `.110:8888` / `0`）；模块禁用时不据独立入口无记录判断网络没包。
- Ubuntu 已安装 ADB。在脚本所在目录打开终端，连接 USB 并确认遥控器上的调试授权。

```bash
adb devices
```

确认目标设备状态为 `device`。

**2. 启动采集**

```bash
bash ./rouke-diagnostics-capture.sh start
```

脚本会重启 QGC。看到 **“采集已启动并通过独立 ADB 会话检查”** 后，才拔掉 USB。

若提示缺少新版 APK 诊断标记或启动失败，先保持 USB 连接，执行 `finish` 导出启动日志，再处理提示的问题。

**3. 拔线后测试**

等待链路恢复，保持 QGC 前台。在“应用设置 → Video → Connection”先记下原来的两个地址，视频源保持 **RTSP**、视频总开关保持开启：清空 **RTSP URL 1** 并提交，使 A8 全程停止拉流；通过 **RTSP URL 2** 的“清空 → 填回 `rtsp://192.168.144.100:8554/live` → 清空”完成以下对照。每次编辑后提交输入，再返回飞行页测试；测试结束恢复原地址。

确实无法关闭 A8 时，全程保持同一状态并记录。每阶段保持 **30～60 秒**，记下开始/结束及每次操作时间：

| 阶段 | 操作 |
|---|---|
| A：无视频 | 停止视频拉流。打开 Rouke 设置，读取 ISO；CH11/CH12 先回中位，再逐轴短拨、回中，记录结果。 |
| B：开启 Rouke | 只开启 Rouke 视频，观察首帧及卡顿；重复同一 ISO 读取和相同通道动作。 |
| C：关闭 Rouke | 停止 Rouke 拉流，再重复上述动作，记录通信是否恢复、多久恢复。 |

**隐藏视频窗口、切换主辅窗或切到地图不等于停止网络拉流。** 使用视频设置停止实际播放，记录具体方式；若设备支持从视频源停止发送，可再做一次远端停止发送的对照，更便于判断共享链路拥塞。无法确认远端是否仍在发送时直接注明，脚本不会自动控制负载或修改网络。

必要时重复 A/B/C。之后再检查白平衡、焦距、图片大小；允许设置且读到原值时，修改一项、读取确认并恢复。CH14 先释放再按下，记录回中结果。建议总计 5～8 分钟；采集上限默认 20 分钟，从 `start` 计时。测试期间不重启 QGC、不录屏，记下拔线、异常和接回 USB 时间。

本轮重点比较商家的 `reserve=A1/data_len=118` 心跳和 `reserve=C1/data_len=48` 尾片在三个阶段是否到达。若商家能提供同期发送时间和频率，一并填入 `observation.txt`，便于对照。

默认使用较低的视频日志量，保留协商格式、解码器、首帧、错误和 RTSP 传输信息。只有需要细查视频管线时，另一次采集使用：

```bash
ROUKE_CAPTURE_VERBOSE_VIDEO=1 bash ./rouke-diagnostics-capture.sh start
```

详细模式日志量与采样开销更大；定位“开视频后控制断开”时优先用默认模式。

**4. 接回 USB 导出**

连接同一遥控器，执行：

```bash
bash ./rouke-diagnostics-capture.sh finish
```

终端会显示原始目录和压缩包路径，默认位于 Ubuntu 用户主目录：

```text
~/rouke-diagnostics-日期-时间-随机后缀/
~/rouke-diagnostics-日期-时间-随机后缀.tar.gz
```

若导出失败或提示仍在收尾，接好 USB 后再次执行 `finish`。

**5. 填写记录并提交**

填写输出目录中的 `observation.txt`，记录测试操作和现象，然后重新打包。将下面的目录名替换为实际输出目录：

```bash
log_dir="$HOME/rouke-diagnostics-实际目录名"
tar -czf "$log_dir.tar.gz" -C "$(dirname "$log_dir")" "$(basename "$log_dir")"
```

提交更新后的整个 `.tar.gz`。采集结束后正常退出并重新打开 QGC，恢复日常日志量。

## 二、日志内容

| 文件或目录 | 记录内容 |
|---|---|
| `index-video.txt` | 视频拉流、H.264/H.265 格式、硬解码器、出帧、显示及重连事件。 |
| `index-payload.txt` | MAVLink 53022 发送、外层接收、内层校验及命令回应；周期/超时快照按 `session/report` 对齐。 |
| `index-53022-raw.txt` | 两个 QGC 接收入口的 53022 字段、分行 HEX 和 `ROUKE_WIRE_TOTAL` 累计量；重点查看 `a1Heartbeat`、`c1AckTail` 的阶段增量。 |
| `index-rc.txt` | CH11/CH12/CH14 输入、中位保护、目标速度、停止、回中和发送失败。 |
| `index-network.txt` | 约每 5 秒的网卡收发/丢弃/错误、UDP 错误、CPU 与内核收包统计；约每 15 秒的 UDP socket 队列/丢弃及路由。另含原生 UDP 本地端口和自动学习目标，只说明 QGC 端配置路径，不代表 AI 盒子已改变回程路由。 |
| `device/` | 原始 QGC/Android 日志、USB 插拔时间线、网络状态、媒体服务状态及 APK 启动信息。 |
| `coverage.txt` | 关键日志是否出现、是否被轮转覆盖、采集是否异常结束；不代表功能测试通过。 |
| `index-readme.txt` | 索引阅读顺序、分层接收计数与网络采样字段的解释。 |
| `observation.txt` | 人工填写的操作时间、参数值和屏幕/云台实际表现，用于对应日志。 |

`ROUKE_RX_RELAY_FIRST` 和 `TRANSPORT` 的 `relay` 确认外层 53022 已到达配置端点的接收程序；`INNER` 的 `matched` 确认内层方向/地址通过，命令成功还需看回执结果。系统网络计数是累计值，应按测试阶段比较增量，**不能当作 QGC 专属丢包数**。`PROC_UNAVAILABLE` 表示 Android 权限或文件不可用，不表示零丢包。

原始索引是 **QGC 原始接收记录，不是完整网卡 pcap**。独立 UDP 入口记录数据报中的真实 MAVLink 帧字节；原生 MAVLink 入口记录已解码字段和 payload。只观察 CRC 校验通过的 53022；接收 `reserve` 只记录、不用于拒绝，发送仍为 0。`a1Heartbeat/c1AckTail` 要求地址、reserve、长度及完整数据与商家样例一致。

`payload` 样例命中证明该报文已到独立 Rouke socket；`native` 命中只证明原生链路收到。合法 A1 心跳可以刷新在线状态，但不是参数 ACK；C1 尾片需要同源首片重组成完整内层帧，才能确认命令及参数回执。`decision=outer-pass` 只表示通过初步外层检查。

HEX 每入口最多 4 个样本/秒、总计 240 个，达到限额后累计量仍继续。按同一进程、同一 `lane/session` 的 `ROUKE_WIRE_TOTAL` 比较 A/B/C；没有某时刻的 HEX 不代表没收到，也不要把索引行数当包数。`native` 的 `liveOnly=1` 计数汇总各原生实时链路；不要将两入口相加当网卡总包数。字段及拼接说明见 `index-readme.txt`。

若未命中仍需确认报文是否到达遥控器网卡，可由商家在具备抓包权限的遥控器侧同步采集 pcap；USB 主机抓包不能替代遥控器侧抓包。

所有 `index-*.txt` 都是检索索引，保留 `device/` 原始日志一起提交。
