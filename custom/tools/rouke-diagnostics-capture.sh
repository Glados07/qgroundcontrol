#!/usr/bin/env bash
# Ubuntu host + USB ADB. The recorder runs ON Android after USB is unplugged.
# No device reboot, flight/payload command, log clearing or network changes.
set -euo pipefail
umask 077

MODE=${1:-help}
case "$MODE" in
    start|finish|status) ;;
    help|-h|--help)
        printf 'Usage: bash %s start|finish|status\n' "$0"
        printf 'start 重启 QGC 并在遥控器本地采集；成功后拔 USB 测试。\nfinish 重新接 USB 后导出；status 仅供连接 USB 时检查。\n'
        printf '可选：ANDROID_SERIAL、ROUKE_CAPTURE_SECONDS（默认 1200）、ROUKE_CAPTURE_OUTPUT_DIR（默认 HOME）。\n'
        exit 0 ;;
    *) echo 'Unknown mode. Use start, finish, status or help.' >&2; exit 2 ;;
esac
for tool in adb awk tr grep mktemp tar tee date mkdir; do
    command -v "$tool" >/dev/null || { echo "Missing Ubuntu command: $tool" >&2; exit 2; }
done
DURATION=${ROUKE_CAPTURE_SECONDS:-1200}
[[ $DURATION =~ ^[1-9][0-9]{2,3}$ ]] && (( DURATION >= 180 && DURATION <= 3600 )) || {
    echo 'ROUKE_CAPTURE_SECONDS must be 180..3600 seconds.' >&2; exit 2;
}
ADB=(adb)
[[ -z ${ANDROID_SERIAL:-} ]] || ADB+=(-s "$ANDROID_SERIAL")
SERIAL=$("${ADB[@]}" get-serialno | tr -d '\r\n')
[[ -n $SERIAL && $SERIAL != unknown ]] || { echo 'Connect and authorize one Android device; use ANDROID_SERIAL if needed.' >&2; exit 2; }
ADB=(adb -s "$SERIAL")
[[ $("${ADB[@]}" get-state | tr -d '\r') == device ]] || exit 2
REMOTE_ROOT=/sdcard/Download/QGC_Rouke_Diagnostics

"${ADB[@]}" shell sh -s -- "$MODE" "$DURATION" <<'ANDROID'
set -eu
MODE=$1
DURATION=$2
ROOT=/sdcard/Download/QGC_Rouke_Diagnostics
PACKAGE=org.mavlink.qgroundcontrol
ACTIVITY=org.mavlink.qgroundcontrol.QGCActivity
CAP=$(cat "$ROOT/current" 2>/dev/null || true)
valid_capture() {
    printf '%s\n' "$1" | grep -Eq '^/sdcard/Download/QGC_Rouke_Diagnostics/session_[0-9]{8}_[0-9]{6}_[0-9]+$'
}
valid_capture "$CAP" || CAP=''
# PID reuse protection: boot + /proc start ticks + an exact command argument.
same_process() {
    KIND=$1
    [ -n "$CAP" ] && [ -r "$CAP/$KIND.pid" ] && [ -r "$CAP/$KIND.ticks" ] || return 1
    P=$(cat "$CAP/$KIND.pid")
    case "$P" in ''|*[!0-9]*) return 1 ;; esac
    [ "$P" -gt 1 ] && kill -0 "$P" 2>/dev/null || return 1
    [ "$(cat /proc/sys/kernel/random/boot_id)" = "$(cat "$CAP/boot-id.txt")" ] || return 1
    [ "$(sed 's/.*) //' "/proc/$P/stat" 2>/dev/null | awk '{print $20}')" = "$(cat "$CAP/$KIND.ticks")" ] || return 1
    if [ "$KIND" = logger ]; then TOKEN="$CAP/logcat.txt"; else TOKEN="$CAP/recorder.sh"; fi
    tr '\000' '\n' < "/proc/$P/cmdline" | grep -Fqx "$TOKEN"
}
snapshot() {
    DEST=$1
    { date '+%Y-%m-%dT%H:%M:%S%z'; cat /proc/uptime; cat /proc/sys/kernel/random/boot_id;
      getprop ro.build.fingerprint; getprop ro.product.model; getprop ro.build.version.release;
      getprop ro.build.version.sdk; df -Pk /sdcard; } > "$CAP/device-$DEST.txt" 2>&1
    { ip addr; ip route; ip rule; ip route get 192.168.144.100; ip route get 192.168.144.110;
      cat /proc/net/dev /proc/net/route /proc/net/udp /proc/net/udp6; } > "$CAP/network-$DEST.txt" 2>&1 || true
    dumpsys -t 3 usb > "$CAP/usb-$DEST.txt" 2>&1 || true
    dumpsys -t 3 connectivity > "$CAP/connectivity-$DEST.txt" 2>&1 || true
    dumpsys -t 3 wifi > "$CAP/wifi-$DEST.txt" 2>&1 || true
    dumpsys -t 3 ethernet > "$CAP/ethernet-$DEST.txt" 2>&1 || true
}

