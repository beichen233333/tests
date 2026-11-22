#include "astrae_backend.h"
#include "il2cpp_runtime.hpp"
#include "managed_wire.hpp"
#include "hook_runtime.hpp"
#include <atomic>
#include <chrono>
#include <cstdio>
#include <thread>
#if defined(__ANDROID__)
#include <android/log.h>
#include <pthread.h>
namespace {
constexpr const char *kLogTag="AstraeOffline";
std::atomic<bool> g_started{false};
void log_line(int priority,const char *message){__android_log_write(priority,kLogTag,message?message:"(null)");}
void *runtime_worker(void *){
    log_line(ANDROID_LOG_INFO,"runtime bootstrap worker started; waiting for IL2CPP before touching backend state");
    std::string error;
    if(!astrae::il2cpp::Runtime::instance().initialize_wait(120000,100,&error)){log_line(ANDROID_LOG_ERROR,("IL2CPP initialization failed: "+error).c_str());return nullptr;}
    // Do not touch game code unless both the managed schema and offline backend are ready.
    std::this_thread::sleep_for(std::chrono::milliseconds(750));
    if(!astrae::managed::ManagedWire::instance().load_schema("assets/schema/wire_shapes.json",&error)){log_line(ANDROID_LOG_ERROR,("wire schema initialization failed: "+error+"; no hooks installed").c_str());return nullptr;}
    if(!AstraeOffline_Init(nullptr,nullptr)){log_line(ANDROID_LOG_ERROR,"offline backend initialization failed; no hooks installed and original game path remains untouched");return nullptr;}
    if(!astrae::hooks::install_local_request_hook(&error)){log_line(ANDROID_LOG_ERROR,("local_request hook installation failed: "+error+"; UI hooks will not be installed").c_str());return nullptr;}
    log_line(ANDROID_LOG_INFO,"local_request hook installed with original-function trampoline fallback");
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
