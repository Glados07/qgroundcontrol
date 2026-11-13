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
        printf '可选：ANDROID_SERIAL、ROUKE_CAPTURE_SECONDS（默认 1200）、ROUKE_CAPTURE_OUTPUT_DIR（默认 HOME）、ROUKE_CAPTURE_VERBOSE_VIDEO=1（详细视频日志）。\n'
        exit 0 ;;
    *) echo 'Unknown mode. Use start, finish, status or help.' >&2; exit 2 ;;
esac
for tool in adb awk tr grep mktemp tar tee date mkdir; do
    command -v "$tool" >/dev/null || { echo "Missing Ubuntu command: $tool" >&2; exit 2; }
done
DURATION=${ROUKE_CAPTURE_SECONDS:-1200}
VERBOSE_VIDEO=${ROUKE_CAPTURE_VERBOSE_VIDEO:-0}
[[ $VERBOSE_VIDEO == 0 || $VERBOSE_VIDEO == 1 ]] || { echo 'ROUKE_CAPTURE_VERBOSE_VIDEO must be 0 or 1.' >&2; exit 2; }
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

"${ADB[@]}" shell sh -s -- "$MODE" "$DURATION" "$VERBOSE_VIDEO" <<'ANDROID'
set -eu
MODE=$1
DURATION=$2
VERBOSE_VIDEO=${3:-0}
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
    dumpsys -t 3 media.codec > "$CAP/media-codec-$DEST.txt" 2>&1 || true
    dumpsys -t 3 media.resource_manager > "$CAP/media-resource-$DEST.txt" 2>&1 || true
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
    # Keep RTSP SETUP/Transport and QGC caps/decoder/first-frame markers by
    # default; avoid per-frame decoder/jitter/GL debug during congestion tests.
    GST_LEVELS='2,rtspsrc:5'
    if [ "$VERBOSE_VIDEO" = 1 ]; then
        GST_LEVELS='2,rtspsrc:5,rtpsession:4,rtpjitterbuffer:5,rtph265depay:4,rtph264depay:4,h265parse:4,h264parse:4,decodebin3:4,amcvideodec:5,amc:4,videodecoder:4,qtsink:5,qgcsinkbin:5,qtglwidget:5,qtglutility:4,glupload:4,glcolorconvert:4'
    fi
    printf 'verbose_video=%s\ngst_debug=%s\n' "$VERBOSE_VIDEO" "$GST_LEVELS" > "$CAP/capture-profile.txt"
    APP_ARGS="--logging:gcs.custom.customplugin,gcs.custom.videomanager.dualvideo,gcs.custom.video.androidh264hardwaredecoderadapter,gcs.custom.video.androidh265decoderfallback,gcs.custom.video.androidh265hardwaredecoderadapter,gcs.custom.video.androidvideodecoderpolicy,gcs.custom.video.androidvideodecoderrecovery,gcs.custom.video.a8rtsprecovery,gcs.custom.gimbal.pulledvideoresolution,gcs.custom.gimbal.rouke.payload,gcs.custom.gimbal.rouke.control,gcs.custom.gimbal.rouke.trace,gcs.custom.gimbal.rouke.raw,gcs.custom.gimbal.rouke.channels,gcs.custom.gimbal.rouke.native,gcs.custom.android.unircchannel,gcs.custom.android.unircchannel.trace,qgc.comms.udplink,qgc.android.androidserial,qgc.android.libs.qtandroidserialport.qserialport_android,qgc.videomanager.videomanager,qgc.videomanager.videoreceiver.gstreamer,qgc.videomanager.videoreceiver.gstreamer.gstvideoreceiver,qgc.videomanager.videoreceiver.gstreamer.api --gst-debug=$GST_LEVELS"
    printf '%s\n' "$APP_ARGS" > "$CAP/launch-arguments.txt"
    cat > "$CAP/recorder.sh" <<'RECORDER'
