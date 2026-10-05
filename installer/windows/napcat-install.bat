# ============================================================================
#  NapCat 一键安装（有道词典笔 / RK3562 aarch64）
#
#  功能：在一台全新联网的词典笔上，从零装好
#    Node.js + QQ(arm64) + NapCat.Shell + chroot 环境 + 管理器 miniapp
#    并启动，最后打印 WebUI 地址。
#
#  用法：
#    1. 笔开机、连上 WiFi（与电脑同一局域网）、USB 连接电脑并打开 ADB
#    2. 双击运行本脚本（或 cmd 里执行 napcat-install.bat）
#    3. 全程无需人工干预；结束后用手机 QQ 扫设备屏幕上的二维码登录
#
#  依赖（本脚本自动探测）：
#    - adb.exe      默认找 F:\mytool\adb.exe，可用 set ADB=... 覆盖
#    - curl.exe     Windows 10+ 自带
#    - tar.exe      Windows 10+ 自带
#    - gh.exe       可选：用于从私有仓库下载 libset 束与管理器 AMR
#                     （若本地 bundle\ 目录已有这些文件则不需要 gh）
#
#  安全边界：只写设备的 /userdisk/napcat/，不碰系统分区与 miniapp。
# ============================================================================
@echo off
setlocal enabledelayedexpansion
chcp 65001 >nul
title NapCat 一键安装 - 有道词典笔

set "SCRIPT_DIR=%~dp0"
set "WORK=%SCRIPT_DIR%napcat-work"
set "BUNDLE_DIR=%SCRIPT_DIR%bundle"
set "ADB=F:\mytool\adb.exe"
if defined ADB_SET (set "ADB=%ADB_SET%")
if not exist "%ADB%" (set "ADB=adb")

rem ---------- 下载源 ----------
set "NODE_VER=24.21.0"
set "NODE_URL=https://nodejs.org/dist/v%NODE_VER%/node-v%NODE_VER%-linux-arm64.tar.xz"
set "QQ_URL=https://github.com/Rodert/qq-versions/releases/download/qq-packages-20260813-1d08f1d4/QQ_3.2.32_260812_arm64_01.deb"
set "NAPCAT_URL=https://github.com/NapNeko/NapCatQQ/releases/download/v4.18.29/NapCat.Shell.zip"
set "REPO=Guzhuooo/napcat-manager"
set "LIBSET_NAME=napcat-libset-bundle.tar.gz"
set "AMR_NAME=8001799000000051.1_0_0.amr"

echo ============================================================
echo   NapCat 一键安装 - 有道词典笔 (RK3562 / aarch64)
echo ============================================================

rem ============================================================
rem  步骤 0：环境自检
rem ============================================================
echo [0/7] 环境自检...

where curl.exe >nul 2>nul || (echo   [错误] 找不到 curl.exe，需要 Windows 10+ & goto :fail)
where tar.exe  >nul 2>nul || (echo   [错误] 找不到 tar.exe，需要 Windows 10+ & goto :fail)

"%ADB%" version >nul 2>nul
if errorlevel 1 (
    echo   [错误] 找不到 adb。请确认 F:\mytool\adb.exe 存在，或 set ADB_SET=adb路径 后重试
    goto :fail
)

"%ADB%" devices | findstr /r /c:"device$" >nul
if errorlevel 1 (
    echo   [错误] 未检测到设备。请确认：笔已开机、USB 已连接、ADB 已打开
    echo   当前 adb devices 输出：
    "%ADB%" devices
    goto :fail
)
echo   adb: %ADB%
echo   设备已连接

rem ============================================================
rem  步骤 1：准备下载目录
rem ============================================================
echo [1/7] 准备工作目录 %WORK%
if not exist "%WORK%" mkdir "%WORK%"
cd /d "%WORK%"

rem ============================================================
rem  步骤 2：下载大件（node / QQ / NapCat）
rem ============================================================
set "NEED_DL=0"
if not exist "node.tar.xz"  set NEED_DL=1
if not exist "QQ.deb"       set NEED_DL=1
if not exist "NapCat.Shell.zip" set NEED_DL=1
if "%NEED_DL%"=="1" echo [2/7] 下载运行时与大件（约 270MB，视网速 3~10 分钟）...

if not exist "node.tar.xz" (
    echo   下载 Node.js v%NODE_VER% arm64 ...
    curl.exe -L --retry 3 --fail -o node.tar.xz "%NODE_URL%" || (echo   [错误] node 下载失败 & goto :fail)
)
if not exist "QQ.deb" (
    echo   下载 QQ arm64（约 205MB）...
    curl.exe -L --retry 3 --fail -o QQ.deb "%QQ_URL%" || (echo   [错误] QQ 下载失败 & goto :fail)
)
if not exist "NapCat.Shell.zip" (
    echo   下载 NapCat.Shell ...
    curl.exe -L --retry 3 --fail -o NapCat.Shell.zip "%NAPCAT_URL%" || (echo   [错误] NapCat 下载失败 & goto :fail)
)
for %%F in (node.tar.xz QQ.deb NapCat.Shell.zip) do (
    set /a SZ=%%~zF/1048576 2>nul
    echo   已就绪: %%F (!SZ! MB)
)

rem ============================================================
rem  步骤 3：下载 libset 束与管理器 AMR
rem ============================================================
echo [3/7] 获取依赖库束与管理器...
set "HAVE_LIBSET=0"
if exist "%BUNDLE_DIR%\%LIBSET_NAME%" (
    copy /y "%BUNDLE_DIR%\%LIBSET_NAME%" . >nul && set HAVE_LIBSET=1
    echo   使用本地 bundle 中的 %LIBSET_NAME%
)
if exist "%BUNDLE_DIR%\%AMR_NAME%" (
    copy /y "%BUNDLE_DIR%\%AMR_NAME%" . >nul
    echo   使用本地 bundle 中的 %AMR_NAME%
)

