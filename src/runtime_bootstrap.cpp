#include "astrae_backend.h"
#include "il2cpp_runtime.hpp"
#include "managed_wire.hpp"
#include "hook_runtime.hpp"
#include "lobby_restore.hpp"
#include "debug_log.hpp"
#include "original_store_abi.h"
#include <atomic>
#include <chrono>
#include <cstdio>
#include <exception>
#include <string>
#include <thread>
#include <unistd.h>
#ifndef ASTRAE_BUILD_ID
#define ASTRAE_BUILD_ID "unknown"
#endif
#if defined(__ANDROID__)
#include <android/log.h>
#include <pthread.h>
namespace {
constexpr const char *kCompatStoreDirectory="/storage/emulated/0/Android/data/com.nc.astraeoratio.singleplayer/files/AstraeOffline/SinglePlayer/compat-store";
constexpr uint32_t kLoaderSettleDelayMs=2500;
constexpr uint32_t kGameSettleDelayMs=8000;
constexpr uint32_t kManagedWaitTimeoutMs=120000;
std::atomic<bool> g_started{false};
std::chrono::steady_clock::time_point g_constructor_time{};
bool wait_for_managed_class(const char *name,uint32_t timeout_ms,std::string *error){
    auto &runtime=astrae::il2cpp::Runtime::instance();const auto deadline=std::chrono::steady_clock::now()+std::chrono::milliseconds(timeout_ms);std::string last_error;auto next_log=std::chrono::steady_clock::now();
    do{
        {astrae::il2cpp::Runtime::ThreadScope scope(runtime);if(scope.ok()){if(runtime.find_class(name,&last_error)){ASTRAE_LOGI("Managed class ready: %s",name);if(error)error->clear();return true;}}else last_error=scope.error();}
        auto now=std::chrono::steady_clock::now();if(now>=next_log){ASTRAE_LOGI("waiting for Managed class %s; last=%s",name,last_error.c_str());next_log=now+std::chrono::seconds(5);}std::this_thread::sleep_for(std::chrono::milliseconds(200));
    }while(std::chrono::steady_clock::now()<deadline);
    if(error)*error=std::string("timed out waiting for Managed class ")+name+(last_error.empty()?std::string():std::string("; last error: ")+last_error);return false;
}
bool wait_for_lobby_entry_method(uint32_t timeout_ms,std::string *error){
    auto &runtime=astrae::il2cpp::Runtime::instance();const auto deadline=std::chrono::steady_clock::now()+std::chrono::milliseconds(timeout_ms);std::string last_error;auto next_log=std::chrono::steady_clock::now();
    do{
        {astrae::il2cpp::Runtime::ThreadScope scope(runtime);if(scope.ok()){void *klass=runtime.find_class("_AT.UI.UILoginPageController",&last_error);if(klass){void *method=runtime.find_method(klass,"EnterLobby",0);if(method){ASTRAE_LOGI("lobby entry method resolved: _AT.UI.UILoginPageController.EnterLobby/0 at %p",method);if(error)error->clear();return true;}last_error="UILoginPageController.EnterLobby/0 is not available yet";}}else last_error=scope.error();}
        auto now=std::chrono::steady_clock::now();if(now>=next_log){ASTRAE_LOGI("waiting for lobby entry method; %s",last_error.c_str());next_log=now+std::chrono::seconds(5);}std::this_thread::sleep_for(std::chrono::milliseconds(200));
    }while(std::chrono::steady_clock::now()<deadline);
    if(error)*error="timed out waiting for UILoginPageController.EnterLobby/0: "+last_error;return false;
}
void log_backend_diagnostics(){char *raw=AstraeOffline_GetDiagnostics();if(raw){ASTRAE_LOGI("backend diagnostics: %s",raw);AstraeOffline_Free(raw);}else ASTRAE_LOGW("backend diagnostics returned null");}
void runtime_worker_impl(){
    astrae::debug::initialize();
    ASTRAE_LOGI("runtime worker started; target log=%s",astrae::debug::file_path());
    ASTRAE_LOGI("build identity: %s",ASTRAE_BUILD_ID);
    ASTRAE_LOGI("build flags: runtime_autostart=ON touch_direct_hook=%s local_request_direct_hook=%s shadowhook=%s",
#if defined(ASTRAE_ENABLE_TOUCH_DIRECT_HOOK)
        "ON",
#else
        "OFF",
#endif
#if defined(ASTRAE_ENABLE_LOCAL_REQUEST_DIRECT_HOOK)
        "ON",
#else
        "OFF",
#endif
#if defined(ASTRAE_USE_SHADOWHOOK)
        "ON"
#else
        "OFF"
#endif
    );
    if(g_constructor_time.time_since_epoch().count()!=0){auto elapsed=std::chrono::duration_cast<std::chrono::milliseconds>(std::chrono::steady_clock::now()-g_constructor_time).count();ASTRAE_LOGI("time since ELF constructor: %lld ms",static_cast<long long>(elapsed));}
    ASTRAE_LOGI("waiting %u ms for linker/loader startup to settle",kLoaderSettleDelayMs);std::this_thread::sleep_for(std::chrono::milliseconds(kLoaderSettleDelayMs));
    ASTRAE_LOGI("initializing IL2CPP runtime; waiting for libil2cpp.so, domain and required exports");std::string error;auto il2cpp_start=std::chrono::steady_clock::now();
    if(!astrae::il2cpp::Runtime::instance().initialize_wait(kManagedWaitTimeoutMs,100,&error)){ASTRAE_LOGE("IL2CPP initialization wait failed after %lld ms: %s",static_cast<long long>(std::chrono::duration_cast<std::chrono::milliseconds>(std::chrono::steady_clock::now()-il2cpp_start).count()),error.c_str());return;}
    astrae::il2cpp::Status runtime_status{};{astrae::il2cpp::Runtime::ThreadScope status_scope(astrae::il2cpp::Runtime::instance());if(status_scope.ok())runtime_status=astrae::il2cpp::Runtime::instance().status();else ASTRAE_LOGW("could not attach worker to read IL2CPP status: %s",status_scope.error().c_str());}ASTRAE_LOGI("IL2CPP runtime ready in %lld ms: library=%s assemblies=%zu resolved_symbols=%zu missing_required=%zu core_network_class_ready=%s",
        static_cast<long long>(std::chrono::duration_cast<std::chrono::milliseconds>(std::chrono::steady_clock::now()-il2cpp_start).count()),runtime_status.library_path.c_str(),runtime_status.assembly_count,runtime_status.resolved_symbols,runtime_status.missing_required,runtime_status.core_network_class_ready?"true":"false");
    ASTRAE_LOGI("loading ManagedWire schema from embedded assets or assets/schema/wire_shapes.json");
    if(!astrae::managed::ManagedWire::instance().load_schema("assets/schema/wire_shapes.json",&error)){ASTRAE_LOGE("ManagedWire schema initialization failed: %s",error.c_str());return;}
    ASTRAE_LOGI("ManagedWire schema loaded: %s",astrae::managed::ManagedWire::instance().schema_report().c_str());
    // Open the legacy compatibility Store first. Its schema-v1 save is the source of
    // truth for existing profiles; Backend then imports it once into the native schema.
    ASTRAE_LOGI("opening compatibility Store before Backend migration: %s",kCompatStoreDirectory);
    const bool compat_store_ready=store_open(kCompatStoreDirectory)!=0;
    if(!compat_store_ready)ASTRAE_LOGW("compat Store open failed: %s; existing files will be retained and Backend migration will not overwrite them",store_error());
    else ASTRAE_LOGI("compat Store opened before Backend migration; tutorial content_bits=%llu preserved",static_cast<unsigned long long>(store_content_bits()));
    ASTRAE_LOGI("initializing offline Backend with existing-save recovery/migration");
    if(!AstraeOffline_Init(nullptr,nullptr)){ASTRAE_LOGE("offline Backend initialization failed; existing save was not replaced with a new account; diagnostics follow");log_backend_diagnostics();return;}
    ASTRAE_LOGI("offline Backend initialized successfully");log_backend_diagnostics();
    astrae::lobby::register_local_activity_period_provider();ASTRAE_LOGI("registered local activity-period provider");
    bool local_request_ready=false;
#if defined(ASTRAE_ENABLE_LOCAL_REQUEST_DIRECT_HOOK)
    ASTRAE_LOGI("Hook #5 local_request is enabled; installing local request interception before any touch replacement");
    std::string request_error;if(!astrae::hooks::install_local_request_hook(&request_error))ASTRAE_LOGE("Hook #5 installation failed: %s",request_error.c_str());else{local_request_ready=true;ASTRAE_LOGI("Hook #5 local_request installed before Hook #3; ManagedWire schema=%s",astrae::managed::ManagedWire::instance().schema_report().c_str());}
#else
    ASTRAE_LOGW("Hook #5 local_request is disabled; original game touch must not be replaced because requests would not be guaranteed local");
#endif
#if defined(ASTRAE_ENABLE_TOUCH_DIRECT_HOOK)
    if(!local_request_ready){ASTRAE_LOGE("Hook #3 installation skipped: Hook #5 local request interception is a required safety prerequisite while touch prelude is incomplete");}
    else{
        ASTRAE_LOGI("Hook #3 enabled; waiting for _AT.UI.UILoginPageController");
        if(!wait_for_managed_class("_AT.UI.UILoginPageController",kManagedWaitTimeoutMs,&error))ASTRAE_LOGE("Hook #3 cannot be installed: %s",error.c_str());
        else if(!wait_for_lobby_entry_method(kManagedWaitTimeoutMs,&error))ASTRAE_LOGE("Hook #3 cannot be installed: %s",error.c_str());
        else{
            ASTRAE_LOGI("login controller and EnterLobby method are ready; delaying Hook #3 init by %u ms",kGameSettleDelayMs);std::this_thread::sleep_for(std::chrono::milliseconds(kGameSettleDelayMs));
            std::string touch_error;if(!astrae::hooks::install_touch_hook(&touch_error))ASTRAE_LOGE("Hook #3 installation FAILED; original game code remains untouched: %s",touch_error.c_str());
            else ASTRAE_LOGI("Hook #3 touch replacement installed after Hook #5; incomplete custom prelude will forward the tap to original touch");
        }
    }
#else
    ASTRAE_LOGW("Hook #3 is disabled; original login touch remains untouched");
#endif
    ASTRAE_LOGI("bootstrap initialization complete; entering 15-second lobby-hook watchdog");uint64_t previous_calls=0;uint64_t elapsed_seconds=0;
    for(;;){std::this_thread::sleep_for(std::chrono::seconds(15));elapsed_seconds+=15;const uint64_t calls=astrae::hooks::touch_hook_call_count();
        if(calls!=previous_calls){ASTRAE_LOGI("watchdog: Hook #3 touch calls increased %llu -> %llu",static_cast<unsigned long long>(previous_calls),static_cast<unsigned long long>(calls));previous_calls=calls;}
        else if(elapsed_seconds<=120)ASTRAE_LOGW("watchdog: no Hook #3 touch callback observed after %llu seconds; hook_installed=%s; inspect prior IL2CPP/UI readiness and target fingerprint lines",static_cast<unsigned long long>(elapsed_seconds),astrae::hooks::touch_hook_installed()?"true":"false");
        else if(elapsed_seconds%60==0)ASTRAE_LOGI("watchdog heartbeat: uptime_after_bootstrap=%llu seconds; touch_calls=%llu",static_cast<unsigned long long>(elapsed_seconds),static_cast<unsigned long long>(calls));
    }
}
void *runtime_worker(void *){try{runtime_worker_impl();}catch(const std::exception &exception){ASTRAE_LOGE("runtime worker exception: %s",exception.what());}catch(...){ASTRAE_LOGE("runtime worker terminated by an unknown C++ exception");}return nullptr;}
__attribute__((constructor)) void astrae_offline_initialize(){
    g_constructor_time=std::chrono::steady_clock::now();astrae::debug::initialize();
    ASTRAE_LOGI("========== new process session ==========");ASTRAE_LOGI("build identity: %s",ASTRAE_BUILD_ID);ASTRAE_LOGI("ELF constructor entered; pid=%ld package-private log=%s file_ready=%s",static_cast<long>(getpid()),astrae::debug::file_path(),astrae::debug::file_ready()?"true":"false");
#if defined(ASTRAE_ENABLE_RUNTIME_AUTOSTART)
    ASTRAE_LOGI("runtime autostart is enabled; scheduling delayed worker");bool expected=false;if(!g_started.compare_exchange_strong(expected,true,std::memory_order_acq_rel)){ASTRAE_LOGW("duplicate constructor invocation ignored");return;}
    pthread_t thread{};const int rc=pthread_create(&thread,nullptr,runtime_worker,nullptr);if(rc!=0){g_started.store(false,std::memory_order_release);ASTRAE_LOGE("cannot create delayed runtime worker: pthread error %d",rc);return;}pthread_detach(thread);ASTRAE_LOGI("runtime worker thread created successfully");
#else
    ASTRAE_LOGE("runtime autostart is DISABLED in this build; no Backend initialization or lobby Hook #3 will run");
#endif
}
}
#endif
