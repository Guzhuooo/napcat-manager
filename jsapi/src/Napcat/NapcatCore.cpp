#include "Napcat/NapcatCore.hpp"

#include <array>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <fstream>
#include <sstream>
#include <stdexcept>
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
const char *kQrcodeInChroot =
    "/userdisk/napcat/rootfs/opt/QQ/resources/app/napcat/cache/qrcode.png";
const char *kWebuiCfg =
    "/userdisk/napcat/rootfs/opt/QQ/resources/app/napcat/config/webui.json";
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

// base64 编码（把二维码 PNG 传给 JS 显示）
std::string base64Encode(const std::string &in)
{
    static const char *tbl =
        "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789+/";
    std::string out;
    out.reserve(((in.size() + 2) / 3) * 4);
    size_t i = 0;
    while (i + 2 < in.size()) {
        const unsigned n = (static_cast<unsigned char>(in[i]) << 16) |
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
        const unsigned n = static_cast<unsigned char>(in[i]) << 16;
        out.push_back(tbl[(n >> 18) & 63]);
        out.push_back(tbl[(n >> 12) & 63]);
        out.push_back('=');
        out.push_back('=');
    } else if (rem == 2) {
        const unsigned n = (static_cast<unsigned char>(in[i]) << 16) |
                           (static_cast<unsigned char>(in[i + 1]) << 8);
        out.push_back(tbl[(n >> 18) & 63]);
        out.push_back(tbl[(n >> 12) & 63]);
        out.push_back(tbl[(n >> 6) & 63]);
        out.push_back('=');
    }
    return out;
}

// 从 JSON 文本里取整数字段（避免引入完整解析依赖）
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
    const long v = std::strtol(json.c_str() + p, &end, 10);
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

// 从 Bson 对象里安全取整数
int bsonInt(const Bson &b, const char *k, int def)
{
    if (!b.is_object()) return def;
    const Bson &v = b[k];
    if (v.is_number()) return v.int_value();
    return def;
}

bool bsonBool(const Bson &b, const char *k, bool def)
{
    if (!b.is_object()) return def;
    const Bson &v = b[k];
    if (v.is_bool()) return v.bool_value();
    return def;
}

std::string bsonStr(const Bson &b, const char *k, const std::string &def)
{
    if (!b.is_object()) return def;
    const Bson &v = b[k];
    if (v.is_string()) return v.string_value();
    return def;
}

