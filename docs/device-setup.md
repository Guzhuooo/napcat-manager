# 设备侧环境搭建（在词典笔上跑 NapCat）

本文记录**如何在一台干净的有道词典笔上**把 NapCat 环境搭起来，
以及如何**完整回滚**。

> 所有改动都落在 `/userdisk/napcat/`，宿主系统零修改。

## 目标设备事实（真机实测）

| 项 | 值 |
|---|---|
| SoC | Rockchip RK3562（4×Cortex-A53，aarch64） |
| 内核 | Linux 5.10.160 |
| 发行版 | Buildroot 2021.11 + busybox |
| libc | glibc 2.36（`/lib -> usr/lib`，`/lib64 -> lib`） |
| 根文件系统 | `/` = erofs **只读**（无法写入，`remount,rw` 也无效） |
| 可写分区 | `/userdata`(~247M)、`/userdisk`(~51G) |
| 屏幕 | 逻辑 936×280，direction 270，触控 `sitronix_ts_spi`（event4） |
| Node.js | **系统没有**（必须自备） |

## 为什么需要 chroot

NapCat 的 `wrapper.node` 需要 `qq_magic_napi_register` 符号，
该符号**只由 QQ 主二进制导出**，PIE 无法 `LD_PRELOAD`/`dlopen`。
因此必须让 `qq` 进程加载它；而 `qq` 是 Electron，需要显示环境（Xvfb）。

`/usr` 只读 → Xvfb 无法放到 `/usr/bin/xkbcomp`（它硬编码此路径）
→ 用 chroot 提供一个**可写的完整根环境**，与宿主完全隔离。

## 目录布局

```
/userdisk/napcat/
├── rootfs/            chroot 根（可写层，含 Xvfb/xkbcomp/xkb 数据）
│   └── opt/QQ/        QQ + NapCat（bind 自 /userdisk/napcat/opt/QQ）
├── opt/QQ/            QQ 3.2.32 arm64（官方 deb 解出）
│   └── resources/app/
│       ├── napcat/    NapCat.Shell
│       └── loadNapCat.cjs
├── runtime/node-*/    官方 Node v24.21.0 linux-arm64
├── lib/               补齐的 227 个 aarch64 运行库
├── mergedlib/         宿主 /lib + 补丁库的合并视图（供 chroot 用）
├── bin/               启停脚本 + Xvfb + xkbcomp
├── config/            管理器配置 manager.json
├── logs/              运行日志
└── state/             PID、二维码缓存
```

## 关键库依赖（系统缺失的部分）

X11 与 GTK 栈在 Buildroot 上**完全不存在**，必须补齐：

- **X11**：`libX11 / libX11-xcb / libXext / libxcb / libXau / libXdmcp`
- **X 扩展**：`libXcursor / libXi / libXrandr / libXfixes / libXcomposite / libXdamage / libXinerama / libXrender / libXtst`
- **GTK/Pango**：`libgtk-3 / libgdk_pixbuf / libcairo / libpango / libpangocairo / libpangoft2 / libharfbuzz / libpixman / libepoxy / libdrm / libgbm`
- **NSS/加密**：`libnss3 / libnspr4 / libsmime3 / libnssutil3`、`libgssapi_krb5 / libkrb5 / libk5crypto / libkrb5support`、`libgcrypt / libgpg-error`、`libbsd / libmd`
- **字体/编解码**：`libfreetype / libfontconfig / libfribidi / libgraphite2 / libdatrie / libthai`、`libjpeg / libtiff / liblcms2`
- **其他**：`libsystemd0 / libcap2 / libselinux1 / libaudit1 / libunwind.so.1 / libxkbfile`

来源：Debian bookworm arm64（glibc 2.36，与设备一致）。

## 搭建步骤

### 1. Node.js

```sh
cd /userdisk/napcat/dl
curl -L -o node.tar.xz https://nodejs.org/dist/v24.21.0/node-v24.21.0-linux-arm64.tar.xz
xz -dc node.tar.xz > node.tar          # busybox tar 不支持 xz，先解压
mkdir -p /userdisk/napcat/runtime && tar -xf node.tar -C /userdisk/napcat/runtime
```

### 2. QQ（arm64）

腾讯官方有 Linux ARM64 版（`im.qq.com/linuxqq` 的「ARM 版下载」）。
官网 CDN 可能 403，用 GitHub 镜像：

```sh
curl -L -o QQ.deb https://github.com/Rodert/qq-versions/releases/download/qq-packages-20260813-1d08f1d4/QQ_3.2.32_260812_arm64_01.deb
```