#!/system/bin/sh
# No ADB file descriptors survive the nohup launch.
CAP=$1
DURATION=$2
VERBOSE_VIDEO=${3:-0}
PACKAGE=org.mavlink.qgroundcontrol
printf '%s\n' "$$" > "$CAP/recorder.pid"
sed 's/.*) //' "/proc/$$/stat" | awk '{print $20}' > "$CAP/recorder.ticks"
START=$(cut -d. -f1 /proc/uptime)
stamp() { printf '%s uptime=%s wall=%s\n' "$1" "$(cut -d. -f1 /proc/uptime)" "$(date '+%Y-%m-%dT%H:%M:%S%z')" >> "$CAP/timeline.txt"; }
# Read-only kernel counters. Access may be denied on production Android;
# record that explicitly rather than interpreting unavailable data as zero.
read_proc() {
    printf 'PROC_BEGIN path=%s\n' "$1"
    if [ -r "$1" ]; then
        cat "$1" || printf 'PROC_UNAVAILABLE path=%s reason=read_error\n' "$1"
    else
        printf 'PROC_UNAVAILABLE path=%s reason=permission_or_missing\n' "$1"
    fi
}
sample_network() {
    printf '\nNET_SAMPLE uptime=%s elapsed=%s wall=%s\n' "$NOW" "$ELAPSED" "$(date '+%Y-%m-%dT%H:%M:%S%z')"
    read_proc /proc/net/dev
    printf 'PROC_BEGIN path=/proc/net/snmp section=Udp\n'
    if [ -r /proc/net/snmp ]; then
        awk '$1 == "Udp:" {print; found=1} END {if (!found) print "PROC_UNAVAILABLE path=/proc/net/snmp reason=no_udp_fields"}' /proc/net/snmp ||
            printf 'PROC_UNAVAILABLE path=/proc/net/snmp reason=read_error\n'
    else printf 'PROC_UNAVAILABLE path=/proc/net/snmp reason=permission_or_missing\n'; fi
    printf 'PROC_BEGIN path=/proc/stat section=cpu_total\n'
    if [ -r /proc/stat ]; then
        awk '$1 == "cpu" {print; found=1} END {if (!found) print "PROC_UNAVAILABLE path=/proc/stat reason=no_cpu_fields"}' /proc/stat ||
            printf 'PROC_UNAVAILABLE path=/proc/stat reason=read_error\n'
    else printf 'PROC_UNAVAILABLE path=/proc/stat reason=permission_or_missing\n'; fi
    read_proc /proc/net/softnet_stat
    APP_PIDS=$(pidof "$PACKAGE" 2>/dev/null || true)
    if [ -z "$APP_PIDS" ]; then printf 'APP_UNAVAILABLE package=%s reason=no_pid\n' "$PACKAGE"; fi
    for P in $APP_PIDS; do
        printf 'APP_SAMPLE pid=%s\n' "$P"
        read_proc "/proc/$P/stat"
        if [ -r "/proc/$P/status" ]; then
            awk '/^(Name|Uid|Threads):/ {print}' "/proc/$P/status" || true
        else printf 'PROC_UNAVAILABLE path=/proc/%s/status reason=permission_or_missing\n' "$P"; fi
    done
}
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
    } >> "$CAP/progress.txt" 2>&1
    sample_network >> "$CAP/network-stats.txt" 2>&1
    # Configuration props alone do NOT prove that the physical cable is connected.
    if [ -r /sys/class/android_usb/android0/state ]; then
        USB=$(cat /sys/class/android_usb/android0/state)
        if [ "$USB" != "$LAST_USB" ]; then stamp "USB_SYSFS_$USB"; LAST_USB=$USB; fi
    fi
    if [ "$((I % 3))" -eq 0 ]; then
        {
            printf '\nuptime=%s elapsed=%s\n' "$NOW" "$ELAPSED"
            ip addr; ip route; ip route get 192.168.144.100; ip route get 192.168.144.110
            if [ "$VERBOSE_VIDEO" = 1 ]; then dumpsys -t 2 usb; fi
        } >> "$CAP/network-progress.txt" 2>&1
        {
            printf '\nSOCKET_SAMPLE uptime=%s elapsed=%s wall=%s\n' "$NOW" "$ELAPSED" "$(date '+%Y-%m-%dT%H:%M:%S%z')"
            read_proc /proc/net/udp
            read_proc /proc/net/udp6
        } >> "$CAP/socket-queues.txt" 2>&1
        FREE_KB=$(df -Pk /sdcard | awk 'END {print $4}')
        case "$FREE_KB" in ''|*[!0-9]*) ;; *)
            if [ "$FREE_KB" -lt 262144 ]; then REASON=low_storage; break; fi ;;
        esac
    fi
    if [ "$VERBOSE_VIDEO" = 1 ] && [ "$((I % 6))" -eq 0 ]; then
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
    # Aim for a 5 s cadence without counting sampling work twice. A slow
    # platform command can still delay a sample; always use recorded uptime.
    WAIT=$((NOW + 5 - $(cut -d. -f1 /proc/uptime)))
    [ "$WAIT" -gt 0 ] || WAIT=1
    sleep "$WAIT"
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
    $NOHUP /system/bin/sh "$CAP/recorder.sh" "$CAP" "$DURATION" "$VERBOSE_VIDEO" \
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
        if awk 'index($0, "ROUKE_DIAGNOSTICS schema=5 msgid=53022 len=132 crc=37") && /(^|[[:space:]])reservePolicy=ignored([[:space:]]|$)/ {found=1} END {exit !found}' "$CAP/app-diagnostic-readiness.txt"; then
            printf 'schema=5 reservePolicy=ignored verified for QGC pid=%s\n' "$APP_PID" > "$CAP/diagnostics-schema.txt"
            break
        fi
        if [ "$I" -ge 20 ]; then
            echo '未检测到新版 APK 的 schema=5 / reservePolicy=ignored 标记，暂不要拔 USB。可能是旧 APK 或 QGC 未完成启动。' >&2
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
    printf '固定 A8 状态，依次无视频 → 开 Rouke → 关 Rouke，各保持 30～60 秒并记录时间；每阶段读同一参数、短拨 CH11/12。\n'
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
A8全程关闭或固定状态（必须说明）：
A 无视频 30～60s：开始/结束时间；如何停止拉流；ISO读取时间/结果；CH11/12短拨时间/转动及停止结果：
B 开 Rouke 30～60s：开始/结束时间；首帧/卡顿时间；ISO读取时间/结果；CH11/12短拨时间/结果：
C 关 Rouke 30～60s：开始/结束时间；如何停止拉流；ISO读取时间/结果；CH11/12是否恢复及时间：
重复 A/B/C（如做）：各阶段时间和结果：
其他参数测试：白平衡/焦距/图片大小读到的值；允许修改时填写参数名、原值、目标值、回显、恢复原值：
CH14测试：释放及按下时间/回中结果：
远端是否仍在发送视频（仅隐藏窗口不等于停止传输，不确定请注明）：
是否切后台/录屏/改IP或视频设置/重启QGC：
三类症状是否在同一时间出现：
商家同期发送A1/118心跳、C1/48尾片的时间/频率及对应日志（若可提供）：
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
cat >> "$LOCAL/index-readme.txt" <<'FIELDS'