if [ "$MODE" = status ]; then
    [ -n "$CAP" ] && [ -d "$CAP" ] || { echo 'No capture found on this controller.' >&2; exit 7; }
    echo "Device capture: $CAP"
    if same_process recorder; then echo 'recorder: running'; else echo 'recorder: not running'; fi
    if same_process logger; then echo 'logcat: running'; else echo 'logcat: not running'; fi
    cat "$CAP/heartbeat.txt" "$CAP/recorder.done" 2>/dev/null || true
    df -Pk /sdcard
    exit 0
fi
if [ "$MODE" = start ]; then
    if same_process recorder || same_process logger; then
        echo 'A capture is running. Use finish before starting another.' >&2; exit 3
    fi
    if [ -n "$CAP" ] && [ ! -f "$CAP/exported.txt" ]; then
        echo 'Previous capture has not been exported successfully. Use finish first.' >&2; exit 3
    fi
    pm path "$PACKAGE" | grep -q '^package:' || { echo 'QGC package not installed.' >&2; exit 4; }
    command -v logcat >/dev/null
    if [ -x /system/bin/nohup ]; then NOHUP=/system/bin/nohup
    elif /system/bin/toybox nohup true >/dev/null 2>&1; then NOHUP='/system/bin/toybox nohup'
    else echo 'No working nohup; do not unplug USB.' >&2; exit 4; fi
    FREE_KB=$(df -Pk /sdcard | awk 'END {print $4}')
    case "$FREE_KB" in ''|*[!0-9]*) echo 'Cannot verify free space.' >&2; exit 4 ;; esac
    [ "$FREE_KB" -ge 1048576 ] || { echo 'Need at least 1 GiB free on controller.' >&2; exit 4; }
    mkdir -p "$ROOT"
    CAP="$ROOT/session_$(date +%Y%m%d_%H%M%S)_$$"
    mkdir "$CAP"
    printf '%s\n' "$CAP" > "$ROOT/current"
    cat /proc/sys/kernel/random/boot_id > "$CAP/boot-id.txt"
    printf '%s\n' "$DURATION" > "$CAP/duration-seconds.txt"
    snapshot start
    dumpsys -l > "$CAP/dumpsys-services.txt" 2>&1 || true
    dumpsys package "$PACKAGE" > "$CAP/package.txt" 2>&1 || true
    logcat -g > "$CAP/logd-start.txt" 2>&1 || true
    # Keep Qt's GStreamer logging bridge; do not set GST_DEBUG via an env override.
    APP_ARGS='--logging:gcs.custom.customplugin,gcs.custom.videomanager.dualvideo,gcs.custom.video.androidh264hardwaredecoderadapter,gcs.custom.video.androidh265decoderfallback,gcs.custom.video.androidh265hardwaredecoderadapter,gcs.custom.video.androidvideodecoderpolicy,gcs.custom.video.androidvideodecoderrecovery,gcs.custom.video.a8rtsprecovery,gcs.custom.gimbal.pulledvideoresolution,gcs.custom.gimbal.rouke.payload,gcs.custom.gimbal.rouke.control,gcs.custom.gimbal.rouke.trace,gcs.custom.gimbal.rouke.channels,gcs.custom.gimbal.rouke.native,gcs.custom.android.unircchannel,gcs.custom.android.unircchannel.trace,qgc.android.androidserial,qgc.android.libs.qtandroidserialport.qserialport_android,qgc.videomanager.videomanager,qgc.videomanager.videoreceiver.gstreamer,qgc.videomanager.videoreceiver.gstreamer.gstvideoreceiver,qgc.videomanager.videoreceiver.gstreamer.api --gst-debug=2,rtspsrc:5,rtpsession:4,rtpjitterbuffer:5,rtph265depay:4,rtph264depay:4,h265parse:4,h264parse:4,decodebin3:4,amcvideodec:5,amc:4,videodecoder:4,qtsink:5,qgcsinkbin:5,qtglwidget:5,qtglutility:4,glupload:4,glcolorconvert:4'
    printf '%s\n' "$APP_ARGS" > "$CAP/launch-arguments.txt"
    cat > "$CAP/recorder.sh" <<'RECORDER'
