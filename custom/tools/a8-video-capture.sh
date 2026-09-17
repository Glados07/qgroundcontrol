#!/usr/bin/env bash
# Ubuntu + USB ADB: start, unplug and test, reconnect and finish.
# Diagnostics only: no decoder/network/settings changes; no device logs deleted.
set -euo pipefail
umask 077

MODE=${1:-start}
case "$MODE" in start|finish) ;; *) echo "Usage: bash $0 [start|finish]" >&2; exit 2 ;; esac
for TOOL in adb awk tr grep mktemp tar tee; do
    command -v "$TOOL" >/dev/null || { echo "Missing command: $TOOL" >&2; exit 2; }
done
ADB=(adb)
[[ -z ${ANDROID_SERIAL:-} ]] || ADB+=(-s "$ANDROID_SERIAL")
[[ $("${ADB[@]}" get-state | tr -d '\r') == device ]] || exit 2

"${ADB[@]}" shell sh -s -- "$MODE" <<'ANDROID'
set -eu
MODE=$1
ROOT=/sdcard/Download/QGC_A8_Stability
PACKAGE=org.mavlink.qgroundcontrol
ACTIVITY=org.mavlink.qgroundcontrol.QGCActivity
CAP=$(cat "$ROOT/current" 2>/dev/null || true)
valid_capture() {
    printf '%s\n' "$1" | grep -Eq '^/sdcard/Download/QGC_A8_Stability/session_[0-9]{8}_[0-9]{6}_[0-9]+$'
}
valid_capture "$CAP" || CAP=''
same_logger() {
    [ -n "$CAP" ] && [ -r "$CAP/logger.pid" ] || return 1
    PID=$(cat "$CAP/logger.pid")
    case "$PID" in ''|*[!0-9]*) return 1 ;; esac
    [ "$PID" -gt 1 ] && kill -0 "$PID" 2>/dev/null || return 1
    [ "$(cat /proc/sys/kernel/random/boot_id)" = "$(cat "$CAP/boot-id.txt")" ] || return 1
    [ "$(sed 's/.*) //' "/proc/$PID/stat" | awk '{print $20}')" = "$(cat "$CAP/logger.ticks")" ] || return 1
    tr '\000' '\n' < "/proc/$PID/cmdline" | grep -Fqx "$CAP/logcat.txt"
}