`.deb` 用 `ar x` + `tar -xf data.tar.xz` 解到 `/userdisk/napcat/opt/QQ`。

### 3. NapCat.Shell

```sh
curl -L -o napcat.zip https://github.com/NapNeko/NapCatQQ/releases/download/v4.18.29/NapCat.Shell.zip
unzip -q napcat.zip -d /userdisk/napcat/app
cp -r /userdisk/napcat/app/* /userdisk/napcat/opt/QQ/resources/app/napcat/
```

### 4. chroot 根

```sh
R=/userdisk/napcat/rootfs
mkdir -p $R/{lib,usr/lib,usr/bin,bin,sbin,etc,tmp,dev,proc,sys,opt/lib,opt/QQ}
# 只读复用宿主库（可写副本用于 /usr/bin）
cp -a /proc/1/root/usr/bin/. $R/usr/bin/
cp -a /proc/1/root/usr/share/. $R/usr/share/
cp -r /userdisk/napcat/share/X11/xkb $R/usr/share/X11/xkb
cp /userdisk/napcat/bin/{Xvfb,xkbcomp} $R/usr/bin/
# 关键：lib64 链路
ln -sfn /usr/lib $R/lib64 && ln -sfn /usr/lib $R/usr/lib64
mount --bind /userdisk/napcat/mergedlib $R/lib
mount --bind /userdisk/napcat/mergedlib $R/usr/lib
mount --bind /userdisk/napcat/opt/QQ  $R/opt/QQ
mount --bind /dev $R/dev; mount --bind /proc $R/proc; mount --bind /sys $R/sys
```

### 5. 启动

```sh
# Xvfb（虚拟显示）
env -u LD_LIBRARY_PATH chroot $R /bin/sh -c \
  'exec /usr/bin/Xvfb :99 -screen 0 1280x800x24 -nolisten tcp'

# NapCat
env -u LD_LIBRARY_PATH DISPLAY=:99 chroot $R /bin/sh -c \
  'cd /opt/QQ && exec ./qq --no-sandbox'
```

## 踩过的坑（重要）

1. **`ps -ef` 是三列**（`PID USER TIME CMD`），没有 UID 列 —— 按四列解析会拿到用户名。
2. **`/lib` 是符号链接**指向 `usr/lib`，而 glibc 加载器走 `/lib64` 链 —— chroot 里必须补 `lib64 -> /usr/lib`。
3. **WebUI 默认 `host: "::"`（IPv6）**，本内核没编 IPv6 → `EAFNOSUPPORT`，必须改 `0.0.0.0`。
4. **`libbugly.so` 依赖 GnuTLS 符号但不声明 NEEDED** → 需 `LD_PRELOAD=/lib/libgnutls.so.30`。
5. **宿主 `LD_LIBRARY_PATH` 会污染 chroot**（含 `/oem/YoudaoDictPen/...`）→ 必须 `env -u LD_LIBRARY_PATH`。
6. **busybox `ar` 只能列出不能解包** → `.deb` 需在开发机解或另装 binutils。
7. **OneBot 配置按账号隔离**：`onebot11_<QQ>.json` 每号一份。换账号扫码后
   NapCat 为新号生成默认配置（全关），旧配置不生效 —— 管理器必须定位
   「当前活跃账号」（进程 `-q` 参数优先，其次最新文件）。
8. **QQ 快速登录凭据会过期**（腾讯安全策略），提示"用户身份已失效"时
   需重新扫码；管理器持久化当前 QQ 号，重启自动 `-q` 快速登录，
   失效时由 NapCat 回落二维码。
9. **chroot 内路径不能用宿主绝对路径**：`chroot $R /bin/sh /userdisk/...`
   会报 can't open —— 必须用 chroot 内路径 `/root/bin/...`。

## 完整回滚

```sh
# 1. 停止 NapCat
sh /userdisk/napcat/bin/stop-napcat.sh

# 2. 卸载 chroot 的 bind mount
for m in $(mount | grep 'napcat/rootfs' | awk '{print $3}' | sort -r); do
  umount "$m"
done

# 3. 删除整个目录（宿主系统从未被修改，无需还原）
rm -rf /userdisk/napcat
```

**回滚后宿主系统与出厂状态一致** —— 因为全程只写了 `/userdisk/napcat/`，
`/`（erofs）从未被写入。

## 卸载管理器 miniapp

```sh
adb shell miniapp_cli uninstall 8001799000000051
```