#!/system/bin/sh
# No ADB file descriptors survive the nohup launch.
CAP=$1
DURATION=$2
PACKAGE=org.mavlink.qgroundcontrol
printf '%s\n' "$$" > "$CAP/recorder.pid"
sed 's/.*) //' "/proc/$$/stat" | awk '{print $20}' > "$CAP/recorder.ticks"
START=$(cut -d. -f1 /proc/uptime)
stamp() { printf '%s uptime=%s wall=%s\n' "$1" "$(cut -d. -f1 /proc/uptime)" "$(date '+%Y-%m-%dT%H:%M:%S%z')" >> "$CAP/timeline.txt"; }
stamp CAPTURE_BEGIN_USB_CONNECTED
log -p i -t QGC_ROUKE_CAPTURE "CAPTURE_BEGIN $CAP" 2>/dev/null || true
# ~512 MiB ring, all PIDs retained (MediaCodec services and crashes matter).
logcat -b main -b system -b crash -b events -v threadtime -T 1 \
    -f "$CAP/logcat.txt" -r 16384 -n 31 '*:V' &
LOGGER=$!
printf '%s\n' "$LOGGER" > "$CAP/logger.pid"
sed 's/.*) //' "/proc/$LOGGER/stat" | awk '{print $20}' > "$CAP/logger.ticks"
REASON=logger_exited
I=0
LAST_USB=''
while kill -0 "$LOGGER" 2>/dev/null; do
    NOW=$(cut -d. -f1 /proc/uptime)
    ELAPSED=$((NOW - START))
    if [ -f "$CAP/stop" ]; then REASON=finish_requested; break; fi
    if [ "$ELAPSED" -ge "$DURATION" ]; then REASON=duration_limit; break; fi
    printf 'uptime=%s elapsed=%s remaining=%s\n' "$NOW" "$ELAPSED" "$((DURATION - ELAPSED))" > "$CAP/heartbeat.txt"
    {
        printf '\nuptime=%s elapsed=%s wall=%s usb.config=%s usb.state=%s\n' "$NOW" "$ELAPSED" "$(date '+%Y-%m-%dT%H:%M:%S%z')" "$(getprop sys.usb.config)" "$(getprop sys.usb.state)"
        cat /proc/net/dev /proc/net/snmp /proc/stat
        for P in $(pidof "$PACKAGE"); do cat "/proc/$P/stat"; done
    } >> "$CAP/progress.txt" 2>&1
    # Configuration props alone do NOT prove that the physical cable is connected.
    if [ -r /sys/class/android_usb/android0/state ]; then
        USB=$(cat /sys/class/android_usb/android0/state)
        if [ "$USB" != "$LAST_USB" ]; then stamp "USB_SYSFS_$USB"; LAST_USB=$USB; fi
    fi
    if [ "$((I % 3))" -eq 0 ]; then
        {
            printf '\nuptime=%s elapsed=%s\n' "$NOW" "$ELAPSED"
            ip addr; ip route; ip route get 192.168.144.100; ip route get 192.168.144.110
            cat /proc/net/udp /proc/net/udp6
            dumpsys -t 2 usb
        } >> "$CAP/network-progress.txt" 2>&1
        FREE_KB=$(df -Pk /sdcard | awk 'END {print $4}')
        case "$FREE_KB" in ''|*[!0-9]*) ;; *)
            if [ "$FREE_KB" -lt 262144 ]; then REASON=low_storage; break; fi ;;
        esac
    fi
    if [ "$((I % 6))" -eq 0 ]; then
        # Unsupported/permission errors are retained; root is not required.
        dumpsys -t 3 media.codec > "$CAP/media-codec-$I.txt" 2>&1 || true
        dumpsys -t 3 media.resource_manager > "$CAP/media-resource-$I.txt" 2>&1 || true
        dumpsys -t 3 connectivity > "$CAP/connectivity-$I.txt" 2>&1 || true
    fi
    if [ "$ELAPSED" -ge 25 ] && [ ! -f "$CAP/startup-logcat.txt" ]; then
        logcat -b main -b system -b crash -d -v threadtime \
            -f "$CAP/startup-logcat.txt" '*:V' 2> "$CAP/startup-ring-error.txt" || true
    fi
    I=$((I + 1))
    sleep 5
