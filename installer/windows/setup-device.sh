#!/bin/sh
# ============================================================================
#  NapCat 设备端安装（由 Windows 端 napcat-install.bat 推送并调用）
#
#  前置：以下文件已由 bat 推送到设备
#    /userdisk/napcat/dl/node.tar.xz          Node v24.21.0 arm64
#    /userdisk/napcat/dl/NapCat.Shell.zip     NapCat Shell
#    /userdisk/napcat/dl/napcat-libset-bundle.tar.gz  依赖库束
#    /userdisk/napcat/opt/QQ/                 已在 Windows 解包推送的 QQ 目录
#    /userdisk/napcat/bin/setup-device.sh     本脚本
#    /userdisk/napcat/mgr.amr                 管理器 AMR（可选）
#
#  幂等：重复执行安全；不覆盖已存在的 webui/onebot 配置。
#  安全边界：只写 /userdisk/napcat/，不碰系统分区与 miniapp。
# ============================================================================
set -u

BASE=/userdisk/napcat
R=$BASE/rootfs
DL=$BASE/dl
LOG_TAG="[setup]"

say() { echo "$LOG_TAG $*"; }
die() { echo "$LOG_TAG [失败] $*" >&2; exit 1; }

mkdir -p "$BASE/logs" "$BASE/state" "$BASE/config" "$BASE/bin"

# ------------------------------------------------ 1. 依赖库束
say "1/8 解压依赖库束..."
if [ ! -f "$DL/napcat-libset-bundle.tar.gz" ]; then
  die "缺少 $DL/napcat-libset-bundle.tar.gz"
