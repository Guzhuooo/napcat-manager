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

# 判断路径是否已是挂载点。
# 不能用 mountpoint/stat：同一文件系统内的 bind 挂载 st_dev 不变，
# busybox mountpoint 会误判为"未挂载"从而重复叠挂。用 /proc/mounts 精确比对第二列。
is_mounted() {
    awk -v m="$1" '$2 == m { found = 1; exit } END { exit(found ? 0 : 1) }' /proc/mounts
}

# 幂等 bind 挂载（已挂则跳过；否则先建目录再挂）
ensure_bind() { # src dst
    is_mounted "$2" && return 0
    mkdir -p "$2" 2>/dev/null
    mount --bind "$1" "$2" 2>/dev/null
}

ensure_mounts() {
    # chroot 的 bind mount 在设备重启后会全部丢失，这里按需完整重建。
    # 注意：chroot 内要跑 Xvfb/QQ，/bin /sbin /tmp 一个都不能少
    # （缺 /bin 会让 chroot 连 /bin/sh 都没有，Xvfb 直接起不来）。
    ensure_bind /dev  "$R/dev"
    ensure_bind /proc "$R/proc"
    ensure_bind /sys  "$R/sys"
    ensure_bind /tmp  "$R/tmp"
    ensure_bind /bin  "$R/bin"
    ensure_bind /sbin "$R/sbin"
    ensure_bind "$BASE/mergedlib" "$R/lib"
    ensure_bind "$BASE/mergedlib" "$R/usr/lib"
    ensure_bind "$BASE/opt/QQ"    "$R/opt/QQ"

    # 自检：chroot 必须可见 /bin/sh，否则后续启动必然失败，提前报清楚
    if [ ! -x "$R/bin/sh" ]; then
        echo "  [错误] chroot 不可用：$R/bin/sh 缺失，挂载失败或环境不完整"
        return 1
    fi
    return 0
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
    local qq="${1:-}"
    local pid
    pid=$(napcat_pid)
    if [ -n "$pid" ]; then
        echo "NapCat 已在运行 (PID $pid)"
        return 0
    fi

    # 记住账号：下次启动/重启免扫码（凭据失效时 NapCat 自动回落二维码）
    if [ -n "$qq" ]; then
        echo "$qq" > "$BASE/state/account"
    fi

    ensure_mounts || return 1
    start_xvfb || return 1

    # 优先用保存的账号快速登录
    local acct="$qq"
    [ -z "$acct" ] && [ -f "$BASE/state/account" ] && acct=$(cat "$BASE/state/account" 2>/dev/null)

    echo "启动 NapCat ${acct:+(快速登录 $acct)} ..."
    setsid env -u LD_LIBRARY_PATH DISPLAY=$XVFB_DISPLAY \
        chroot "$R" /bin/sh /root/bin/run-napcat.sh ${acct:+"$acct"} \
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
        # cmdline 以 \0 分隔，-q 与号码是相邻两个参数
        echo "账号      : $(tr '\0' '\n' < /proc/$pid/cmdline 2>/dev/null | grep -A1 '^-q$' | tail -1)"
        echo "内存      : $(grep VmRSS /proc/$pid/status 2>/dev/null | awk '{print $2/1024 " MB"}')"
    else
        echo "进程      : 已停止"
        [ -f "$BASE/state/account" ] && echo "记住账号  : $(cat $BASE/state/account)"
    fi
    xvfb_running && echo "Xvfb      : 运行中" || echo "Xvfb      : 未运行"
    echo "WebUI     : http://$ip:6099/webui"

    # OneBot 状态：从当前账号配置读取
    local cfg
    cfg=$(ls -1t "$R/opt/QQ/resources/app/napcat/config"/onebot11_*.json 2>/dev/null | head -1)
    if [ -n "$cfg" ]; then
        local port en
        port=$(sed -n 's/.*"port": \([0-9]*\).*/\1/p' "$cfg" | head -1)
        en=$(grep -c '"enable": true' "$cfg" 2>/dev/null)
        if [ -n "$port" ] && [ "$en" -gt 0 ] 2>/dev/null; then
            echo "OneBot    : $port (已启用)"
        else
            echo "OneBot    : 未启用"
        fi
    fi
    echo "日志      : $LOG"
}

do_restart() {
    do_stop
    sleep 1
    do_start
}

do_ip() {
    ip -4 addr show wlan0 2>/dev/null | awk '/inet /{print $2}' | cut -d/ -f1 | head -1
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
    start)   shift; do_start "${1:-}" ;;
    stop)    do_stop ;;
    restart) do_stop; sleep 1; do_start ;;
    status)  do_status ;;
    ip)      do_ip ;;
    install) shift; do_install "${1:-}" ;;
    *)
        echo "用法: $0 {start|stop|restart|status|ip|install <amr>}"
        exit 1
        ;;
esac