done
stamp "CAPTURE_END_$REASON"
log -p i -t QGC_ROUKE_CAPTURE "CAPTURE_END $REASON $CAP" 2>/dev/null || true
TICKS=$(sed 's/.*) //' "/proc/$LOGGER/stat" 2>/dev/null | awk '{print $20}')
if [ -n "$TICKS" ] && [ "$TICKS" = "$(cat "$CAP/logger.ticks")" ] && \
    tr '\000' '\n' < "/proc/$LOGGER/cmdline" | grep -Fqx "$CAP/logcat.txt"; then
    kill -TERM "$LOGGER" 2>/dev/null || true
fi
wait "$LOGGER" 2>/dev/null || true
logcat -b main -b system -b crash -b events -d -v threadtime \
    -f "$CAP/logcat-tail.txt" '*:V' 2> "$CAP/tail-error.txt" || true
printf '%s\n' "$REASON" > "$CAP/recorder.done"
RECORDER
    $NOHUP /system/bin/sh "$CAP/recorder.sh" "$CAP" "$DURATION" \
        </dev/null > "$CAP/recorder.stdout.txt" 2> "$CAP/recorder.stderr.txt" &
    sleep 2
    same_process recorder && same_process logger || { echo 'Recorder failed; do not unplug. Use finish to collect errors.' >&2; exit 5; }
    am force-stop "$PACKAGE"
    am start -W -n "$PACKAGE/$ACTIVITY" --es applicationArguments "$APP_ARGS" > "$CAP/app-start.txt" 2>&1
    if grep -Ei 'Error:|Exception|does not exist|unable to resolve' "$CAP/app-start.txt"; then exit 6; fi
    I=0
    while ! pidof "$PACKAGE" >/dev/null 2>&1 && [ "$I" -lt 10 ]; do sleep 1; I=$((I + 1)); done
    pidof "$PACKAGE" > "$CAP/app-pid-start.txt" || { echo 'QGC did not stay running; use finish.' >&2; exit 6; }
    # Verify the installed APK, not just that a process with this name exists.
    # Older APKs accept the flags but cannot emit the three control trace streams.
    APP_PID=$(awk '{print $1}' "$CAP/app-pid-start.txt")
    I=0
    while :; do
        logcat -b main -d -v threadtime --pid="$APP_PID" '*:V' \
            > "$CAP/app-diagnostic-readiness.txt" 2> "$CAP/app-diagnostic-readiness-error.txt" || true
        if grep -Fq 'ROUKE_DIAGNOSTICS schema=2 msgid=53022 len=132 crc=37' "$CAP/app-diagnostic-readiness.txt"; then
            printf 'schema=2 verified for QGC pid=%s\n' "$APP_PID" > "$CAP/diagnostics-schema.txt"
            break
        fi
        if [ "$I" -ge 20 ]; then
            echo '未检测到新版 APK 的 Rouke 诊断能力标记，暂不要拔 USB。可能是旧 APK 或 QGC 未完成启动。' >&2
            echo '先执行 finish 导出启动日志；将本地 custom 改动同步到构建电脑、重新构建安装后再 start。' >&2
            exit 10
        fi
        sleep 1
        I=$((I + 1))
    done
    logcat -b main -b system -b crash -d -v threadtime -f "$CAP/app-launch-ring.txt" '*:V' 2> "$CAP/app-launch-ring-error.txt" || true
    printf 'READY_TO_UNPLUG uptime=%s wall=%s\n' "$(cut -d. -f1 /proc/uptime)" "$(date '+%Y-%m-%dT%H:%M:%S%z')" >> "$CAP/timeline.txt"
    echo "Device capture: $CAP"
