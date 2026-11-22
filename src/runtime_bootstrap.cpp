#include "astrae_backend.h"
#include "il2cpp_runtime.hpp"
#include "managed_wire.hpp"
#include "hook_runtime.hpp"
#include <atomic>
#include <cstdio>
#if defined(__ANDROID__)
#include <android/log.h>
#include <pthread.h>
namespace {
constexpr const char *kLogTag="AstraeOffline";
std::atomic<bool> g_started{false};
void log_line(int priority,const char *message){__android_log_write(priority,kLogTag,message?message:"(null)");}
void *runtime_worker(void *){
    log_line(ANDROID_LOG_INFO,"automatic runtime bootstrap started");
    std::string error;
    if(!astrae::managed::ManagedWire::instance().load_schema("assets/schema/wire_shapes.json",&error)){log_line(ANDROID_LOG_ERROR,("wire schema initialization failed: "+error).c_str());return nullptr;}
    if(!AstraeOffline_Init(nullptr,nullptr)){log_line(ANDROID_LOG_ERROR,"offline backend initialization failed; embedded assets or save path unavailable");return nullptr;}
    if(!astrae::il2cpp::Runtime::instance().initialize_wait(120000,100,&error)){log_line(ANDROID_LOG_ERROR,("IL2CPP initialization failed: "+error).c_str());return nullptr;}
    if(!astrae::hooks::install_local_request_hook(&error)){log_line(ANDROID_LOG_ERROR,("local_request hook installation failed: "+error).c_str());return nullptr;}
    log_line(ANDROID_LOG_INFO,"offline request hook installed (1/39 reconstructed hooks)");
    return nullptr;
}
__attribute__((constructor)) void astrae_offline_initialize(){bool expected=false;if(!g_started.compare_exchange_strong(expected,true))return;pthread_t thread{};int rc=pthread_create(&thread,nullptr,runtime_worker,nullptr);if(rc!=0){g_started.store(false);char message[128];std::snprintf(message,sizeof(message),"cannot create bootstrap worker: pthread error %d",rc);log_line(ANDROID_LOG_ERROR,message);return;}pthread_detach(thread);}
}
#endif
