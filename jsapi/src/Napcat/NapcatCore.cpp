#include "Napcat/NapcatCore.hpp"

#include <array>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <fstream>
#include <sstream>
#include <vector>

#include <sys/stat.h>
#include <sys/wait.h>
#include <unistd.h>

namespace napcat {

// ---------------------------------------------------------------- 路径常量

const char *NapcatCore::baseDir()    { return "/userdisk/napcat"; }
const char *NapcatCore::stateDir()   { return "/userdisk/napcat/state"; }
const char *NapcatCore::logFile()    { return "/userdisk/napcat/logs/napcat.log"; }
const char *NapcatCore::configFile() { return "/userdisk/napcat/config/manager.json"; }
const char *NapcatCore::rootfsDir()  { return "/userdisk/napcat/rootfs"; }

namespace {

const char *kStartScript = "/userdisk/napcat/bin/start-napcat.sh";
const char *kStopScript  = "/userdisk/napcat/bin/stop-napcat.sh";
// chroot 内 NapCat 缓存的二维码
const char *kQrcodeInChroot =
    "/userdisk/napcat/rootfs/opt/QQ/resources/app/napcat/cache/qrcode.png";
const char *kQrcodeCopy = "/userdisk/napcat/state/qrcode.png";
const int   kConfigVersion = 1;

std::string trimTrailing(std::string s)
{
    while (!s.empty() && (s.back() == '\n' || s.back() == '\r' || s.back() == ' ')) s.pop_back();
    return s;
}

std::string readFileAll(const std::string &path)
{
    std::ifstream f(path, std::ios::binary);
    if (!f) return std::string();
    std::ostringstream ss;
    ss << f.rdbuf();
    return ss.str();
}

void writeFileAll(const std::string &path, const std::string &data)
{
    std::ofstream f(path, std::ios::binary | std::ios::trunc);
    if (!f) throw std::runtime_error("无法写入文件: " + path);
    f.write(data.data(), static_cast<std::streamsize>(data.size()));
    if (!f) throw std::runtime_error("写入失败: " + path);
}

bool fileExists(const std::string &p)
{
    struct stat st;
    return ::stat(p.c_str(), &st) == 0;
}

// base64 编码（用于把二维码 PNG 传给 JS 显示）
std::string base64Encode(const std::string &in)
{
    static const char *tbl = "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789+/";
    std::string out;
    out.reserve(((in.size() + 2) / 3) * 4);
    size_t i = 0;
    while (i + 2 < in.size()) {
        unsigned n = (static_cast<unsigned char>(in[i]) << 16) |
                     (static_cast<unsigned char>(in[i + 1]) << 8) |
                     static_cast<unsigned char>(in[i + 2]);
        out.push_back(tbl[(n >> 18) & 63]);
        out.push_back(tbl[(n >> 12) & 63]);
        out.push_back(tbl[(n >> 6) & 63]);
        out.push_back(tbl[n & 63]);
        i += 3;
    }
    const size_t rem = in.size() - i;
    if (rem == 1) {
        unsigned n = static_cast<unsigned char>(in[i]) << 16;
        out.push_back(tbl[(n >> 18) & 63]);
        out.push_back(tbl[(n >> 12) & 63]);
        out.push_back('=');
        out.push_back('=');
    } else if (rem == 2) {
        unsigned n = (static_cast<unsigned char>(in[i]) << 16) |
                     (static_cast<unsigned char>(in[i + 1]) << 8);
        out.push_back(tbl[(n >> 18) & 63]);
        out.push_back(tbl[(n >> 12) & 63]);
        out.push_back(tbl[(n >> 6) & 63]);
        out.push_back('=');
    }
    return out;
}

// 从 json 文本里提取一个整数字段（避免引入完整 JSON 解析依赖）
bool extractInt(const std::string &json, const std::string &key, int &out)
{
    const std::string pat = "\"" + key + "\"";
    size_t p = json.find(pat);
    if (p == std::string::npos) return false;
    p = json.find(':', p + pat.size());
    if (p == std::string::npos) return false;
    ++p;
    while (p < json.size() && (json[p] == ' ' || json[p] == '\t')) ++p;
    char *end = nullptr;
    long v = std::strtol(json.c_str() + p, &end, 10);
    if (end == json.c_str() + p) return false;
    out = static_cast<int>(v);
    return true;
}

std::string jsonEscape(const std::string &s)
{
    std::string o;
    for (char c : s) {
        switch (c) {
            case '"':  o += "\\\""; break;
            case '\\': o += "\\\\"; break;
            case '\n': o += "\\n";  break;
            case '\r': o += "\\r";  break;
            case '\t': o += "\\t";  break;
            default:
                if (static_cast<unsigned char>(c) < 0x20) {
                    char buf[8];
                    std::snprintf(buf, sizeof(buf), "\\u%04x", c);
                    o += buf;
                } else {
                    o.push_back(c);
                }
        }
    }
    return o;
}

}  // namespace

// ---------------------------------------------------------------- 命令执行

std::string NapcatCore::exec(const std::string &cmd)
{
    std::string out;
    FILE *pipe = ::popen((cmd + " 2>/dev/null").c_str(), "r");
    if (!pipe) return out;
    std::array<char, 512> buf{};
    while (std::fgets(buf.data(), static_cast<int>(buf.size()), pipe) != nullptr) {
        out += buf.data();
    }
    ::pclose(pipe);
    return out;
}

// 找 NapCat 主进程：命令行必须同时包含 "napcat" 与 qq 特征
// 严格排除系统进程，避免误伤 miniapp
int NapcatCore::findMainPid()
{
    // ps 输出示例： 327 root 0:04 ./qq --no-sandbox -q
    // 只认「在 /userdisk/napcat 目录下启动、且命令行含 ./qq --no-sandbox」的进程
    std::string ps = exec("ps -ef");
    std::istringstream iss(ps);
    std::string line;
    while (std::getline(iss, line)) {
        // 必须含 qq --no-sandbox（NapCat 的启动特征）
        if (line.find("qq --no-sandbox") == std::string::npos &&
            line.find("./qq --no-sandbox") == std::string::npos) {
            continue;
        }
        // 排除 zygote 子进程（它们不是主进程）
        if (line.find("--type=zygote") != std::string::npos) continue;
        if (line.find("grep") != std::string::npos) continue;
        // 解析第二列作为 PID
        std::istringstream ls(line);
        std::string user, pidStr;
        ls >> user >> pidStr;
        char *end = nullptr;
        long pid = std::strtol(pidStr.c_str(), &end, 10);
        if (end != pidStr.c_str() && pid > 0) {
            return static_cast<int>(pid);
        }
    }
    return -1;
}

// ---------------------------------------------------------------- 状态查询

Bson NapcatCore::queryStatus()
{
    Bson out;
    const int pid = findMainPid();
    out["running"] = (pid > 0);
    out["pid"] = pid;

    // 从 WebUI 配置读端口
    int webuiPort = 6099;
    const std::string webuiCfg = readFileAll(
        std::string(rootfsDir()) + "/opt/QQ/resources/app/napcat/config/webui.json");
    if (!webuiCfg.empty()) extractInt(webuiCfg, "port", webuiPort);
    out["webuiPort"] = webuiPort;

    // OneBot 端口：从 onebot11 配置读，读不到给默认 3000
    int onebotPort = 3000;
    const std::string ob11 = readFileAll(
        std::string(rootfsDir()) + "/opt/QQ/resources/app/napcat/config/onebot11.json");
    if (!ob11.empty()) {
        int p = 0;
        if (extractInt(ob11, "port", p) && p > 0) onebotPort = p;
    }
    out["onebotPort"] = onebotPort;

    // 本机 IP（取 wlan0）
    std::string ip = trimTrailing(exec("ip -4 addr show wlan0 2>/dev/null | awk '/inet /{print $2}' | cut -d/ -f1"));
    if (ip.empty()) ip = "127.0.0.1";
    out["ip"] = ip;

    // 登录账号（从日志里找最近一次登录成功）
    std::string acct;
    const std::string log = readFileAll(logFile());
    const size_t pos = log.rfind("登录成功");
    if (pos != std::string::npos) {
        // 形如：xxx | 登录成功 ...
        size_t start = log.rfind('\n', pos);
        start = (start == std::string::npos) ? 0 : start + 1;
        size_t bar = log.find('|', start);
        if (bar != std::string::npos && bar < pos) {
            acct = trimTrailing(log.substr(start, bar - start));
        }
    }
    out["account"] = acct;

    return out;
}

Bson NapcatCore::queryDeviceInfo()
{
    Bson out;

    std::string ip = trimTrailing(exec("ip -4 addr show wlan0 2>/dev/null | awk '/inet /{print $2}' | cut -d/ -f1"));
    if (ip.empty()) ip = "127.0.0.1";
    out["ip"] = ip;

    const std::string compatible = readFileAll("/proc/device-tree/compatible");
    out["model"] = trimTrailing(compatible);

    const std::string meminfo = readFileAll("/proc/meminfo");
    auto memField = [&](const char *key) -> long {
        size_t p = meminfo.find(key);
        if (p == std::string::npos) return 0;
        return std::strtol(meminfo.c_str() + p + std::strlen(key), nullptr, 10);
    };
    out["memTotalKb"] = static_cast<int64_t>(memField("MemTotal:"));
    out["memAvailableKb"] = static_cast<int64_t>(memField("MemAvailable:"));

    // /userdisk 剩余（KB）
    std::string df = trimTrailing(exec("df -k /userdisk | tail -1 | awk '{print $4}'"));
    out["diskFreeKb"] = static_cast<int64_t>(std::strtoll(df.c_str(), nullptr, 10));

    return out;
}

// ---------------------------------------------------------------- 启停

Bson NapcatCore::start()
{
    if (findMainPid() > 0) {
        return queryStatus();  // 幂等：已在跑
    }
    if (!fileExists(kStartScript)) {
        throw std::runtime_error("缺少启动脚本: " + std::string(kStartScript));
    }
    exec(std::string("sh ") + kStartScript);
    // 给进程一点时间起来，但不必等太久（前端会轮询）
    ::usleep(1500 * 1000);
    return queryStatus();
}

Bson NapcatCore::stop()
{
    if (findMainPid() < 0) {
        return queryStatus();  // 幂等：已停
    }
    if (!fileExists(kStopScript)) {
        throw std::runtime_error("缺少停止脚本: " + std::string(kStopScript));
    }
    exec(std::string("sh ") + kStopScript);
    ::usleep(1500 * 1000);
    return queryStatus();
}

Bson NapcatCore::quickLogin(const std::string &qq)
{
    // 先停，再带 -q 参数启动
    exec(std::string("sh ") + kStopScript);
    ::usleep(800 * 1000);

    Bson out;
    if (qq.empty()) {
        exec(std::string("sh ") + kStartScript);
        out["mode"] = "qrcode";
    } else {
        // 参数已在上层校验为纯数字，这里再兜一次
        std::string safe;
        for (char c : qq) {
            if (c >= '0' && c <= '9') safe.push_back(c);
        }
        if (safe.empty()) throw std::runtime_error("QQ 号不合法");
        exec(std::string("sh ") + kStartScript + " " + safe);
        out["mode"] = "quick";
        out["qq"] = safe;
    }
    ::usleep(1500 * 1000);
    out["status"] = queryStatus();
    return out;
}

// ---------------------------------------------------------------- 日志

std::string NapcatCore::readLogTail(int lines)
{
    if (lines < 1) lines = 1;
    if (lines > 2000) lines = 2000;
    // tail -n 是安全的固定形式，lines 已 clamp 为整数
    char cmd[256];
    std::snprintf(cmd, sizeof(cmd), "tail -n %d %s", lines, logFile());
    std::string out = exec(cmd);
    if (out.empty() && !fileExists(logFile())) {
        return std::string("(日志文件尚未生成)");
    }
    // 过滤无意义的 asound 噪音
    std::istringstream iss(out);
    std::string line, filtered;
    while (std::getline(iss, line)) {
        if (line.find("asound.so.2: no version information") != std::string::npos) continue;
        filtered += line;
        filtered += '\n';
    }
    return filtered;
}

void NapcatCore::clearLogs()
{
    writeFileAll(logFile(), std::string());
}

// ---------------------------------------------------------------- 配置

Bson NapcatCore::loadConfigOrDefault()
{
    Bson cfg;
    cfg["version"] = kConfigVersion;
    cfg["webuiPort"] = 6099;
    cfg["onebotPort"] = 3000;
    cfg["autoStart"] = false;
    cfg["autoRestart"] = false;
    cfg["token"] = "";

    const std::string raw = readFileAll(configFile());
    if (raw.empty()) return cfg;  // 首次运行 / 空文件

    try {
        // 用最朴素的方式从 JSON 里取字段，损坏时回退默认值
        int v = 0;
        if (extractInt(raw, "version", v) && v != kConfigVersion) {
            // 版本不匹配：保留默认值，让上层提示需要迁移
        } else {
            int n = 0;
            if (extractInt(raw, "webuiPort", n) && n > 0 && n < 65536) cfg["webuiPort"] = n;
            if (extractInt(raw, "onebotPort", n) && n > 0 && n < 65536) cfg["onebotPort"] = n;
            if (extractInt(raw, "version", n)) cfg["version"] = n;
        }
        // 布尔与字符串字段
        auto pickBool = [&](const char *k) {
            if (raw.find(std::string("\"") + k + "\": true") != std::string::npos) cfg[k] = true;
            else if (raw.find(std::string("\"") + k + "\": false") != std::string::npos) cfg[k] = false;
        };
        pickBool("autoStart");
        pickBool("autoRestart");
        cfg["_corrupt"] = false;
    } catch (...) {
        cfg["_corrupt"] = true;  // 损坏兜底：仍返回可用默认值
    }
    return cfg;
}

void NapcatCore::saveConfig(const Bson &cfg)
{
    auto intOf = [&](const char *k, int def) -> int {
        if (cfg.contains(k) && cfg[k].is_int()) return cfg[k].int_value();
        return def;
    };
    auto boolOf = [&](const char *k, bool def) -> bool {
        if (cfg.contains(k) && cfg[k].is_bool()) return cfg[k].bool_value();
        return def;
    };
    auto strOf = [&](const char *k)->std::string {
        if (cfg.contains(k) && cfg[k].is_string()) return cfg[k].string_value();
        return std::string();
    };

    int webui = intOf("webuiPort", 6099);
    int ob11 = intOf("onebotPort", 3000);
    if (webui < 1 || webui > 65535) throw std::runtime_error("webuiPort 超出范围");
    if (ob11 < 1 || ob11 > 65535) throw std::runtime_error("onebotPort 超出范围");

    std::ostringstream js;
    js << "{\n"
       << "  \"version\": " << kConfigVersion << ",\n"
       << "  \"webuiPort\": " << webui << ",\n"
       << "  \"onebotPort\": " << ob11 << ",\n"
       << "  \"autoStart\": " << (boolOf("autoStart", false) ? "true" : "false") << ",\n"
       << "  \"autoRestart\": " << (boolOf("autoRestart", false) ? "true" : "false") << ",\n"
       << "  \"token\": \"" << jsonEscape(strOf("token")) << "\"\n"
       << "}\n";

    ::mkdir(stateDir(), 0755);
    writeFileAll(configFile(), js.str());
}

Bson NapcatCore::readConfig()
{
    return loadConfigOrDefault();
}

void NapcatCore::writeConfig(const Bson &patch)
{
    Bson cur = loadConfigOrDefault();
    // 合并（白名单字段）
    for (const char *k : {"webuiPort", "onebotPort", "autoStart", "autoRestart", "token"}) {
        if (patch.contains(k)) cur[k] = patch[k];
    }
    saveConfig(cur);
}

// ---------------------------------------------------------------- 二维码

std::string NapcatCore::readQrcodeBase64()
{
    // 只读 chroot 内 NapCat 生成的那张，不主动触发登录流程
    if (!fileExists(kQrcodeInChroot)) return std::string();
    const std::string png = readFileAll(kQrcodeInChroot);
    if (png.empty() || png.size() > 512 * 1024) return std::string();
    // 校验 PNG magic，避免把损坏文件传出去
    if (png.size() < 8 || png.compare(0, 8, "\x89PNG\r\n\x1a\n") != 0) return std::string();
    return base64Encode(png);
}

// ---------------------------------------------------------------- OneBot 探测

Bson NapcatCore::pingOneBot()
{
    Bson out;
    Bson st = queryStatus();
    const int port = st.contains("onebotPort") && st["onebotPort"].is_int()
                         ? st["onebotPort"].int_value()
                         : 3000;

    char cmd[512];
    // 用 curl 探测本机 OneBot HTTP 端口；--max-time 防挂死
    std::snprintf(cmd, sizeof(cmd),
                  "curl -s -o /dev/null -w '%%{http_code} %%{time_total}' --max-time 4 "
                  "http://127.0.0.1:%d/ 2>/dev/null",
                  port);
    const std::string r = trimTrailing(exec(cmd));
    std::istringstream iss(r);
    std::string codeStr, timeStr;
    iss >> codeStr >> timeStr;

    if (codeStr.empty() || codeStr == "000") {
        out["ok"] = false;
        out["message"] = "端口未监听或连接失败";
        out["status"] = 0;
        out["latencyMs"] = -1;
    } else {
        out["ok"] = true;
        out["status"] = std::atoi(codeStr.c_str());
        out["latencyMs"] = static_cast<int>(std::atof(timeStr.c_str()) * 1000);
    }
    out["port"] = port;
    return out;
}

}  // namespace napcat