else
    [ -n "$CAP" ] && [ -d "$CAP" ] || { echo 'No capture found on this controller.' >&2; exit 7; }
    if [ ! -f "$CAP/usb-return.txt" ]; then
        { date '+%Y-%m-%dT%H:%M:%S%z'; cat /proc/uptime; } > "$CAP/usb-return.txt"
        printf 'USB_RETURN_FINISH uptime=%s wall=%s\n' "$(cut -d. -f1 /proc/uptime)" "$(date '+%Y-%m-%dT%H:%M:%S%z')" >> "$CAP/timeline.txt"
    fi
    touch "$CAP/stop"
    I=0
    while same_process recorder && [ "$I" -lt 40 ]; do sleep 1; I=$((I + 1)); done
    if same_process recorder; then
        echo 'Recorder is still finalizing. Run finish again shortly; evidence is preserved.' >&2; exit 8
    fi
    # Recover a logger orphaned by a dead recorder, without killing another tool.
    if same_process logger; then
        kill -TERM "$(cat "$CAP/logger.pid")" 2>/dev/null || true
        sleep 2
        same_process logger && { echo 'Logger is still stopping. Retry finish.' >&2; exit 8; }
    fi
    if [ ! -f "$CAP/recorder.done" ]; then
        echo 'WARNING: recorder did not finalize normally; exporting partial evidence.' >&2
        printf 'missing recorder.done; inspect boot IDs, heartbeat and recorder stderr\n' > "$CAP/incomplete-capture.txt"
    fi
    snapshot finish
    logcat -b main -b system -b crash -b events -d -v threadtime -f "$CAP/logcat-ring-after.txt" '*:V' 2> "$CAP/ring-error.txt" || true
    logcat -g > "$CAP/logd-finish.txt" 2>&1 || true
fi
ANDROID

CAP=$("${ADB[@]}" shell cat "$REMOTE_ROOT/current" | tr -d '\r\n')
[[ $CAP =~ ^/sdcard/Download/QGC_Rouke_Diagnostics/session_[0-9]{8}_[0-9]{6}_[0-9]+$ ]] || exit 7
if [[ $MODE == status ]]; then exit 0; fi
if [[ $MODE == start ]]; then
    # Verify after the original ADB shell exits, using a separate connection.
    "${ADB[@]}" shell sh -s -- "$CAP" <<'VERIFY'
