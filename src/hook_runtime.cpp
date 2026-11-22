#include "hook_runtime.hpp"
#include "original_hook_manifest.hpp"
#include "json.hpp"
#include "astrae_backend.h"
#include "lobby_restore.hpp"
#include "debug_log.hpp"
#include <array>
#include <atomic>
#include <cstring>
#include <mutex>
#include <string>
#include <vector>
#if defined(__ANDROID__) && defined(__aarch64__) && defined(ASTRAE_USE_SHADOWHOOK)
#include <shadowhook.h>
#include <dlfcn.h>
#include <link.h>
#include <android/log.h>
#endif

namespace astrae::hooks {
#if defined(__ANDROID__) && defined(__aarch64__) && defined(ASTRAE_USE_SHADOWHOOK)
namespace {
constexpr size_t kFingerprintSize=16;
constexpr uint32_t kHookLimit=40;
struct ExecRange {uintptr_t begin=0,end=0;};
struct ModuleScan {uintptr_t base=0;bool found=false;std::string path;std::vector<ExecRange> executable;};
enum class InstallMode:uint8_t {Trampoline=1,DirectReplacement=2};
static std::mutex g_patch_mutex;
static std::once_flag g_shadowhook_once;
static std::atomic<int> g_shadowhook_init_result{-1};
static std::array<std::atomic<void *>,kHookLimit> g_stubs{};
static std::array<std::atomic<void *>,kHookLimit> g_replacements{};
static std::array<std::atomic<uint8_t>,kHookLimit> g_modes{};
static std::array<std::atomic<bool>,kHookLimit> g_installed{};
static std::array<std::atomic<uint64_t>,kHookLimit> g_calls{};
static std::atomic<bool> g_install_attempted{false};
static std::atomic<size_t> g_ui_installed{0};
static std::atomic<bool> g_local_request_installed{false};
static std::atomic<bool> g_touch_installed{false};
static std::atomic<bool> g_managed_request_seen{false};
static int scan_module(dl_phdr_info *info,size_t,void *opaque){
    auto *scan=static_cast<ModuleScan *>(opaque);
    if(static_cast<uintptr_t>(info->dlpi_addr)!=scan->base)return 0;
    scan->found=true;scan->path=info->dlpi_name?info->dlpi_name:"";
    for(ElfW(Half)i=0;i<info->dlpi_phnum;++i){const auto &ph=info->dlpi_phdr[i];if(ph.p_type!=PT_LOAD||(ph.p_flags&PF_X)==0||ph.p_memsz<kFingerprintSize)continue;uintptr_t start=static_cast<uintptr_t>(info->dlpi_addr)+static_cast<uintptr_t>(ph.p_vaddr);if(start>UINTPTR_MAX-static_cast<uintptr_t>(ph.p_memsz))continue;scan->executable.push_back({start,start+static_cast<uintptr_t>(ph.p_memsz)});}
    return 1;
}
static bool executable_contains(const ModuleScan &scan,uintptr_t address,size_t size){if(address>UINTPTR_MAX-size)return false;for(const auto &range:scan.executable)if(address>=range.begin&&address+size<=range.end)return true;return false;}
static std::string hex_bytes(const uint8_t *p,size_t n){static const char *digits="0123456789abcdef";std::string result;result.reserve(n*2);for(size_t i=0;i<n;++i){result.push_back(digits[p[i]>>4]);result.push_back(digits[p[i]&15]);}return result;}
static bool add_overflow(uintptr_t a,uintptr_t b,uintptr_t &out){if(a>UINTPTR_MAX-b)return true;out=a+b;return false;}
static const HookSpec *find_spec(uint32_t id){for(const auto &hook:kOriginalHooks)if(hook.id==id)return &hook;return nullptr;}
static bool initialize_hook_engine(std::string *error){
    ASTRAE_LOGI("initializing ByteDance ShadowHook after managed readiness checks; direct-address-only profile skips private Linker/soinfo hooks; target ELF metadata is resolved via xDL; shadowhook debug=ON");
    std::call_once(g_shadowhook_once,[]{g_shadowhook_init_result.store(shadowhook_init(SHADOWHOOK_MODE_SHARED,true),std::memory_order_release);});
    const int result=g_shadowhook_init_result.load(std::memory_order_acquire);
    if(result!=0){const char *detail=shadowhook_to_errmsg(result);if(error)*error="ShadowHook initialization failed: "+std::to_string(result)+(detail?std::string(" (")+detail+")":std::string());ASTRAE_LOGE("ShadowHook initialization failed: code=%d detail=%s",result,detail?detail:"<null>");if(result==12)ASTRAE_LOGE("Unexpected ShadowHook code=12: this build should compile ASTRAE_SHADOWHOOK_DIRECT_ADDRESS_ONLY and skip sh_linker_init; verify the patched ShadowHook source and compile definitions.");return false;}
    const char *version=shadowhook_get_version();if(!version||std::strstr(version,"2.0.1")==nullptr){if(error)*error=std::string("unexpected ShadowHook runtime version: ")+(version?version:"<null>");ASTRAE_LOGE("unexpected ShadowHook version: %s",version?version:"<null>");return false;}
    if(error)error->clear();ASTRAE_LOGI("ShadowHook initialized successfully: %s; mode=shared; linker_monitor=disabled; direct_address_metadata=xDL",version);return true;
}
static void *resolve_target_module(void **out_handle,ModuleScan *out_scan,std::string *error){
    void *handle=dlopen("libil2cpp.so",RTLD_NOW|RTLD_NOLOAD);
    if(!handle){if(error)*error="libil2cpp.so is not loaded";return nullptr;}
    void *symbol=dlsym(handle,"il2cpp_thread_attach");
    if(!symbol){if(error)*error="il2cpp_thread_attach was not found";dlclose(handle);return nullptr;}
    Dl_info info{};
    if(!dladdr(symbol,&info)||!info.dli_fbase){if(error)*error="cannot resolve libil2cpp base address";dlclose(handle);return nullptr;}
    ModuleScan scan;scan.base=reinterpret_cast<uintptr_t>(info.dli_fbase);dl_iterate_phdr(scan_module,&scan);
    if(!scan.found){if(error)*error="dl_iterate_phdr could not find libil2cpp";dlclose(handle);return nullptr;}
    *out_handle=handle;*out_scan=std::move(scan);return info.dli_fbase;
}
static bool install_one(const HookSpec &spec,void *replacement,InstallMode mode,std::string *error){
    std::lock_guard<std::mutex> lock(g_patch_mutex);g_install_attempted.store(true,std::memory_order_release);
    if(spec.id>=kHookLimit){if(error)*error="hook id exceeds internal state table";return false;}
    if(!replacement){if(error)*error="replacement callback is null";ASTRAE_LOGE("Hook #%u rejected: replacement callback is null",spec.id);return false;}
    if(g_installed[spec.id].load(std::memory_order_acquire)){bool same_mode=g_modes[spec.id].load(std::memory_order_acquire)==static_cast<uint8_t>(mode);bool same_replacement=g_replacements[spec.id].load(std::memory_order_acquire)==replacement;if(same_mode&&same_replacement)return true;if(error)*error="hook id is already installed with a different mode or replacement";return false;}
    const bool wants_trampoline=mode==InstallMode::Trampoline;
    if(spec.has_trampoline!=wants_trampoline){const std::string message=std::string(spec.replacement_symbol)+(spec.has_trampoline?" requires an original-function trampoline":" is a direct-replacement hook");if(error)*error=message;ASTRAE_LOGE("Hook #%u mode mismatch: %s",spec.id,message.c_str());return false;}
    Dl_info replacement_info{},self_info{};
    if(!dladdr(replacement,&replacement_info)||!replacement_info.dli_fbase||!dladdr(reinterpret_cast<void *>(&install_one),&self_info)||replacement_info.dli_fbase!=self_info.dli_fbase){if(error)*error="replacement callback must reside in the same loaded ELF image as the hook engine";ASTRAE_LOGE("Hook #%u replacement callback is not in the same loaded ELF image",spec.id);return false;}
    void *handle=nullptr;ModuleScan scan;void *base_ptr=resolve_target_module(&handle,&scan,error);if(!base_ptr){ASTRAE_LOGE("Hook #%u cannot resolve target libil2cpp module: %s",spec.id,error?error->c_str():"unknown");return false;}
    uintptr_t target=0;if(add_overflow(reinterpret_cast<uintptr_t>(base_ptr),spec.target_offset,target)||!executable_contains(scan,target,kFingerprintSize)){if(error)*error="hook target is outside executable libil2cpp ranges";ASTRAE_LOGE("Hook #%u target RVA 0x%llx is not in an executable PT_LOAD range (base=%p)",spec.id,static_cast<unsigned long long>(spec.target_offset),base_ptr);dlclose(handle);return false;}
    const auto *bytes=reinterpret_cast<const uint8_t *>(target);
    if(std::memcmp(bytes,spec.expected,kFingerprintSize)!=0){if(error)*error=std::string(spec.replacement_symbol)+" target fingerprint mismatch; refusing to patch this game build (actual="+hex_bytes(bytes,kFingerprintSize)+")";ASTRAE_LOGE("Hook #%u fingerprint mismatch; expected=%s actual=%s; refusing to patch",spec.id,hex_bytes(spec.expected,kFingerprintSize).c_str(),hex_bytes(bytes,kFingerprintSize).c_str());dlclose(handle);return false;}
    ASTRAE_LOGI("Hook #%u target validated: %s base=%p target=%p RVA=0x%llx; bytes=%s",spec.id,spec.replacement_symbol,base_ptr,reinterpret_cast<void*>(target),static_cast<unsigned long long>(spec.target_offset),hex_bytes(bytes,kFingerprintSize).c_str());
    if(!initialize_hook_engine(error)){dlclose(handle);return false;}
    void *original=nullptr;
    void *stub=shadowhook_hook_func_addr_2(reinterpret_cast<void *>(target),replacement,&original,SHADOWHOOK_HOOK_WITH_SHARED_MODE);
    if(!stub){const int hook_error=shadowhook_get_errno();const char *detail=shadowhook_to_errmsg(hook_error);if(error)*error=std::string(spec.replacement_symbol)+" ShadowHook operation failed: "+std::to_string(hook_error)+(detail?std::string(" (")+detail+")":std::string());ASTRAE_LOGE("Hook #%u ShadowHook install failed: code=%d detail=%s",spec.id,hook_error,detail?detail:"<null>");dlclose(handle);return false;}
    if(wants_trampoline&&!original){shadowhook_unhook(stub);if(error)*error=std::string(spec.replacement_symbol)+" ShadowHook succeeded without returning the required original trampoline";dlclose(handle);return false;}
    g_stubs[spec.id].store(stub,std::memory_order_release);g_replacements[spec.id].store(replacement,std::memory_order_release);g_modes[spec.id].store(static_cast<uint8_t>(mode),std::memory_order_release);g_installed[spec.id].store(true,std::memory_order_release);
    if(spec.id==5)g_local_request_installed.store(true,std::memory_order_release);if(spec.id==3)g_touch_installed.store(true,std::memory_order_release);
    dlclose(handle);if(error)error->clear();ASTRAE_LOGI("Hook #%u installed successfully: symbol=%s mode=%s replacement=%p original=%p stub=%p",spec.id,spec.replacement_symbol,wants_trampoline?"trampoline":"direct-replacement",replacement,original,stub);return true;
}
#define ASTRAE_PROXY_BEGIN(id) g_calls[id].fetch_add(1,std::memory_order_relaxed)
static void local_request_replacement(void *network,void *,void *,void *request,int,void *){
    SHADOWHOOK_STACK_SCOPE();
    const uint64_t call=g_calls[5].fetch_add(1,std::memory_order_relaxed)+1;ASTRAE_LOGI("Hook #5 local_request entered: call=%llu network=%p request=%p",static_cast<unsigned long long>(call),network,request);
    try{
        if(network&&request&&AstraeOffline_HandleManagedRequest(network,request)){g_managed_request_seen.store(true,std::memory_order_release);return;}
        static constexpr char kFallbackError[]={"{\"NotiFlag\":0,\"RefreshProtocols\":[],\"ErrorType\":0,\"Reason\":\"Offline handler could not process this request\"}"};
        if(network&&AstraeOffline_RespondManaged(network,"ErrorPacket",kFallbackError)){g_managed_request_seen.store(true,std::memory_order_release);__android_log_write(ANDROID_LOG_WARN,"AstraeOffline","request was not mapped; returned ErrorPacket instead of dropping it");return;}
        __android_log_write(ANDROID_LOG_ERROR,"AstraeOffline","request handling and ErrorPacket fallback both failed");
    }catch(const std::exception &exception){ASTRAE_LOGE("local_request proxy exception: %s",exception.what());}
    catch(...){ASTRAE_LOGE("local_request proxy unknown exception");}
}
static void touch_replacement(void *controller,void *method){
    SHADOWHOOK_STACK_SCOPE();
    const uint64_t call=g_calls[3].fetch_add(1,std::memory_order_relaxed)+1;ASTRAE_LOGI("Hook #3 touch entered: call=%llu controller=%p method=%p",static_cast<unsigned long long>(call),controller,method);
    try{std::string error;if(!astrae::lobby::touch_enter_lobby(controller,&error))ASTRAE_LOGE("touch dispatcher rejected call %llu: %s",static_cast<unsigned long long>(call),error.c_str());else ASTRAE_LOGI("touch dispatcher completed call %llu",static_cast<unsigned long long>(call));}
    catch(const std::exception &exception){ASTRAE_LOGE("touch proxy exception: %s",exception.what());}
    catch(...){ASTRAE_LOGE("touch proxy unknown exception");}
}
static void text_locale_hook(void *a,int b,int c,void *d){SHADOWHOOK_STACK_SCOPE();ASTRAE_PROXY_BEGIN(9);SHADOWHOOK_CALL_PREV(text_locale_hook,a,b,c,d);}
static void banner_click_hook(void *a,int b,void *c){SHADOWHOOK_STACK_SCOPE();ASTRAE_PROXY_BEGIN(12);SHADOWHOOK_CALL_PREV(banner_click_hook,a,b,c);}
static void ui_input_tick_hook(void *a,void *b){SHADOWHOOK_STACK_SCOPE();ASTRAE_PROXY_BEGIN(15);SHADOWHOOK_CALL_PREV(ui_input_tick_hook,a,b);}
static void *unlock_notifications_hook(void *a,int b,int c,int d,void *e){SHADOWHOOK_STACK_SCOPE();ASTRAE_PROXY_BEGIN(16);return SHADOWHOOK_CALL_PREV(unlock_notifications_hook,a,b,c,d,e);}
static void unlock_popup_hidden_hook(void *a,void *b){SHADOWHOOK_STACK_SCOPE();ASTRAE_PROXY_BEGIN(21);SHADOWHOOK_CALL_PREV(unlock_popup_hidden_hook,a,b);}
static void unlock_popup_step_hook(void *a,void *b){SHADOWHOOK_STACK_SCOPE();ASTRAE_PROXY_BEGIN(22);SHADOWHOOK_CALL_PREV(unlock_popup_step_hook,a,b);}
static void action_pack_shown_hook(void *a,int b,void *c){SHADOWHOOK_STACK_SCOPE();ASTRAE_PROXY_BEGIN(23);SHADOWHOOK_CALL_PREV(action_pack_shown_hook,a,b,c);}
static void action_pack_hidden_hook(void *a,void *b){SHADOWHOOK_STACK_SCOPE();ASTRAE_PROXY_BEGIN(24);SHADOWHOOK_CALL_PREV(action_pack_hidden_hook,a,b);}
static void action_daily_shown_hook(void *a,void *b){SHADOWHOOK_STACK_SCOPE();ASTRAE_PROXY_BEGIN(25);SHADOWHOOK_CALL_PREV(action_daily_shown_hook,a,b);}
static void action_daily_hidden_hook(void *a,void *b){SHADOWHOOK_STACK_SCOPE();ASTRAE_PROXY_BEGIN(26);SHADOWHOOK_CALL_PREV(action_daily_hidden_hook,a,b);}
static void title_loop_step_hook(void *a,void *b){SHADOWHOOK_STACK_SCOPE();ASTRAE_PROXY_BEGIN(29);SHADOWHOOK_CALL_PREV(title_loop_step_hook,a,b);}
static void first_day_step_hook(void *a,void *b){SHADOWHOOK_STACK_SCOPE();ASTRAE_PROXY_BEGIN(30);SHADOWHOOK_CALL_PREV(first_day_step_hook,a,b);}
static void first_day_load_hook(void *a,void *b,void *c,void *d){SHADOWHOOK_STACK_SCOPE();ASTRAE_PROXY_BEGIN(31);SHADOWHOOK_CALL_PREV(first_day_load_hook,a,b,c,d);}
static void first_day_destroy_hook(void *a,void *b){SHADOWHOOK_STACK_SCOPE();ASTRAE_PROXY_BEGIN(32);SHADOWHOOK_CALL_PREV(first_day_destroy_hook,a,b);}
static void first_day_button_click_hook(void *a,void *b){SHADOWHOOK_STACK_SCOPE();ASTRAE_PROXY_BEGIN(33);SHADOWHOOK_CALL_PREV(first_day_button_click_hook,a,b);}
static void first_day_root_update_hook(void *a,void *b){SHADOWHOOK_STACK_SCOPE();ASTRAE_PROXY_BEGIN(34);SHADOWHOOK_CALL_PREV(first_day_root_update_hook,a,b);}
static void native_facility_click_hook(void *a,void *b,void *c){SHADOWHOOK_STACK_SCOPE();ASTRAE_PROXY_BEGIN(35);SHADOWHOOK_CALL_PREV(native_facility_click_hook,a,b,c);}
static void item_selection_initialize_hook(void *a,void *b,int c,int d,void *e){SHADOWHOOK_STACK_SCOPE();ASTRAE_PROXY_BEGIN(38);SHADOWHOOK_CALL_PREV(item_selection_initialize_hook,a,b,c,d,e);}
static void signed_exchange_refresh_hook(void *a,void *b,int c,void *d){SHADOWHOOK_STACK_SCOPE();ASTRAE_PROXY_BEGIN(39);SHADOWHOOK_CALL_PREV(signed_exchange_refresh_hook,a,b,c,d);}
#undef ASTRAE_PROXY_BEGIN
struct UiBinding {uint32_t id;void *replacement;};
static const UiBinding kUiBindings[]={
    {9,reinterpret_cast<void *>(&text_locale_hook)},{12,reinterpret_cast<void *>(&banner_click_hook)},{15,reinterpret_cast<void *>(&ui_input_tick_hook)},{16,reinterpret_cast<void *>(&unlock_notifications_hook)},
    {21,reinterpret_cast<void *>(&unlock_popup_hidden_hook)},{22,reinterpret_cast<void *>(&unlock_popup_step_hook)},{23,reinterpret_cast<void *>(&action_pack_shown_hook)},{24,reinterpret_cast<void *>(&action_pack_hidden_hook)},
    {25,reinterpret_cast<void *>(&action_daily_shown_hook)},{26,reinterpret_cast<void *>(&action_daily_hidden_hook)},{29,reinterpret_cast<void *>(&title_loop_step_hook)},{30,reinterpret_cast<void *>(&first_day_step_hook)},
    {31,reinterpret_cast<void *>(&first_day_load_hook)},{32,reinterpret_cast<void *>(&first_day_destroy_hook)},{33,reinterpret_cast<void *>(&first_day_button_click_hook)},{34,reinterpret_cast<void *>(&first_day_root_update_hook)},
    {35,reinterpret_cast<void *>(&native_facility_click_hook)},{38,reinterpret_cast<void *>(&item_selection_initialize_hook)},{39,reinterpret_cast<void *>(&signed_exchange_refresh_hook)}
};
}
#endif

bool install_local_request_hook(std::string *error){
#if defined(__ANDROID__) && defined(__aarch64__) && defined(ASTRAE_USE_SHADOWHOOK) && defined(ASTRAE_ENABLE_LOCAL_REQUEST_DIRECT_HOOK)
    const HookSpec *spec=nullptr;for(const auto &hook:kOriginalHooks)if(hook.id==5&&std::strcmp(hook.replacement_symbol,"local_request")==0){spec=&hook;break;}
    if(!spec){if(error)*error="original hook manifest does not contain hook #5 local_request";return false;}if(spec->has_trampoline){if(error)*error="manifest mismatch: local_request is expected to be a direct replacement";return false;}
    return install_direct_replacement_hook(5,reinterpret_cast<void *>(&local_request_replacement),error);
#elif defined(__ANDROID__) && defined(__aarch64__)
    if(error)*error="Hook #5 local_request is disabled in this build";return false;
#else
    if(error)*error="automatic local_request patching is supported only on Android arm64";return false;
#endif
}
bool install_touch_hook(std::string *error){
#if defined(__ANDROID__) && defined(__aarch64__) && defined(ASTRAE_USE_SHADOWHOOK)
    const HookSpec *spec=find_spec(3);if(!spec||std::strcmp(spec->replacement_symbol,"touch")!=0){if(error)*error="original hook manifest does not contain Hook #3 touch";return false;}if(spec->has_trampoline){if(error)*error="manifest mismatch: original touch must be a direct replacement";return false;}
    return install_direct_replacement_hook(3,reinterpret_cast<void *>(&touch_replacement),error);
#else
    if(error)*error="touch direct replacement requires Android arm64 and ShadowHook";return false;
#endif
}
bool install_direct_replacement_hook(uint32_t hook_id,void *replacement,std::string *error){
#if defined(__ANDROID__) && defined(__aarch64__) && defined(ASTRAE_USE_SHADOWHOOK)
    const HookSpec *spec=find_spec(hook_id);if(!spec){if(error)*error="hook id is absent from original manifest";return false;}if(spec->has_trampoline){if(error)*error=std::string(spec->replacement_symbol)+" is trampoline-backed, not a direct replacement";return false;}return install_one(*spec,replacement,InstallMode::DirectReplacement,error);
#else
    (void)hook_id;(void)replacement;if(error)*error="direct replacement hooks require Android arm64 and ShadowHook";return false;
#endif
}
size_t install_ui_passthrough_hooks(std::string *error){
#if defined(__ANDROID__) && defined(__aarch64__) && defined(ASTRAE_USE_SHADOWHOOK)
    if(error)error->clear();size_t installed=0;for(const auto &binding:kUiBindings){const HookSpec *spec=find_spec(binding.id);if(!spec)continue;std::string local_error;if(install_one(*spec,binding.replacement,InstallMode::Trampoline,&local_error))++installed;else if(error&&error->empty())*error=std::string(spec->replacement_symbol)+": "+local_error;}g_ui_installed.store(installed,std::memory_order_release);return installed;
#else
    if(error)*error="UI passthrough hooks require Android arm64 and ShadowHook";return 0;
#endif
}
uint64_t touch_hook_call_count(){
#if defined(__ANDROID__) && defined(__aarch64__) && defined(ASTRAE_USE_SHADOWHOOK)
    return g_calls[3].load(std::memory_order_acquire);
#else
    return 0;
#endif
}
bool touch_hook_installed(){
#if defined(__ANDROID__) && defined(__aarch64__) && defined(ASTRAE_USE_SHADOWHOOK)
    return g_touch_installed.load(std::memory_order_acquire);
#else
    return false;
#endif
}
bool local_request_hook_installed(){
#if defined(__ANDROID__) && defined(__aarch64__) && defined(ASTRAE_USE_SHADOWHOOK)
    return g_local_request_installed.load(std::memory_order_acquire);
#else
    return false;
#endif
}
bool managed_request_seen(){
#if defined(__ANDROID__) && defined(__aarch64__) && defined(ASTRAE_USE_SHADOWHOOK)
    return g_managed_request_seen.load(std::memory_order_acquire);
#else
    return false;
#endif
}
std::string validate_original_hooks_json(){
    json::Value out=json::Value::object();out["platformSupported"]=
#if defined(__ANDROID__) && defined(__aarch64__) && defined(ASTRAE_USE_SHADOWHOOK)
    true;
#else
    false;
#endif
    out["hookEngine"]="ByteDance ShadowHook v2.0.1";
    out["installAttempted"]=
#if defined(__ANDROID__) && defined(__aarch64__) && defined(ASTRAE_USE_SHADOWHOOK)
    g_install_attempted.load(std::memory_order_acquire);
#else
    false;
#endif
    out["totalHooks"]=static_cast<uint64_t>(kOriginalHookCount);out["uiPassthroughInstalled"]=
#if defined(__ANDROID__) && defined(__aarch64__) && defined(ASTRAE_USE_SHADOWHOOK)
    static_cast<uint64_t>(g_ui_installed.load(std::memory_order_acquire));
#else
    uint64_t(0);
#endif
    out["touchHookInstalled"]=
#if defined(__ANDROID__) && defined(__aarch64__) && defined(ASTRAE_USE_SHADOWHOOK)
    g_touch_installed.load(std::memory_order_acquire);
#else
    false;
#endif
    out["localRequestHookInstalled"]=
#if defined(__ANDROID__) && defined(__aarch64__) && defined(ASTRAE_USE_SHADOWHOOK)
    g_local_request_installed.load(std::memory_order_acquire);
#else
    false;
#endif
    out["expectedTrampolines"]=uint64_t(0);out["expectedDirectHooks"]=uint64_t(0);for(const auto &h:kOriginalHooks)if(h.has_trampoline)out["expectedTrampolines"]=out["expectedTrampolines"].as_u64()+1;else out["expectedDirectHooks"]=out["expectedDirectHooks"].as_u64()+1;
#if defined(__ANDROID__) && defined(__aarch64__) && defined(ASTRAE_USE_SHADOWHOOK)
    void *handle=nullptr;ModuleScan scan;void *base_ptr=resolve_target_module(&handle,&scan,nullptr);if(!base_ptr){out["moduleLoaded"]=false;out["error"]="libil2cpp.so or il2cpp_thread_attach is unavailable";return json::dump(out,true);}uintptr_t base=reinterpret_cast<uintptr_t>(base_ptr);json::Value rows=json::Value::array();uint64_t matched=0,mismatched=0,unreadable=0,installed=0,installed_trampoline=0,installed_direct=0;
    for(const auto &h:kOriginalHooks){json::Value row=json::Value::object();row["id"]=h.id;row["name"]=h.replacement_symbol;row["mode"]=h.has_trampoline?"trampoline":"direct_replacement";row["offset"]="0x"+[] (uintptr_t v){char b[2*sizeof(uintptr_t)+1];static const char *d="0123456789abcdef";char *p=b+sizeof(b);*--p='\0';do{*--p=d[v&15];v>>=4;}while(v);return std::string(p);}(h.target_offset);uintptr_t address=0;
        if(add_overflow(base,h.target_offset,address)||!executable_contains(scan,address,kFingerprintSize)){row["status"]="not_executable_or_unmapped";++unreadable;}
        else{const auto *actual=reinterpret_cast<const uint8_t *>(address);bool is_installed=h.id<kHookLimit&&g_installed[h.id].load(std::memory_order_acquire);row["actualBytes"]=hex_bytes(actual,kFingerprintSize);row["expectedBytes"]=hex_bytes(h.expected,kFingerprintSize);if(is_installed){row["status"]=h.has_trampoline?"installed_trampoline":"installed_direct_replacement";++installed;if(h.has_trampoline)++installed_trampoline;else ++installed_direct;}else if(std::memcmp(actual,h.expected,kFingerprintSize)==0){row["status"]="matched";++matched;}else{row["status"]="mismatch";++mismatched;}}
        if(h.id<kHookLimit){row["calls"]=g_calls[h.id].load(std::memory_order_relaxed);row["stub"]=reinterpret_cast<uintptr_t>(g_stubs[h.id].load(std::memory_order_acquire));}rows.push_back(std::move(row));
    }
    dlclose(handle);out["hooks"]=std::move(rows);out["checked"]=matched+mismatched+installed;out["matched"]=matched;out["installed"]=installed;out["installedTrampolineHooks"]=installed_trampoline;out["installedDirectHooks"]=installed_direct;out["mismatched"]=mismatched;out["unreadable"]=unreadable;out["runtimeHookInstalled"]=installed>0;out["fingerprintCompatible"]=(mismatched==0&&unreadable==0);
#else
    out["moduleLoaded"]=false;out["matched"]=uint64_t(0);out["mismatched"]=uint64_t(0);out["unreadable"]=static_cast<uint64_t>(kOriginalHookCount);out["fingerprintCompatible"]=false;out["error"]="Android arm64 ShadowHook runtime is unavailable on this host platform";
#endif
    out["note"]="Hook installation is opt-in. Hook #3 waits for IL2CPP and _AT.UI.UILoginPageController readiness plus an 8-second settle delay; patching is delegated to ByteDance ShadowHook v2.0.1 in direct-address-only shared mode; the private Linker monitor is disabled. Hook #3 is still a minimal touch replacement; Hook #5 is disabled by default; UI passthrough hooks are not installed automatically.";return json::dump(out,true);
}
}
