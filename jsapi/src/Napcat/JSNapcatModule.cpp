#include "Napcat/JSNapcatModule.hpp"
#include "Napcat/NapcatCore.hpp"
#include <jqutil_v2/jqbson.h>
#include <jqutil_v2/JQAsyncExecutor.h>

#include <mutex>
#include <stdexcept>
#include <string>
#include <thread>

using namespace napcat;

// ============================================================================
//  异步执行辅助
//
//  关键约束：JQAsyncInfo 内部持有的是 weak pointer，一旦 handler 返回、
//  对象被 GC，post() 会静默失败（Promise 永久挂起）。
//
//  因此不能简单 std::thread(...).detach() 后直接 post。
//  正确做法：用 SDK 的 JQAsyncExecutor 注册回调，用它的线程安全接口
//  onCallbackAsync / onErrorAsync 回传结果 —— 它在对象销毁时会自动失效，
//  不会产生悬垂调用。
// ============================================================================

namespace {

// 在后台线程执行 fn，并把结果通过 executor 回传。
// 这里用 JQAsyncInfo::postJSThread 保证最终在 JS 线程兑现 Promise。
template <typename Fn>
void runAsync(JQAsyncInfo info, Fn fn)
{
    // 把 info 复制进线程（JQAsyncInfo 可拷贝）
    std::thread([info, fn]() mutable {
        try {
            Bson out = fn();
            info.post(out);
        } catch (const std::exception &e) {
            info.postError(e.what());
        } catch (...) {
            info.postError("未知错误");
        }
    }).detach();
}

}  // namespace


extern JSValue createNapcatModule(JQModuleEnv *env)
{
    JQFunctionTemplateRef tpl = JQFunctionTemplate::New(env, "NapcatModule");
    tpl->InstanceTemplate()->setObjectCreator([]() { return new JSNapcatModule(); });

    // 同步：纯返回常量或极短计算
    tpl->SetProtoMethod("getVersion", &JSNapcatModule::getVersion);

    // 异步：涉及进程/文件/网络的阻塞操作
    tpl->SetProtoMethodPromise("getStatus", &JSNapcatModule::getStatus);
    tpl->SetProtoMethodPromise("getDeviceInfo", &JSNapcatModule::getDeviceInfo);
    tpl->SetProtoMethodPromise("start", &JSNapcatModule::start);
    tpl->SetProtoMethodPromise("stop", &JSNapcatModule::stop);
    tpl->SetProtoMethodPromise("getLogs", &JSNapcatModule::getLogs);
    tpl->SetProtoMethodPromise("clearLogs", &JSNapcatModule::clearLogs);
    tpl->SetProtoMethodPromise("readConfig", &JSNapcatModule::readConfig);
    tpl->SetProtoMethodPromise("writeConfig", &JSNapcatModule::writeConfig);
    tpl->SetProtoMethodPromise("setOnebot", &JSNapcatModule::setOnebot);
    tpl->SetProtoMethodPromise("getQrcode", &JSNapcatModule::getQrcode);
    tpl->SetProtoMethodPromise("quickLogin", &JSNapcatModule::quickLogin);
    tpl->SetProtoMethodPromise("pingOneBot", &JSNapcatModule::pingOneBot);

    JSNapcatModule::InitTpl(tpl);
    return tpl->CallConstructor();
}

JSNapcatModule::JSNapcatModule() = default;
JSNapcatModule::~JSNapcatModule() = default;

// ---------------------------------------------------------------- 同步方法

void JSNapcatModule::getVersion(JQFunctionInfo &info)
{
    try {
        info.GetReturnValue().Set(std::string("1.0.0"));
    } catch (const std::exception &e) {
        info.GetReturnValue().ThrowInternalError(e.what());
    }
}

// ---------------------------------------------------------------- 异步方法

void JSNapcatModule::getStatus(JQAsyncInfo &info)
{
    std::thread([this, info]() mutable {
        try {
            Bson out;
            {
                std::lock_guard<std::mutex> lock(mutex_);
                out = NapcatCore::queryStatus();
            }
            info.post(out);
        } catch (const std::exception &e) {
            info.postError(e.what());
        }
    }).detach();
}

void JSNapcatModule::getDeviceInfo(JQAsyncInfo &info)
{
    std::thread([info]() mutable {
        try {
            info.post(NapcatCore::queryDeviceInfo());
        } catch (const std::exception &e) {
            info.postError(e.what());
        }
    }).detach();
}