set -eu
CAP=$1
[ "$(cat /proc/sys/kernel/random/boot_id)" = "$(cat "$CAP/boot-id.txt")" ]
for KIND in recorder logger; do
    PID=$(cat "$CAP/$KIND.pid")
    case "$PID" in ''|*[!0-9]*) exit 1 ;; esac
    [ "$PID" -gt 1 ] && kill -0 "$PID"
    [ "$(sed 's/.*) //' "/proc/$PID/stat" | awk '{print $20}')" = "$(cat "$CAP/$KIND.ticks")" ]
    if [ "$KIND" = recorder ]; then TOKEN="$CAP/recorder.sh"; else TOKEN="$CAP/logcat.txt"; fi
    tr '\000' '\n' < "/proc/$PID/cmdline" | grep -Fqx "$TOKEN"
done
[ -s "$CAP/logcat.txt" ]
pidof org.mavlink.qgroundcontrol >/dev/null
VERIFY
    printf '\n采集已启动并通过独立 ADB 会话检查。现在拔掉 USB，再等链路恢复后测试。\n'
    printf '无人机和负载持续供电，QGC 保持前台；此处只重启了 QGC，没有重启设备。\n'
    printf '依次测试：视频硬解 → 参数读取/设置 → CH11/CH12/CH14；记录每阶段和异常的时间。\n'
    printf '采集上限 %s 秒，从启动计时。USB 插着时无画面不作为视频故障证据。\n' "$DURATION"
    printf '测试完成再接回 USB，执行：bash "%s" finish\n' "$0"
    exit 0
fi