schema=5 / reservePolicy=ignored：接收 reserve 只记录、不作为拒绝条件，发送保持 0。
ROUKE_RX_CONFIG 记录 session、端点及本地端口；同 session/report 的
ROUKE_RX_SNAPSHOT / TRANSPORT / INNER 为一次周期或超时快照。
ROUKE_RX_RELAY_FIRST 表示配置端点第一次送来 CRC 通过的 MAVLink 2 / 53022，
此时还未检查 relay 字段和内层。ROUKE_RX cmd 表示完整内层通过方向/地址检查，
命令是否成功仍看 ack/accepted/pendingMatch 及 ROUKE_RX_RESULT success。
SNAPSHOT 的 ageMs/gapMs/last*Ms 单位为毫秒；last*Ms=-1 表示从未发生。
gapMs 是距上次任意快照（含 configured/timeout）的间隔；周期日志缺失或变长
不等于零收包，需结合日志覆盖、进程存活及 CPU/事件循环调度判断。
TRANSPORT 的 udp/bytes/peer/rejectAddress/rejectPort/badCrc/incomplete/other/relay/
invalidRelay 等是会话累计；bytes 单位为字节，relay 在内层检查前累计。
INNER 的 valid/matched/rejectRoute/rejectPayload 等是累计；pending 为当前重组流数。
valid 为完整合法信封，matched 为进一步通过方向/地址检查；都不等于参数请求成功。
日志索引可重复包含 ring/tail；coverage 的命中行数只是覆盖情况，不是网络报文数。