void JSNapcatModule::start(JQAsyncInfo &info)
{
    std::thread([this, info]() mutable {
        try {
            std::lock_guard<std::mutex> lock(mutex_);
            info.post(NapcatCore::start());
        } catch (const std::exception &e) {
            info.postError(e.what());
        }
    }).detach();
}

void JSNapcatModule::stop(JQAsyncInfo &info)
{
    std::thread([this, info]() mutable {
        try {
            std::lock_guard<std::mutex> lock(mutex_);
            info.post(NapcatCore::stop());
        } catch (const std::exception &e) {
            info.postError(e.what());
        }
    }).detach();
}

void JSNapcatModule::getLogs(JQAsyncInfo &info)
{
    int tail = 200;
    if (info.Length() >= 1 && info[0].is_number()) tail = info[0].int_value();
    tail = clampInt(tail, 1, 2000);

    std::thread([info, tail]() mutable {
        try {
            info.post(Bson::object{{"text", NapcatCore::readLogTail(tail)}});
        } catch (const std::exception &e) {
            info.postError(e.what());
        }
    }).detach();
}

void JSNapcatModule::clearLogs(JQAsyncInfo &info)
{
    std::thread([info]() mutable {
        try {
            NapcatCore::clearLogs();
            info.post(Bson::object{{"ok", true}});
        } catch (const std::exception &e) {
            info.postError(e.what());
        }
    }).detach();
}

void JSNapcatModule::readConfig(JQAsyncInfo &info)
{
    std::thread([info]() mutable {
        try {
            info.post(NapcatCore::readConfig());
        } catch (const std::exception &e) {
            info.postError(e.what());
        }
    }).detach();
}

void JSNapcatModule::writeConfig(JQAsyncInfo &info)
{
    // 参数校验：必须是对象，且字段都经过白名单与范围检查
    if (info.Length() < 1 || !info[0].is_object()) {
        info.postError("writeConfig: 需要一个配置对象");
        return;
    }
    Bson patch = info[0];
    std::thread([info, patch]() mutable {
        try {
            NapcatCore::writeConfig(patch);
            info.post(Bson::object{{"ok", true}});
        } catch (const std::exception &e) {
            info.postError(e.what());
        }
    }).detach();
}

void JSNapcatModule::getQrcode(JQAsyncInfo &info)
{
    std::thread([info]() mutable {
        try {
            info.post(Bson::object{{"base64", NapcatCore::readQrcodeBase64()}});
        } catch (const std::exception &e) {
            info.postError(e.what());
        }
    }).detach();
}

void JSNapcatModule::setOnebot(JQAsyncInfo &info)
{
    // 参数：enable(bool), port(int)
    bool enable = true;
    int port = 3000;
    if (info.Length() >= 1 && info[0].is_bool()) enable = info[0].bool_value();
    if (info.Length() >= 2 && info[1].is_number()) port = info[1].int_value();
    port = clampInt(port, 1, 65535);

    std::thread([info, enable, port]() mutable {
        try {
            info.post(NapcatCore::setOnebot(enable, port));
        } catch (const std::exception &e) {
            info.postError(e.what());
        }
    }).detach();
}

void JSNapcatModule::quickLogin(JQAsyncInfo &info)
{
    // QQ 号必须是 5-12 位纯数字
    std::string qq;
    if (info.Length() >= 1 && info[0].is_string()) qq = info[0].string_value();
    if (!qq.empty()) {
        if (qq.size() < 5 || qq.size() > 12) {
            info.postError("quickLogin: QQ 号长度不合法");
            return;
        }
        for (char c : qq) {
            if (c < '0' || c > '9') {
                info.postError("quickLogin: QQ 号必须是纯数字");
                return;
            }
        }
    }
    std::thread([this, info, qq]() mutable {
        try {
            std::lock_guard<std::mutex> lock(mutex_);
            info.post(NapcatCore::quickLogin(qq));
        } catch (const std::exception &e) {
            info.postError(e.what());
        }
    }).detach();
}

void JSNapcatModule::pingOneBot(JQAsyncInfo &info)
{
    std::thread([info]() mutable {
        try {
            info.post(NapcatCore::pingOneBot());
        } catch (const std::exception &e) {
            info.postError(e.what());
        }
    }).detach();
}

// ---------------------------------------------------------------- 辅助

int JSNapcatModule::clampInt(int v, int lo, int hi)
{
    if (v < lo) return lo;
    if (v > hi) return hi;
    return v;
}