if [ "$MODE" = start ]; then
    if same_logger; then
        echo 'A capture is already running. Use finish to export it first.' >&2
        exit 3
    fi
    # Refuse to overwrite the pointer to an unfinished capture.
    if [ -n "$CAP" ] && [ ! -f "$CAP/export-ready.txt" ]; then
        echo 'Previous capture is unfinished. Run finish first (also recovers stopped loggers).' >&2
        exit 3
    fi
    command -v logcat >/dev/null
    if [ -x /system/bin/nohup ]; then
        NOHUP=/system/bin/nohup
    elif /system/bin/toybox nohup true >/dev/null 2>&1; then
        NOHUP='/system/bin/toybox nohup'
    else
        echo 'No nohup on this device; do not unplug USB.' >&2; exit 4
    fi
    pm path "$PACKAGE" | grep -q '^package:' || { echo 'QGC package not installed' >&2; exit 4; }
    FREE_KB=$(df -Pk /sdcard | awk 'END {print $4}')
    case "$FREE_KB" in ''|*[!0-9]*) echo 'Cannot verify device free space' >&2; exit 4 ;; esac
    [ "$FREE_KB" -ge 1048576 ] || { echo 'Need at least 1 GiB free on controller' >&2; exit 4; }
    mkdir -p "$ROOT"
    CAP="$ROOT/session_$(date +%Y%m%d_%H%M%S)_$$"
    mkdir "$CAP"
    printf '%s\n' "$CAP" > "$ROOT/current"
    cat /proc/sys/kernel/random/boot_id > "$CAP/boot-id.txt"
    { date; cat /proc/uptime; getprop ro.build.fingerprint; getprop ro.product.model; df -Pk /sdcard; } > "$CAP/device-start.txt"
    dumpsys package "$PACKAGE" > "$CAP/package.txt" 2>&1 || true
    logcat -g > "$CAP/logd-start.txt" 2>&1 || true

    # No GST_DEBUG environment override: retain QGC's Qt logging bridge.
    # applicationArguments also works for non-debuggable Qt 6.8 APKs.
    APP_ARGS='--logging:gcs.custom.customplugin,gcs.custom.videomanager.dualvideo,gcs.custom.video.a8rtsprecovery,gcs.custom.video.clockdiagnostics,gcs.custom.video.androidh265decoderfallback,gcs.custom.video.androidh265hardwaredecoderadapter,gcs.custom.video.androidvideodecoderpolicy,gcs.custom.video.androidvideodecoderrecovery,qgc.videomanager.videomanager,qgc.videomanager.videoreceiver.gstreamer.gstvideoreceiver,qgc.videomanager.videoreceiver.gstreamer.api --gst-debug=2,rtspsrc:5,rtpsession:4,rtpjitterbuffer:5,rtph265depay:4,rtph264depay:4,h265parse:4,h264parse:4,amcvideodec:5,amc:4,videodecoder:4'
    printf '%s\n' "$APP_ARGS" > "$CAP/launch-arguments.txt"

    # The detached shell owns/waits for logcat: no stale-PID kill on timeout.
    # Maximum runtime 15 minutes and 32 x 16 MiB rotated files (~512 MiB).
    # All inherited ADB descriptors are redirected before detachment.
    $NOHUP /system/bin/sh -c '
        CAP=$1
        /system/bin/logcat -b main -b system -b crash -v threadtime -T 1 \
            -f "$CAP/logcat.txt" -r 16384 -n 31 "*:V" &
        LOGGER=$!
        printf "%s\n" "$LOGGER" > "$CAP/logger.pid"
        sed "s/.*) //" "/proc/$LOGGER/stat" | awk "{print \$20}" > "$CAP/logger.ticks"
        START=$(cut -d. -f1 /proc/uptime)
        printf "CAPTURE_BEGIN uptime=%s wall=%s\n" "$START" "$(date)" >> "$CAP/timeline.txt"
        log -p i -t QGC_A8_CAPTURE "CAPTURE_BEGIN $CAP" || true
        I=0
        while kill -0 "$LOGGER" 2>/dev/null && [ ! -f "$CAP/stop" ]; do
            NOW=$(cut -d. -f1 /proc/uptime)
            [ "$((NOW - START))" -lt 900 ] || break
            {
                printf "\nuptime=%s wall=%s usb.config=%s usb.state=%s\n" "$NOW" "$(date)" "$(getprop sys.usb.config)" "$(getprop sys.usb.state)"
                if [ -r /sys/class/android_usb/android0/state ]; then cat /sys/class/android_usb/android0/state; fi
                cat /proc/net/dev /proc/net/snmp /proc/stat
                for P in $(pidof org.mavlink.qgroundcontrol); do cat "/proc/$P/stat"; done
            } >> "$CAP/progress.txt" 2>&1
            if [ "$((I % 6))" -eq 0 ]; then
                # Some firmware lacks media.codec. Preserve the error as evidence.
                dumpsys -t 3 media.codec > "$CAP/media-codec-$I.txt" 2>&1 || true
                dumpsys -t 3 media.resource_manager > "$CAP/media-resource-$I.txt" 2>&1 || true
            fi
            I=$((I + 1))
            sleep 5
        done
        printf "CAPTURE_END uptime=%s wall=%s requested=%s\n" "$(cut -d. -f1 /proc/uptime)" "$(date)" "$(test -f "$CAP/stop" && echo yes || echo no)" >> "$CAP/timeline.txt"
        log -p i -t QGC_A8_CAPTURE "CAPTURE_END $CAP" || true
        # Check identity again; a shell can reap background jobs asynchronously.
        TICKS=$(sed "s/.*) //" "/proc/$LOGGER/stat" 2>/dev/null | awk "{print \$20}")
        if [ -n "$TICKS" ] && [ "$TICKS" = "$(cat "$CAP/logger.ticks")" ] && \
            tr "\000" "\n" < "/proc/$LOGGER/cmdline" | grep -Fqx "$CAP/logcat.txt"; then
            kill -TERM "$LOGGER" 2>/dev/null || true
        fi
        wait "$LOGGER" 2>/dev/null || true
        # Preserve any stdio tail lost on SIGTERM. This dump overlaps main logs.
        logcat -b main -b system -b crash -d -v threadtime -f "$CAP/logcat-tail.txt" "*:V" 2> "$CAP/tail-error.txt" || true
        printf "done\n" > "$CAP/recorder.done"
    ' qgc-a8-recorder "$CAP" </dev/null > "$CAP/recorder.stdout.txt" 2> "$CAP/recorder.stderr.txt" &

    sleep 2
    same_logger || { echo 'Recorder did not start; do not unplug. Run finish to collect errors.' >&2; exit 5; }
    am force-stop "$PACKAGE"
    am start -W -n "$PACKAGE/$ACTIVITY" --es applicationArguments "$APP_ARGS" > "$CAP/app-start.txt" 2>&1
    if grep -Ei 'Error:|Exception|does not exist|unable to resolve' "$CAP/app-start.txt"; then exit 6; fi
    I=0
    while ! pidof "$PACKAGE" >/dev/null 2>&1 && [ "$I" -lt 10 ]; do sleep 1; I=$((I + 1)); done
    pidof "$PACKAGE" > "$CAP/app-pid-start.txt" || { echo 'QGC did not stay running; use finish.' >&2; exit 6; }
    echo "Device capture: $CAP"