index-53022-raw.txt 为 QGC 入口观察，不是网卡 pcap：
ROUKE_WIRE_RX 记录通过 MAVLink CRC 的 53022 字段，lane=payload 是独立控制 UDP
socket（在远端来源及内层业务过滤前观察）；lane=native 是原生 MAVLink 已解析入口。
payload 的 peer/localPort/peerMatch 定位回到哪个 socket，native 的 channel/linkType/
udpConfiguredLocalPort/logReplay 定位原生链路；native 不提供已验证的远端 UDP 源地址。
native 的 WIRE_TOTAL 带 liveOnly=1，按观察会话汇总所有非回放原生链路，不是逐 link 计数；
旧 ROUKE_NATIVE_RX 仍可能含 logReplay=1，仅其非回放记录可作为实时证据。
sys/comp/seq/len/checksum 是外层信息；payload/reserve/dataLen 是 53022 字段，reserve 为十六进制。
ROUKE_WIRE_DATA 按 offset 拼接有效 data；独立 socket 的 ROUKE_WIRE_FRAME 直接取
已收 UDP 数据报内的真实 MAVLink 帧字节（representation=udp-bytes），不含以太网/IP/UDP头。
原生入口只有解码消息，representation=decoded，ROUKE_WIRE_PAYLOAD 输出外层 payload，
不伪称线上完整帧。每行最多 48 字节 HEX，用 lane/session/sample 关联，再按 offset 排序。
ROUKE_WIRE_TOTAL 每秒输出 valid53022/reserveA1/reserveC1/a1Heartbeat/c1AckTail/
peerRejected/emitted/suppressed/cap，累计量按同进程、同 lane/session 比较阶段增量。
reserveA1/reserveC1 只统计该 reserve；sampleMatch 与 a1Heartbeat/c1AckTail 要求
payload_addr=0、reserve、data_len 和完整 data 字节都与商家样例相同，不只是长度相同。
C1 尾片必须与同源首片重组成完整内层帧，才能确认命令并进入 ACK 匹配。
payload 样例命中说明该报文已到独立 socket，不能把这一报文归因为视频拥塞丢失；
native 命中只证明原生链路收到，不证明独立控制 socket 收到。
decision=outer-pass 只表示通过初步外层检查，后续业务结果看 INNER/ROUKE_RX_RESULT。
两个 lane 可能观察同一网络流量的副本，不能相加当作唯一网卡报文总数。
详细样本按 reserve 分类限速，每入口最多 4 个/秒、总上限 240；超过限额仍累计 TOTAL，
因此缺少某一时刻的 HEX 不等于未收到，不能用 RX 命中行数代替 TOTAL 计数。
合法的 A1 heartbeat 内层帧可刷新在线状态，但不是参数 ACK；raw 样例命中不等于参数请求成功。
坏 CRC、未到 QGC socket 或日志覆盖范围外的字节不能由这些记录证明，需要网卡抓包补证。

index-network.txt 汇集下列原始采样及 qgc.comms.udplink / UDP Adding target 日志（文件名/行号保留）：
原生 UDP 日志记录本地绑定端口和自动学习目标，只说明 QGC 端配置/目标发现，不证明 AI 盒子已改变回程路由。
network-stats.txt：约每 5 秒；NET_SAMPLE 的 uptime/elapsed 单位为秒，wall 为设备时钟。
/proc/net/dev：每个网卡 RX/TX bytes/packets/errs/drop 等累计计数，按标题列读取。
/proc/net/snmp：Udp 标题与下一行数值对应，关注 InDatagrams/OutDatagrams/InErrors/
RcvbufErrors/SndbufErrors/NoPorts；这些是网络命名空间累计值，不是 QGC 专属丢包。
/proc/stat 的 cpu：全机 CPU 累计 tick；/proc/PID/stat 的 utime/stime 为进程 CPU tick，
starttime 可区分 PID 重用；同 PID、同 starttime 的样本才可计算增量。
/proc/net/softnet_stat：每 CPU 一行十六进制，前三列为 processed/dropped/time_squeeze；
属于内核收包处理累计统计，不直接证明链路哪端丢了 53022。
socket-queues.txt：约每 15 秒；/proc/net/udp{,6} 的 tx_queue:rx_queue 是十六进制队列，
尾列 drops 为该 socket 的累计丢弃，结合 local_address、uid、inode 及本次 QGC localPort 关联。
Android 可能禁止读取以上文件；PROC_UNAVAILABLE 表示不可取得，不能解释为零。
network-progress/start/finish：地址和路由快照。USB 连接期间路由与正式测试阶段可能不同。
分析时按 uptime 与 observation 的开/关视频阶段比较增量，不直接比较累计总量；
接口重建、设备重启或计数下降需重新取基线。默认不输出 AMC 逐帧日志，详细模式由 capture-profile.txt 标识。
FIELDS
NETWORK_LOGS=("$LOCAL"/device/network-stats.txt "$LOCAL"/device/socket-queues.txt "$LOCAL"/device/network-progress.txt "$LOCAL"/device/network-start.txt "$LOCAL"/device/network-finish.txt)
: > "$LOCAL/index-network.txt"
for file in "${NETWORK_LOGS[@]}"; do
    [[ ! -f $file ]] || grep -nH '^' "$file" >> "$LOCAL/index-network.txt" || true
