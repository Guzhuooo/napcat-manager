// NapCat 管理器 native 模块
//
// 安全边界（硬性约束）：
//   - 只操作 /userdisk/napcat/ 目录内的文件与进程
//   - 绝不触碰系统 miniapp / guardian_run / DictPen 等厂商进程
//   - 所有外部输入（端口、开关、日志行数、QQ 号）都做范围/枚举/长度校验
//   - 阻塞 I/O 一律走 Promise（JQAsyncInfo），不占用 JS 线程
#pragma once

#include <jqutil_v2/jqutil.h>
#include <memory>
#include <mutex>
#include <string>

using namespace JQUTIL_NS;

class JSNapcatModule : public JQPublishObject
{
public:
    JSNapcatModule();
    ~JSNapcatModule();

    // ---- 状态查询 ----
    void getVersion(JQFunctionInfo &info);   // -> "1.0.0"
    void getStatus(JQAsyncInfo &info);       // -> {running,pid,webuiPort,onebotPort,ip,uptime}
    void getDeviceInfo(JQAsyncInfo &info);   // -> {ip,model,memTotal,memFree,diskFree}

    // ---- 进程控制 ----
    void start(JQAsyncInfo &info);           // 幂等启动；已在跑则直接返回当前状态
    void stop(JQAsyncInfo &info);            // 幂等停止；只停 napcat 自己的进程树

    // ---- 日志 ----
    void getLogs(JQAsyncInfo &info);         // (tailLines) -> string
    void clearLogs(JQAsyncInfo &info);       // -> ok

    // ---- 配置 ----
    void readConfig(JQAsyncInfo &info);      // -> object
    void writeConfig(JQAsyncInfo &info);     // (object) -> ok

    // ---- 登录 ----
    void getQrcode(JQAsyncInfo &info);       // -> base64 png（无则空串）
    void quickLogin(JQAsyncInfo &info);      // (qq号) -> 触发快速登录

    // ---- OneBot 测试 ----
    void pingOneBot(JQAsyncInfo &info);      // -> {ok, latencyMs, status}

private:
    // 校验辅助
    static int clampInt(int v, int lo, int hi);

    std::mutex mutex_;
};

extern JSValue createNapcatModule(JQModuleEnv *env);