std::string localIp()
{
    return std::string();
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

// 找 NapCat 主进程。
// 严格按「qq --no-sandbox」特征匹配，排除 zygote 子进程，
// 绝不会误伤系统 miniapp / guardian_run / DictPen。
int NapcatCore::findMainPid()
{
    const std::string ps = exec("ps -ef");
    std::istringstream iss(ps);
    std::string line;
    while (std::getline(iss, line)) {
        if (line.find("qq --no-sandbox") == std::string::npos) continue;
        if (line.find("--type=zygote") != std::string::npos) continue;
        if (line.find("grep") != std::string::npos) continue;
        if (line.find("sh -c") != std::string::npos) continue;

        std::istringstream ls(line);
        std::string user, pidStr;
        ls >> user >> pidStr;
        char *end = nullptr;
        const long pid = std::strtol(pidStr.c_str(), &end, 10);
        if (end != pidStr.c_str() && pid > 0) return static_cast<int>(pid);
    }
    return -1;
}

// ---------------------------------------------------------------- 状态查询

Bson NapcatCore::queryStatus()
{
    const int pid = findMainPid();

    int webuiPort = 6099;
    const std::string webuiCfg = readFileAll(kWebuiCfg);
    if (!webuiCfg.empty()) extractInt(webuiCfg, "port", webuiPort);

    // OneBot 端口：从 manager.json 读（用户可配），默认 3000
    const Bson cfg = loadConfigOrDefault();
    const int onebotPort = bsonInt(cfg, "onebotPort", 3000);

    // 本机 IP（wlan0）
    std::string ip = trimTrailing(
        exec("ip -4 addr show wlan0 2>/dev/null | awk '/inet /{print $2}' | cut -d/ -f1"));
    if (ip.empty()) ip = "127.0.0.1";

    // 登录账号：从日志里找最近一次登录成功
    std::string acct;
    const std::string log = readFileAll(logFile());
    const size_t pos = log.rfind("登录成功");
    if (pos != std::string::npos) {
        size_t start = log.rfind('\n', pos);
        start = (start == std::string::npos) ? 0 : start + 1;
        const size_t bar = log.find('|', start);
        if (bar != std::string::npos && bar < pos) {
            acct = trimTrailing(log.substr(start, bar - start));
        }
    }

    return Bson::object{
        {"running", pid > 0},
        {"pid", static_cast<double>(pid)},
        {"webuiPort", static_cast<double>(webuiPort)},
        {"onebotPort", static_cast<double>(onebotPort)},
        {"ip", ip},
        {"account", acct}};
}

Bson NapcatCore::queryDeviceInfo()
{
    std::string ip = trimTrailing(
        exec("ip -4 addr show wlan0 2>/dev/null | awk '/inet /{print $2}' | cut -d/ -f1"));
    if (ip.empty()) ip = "127.0.0.1";

    const std::string meminfo = readFileAll("/proc/meminfo");
    auto memField = [&](const char *key) -> double {
        const size_t p = meminfo.find(key);
        if (p == std::string::npos) return 0;
        return static_cast<double>(
            std::strtoll(meminfo.c_str() + p + std::strlen(key), nullptr, 10));
    };

    const std::string df =
        trimTrailing(exec("df -k /userdisk 2>/dev/null | tail -1 | awk '{print $4}'"));
    const double diskFree = static_cast<double>(std::strtoll(df.c_str(), nullptr, 10));

    return Bson::object{
        {"ip", ip},
        {"model", trimTrailing(readFileAll("/proc/device-tree/compatible"))},
        {"memTotalKb", memField("MemTotal:")},
        {"memAvailableKb", memField("MemAvailable:")},
        {"diskFreeKb", diskFree}};
}

// ---------------------------------------------------------------- 启停

Bson NapcatCore::start()
{
    if (findMainPid() > 0) return queryStatus();  // 幂等
    if (!fileExists(kStartScript)) {
        throw std::runtime_error("缺少启动脚本: " + std::string(kStartScript));
    }
    exec(std::string("sh ") + kStartScript);
    ::usleep(1500 * 1000);
    return queryStatus();
}

Bson NapcatCore::stop()
{
    if (findMainPid() < 0) return queryStatus();  // 幂等
    if (!fileExists(kStopScript)) {
        throw std::runtime_error("缺少停止脚本: " + std::string(kStopScript));
    }
    exec(std::string("sh ") + kStopScript);
    ::usleep(1500 * 1000);
    return queryStatus();
}

Bson NapcatCore::quickLogin(const std::string &qq)
{
    exec(std::string("sh ") + kStopScript);
    ::usleep(800 * 1000);

    std::string mode = "qrcode";
    std::string safeQq;

    if (!qq.empty()) {
        // 二次兜底：只保留数字
        for (char c : qq) {
            if (c >= '0' && c <= '9') safeQq.push_back(c);
        }
        if (safeQq.empty()) throw std::runtime_error("QQ 号不合法");
        if (safeQq.size() < 5 || safeQq.size() > 12) {
            throw std::runtime_error("QQ 号长度不合法");
        }
        exec(std::string("sh ") + kStartScript + " " + safeQq);
        mode = "quick";
    } else {
        exec(std::string("sh ") + kStartScript);
    }

    ::usleep(1500 * 1000);

    return Bson::object{
        {"mode", mode},
        {"qq", safeQq},
        {"status", queryStatus()}};
}

// ---------------------------------------------------------------- 日志

std::string NapcatCore::readLogTail(int lines)
{
    if (lines < 1) lines = 1;
    if (lines > 2000) lines = 2000;
    char cmd[256];
    std::snprintf(cmd, sizeof(cmd), "tail -n %d %s", lines, logFile());
    const std::string raw = exec(cmd);
    if (raw.empty() && !fileExists(logFile())) return std::string("(日志文件尚未生成)");

    // 过滤无意义的 asound 噪音
    std::istringstream iss(raw);
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
    int webui = 6099, onebot = 3000, ver = kConfigVersion;
    bool autoStart = false, autoRestart = false, corrupt = false;
    std::string token;

    const std::string raw = readFileAll(configFile());
    if (!raw.empty()) {
        try {
            int v = 0;
            if (extractInt(raw, "version", v)) ver = v;
            int n = 0;
            if (extractInt(raw, "webuiPort", n) && n > 0 && n < 65536) webui = n;
            if (extractInt(raw, "onebotPort", n) && n > 0 && n < 65536) onebot = n;

            if (raw.find("\"autoStart\": true") != std::string::npos) autoStart = true;
            if (raw.find("\"autoRestart\": true") != std::string::npos) autoRestart = true;

            const std::string key = "\"token\"";
            const size_t p = raw.find(key);
            if (p != std::string::npos) {
                const size_t q1 = raw.find('"', raw.find(':', p) + 1);
                if (q1 != std::string::npos) {
                    const size_t q2 = raw.find('"', q1 + 1);
                    if (q2 != std::string::npos) token = raw.substr(q1 + 1, q2 - q1 - 1);
                }
            }
        } catch (...) {
            corrupt = true;  // 损坏兜底：仍返回可用默认值
        }
    }

    return Bson::object{
        {"version", static_cast<double>(ver)},
        {"webuiPort", static_cast<double>(webui)},
        {"onebotPort", static_cast<double>(onebot)},
        {"autoStart", autoStart},
        {"autoRestart", autoRestart},
        {"token", token},
        {"_corrupt", corrupt}};
}

void NapcatCore::saveConfig(const Bson &cfg)
{
    const int webui = bsonInt(cfg, "webuiPort", 6099);
    const int ob11 = bsonInt(cfg, "onebotPort", 3000);
    if (webui < 1 || webui > 65535) throw std::runtime_error("webuiPort 超出范围");
    if (ob11 < 1 || ob11 > 65535) throw std::runtime_error("onebotPort 超出范围");

    std::ostringstream js;
    js << "{\n"
       << "  \"version\": " << kConfigVersion << ",\n"
       << "  \"webuiPort\": " << webui << ",\n"
       << "  \"onebotPort\": " << ob11 << ",\n"
       << "  \"autoStart\": " << (bsonBool(cfg, "autoStart", false) ? "true" : "false") << ",\n"
       << "  \"autoRestart\": " << (bsonBool(cfg, "autoRestart", false) ? "true" : "false") << ",\n"
       << "  \"token\": \"" << jsonEscape(bsonStr(cfg, "token", "")) << "\"\n"
       << "}\n";

    writeFileAll(configFile(), js.str());
}

Bson NapcatCore::readConfig()
{
    return loadConfigOrDefault();
}

void NapcatCore::writeConfig(const Bson &patch)
{
    // 以当前值为基础做白名单合并
    const Bson cur = loadConfigOrDefault();

    const int webui = bsonInt(patch, "webuiPort", bsonInt(cur, "webuiPort", 6099));
    const int ob11 = bsonInt(patch, "onebotPort", bsonInt(cur, "onebotPort", 3000));
    const bool aStart = bsonBool(patch, "autoStart", bsonBool(cur, "autoStart", false));
    const bool aRestart = bsonBool(patch, "autoRestart", bsonBool(cur, "autoRestart", false));
    const std::string token = bsonStr(patch, "token", bsonStr(cur, "token", ""));

    const Bson merged = Bson::object{
        {"version", static_cast<double>(kConfigVersion)},
        {"webuiPort", static_cast<double>(webui)},
        {"onebotPort", static_cast<double>(ob11)},
        {"autoStart", aStart},
        {"autoRestart", aRestart},
        {"token", token}};

    saveConfig(merged);
}

// ---------------------------------------------------------------- 二维码

std::string NapcatCore::readQrcodeBase64()
{
    // 只读 NapCat 已生成的那张；不主动触发登录流程
    if (!fileExists(kQrcodeInChroot)) return std::string();
    const std::string png = readFileAll(kQrcodeInChroot);
    if (png.size() < 8 || png.size() > 512 * 1024) return std::string();
    // 校验 PNG magic，避免传出损坏文件
    if (png.compare(0, 8, "\x89PNG\r\n\x1a\n") != 0) return std::string();
    return base64Encode(png);
}

// ---------------------------------------------------------------- OneBot 探测

Bson NapcatCore::pingOneBot()
{
    const Bson st = queryStatus();
    const int port = bsonInt(st, "onebotPort", 3000);

    char cmd[512];
    std::snprintf(cmd, sizeof(cmd),
                  "curl -s -o /dev/null -w '%%{http_code} %%{time_total}' --max-time 4 "
                  "http://127.0.0.1:%d/ 2>/dev/null",
                  port);
    const std::string r = trimTrailing(exec(cmd));

    std::istringstream iss(r);
    std::string codeStr, timeStr;
    iss >> codeStr >> timeStr;

    if (codeStr.empty() || codeStr == "000") {
        return Bson::object{
            {"ok", false},
            {"message", std::string("端口未监听或连接失败")},
            {"status", 0.0},
            {"latencyMs", -1.0},
            {"port", static_cast<double>(port)}};
    }

    return Bson::object{
        {"ok", true},
        {"message", std::string()},
        {"status", static_cast<double>(std::atoi(codeStr.c_str()))},
        {"latencyMs", std::atof(timeStr.c_str()) * 1000.0},
        {"port", static_cast<double>(port)}};
}

}  // namespace napcat