done
coverage() { printf '%s\n' "$*" | tee -a "$LOCAL/coverage.txt"; }
coverage '这是日志覆盖提示，不是自动诊断或零断流结论。'
if [[ -f $LOCAL/device/diagnostics-schema.txt ]] && grep -Fq 'schema=5 reservePolicy=ignored verified' "$LOCAL/device/diagnostics-schema.txt"; then
    coverage 'FOUND Rouke schema=5 reservePolicy=ignored（本次启动已确认入口诊断及reserve接收策略；不代表三链路已通过）'
else
    coverage 'WARNING: 启动时未验证新版 APK 诊断能力；检查 app-diagnostic-readiness.txt，不能据缺少明细推断控制未发送。'
fi
if (( ${#LOGS[@]} )); then
    grep -nHEi 'qgc\.comms\.udplink|UDP Adding target:' "${LOGS[@]}" >> "$LOCAL/index-network.txt" || true
    grep -nHEi 'videomanager|gcs\.custom\.video|pulledvideoresolution|192\.168\.144\.100|secondaryVideoContent|Secondary |androidh265|androidvideodecoder|Negotiated compressed video|source frame|decoder.*frame|sink.*frame|amcvid|MediaCodec|CCodec|ACodec|qml6|qt6gl|rtspsrc|not-negotiated' "${LOGS[@]}" > "$LOCAL/index-video.txt" || true
    grep -nHEi 'rouke\.(payload|trace|raw|native)|ROUKE_(TX|RX|WIRE|NATIVE_RX)|Rouke UDP|Rouke command|53022|lr1_|get_sdcard_capacity|gimbal_speed_ctrl|payloadAddress' "${LOGS[@]}" > "$LOCAL/index-payload.txt" || true
    grep -nHE 'ROUKE_WIRE_(RX|DATA|FRAME|PAYLOAD|TOTAL)|ROUKE_RX_CONFIG|ROUKE_DIAGNOSTICS' "${LOGS[@]}" > "$LOCAL/index-53022-raw.txt" || true
    grep -nHEi 'unirc|rouke\.channels|ROUKE_RC|CH11|CH12|CH14|gimbal_speed_ctrl|gimbal_center|UART2|SDK_ROUTE' "${LOGS[@]}" > "$LOCAL/index-rc.txt" || true
    for marker in 'Negotiated compressed video source caps' 'gcs.custom.gimbal.rouke.payload' 'gcs.custom.android.unircchannel' 'ROUKE_TX' 'ROUKE_WIRE_TOTAL' 'ROUKE_WIRE_RX' 'ROUKE_RX_RELAY_FIRST' 'ROUKE_RX_TRANSPORT' 'ROUKE_RX_INNER' 'ROUKE_RX cmd ' 'ROUKE_RX_RESULT' 'ROUKE_RC'; do
        if grep -Eql "$marker" "${LOGS[@]}"; then coverage "FOUND $marker"
        else coverage "MISSING $marker（可能未操作、日志未启用、旧 APK、链路未到该阶段或日志被覆盖）"; fi
    done
    for marker in 'ROUKE_RX_RELAY_FIRST' 'ROUKE_RX cmd '; do
        COUNT=$(awk -v marker="$marker" 'index($0, marker) {n++} END {print n+0}' "${LOGS[@]}")
        coverage "INDEX_LINES $marker=$COUNT（可能含重复快照，仅表示日志覆盖；报文累计量看 TRANSPORT/INNER）"
    done
    coverage '默认关闭 AMC/GL 逐帧 debug；视频判断结合 QGC codec、decoder/sink 首帧和错误。'
    coverage '53022 HEX 有限采样；A/B/C阶段样例接收数量看同lane/session的WIRE_TOTAL增量，日志索引行数不是包数。'
else
    coverage 'WARNING: 没有 logcat 文件，只能检查采集器/启动错误。'
fi
for file in network-stats.txt socket-queues.txt; do
    if [[ -s $LOCAL/device/$file ]]; then coverage "FOUND $file（不可读项保留 PROC_UNAVAILABLE）"
    else coverage "MISSING $file（本次未取得该阶段采样）"; fi
done
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
