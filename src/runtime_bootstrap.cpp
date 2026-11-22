#include "astrae_backend.h"
#include "il2cpp_runtime.hpp"
#include "managed_wire.hpp"
#include "hook_runtime.hpp"
#include "lobby_restore.hpp"
#include "original_store_abi.h"
#include <atomic>
#include <chrono>
#include <cstdio>
#include <thread>
#if defined(__ANDROID__) && defined(ASTRAE_ENABLE_RUNTIME_AUTOSTART)
#include <android/log.h>
#include <pthread.h>
namespace {
constexpr const char *kLogTag="AstraeOffline";
constexpr const char *kCompatStoreDirectory="/sdcard/Android/data/com.nc.astraeoratio.singleplayer/files/AstraeOffline/SinglePlayer/compat-store";
std::atomic<bool> g_started{false};
void log_line(int priority,const char *message){__android_log_write(priority,kLogTag,message?message:"(null)");}
void *runtime_worker(void *){
    log_line(ANDROID_LOG_INFO,"runtime bootstrap worker started; waiting for IL2CPP before touching backend state");
    std::string error;
    if(!astrae::il2cpp::Runtime::instance().initialize_wait(120000,100,&error)){log_line(ANDROID_LOG_ERROR,("IL2CPP initialization failed: "+error).c_str());return nullptr;}
    // Do not touch game code unless both the managed schema and offline backend are ready.
    std::this_thread::sleep_for(std::chrono::milliseconds(750));
    if(!astrae::managed::ManagedWire::instance().load_schema("assets/schema/wire_shapes.json",&error)){log_line(ANDROID_LOG_ERROR,("wire schema initialization failed: "+error+"; no hooks installed").c_str());return nullptr;}
    if(!AstraeOffline_Init(nullptr,nullptr)){log_line(ANDROID_LOG_ERROR,"offline backend initialization failed; no hooks installed");return nullptr;}
    // Keep the legacy Store ABI alive for compatibility exports. Its file is separate from
    // Backend/save.json because the two stores intentionally have different schemas.
    if(!store_open(kCompatStoreDirectory))log_line(ANDROID_LOG_WARN,(std::string("compat Store open failed; lobby route will still use the embedded bypass bit: ")+store_error()).c_str());
    else{int changed=0;if(!store_skip_first_day(&changed))log_line(ANDROID_LOG_WARN,(std::string("compat first-day skip could not be persisted: ")+store_error()).c_str());}
    astrae::lobby::register_local_activity_period_provider();
    if(!astrae::hooks::install_local_request_hook(&error))log_line(ANDROID_LOG_WARN,("Hook #5 local_request unavailable: "+error+"; lobby entry remains enabled").c_str());
    else log_line(ANDROID_LOG_INFO,"Hook #5 local_request installed with ErrorPacket fallback for unmapped requests");
    std::string touch_error;
    if(!astrae::hooks::install_touch_hook(&touch_error)){log_line(ANDROID_LOG_ERROR,("Hook #3 touch installation failed: "+touch_error).c_str());return nullptr;}
    log_line(ANDROID_LOG_INFO,"Hook #3 touch installed: minimal path skips first-day tutorial and dispatches UILoginPageController.EnterLobby");
    // UI code is patched only after a request was actually accepted by the offline backend.
    // This keeps the early splash/login initialization path free of UI code-page changes.
    constexpr int kWaitTicks=1200;
    for(int i=0;i<kWaitTicks&&!astrae::hooks::managed_request_seen();++i)std::this_thread::sleep_for(std::chrono::milliseconds(100));
    if(!astrae::hooks::managed_request_seen()){log_line(ANDROID_LOG_WARN,"no offline-managed request observed within 120 seconds; UI hooks left disabled");return nullptr;}
    std::this_thread::sleep_for(std::chrono::milliseconds(500));
    std::string ui_error;size_t ui_count=astrae::hooks::install_ui_passthrough_hooks(&ui_error);
    char message[256];std::snprintf(message,sizeof(message),"deferred UI passthrough hooks installed: %zu (ABI-known/trampoline-backed only)%s%s",ui_count,ui_error.empty()?"":"; first skipped hook: ",ui_error.c_str());log_line(ANDROID_LOG_INFO,message);
    return nullptr;
}
__attribute__((constructor)) void astrae_offline_initialize(){bool expected=false;if(!g_started.compare_exchange_strong(expected,true))return;pthread_t thread{};int rc=pthread_create(&thread,nullptr,runtime_worker,nullptr);if(rc!=0){g_started.store(false);char message[128];std::snprintf(message,sizeof(message),"cannot create runtime worker: pthread error %d",rc);log_line(ANDROID_LOG_ERROR,message);return;}pthread_detach(thread);}
}
#endif
