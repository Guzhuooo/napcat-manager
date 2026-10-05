# NapCat 管理器

有道词典笔（Rockchip RK3562 / aarch64）上的 **NapCat 管理器** — 一个 Falcon miniapp，
通过自建 native JSAPI 启停、监控并配置设备上的 NapCat。

## 这是什么

已在真机验证：词典笔上跑通了 **NapCat Shell（无头模式）+ OneBot + WebUI**，
可正常登录 QQ 并收发群消息。本项目提供**设备端图形化管理界面**，
不必再靠 adb 命令行操作。

## 架构

```
┌─────────────────────────────────────────────┐
│  Falcon miniapp (Vue 3)                      │
│    ui/src/pages/index/index.vue              │
│         ↓ services/napcat.js  (适配层)        │
│  libjsapi_napcat.so  (native, aarch64)       │
│         ↓ popen / 文件读写                    │
├─────────────────────────────────────────────┤
│  /userdisk/napcat/                           │
│    bin/      启停脚本                         │
│    rootfs/   chroot（QQ + NapCat）            │
│    lib/      补齐的 aarch64 运行库             │
│    logs/     运行日志                         │
│    state/    PID / 配置                       │
└─────────────────────────────────────────────┘
```

## 关键约束

本项目严格遵守设备安全边界：

- **只操作 `/userdisk/napcat/`**，不碰只读根文件系统（erofs）
- **绝不触碰系统进程**：miniapp / guardian_run / DictPen 等厂商进程
- 进程识别按「命令行含 `qq --no-sandbox`」精确匹配，不会误伤
- 所有外部输入（端口、QQ 号、日志行数）做范围与格式校验，杜绝命令注入
- native 全部异步（Promise），阻塞 I/O 不占用 QuickJS 线程

## 构建

> **native 的 C/C++ 一律在 GitHub Actions 上交叉编译，本地不编译。**

推送 `jsapi/**` 的改动会触发 `.github/workflows/build-jsapi.yml`：

1. 安装 `gcc-aarch64-linux-gnu` 交叉工具链
2. CMake 配置 + Ninja 构建
3. 校验产物是 aarch64 ELF、导出 `custom_init_jsapis`、GLIBC 符号 ≤ 2.36
4. 上传 `libjsapi_napcat.so` 作为 artifact

取回产物：

```bash
gh run download --repo Guzhuooo/napcat-manager \
  --name libjsapi_napcat-aarch64 --dir ./dist
cp dist/libs/libjsapi_napcat.so ui/libs/
```

前端打包（本机仅此一步，不涉及 C/C++）：

```bash
pnpm -C ui package     # 产出 ui/8001799000000051.1_0_0.amr
```

## 部署

```bash
adb push ui/8001799000000051.1_0_0.amr /tmp/mgr.amr
adb shell miniapp_cli install /tmp/mgr.amr
adb shell miniapp_cli start 8001799000000051 index
```

## 设备侧依赖

管理器本身很轻，但要让 NapCat 跑起来，设备上需要
`/userdisk/napcat/` 这套环境（chroot + xvfb + QQ arm64 + NapCat）。
详见 `docs/device-setup.md`。

## 目录

```
jsapi/                      native 扩展（CI 编译）
  src/JSAPI.cpp             模块注册入口
  src/Napcat/
    NapcatCore.{hpp,cpp}    进程/文件/网络逻辑（与 QuickJS 解耦）
    JSNapcatModule.{hpp,cpp} QuickJS 绑定层
  iot-miniapp-sdk/          阿里 IoT miniapp SDK
ui/                         前端
  src/app.js                生命周期 + setViewPort(936)
  src/base-page.js          资源回收基类（timer/订阅/generation）
  src/services/napcat.js    native 适配层
  src/pages/index/index.vue 主页面
  libs/                     放置 CI 产出的 .so
.github/workflows/          交叉编译工作流
```

## 已验证的设备事实

| 项 | 值 |
|---|---|
| SoC | Rockchip RK3562（4×Cortex-A53） |
| 架构 | aarch64 / glibc 2.36 / libstdc++ 6.0.29 |
| 内存 | 992MB + 1GB swap |
| 屏幕 | 逻辑 936×280（direction 270） |
| 触控 | sitronix_ts_spi |
| Node | v24.21.0（自备，设备原生无 node） |
| NapCat | Shell 模式 + Xvfb 虚拟屏，已登录并收发消息 |

## 许可

仅供学习研究。