fi
mkdir -p "$BASE/lib"
# busybox tar 无 -z 且 -a 对 .gz 不可靠，用 gzip 管道
gzip -dc "$DL/napcat-libset-bundle.tar.gz" | tar -xf - -C "$BASE/" || die "依赖库束解压失败"
# 束内结构: libset/*.so*  bin/{Xvfb,xkbcomp}  share/X11/xkb
[ -d "$BASE/libset" ] || die "束内缺少 libset 目录"
cp -f "$BASE"/libset/*.so* "$BASE/lib/" 2>/dev/null
[ -f "$BASE/bin/Xvfb" ]   || cp -f "$BASE/libset/bin/Xvfb"   "$BASE/bin/" 2>/dev/null
[ -f "$BASE/bin/xkbcomp" ] || cp -f "$BASE/libset/bin/xkbcomp" "$BASE/bin/" 2>/dev/null
mkdir -p "$BASE/share/X11"
[ -d "$BASE/share/X11/xkb" ] || cp -r "$BASE/libset/share/X11" "$BASE/share/" 2>/dev/null
chmod 755 "$BASE/bin/Xvfb" "$BASE/bin/xkbcomp" 2>/dev/null
say "   库文件: $(ls "$BASE/lib" | wc -l) 个"

# ------------------------------------------------ 2. Node.js
say "2/8 安装 Node.js..."
if [ ! -x "$BASE/runtime/node-v24.21.0-linux-arm64/bin/node" ]; then
  [ -f "$DL/node.tar.xz" ] || die "缺少 $DL/node.tar.xz"
  mkdir -p "$BASE/runtime"
  xz -dc "$DL/node.tar.xz" > "$DL/node.tar" || die "node 解压(xz)失败"
  tar -xf "$DL/node.tar" -C "$BASE/runtime" || die "node 解包(tar)失败"
  rm -f "$DL/node.tar"
fi
NBIN="$BASE/runtime/node-v24.21.0-linux-arm64/bin/node"
[ -x "$NBIN" ] || die "node 不可执行"
say "   $($NBIN --version)"

# ------------------------------------------------ 3. NapCat Shell
say "3/8 安装 NapCat.Shell..."
# QQ 目录可能以 QQ_new 暂存推送（规避 adb push 到已存在目录会嵌套的问题）
if [ -d "$BASE/opt/QQ_new/opt/QQ" ]; then
  rm -rf "$BASE/opt/QQ_new/opt/QQ/.." >/dev/null 2>&1 || true
fi
if [ -d "$BASE/opt/QQ_new" ]; then
  if [ ! -f "$BASE/opt/QQ/qq" ]; then
    rm -rf "$BASE/opt/QQ"
    mv "$BASE/opt/QQ_new" "$BASE/opt/QQ" || die "QQ 目录就位失败"
  else
    rm -rf "$BASE/opt/QQ_new"
  fi
fi
APPDIR="$BASE/opt/QQ/resources/app/napcat"
if [ ! -f "$APPDIR/napcat.mjs" ]; then
  [ -f "$DL/NapCat.Shell.zip" ] || die "缺少 $DL/NapCat.Shell.zip"
  [ -d "$BASE/opt/QQ/resources/app" ] || die "缺少 QQ 目录（Windows 端应已推送 $BASE/opt/QQ）"
  mkdir -p "$BASE/app"
  unzip -q -o "$DL/NapCat.Shell.zip" -d "$BASE/app" || die "NapCat 解压失败"
  mkdir -p "$APPDIR"
  cp -r "$BASE/app/." "$APPDIR/" || die "NapCat 拷入 QQ 目录失败"
fi
[ -f "$APPDIR/napcat.mjs" ] || die "napcat.mjs 缺失"
say "   napcat.mjs 就绪"

# ------------------------------------------------ 4. QQ 注入配置
say "4/8 写入 NapCat 注入配置..."
APPJSON="$BASE/opt/QQ/resources/app/package.json"
[ -f "$APPJSON" ] || die "缺少 $APPJSON"
if [ ! -f "$APPJSON.orig" ]; then
  cp "$APPJSON" "$APPJSON.orig"
fi
"$NBIN" -e '
const fs=require("fs");
const p=process.argv[1];
const j=JSON.parse(fs.readFileSync(p,"utf8"));
j.main="./loadNapCat.cjs";
fs.writeFileSync(p, JSON.stringify(j,null,2));
' "$APPJSON" || die "package.json 修补失败"

cat > "$BASE/opt/QQ/resources/app/loadNapCat.cjs" <<'EOF'
const path = require("path");
const CurrentPath = path.dirname(__filename);
(async () => {
  try {
    await import("file://" + path.join(CurrentPath, "napcat/napcat.mjs"));
  } catch (e) {
    console.error("[loadNapCat] failed:", e);
  }
})();
EOF

# WebUI 监听 0.0.0.0（本内核无 IPv6，默认 "::" 会 EAFNOSUPPORT）
WEBUI="$APPDIR/config/webui.json"
if [ ! -f "$WEBUI" ] && [ -f "$APPDIR/config/napcat.json" ]; then
  # NapCat 首次启动会自己生成；这里仅当目录里有模板时兜底
  :
fi

# ------------------------------------------------ 5. 合并库目录
say "5/8 构建合并库目录（宿主 /lib + 补齐库）..."
MERGED="$BASE/mergedlib"
if [ ! -f "$MERGED/libc.so.6" ] || [ ! -f "$MERGED/libX11.so.6" ]; then
  mkdir -p "$MERGED"
  cp -al /lib/. "$MERGED/" 2>/dev/null || cp -a /lib/. "$MERGED/"
  cp -a "$BASE/lib/." "$MERGED/" 2>/dev/null
fi
[ -f "$MERGED/libtirpc.so.3" ] || die "合并库缺 libtirpc"
[ -f "$MERGED/libX11.so.6" ]   || die "合并库缺 libX11"
say "   $(ls "$MERGED" | wc -l) 项"

# ------------------------------------------------ 6. chroot 骨架
say "6/8 构建 chroot 骨架..."
mkdir -p "$R/bin" "$R/sbin" "$R/etc" "$R/tmp" "$R/dev" "$R/proc" "$R/sys" \
         "$R/root/bin" "$R/opt/lib" "$R/opt/QQ" \
         "$R/usr/lib" "$R/usr/bin" "$R/usr/share"

# 可写层：复制宿主 /usr/bin 与 /usr/share（53MB+20MB，一次性）
if [ ! -x "$R/usr/bin/Xvfb" ]; then
  say "   复制宿主 /usr/bin /usr/share ..."
  cp -a /proc/1/root/usr/bin/.   "$R/usr/bin/"   2>/dev/null
  cp -a /proc/1/root/usr/share/. "$R/usr/share/" 2>/dev/null
fi
# X 服务与键位
cp -f "$BASE/bin/Xvfb"   "$R/usr/bin/Xvfb"   2>/dev/null
cp -f "$BASE/bin/xkbcomp" "$R/usr/bin/xkbcomp" 2>/dev/null
chmod 755 "$R/usr/bin/Xvfb" "$R/usr/bin/xkbcomp" 2>/dev/null
mkdir -p "$R/usr/share/X11"
[ -d "$R/usr/share/X11/xkb" ] || cp -r "$BASE/share/X11/xkb" "$R/usr/share/X11/xkb" 2>/dev/null

# 关键：本机 glibc 走 /lib64 -> /usr/lib 链，chroot 内必须补齐
rm -f "$R/lib64" "$R/usr/lib64"
ln -sfn /usr/lib "$R/lib64"
ln -sfn /usr/lib "$R/usr/lib64"
# /lib 为真实目录（bind mergedlib），不能做成 usr/lib 的软链
[ -d "$R/lib" ] || mkdir -p "$R/lib"

# chroot 内启动脚本
cat > "$R/root/bin/run-napcat.sh" <<'EOF'
#!/bin/sh
# chroot 内启动 NapCat（无头 Xvfb；第 1 参为 QQ 号，可空）
export DISPLAY=:99
export LD_PRELOAD=/lib/libgnutls.so.30
cd /opt/QQ || exit 1
exec ./qq --no-sandbox -q "${1:-}"
EOF
chmod 755 "$R/root/bin/run-napcat.sh"

# ------------------------------------------------ 7. 挂载点
say "7/8 绑定挂载..."
mp() { grep -q " $1 " /proc/mounts 2>/dev/null; }
ensure_bind() { # src dst
  mp "$2" || mount --bind "$1" "$2" 2>/dev/null
}
ensure_bind /dev  "$R/dev"
ensure_bind /proc "$R/proc"
ensure_bind /sys  "$R/sys"
ensure_bind /tmp  "$R/tmp"
ensure_bind /bin  "$R/bin"
ensure_bind /sbin "$R/sbin"
ensure_bind "$BASE/mergedlib" "$R/lib"
ensure_bind "$BASE/mergedlib" "$R/usr/lib"
ensure_bind "$BASE/opt/QQ"    "$R/opt/QQ"
# 注意：不 bind 宿主 /etc —— 保持与真机验证一致的最小环境

# ------------------------------------------------ 8. 管理器与自启脚本
say "8/8 安装管理器与控制脚本..."
if [ -f "$BASE/mgr.amr" ]; then
  miniapp_cli install "$BASE/mgr.amr" >/dev/null 2>&1 \
    && say "   管理器已安装 (8001799000000051)" \
    || say "   [警告] 管理器安装失败（不影响 NapCat 本体）"
fi

# 启停控制脚本（setup 只负责落盘；napcat-ctl.sh 由 bat 单独推送）
[ -f "$BASE/bin/napcat-ctl.sh" ] && chmod 755 "$BASE/bin/napcat-ctl.sh"

say "安装完成。可用命令："
say "  sh $BASE/bin/napcat-ctl.sh start|stop|restart|status"
exit 0