OUTPUT_ROOT=${ROUKE_CAPTURE_OUTPUT_DIR:-$HOME}
mkdir -p "$OUTPUT_ROOT"
LOCAL=$(mktemp -d "$OUTPUT_ROOT/rouke-diagnostics-$(date +%Y%m%d-%H%M%S)-XXXXXX")
printf '%s\n' "$SERIAL" > "$LOCAL/adb-serial.txt"
{ date '+%Y-%m-%dT%H:%M:%S%z'; date -u; adb version; } > "$LOCAL/host.txt"
printf '%s\n' "$CAP" > "$LOCAL/remote-path.txt"
"${ADB[@]}" pull "$CAP" "$LOCAL/device"
cat > "$LOCAL/observation.txt" <<'NOTES'
请保留原始日志，填写此表后重新打包整个目录：
APK/代码提交：
遥控器型号/Android版本：
无人机和负载是否全程持续供电：
拔 USB 时间 / 重新插 USB 时间（注明使用手机还是遥控器时钟）：
拔线后链路恢复时间 / 始终未恢复：
视频阶段起止：A8是否正常；Rouke首次出画面、黑屏/卡顿时间；是否切换主辅窗：
参数阶段起止：打开设置时间；ISO/白平衡/焦距/图片大小读到的值；设置参数名、原值、目标值、回显、恢复原值：
遥控阶段起止：CH11/12初始中位；各轴正/反/回中时间；是否转动/停止；CH14释放及按下时间：
是否切后台/录屏/改IP或视频设置/重启QGC：
三类症状是否在同一时间出现：
NOTES
shopt -s nullglob
LOGS=()
declare -A SEEN_LOGS=()
for file in "$LOCAL"/device/logcat.txt* "$LOCAL"/device/*-logcat.txt "$LOCAL"/device/*-ring.txt "$LOCAL"/device/logcat-tail.txt "$LOCAL"/device/logcat-ring-after.txt "$LOCAL"/device/app-diagnostic-readiness.txt; do
    if [[ -f $file && -z ${SEEN_LOGS[$file]:-} ]]; then
        LOGS+=("$file")
        SEEN_LOGS[$file]=1
    fi
done
INDEX_NOTE='索引保留文件名/行号；轮转文件编号不是时间顺序，ring/tail 可能与原日志重叠。未命中不等于设备无事件。'
printf '%s\n' "$INDEX_NOTE" > "$LOCAL/index-readme.txt"
coverage() { printf '%s\n' "$*" | tee -a "$LOCAL/coverage.txt"; }
coverage '这是日志覆盖提示，不是自动诊断或零断流结论。'
if [[ -f $LOCAL/device/diagnostics-schema.txt ]]; then
    coverage 'FOUND Rouke schema=2（本次启动检测到 APK 诊断能力；不代表三链路已通过）'
else
    coverage 'WARNING: 启动时未验证新版 APK 诊断能力；检查 app-diagnostic-readiness.txt，不能据缺少明细推断控制未发送。'
fi
if (( ${#LOGS[@]} )); then
    grep -nHEi 'videomanager|gcs\.custom\.video|pulledvideoresolution|192\.168\.144\.100|secondaryVideoContent|Secondary |androidh265|androidvideodecoder|Negotiated compressed video|source frame|decoder.*frame|sink.*frame|amcvid|MediaCodec|CCodec|ACodec|qml6|qt6gl|rtspsrc|not-negotiated' "${LOGS[@]}" > "$LOCAL/index-video.txt" || true
    grep -nHEi 'rouke\.(payload|trace|native)|ROUKE_(TX|RX|NATIVE_RX)|Rouke UDP|Rouke command|53022|lr1_|get_sdcard_capacity|gimbal_speed_ctrl|payloadAddress' "${LOGS[@]}" > "$LOCAL/index-payload.txt" || true
    grep -nHEi 'unirc|rouke\.channels|ROUKE_RC|CH11|CH12|CH14|gimbal_speed_ctrl|gimbal_center|UART2|SDK_ROUTE' "${LOGS[@]}" > "$LOCAL/index-rc.txt" || true
    for marker in 'Negotiated compressed video source caps' 'gcs.custom.gimbal.rouke.payload' 'gcs.custom.android.unircchannel' 'ROUKE_TX' 'ROUKE_RX' 'ROUKE_RC'; do
        if grep -Eql "$marker" "${LOGS[@]}"; then coverage "FOUND $marker"
        else coverage "MISSING $marker（可能未操作、日志未启用、旧 APK、链路未到该阶段或日志被覆盖）"; fi
    done
    if ! grep -Eql 'Queueing buffer|Got output buffer' "${LOGS[@]}"; then
        coverage '未找到 AMC 逐帧日志，不能排除短时硬解间断；需同时看 decoder/sink 证据。'
    fi
else
    coverage 'WARNING: 没有 logcat 文件，只能检查采集器/启动错误。'
fi
[[ ! -f $LOCAL/device/logcat.txt.31 ]] || coverage 'WARNING: 已达到轮转容量，采集开头可能被覆盖；检查 startup/launch ring 与 timeline。'
[[ -f $LOCAL/device/recorder.done ]] || coverage 'WARNING: 缺少正常结束标记，本次是部分证据。'
tar -czf "$LOCAL.tar.gz" -C "$(dirname "$LOCAL")" "$(basename "$LOCAL")"
tar -tzf "$LOCAL.tar.gz" >/dev/null
printf '\n日志压缩包：%s.tar.gz\n原始目录：%s\n遥控器原件保留：%s\n' "$LOCAL" "$LOCAL" "$CAP"
# A failed pull/archive must not unlock the next start and orphan this capture.
if ! "${ADB[@]}" shell sh -s -- "$CAP" <<'EXPORTED'
set -eu
CAP=$1
[ "$(cat /sdcard/Download/QGC_Rouke_Diagnostics/current)" = "$CAP" ]
printf 'archive exported successfully\n' > "$CAP/exported.txt"
EXPORTED
then
    printf '本地压缩包已生成，但遥控器导出标记未写入。接好 USB 后再次 finish 即可；原件保留。\n' >&2
    exit 9
fi
printf '请填写 observation.txt；更新后重新打包目录，再提供整个压缩包。\n'
