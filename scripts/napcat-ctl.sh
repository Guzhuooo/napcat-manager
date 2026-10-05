#!/bin/sh
# ============================================================================
#  NapCat 设备侧启停控制（在词典笔上运行）
#
#  用法:
#    sh napcat-ctl.sh start     启动（含 Xvfb）
#    sh napcat-ctl.sh stop      停止
#    sh napcat-ctl.sh restart   重启
#    sh napcat-ctl.sh status    查看状态
#    sh napcat-ctl.sh install   安装 miniapp 管理器
#
#  安全边界：只操作 /userdisk/napcat/ 与 QQ/NapCat 进程，
#  绝不触碰系统 miniapp / guardian_run / DictPen。
# ============================================================================
set -u

BASE=/userdisk/napcat
R=$BASE/rootfs
PIDFILE=$BASE/state/napcat.pid
LOG=$BASE/logs/napcat.log
XVFB_LOG=$BASE/logs/xvfb.log
XVFB_DISPLAY=:99

mkdir -p "$BASE/logs" "$BASE/state"

# ---------- 内部工具 ----------

# 找 NapCat 主进程 PID（严格匹配，排除 zygote / sh -c / grep）
napcat_pid() {
    ps -ef 2>/dev/null | grep 'qq --no-sandbox' | grep -v grep \
        | grep -v -- '--type=zygote' | grep -v 'sh -c' \
        | awk '{print $1}' | head -1
}

xvfb_running() {
    pgrep -f "Xvfb $XVFB_DISPLAY" >/dev/null 2>&1
}

ensure_mounts() {
    # chroot 的 bind mount 在重启后会丢失，这里按需重建
    for d in /dev /proc /sys; do
        mountpoint -q "$R$d" 2>/dev/null || mount --bind "$d" "$R$d" 2>/dev/null
    done
    [ -e "$R/lib/libtirpc.so.3" ] || mount --bind "$BASE/mergedlib" "$R/lib" 2>/dev/null
    [ -e "$R/usr/lib/libtirpc.so.3" ] || mount --bind "$BASE/mergedlib" "$R/usr/lib" 2>/dev/null
    mountpoint -q "$R/opt/QQ" 2>/dev/null || mount --bind "$BASE/opt/QQ" "$R/opt/QQ" 2>/dev/null
}

start_xvfb() {
    xvfb_running && return 0
    echo "启动 Xvfb $XVFB_DISPLAY ..."
    setsid env -u LD_LIBRARY_PATH chroot "$R" /bin/sh -c \
        "exec /usr/bin/Xvfb $XVFB_DISPLAY -screen 0 1280x800x24 -nolisten tcp" \
        > "$XVFB_LOG" 2>&1 < /dev/null &
    sleep 3
    xvfb_running && echo "  Xvfb 就绪" || { echo "  Xvfb 启动失败，见 $XVFB_LOG"; return 1; }
}

# ---------- 命令 ----------

do_start() {
    local pid
    pid=$(napcat_pid)
    if [ -n "$pid" ]; then
        echo "NapCat 已在运行 (PID $pid)"
        return 0
    fi

    ensure_mounts
    start_xvfb || return 1

    echo "启动 NapCat ..."
    setsid env -u LD_LIBRARY_PATH DISPLAY=$XVFB_DISPLAY \
        chroot "$R" /bin/sh -c \
        'cd /opt/QQ && exec ./qq --no-sandbox' \
        > "$LOG" 2>&1 < /dev/null &
    echo $! > "$PIDFILE"
    sleep 4

    pid=$(napcat_pid)
    if [ -n "$pid" ]; then
        echo "已启动 (PID $pid)"
        echo "WebUI: http://$(ip -4 addr show wlan0 2>/dev/null | awk '/inet /{print $2}' | cut -d/ -f1):6099/webui"
    else
        echo "启动失败，末尾日志："
        tail -20 "$LOG" 2>/dev/null | grep -v asound
        return 1
    fi
}

do_stop() {
    local pid
    pid=$(napcat_pid)
    if [ -z "$pid" ]; then
        echo "NapCat 未在运行"
        rm -f "$PIDFILE"
        return 0
    fi
    echo "停止 NapCat (PID $pid) ..."
    # 先 TERM 主进程，再清理 zygote 子进程
    for p in $(ps -ef 2>/dev/null | grep 'qq --no-sandbox' | grep -v grep | awk '{print $1}'); do
        kill -TERM "$p" 2>/dev/null
    done
    sleep 2
    for p in $(ps -ef 2>/dev/null | grep 'qq --no-sandbox' | grep -v grep | awk '{print $1}'); do
        kill -KILL "$p" 2>/dev/null
    done
    rm -f "$PIDFILE"
    echo "已停止（Xvfb 保持运行，可复用）"
}

do_status() {
    local pid ip
    pid=$(napcat_pid)
    ip=$(ip -4 addr show wlan0 2>/dev/null | awk '/inet /{print $2}' | cut -d/ -f1)
    [ -z "$ip" ] && ip=127.0.0.1

    echo "=== NapCat 状态 ==="
    if [ -n "$pid" ]; then
        echo "进程      : 运行中 (PID $pid)"
        echo "内存      : $(grep VmRSS /proc/$pid/status 2>/dev/null | awk '{print $2/1024 " MB"}')"
    else
        echo "进程      : 已停止"
    fi
    xvfb_running && echo "Xvfb      : 运行中" || echo "Xvfb      : 未运行"
    echo "WebUI     : http://$ip:6099/webui"
    echo "OneBot    : 3000 (在 WebUI 中配置)"
    echo "日志      : $LOG"
    echo "账号      : $(grep -o '登录成功.*' "$LOG" 2>/dev/null | tail -1)"
}

do_restart() {
    do_stop
    sleep 1
    do_start
}

do_install() {
    local amr=$1
    if [ -z "$amr" ] || [ ! -f "$amr" ]; then
        echo "用法: $0 install <path/to/xxx.amr>"
        return 1
    fi
    miniapp_cli install "$amr" || return 1
    miniapp_cli start 8001799000000051 index
}

# ---------- 入口 ----------

case "${1:-status}" in
    start)   do_start ;;
    stop)    do_stop ;;
    restart) do_restart ;;
    status)  do_status ;;
    install) shift; do_install "${1:-}" ;;
    *)
        echo "用法: $0 {start|stop|restart|status|install <amr>}"
        exit 1
        ;;
esac
