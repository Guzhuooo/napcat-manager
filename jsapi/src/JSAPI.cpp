// ============================================================================
//  NapCat 管理器 - JSAPI 注册入口
//
//  命名一致性（三者必须相同）：
//    .so 文件名        libjsapi_napcat.so
//    registerCModuleLoader("napcat", ...)
//    JS 侧             import { NapcatModule } from 'napcat'
// ============================================================================

#include <jsmodules/JSCModuleExtension.h>
#include <jquick_config.h>
#include "Napcat/JSNapcatModule.hpp"

using namespace JQUTIL_NS;

static std::vector<std::string> exportList = {
    "NapcatModule",
};

static int module_init(JSContext *ctx, JSModuleDef *m)
{
    auto env = JQModuleEnv::CreateModule(ctx, m, "napcat");
    env->setModuleExport("NapcatModule", createNapcatModule(env.get()));
    env->setModuleExportDone(JS_UNDEFINED, exportList);
    return 0;
}

DEF_MODULE_LOAD_FUNC_EXPORT(napcat, module_init, exportList)

extern "C" JQUICK_EXPORT void custom_init_jsapis()
{
    registerCModuleLoader("napcat", &napcat_module_load);
}