if "%HAVE_LIBSET%"=="0" (
    where gh.exe >nul 2>nul
    if errorlevel 1 (
        echo   [提示] 本地无 bundle\%LIBSET_NAME% 且未找到 gh.exe
        echo   [提示] libset 束存于私有仓库 %REPO% 的 Release v1.0.0，
        echo          请手动下载后放到 %BUNDLE_DIR%\ 再重试。
        goto :fail
    )
    echo   通过 gh 从 %REPO% Release 下载...
    gh.exe release download v1.0.0 --repo %REPO% --pattern "%LIBSET_NAME%" --clobber --output-dir . || (echo   [错误] libset 下载失败 & goto :fail)
    gh.exe release download v1.0.0 --repo %REPO% --pattern "%AMR_NAME%"  --clobber --output-dir . 2>nul
    if not exist "%LIBSET_NAME%" (echo   [错误] libset 仍未就绪 & goto :fail)
)

rem ============================================================
rem  步骤 4：解包 QQ（Windows 自带 tar 可直接解 .deb）
rem ============================================================
echo [4/7] 解包 QQ ...
if not exist "qqroot\opt\QQ\qq" (
    if exist "qqroot" rmdir /s /q qqroot
    mkdir qqroot
    tar.exe -xf QQ.deb || (echo   [错误] 解 deb 失败 & goto :fail)
    tar.exe -xf data.tar.xz -C qqroot || (echo   [错误] 解 data.tar.xz 失败 & goto :fail)
    del /f /q control.tar.xz data.tar.xz debian-binary 2>nul
)
if not exist "qqroot\opt\QQ\qq" (echo   [错误] 未找到 qqroot\opt\QQ\qq & goto :fail)
echo   QQ 解包完成

rem ============================================================
rem  步骤 5：推送全部文件到设备
rem ============================================================
echo [5/7] 推送文件到设备（约 800MB，USB 下 3~5 分钟）...
set "DEV=/userdisk/napcat"

"%ADB%" shell "mkdir -p %DEV%/dl %DEV%/lib %DEV%/bin %DEV%/opt %DEV%/config %DEV%/logs %DEV%/state %DEV%/share" >nul

echo   推送依赖库束...
"%ADB%" push "%LIBSET_NAME%" %DEV%/dl/ >nul || (echo   [错误] libset 推送失败 & goto :fail)

echo   推送 node 与 NapCat...
"%ADB%" push node.tar.xz %DEV%/dl/ >nul || (echo   [错误] node 推送失败 & goto :fail)
"%ADB%" push NapCat.Shell.zip %DEV%/dl/ >nul || (echo   [错误] NapCat 推送失败 & goto :fail)

echo   推送 QQ（最大件）...
"%ADB%" shell "rm -rf %DEV%/opt/QQ_new" >nul
"%ADB%" push "qqroot\opt\QQ" %DEV%/opt/QQ_new >nul || (echo   [错误] QQ 推送失败 & goto :fail)

echo   推送控制脚本...
"%ADB%" push "%SCRIPT_DIR%napcat-ctl.sh"    %DEV%/bin/napcat-ctl.sh >nul
"%ADB%" push "%SCRIPT_DIR%start-napcat.sh"  %DEV%/bin/start-napcat.sh >nul

echo   推送管理器 AMR...
if exist "%AMR_NAME%" "%ADB%" push "%AMR_NAME%" %DEV%/mgr.amr >nul

echo   推送安装脚本...
"%ADB%" push "%SCRIPT_DIR%setup-device.sh" %DEV%/bin/setup-device.sh >nul || (echo   [错误] setup-device.sh 推送失败 & goto :fail)

rem ============================================================
rem  步骤 6：设备端安装（解包/建 chroot/配置/装管理器）
rem ============================================================
echo [6/7] 设备端安装中（解包约 3~6 分钟，请勿断开）...
"%ADB%" shell "sh %DEV%/bin/setup-device.sh 2>&1"
if errorlevel 1 (echo   [错误] 设备端安装失败，见上方输出 & goto :fail)

rem ============================================================
rem  步骤 7：启动并打印连接信息
rem ============================================================
echo [7/7] 启动 NapCat ...
"%ADB%" shell "sh %DEV%/bin/napcat-ctl.sh start 2>&1" 
"%ADB%" shell "sleep 6; sh %DEV%/bin/napcat-ctl.sh status 2>&1"

set "IP="
for /f "delims=" %%a in ('"%ADB%" shell "sh %DEV%/bin/napcat-ctl.sh ip 2>/dev/null" 2^>nul') do set "RAWIP=%%a"
if defined RAWIP (
    rem adb 输出行尾带 CR，去掉最后一个字符
    set "IP=!RAWIP:~0,-1!"
)
if not defined IP set "IP=192.168.0.102"
echo.
echo ============================================================
echo   安装完成！
echo   ------------------------------------------------------
echo   1. 用手机 QQ 扫描笔屏幕上的二维码登录
echo      （或打开管理器 miniapp 点「二维码」唤出）
echo   2. WebUI:    http://%IP%:6099/webui
echo   3. OneBot:   http://%IP%:3000  (HTTP API)
echo               ws://%IP%:3000    (WebSocket)
echo   4. 对接说明见仓库 docs/connect.md
echo ============================================================
goto :eof

:fail
echo.
echo ============================================================
echo   安装失败。请把上面的输出发给开发者或自查：
echo   - 设备 ADB 是否开启、USB 是否稳定
echo   - 网络是否能访问 nodejs.org / github.com / ghproxy
echo   - 磁盘空间：设备 /userdisk 需 2GB 以上
echo ============================================================
exit /b 1
