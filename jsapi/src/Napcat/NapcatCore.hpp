// NapCat 控制核心（与 QuickJS 解耦，便于单独测试）
//
// 设计原则：
//   1. 只操作 /userdisk/napcat/ 下资源
//   2. 进程识别严格按「命令行包含 /userdisk/napcat」判定，
//      绝不误伤系统 miniapp / guardian_run / DictPen
//   3. 启停幂等：重复调用不产生副作用
//   4. 所有 shell 调用使用固定字符串或经严格校验的参数，杜绝注入
#pragma once

#include <jqutil_v2/jqbson.h>
#include <string>

using namespace JQUTIL_NS;

namespace napcat {

class NapcatCore
{
public:
    // ---- 查询 ----
    static Bson queryStatus();       // running/pid/webuiPort/onebotPort/ip/account
    static Bson queryDeviceInfo();   // ip/model/mem/disk

    // ---- 控制 ----
    static Bson start();             // 幂等；返回启动后的状态
    static Bson stop();              // 幂等；返回停止后的状态
    static Bson quickLogin(const std::string &qq);  // 带 QQ 号重启以快速登录

    // ---- 日志 ----
    static std::string readLogTail(int lines);
    static void clearLogs();

    // ---- 配置 ----
    static Bson readConfig();
    static void writeConfig(const Bson &patch);

    // ---- OneBot 网络配置 ----
    // 直接改写 NapCat 的 onebot11_<account>.json，让 OneBot 服务可开关。
    // 会先备份原文件；写入后需重启 NapCat 生效。
    static Bson setOnebot(bool enable, int port);

    // ---- 登录 ----
    static std::string readQrcodeBase64();

    // ---- OneBot 探测 ----
    static Bson pingOneBot();

    // ---- 路径常量 ----
    static const char *baseDir();
    static const char *logFile();
    static const char *configFile();
    static const char *stateDir();
    static const char *rootfsDir();

    // 执行命令并取 stdout（命令均为内部常量或经校验的整数参数）
    static std::string exec(const std::string &cmd);

private:
    // 在 napcat 进程集合里找主进程 PID；找不到返回 -1
    static int findMainPid();

    // 读取 config，若不存在返回默认值（含 schema 版本与损坏兜底）
    static Bson loadConfigOrDefault();

    // 校验并落盘
    static void saveConfig(const Bson &cfg);
};

}  // namespace napcat