else
    [ -n "$CAP" ] && [ -d "$CAP" ] || { echo 'No capture found on this controller' >&2; exit 7; }
    # A stop file terminates only this recorder, not QGC or other logcat tools.
    { date; cat /proc/uptime; } > "$CAP/usb-return.txt"
    touch "$CAP/stop"
    I=0
    while [ ! -f "$CAP/recorder.done" ] && [ "$I" -lt 20 ]; do sleep 1; I=$((I + 1)); done
    if same_logger; then
        echo 'Recorder still running. Wait 10 seconds and run finish again.' >&2; exit 8
    fi
    if [ ! -f "$CAP/recorder.done" ]; then
        echo 'WARNING: recorder exited abnormally; exporting available evidence.' >&2
    fi
    logcat -b main -b system -b crash -d -v threadtime -f "$CAP/logcat-ring-after.txt" '*:V' 2> "$CAP/ring-error.txt" || true
    logcat -g > "$CAP/logd-finish.txt" 2>&1 || true
    { date; cat /proc/uptime; cat /proc/sys/kernel/random/boot_id; df -Pk /sdcard; } > "$CAP/device-finish.txt"
    printf 'ready\n' > "$CAP/export-ready.txt"
fi
ANDROID

if [[ $MODE == start ]]; then
    # Confirm survival in a separate ADB session, after the launching shell exits.
    "${ADB[@]}" shell sh -s <<'VERIFY'
set -eu
CAP=$(cat /sdcard/Download/QGC_A8_Stability/current)
printf '%s\n' "$CAP" | grep -Eq '^/sdcard/Download/QGC_A8_Stability/session_[0-9]{8}_[0-9]{6}_[0-9]+$'
PID=$(cat "$CAP/logger.pid")
case "$PID" in ''|*[!0-9]*) exit 1 ;; esac
kill -0 "$PID"
tr '\000' '\n' < "/proc/$PID/cmdline" | grep -Fqx "$CAP/logcat.txt"
[ -s "$CAP/logcat.txt" ]
pidof org.mavlink.qgroundcontrol >/dev/null
VERIFY
    printf '\n采集已启动。现在拔 USB，进入双云台界面测试 10 分钟。\n'
    printf '保持相机供电、QGC 前台；不要切换视频设置或录屏。\n'
    printf '完成后接回 USB，执行：bash "%s" finish\n' "$0"
    printf '本地记录最多运行 15 分钟；不需要让 Ubuntu 保持连接。\n'
    exit 0
fi

CAP=$("${ADB[@]}" shell cat /sdcard/Download/QGC_A8_Stability/current | tr -d '\r\n')
[[ $CAP =~ ^/sdcard/Download/QGC_A8_Stability/session_[0-9]{8}_[0-9]{6}_[0-9]+$ ]] || exit 7
LOCAL=$(mktemp -d "${A8_CAPTURE_OUTPUT_DIR:-$HOME}/a8-stability-$(date +%Y%m%d-%H%M%S)-XXXXXX")
"${ADB[@]}" get-serialno > "$LOCAL/adb-serial.txt"
"${ADB[@]}" pull "$CAP" "$LOCAL/device"
printf '%s\n' '请填写：A8 是否始终供电；拔/插 USB 大致时间；是否卡顿、马赛克或重连及其时间；是否切后台/改设置。' > "$LOCAL/observation.txt"
# Only a coverage hint, never a zero-disconnection verdict. Keep every raw file.
if ! grep -l -m 1 'Queueing buffer\|Got output buffer' "$LOCAL"/device/logcat.txt* >/dev/null; then
    printf '%s\n' 'WARNING: no AMC per-frame messages found; this capture cannot rule out brief decoder gaps.' | tee "$LOCAL/coverage-warning.txt"
fi
if [[ -f $LOCAL/device/logcat.txt.31 ]]; then
    printf '%s\n' 'WARNING: rotation capacity reached; check whether capture beginning was overwritten.' | tee -a "$LOCAL/coverage-warning.txt"
fi
tar -czf "$LOCAL.tar.gz" -C "$(dirname "$LOCAL")" "$(basename "$LOCAL")"
printf '\n日志压缩包：%s.tar.gz\n原始目录：%s\n遥控器原件保留：%s\n' "$LOCAL" "$LOCAL" "$CAP"
