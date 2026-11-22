#include "hook_runtime.hpp"
#include "original_hook_manifest.hpp"
#include "arm64_hook_engine.hpp"
#include "json.hpp"
#include "astrae_backend.h"
#include "lobby_restore.hpp"
#include <atomic>
#include <array>
#include <cstdint>
#include <cstring>
#include <limits>
#include <mutex>
#include <string>
#include <vector>
#if defined(__ANDROID__)
#include <cerrno>
#include <cstdio>
#include <dlfcn.h>
#include <link.h>
#include <sys/mman.h>
#include <unistd.h>
#include <android/log.h>
#endif

namespace astrae::hooks {
#if defined(__ANDROID__) && defined(__aarch64__)
namespace {
constexpr size_t kFingerprintSize=16;
constexpr uint32_t kHookLimit=40;
constexpr size_t kRelayOffset=64;
constexpr size_t kTrampolineOffset=0;
struct ExecRange {uintptr_t begin=0,end=0;int protection=PROT_READ|PROT_EXEC;};
struct ModuleScan {uintptr_t base=0;bool found=false;std::string path;std::vector<ExecRange> executable;};
struct HookAllocation {void *memory=nullptr;size_t size=0;void *relay=nullptr;void *trampoline=nullptr;uint32_t patch_instruction=0;};
enum class InstallMode:uint8_t {Trampoline=1,DirectReplacement=2};
static std::mutex g_patch_mutex;
static std::array<std::atomic<void *>,kHookLimit> g_original{};
static std::array<std::atomic<void *>,kHookLimit> g_allocations{};
static std::array<std::atomic<void *>,kHookLimit> g_relays{};
static std::array<std::atomic<void *>,kHookLimit> g_replacements{};
static std::array<std::atomic<uint32_t>,kHookLimit> g_patch_instructions{};
static std::array<std::atomic<uint32_t>,kHookLimit> g_original_instructions{};
static std::array<std::atomic<uint8_t>,kHookLimit> g_modes{};
static std::array<std::atomic<bool>,kHookLimit> g_installed{};
static std::array<std::atomic<uint64_t>,kHookLimit> g_calls{};
static std::atomic<bool> g_install_attempted{false};
static std::atomic<size_t> g_ui_installed{0};
static std::atomic<bool> g_local_request_installed{false};
static std::atomic<bool> g_touch_installed{false};
static std::atomic<bool> g_managed_request_seen{false};

static int segment_protection(ElfW(Word) flags){int p=0;if(flags&PF_R)p|=PROT_READ;if(flags&PF_W)p|=PROT_WRITE;if(flags&PF_X)p|=PROT_EXEC;return p;}
static int scan_module(dl_phdr_info *info,size_t,void *opaque){auto *scan=static_cast<ModuleScan *>(opaque);if(static_cast<uintptr_t>(info->dlpi_addr)!=scan->base)return 0;scan->found=true;scan->path=info->dlpi_name?info->dlpi_name:"";for(ElfW(Half)i=0;i<info->dlpi_phnum;++i){const auto &ph=info->dlpi_phdr[i];if(ph.p_type!=PT_LOAD||(ph.p_flags&PF_X)==0||ph.p_memsz<kFingerprintSize)continue;uintptr_t start=static_cast<uintptr_t>(info->dlpi_addr)+static_cast<uintptr_t>(ph.p_vaddr);if(start>std::numeric_limits<uintptr_t>::max()-static_cast<uintptr_t>(ph.p_memsz))continue;scan->executable.push_back({start,start+static_cast<uintptr_t>(ph.p_memsz),segment_protection(ph.p_flags)});}return 1;}
static const ExecRange *find_executable_range(const ModuleScan &scan,uintptr_t address,size_t size){if(address>std::numeric_limits<uintptr_t>::max()-size)return nullptr;for(const auto &r:scan.executable)if(address>=r.begin&&address+size<=r.end)return &r;return nullptr;}
static bool executable_contains(const ModuleScan &scan,uintptr_t address,size_t size){return find_executable_range(scan,address,size)!=nullptr;}
static std::string hex_bytes(const uint8_t *p,size_t n){static const char *d="0123456789abcdef";std::string s;s.reserve(n*2);for(size_t i=0;i<n;++i){s.push_back(d[p[i]>>4]);s.push_back(d[p[i]&15]);}return s;}
static bool add_overflow(uintptr_t a,uintptr_t b,uintptr_t &out){if(a>std::numeric_limits<uintptr_t>::max()-b)return true;out=a+b;return false;}
static bool branch_reaches(uintptr_t from,uintptr_t to){uint32_t ignored=0;return astrae::hooks::arm64::encode_branch(from,to,ignored,nullptr);}
static void write_absolute_branch(uint8_t *where,uintptr_t destination){const uint32_t ldr_x16_literal=0x58000050u;const uint32_t br_x16=0xd61f0200u;std::memcpy(where,&ldr_x16_literal,sizeof(ldr_x16_literal));std::memcpy(where+4,&br_x16,sizeof(br_x16));std::memcpy(where+8,&destination,sizeof(destination));}
static void flush_instruction_cache(void *begin,size_t length){auto *first=static_cast<char *>(begin);__builtin___clear_cache(first,first+length);}
static void *map_near_target(uintptr_t target,size_t length,size_t relay_offset,std::string *error){long raw_page=sysconf(_SC_PAGESIZE);if(raw_page<=0){if(error)*error="cannot query Android page size";return nullptr;}const uintptr_t page=static_cast<uintptr_t>(raw_page);if((page&(page-1))!=0){if(error)*error="Android page size is not a power of two";return nullptr;}uintptr_t center=target&~(page-1);const uintptr_t step=1024u*1024u;bool fixed_unsupported=false;
#ifdef MAP_FIXED_NOREPLACE
    for(uintptr_t distance=0;distance<=static_cast<uintptr_t>(120u*1024u*1024u)&&!fixed_unsupported;distance+=step){for(int sign=0;sign<(distance?2:1);++sign){uintptr_t candidate=center;if(sign==0){if(candidate>std::numeric_limits<uintptr_t>::max()-distance)continue;candidate+=distance;}else{if(candidate<distance)continue;candidate-=distance;}uintptr_t branch_pc=0;if(add_overflow(candidate,relay_offset,branch_pc)||!branch_reaches(target,branch_pc))continue;void *mapped=mmap(reinterpret_cast<void *>(candidate),length,PROT_READ|PROT_WRITE,MAP_PRIVATE|MAP_ANONYMOUS|MAP_FIXED_NOREPLACE,-1,0);if(mapped==MAP_FAILED){if(errno==EINVAL)fixed_unsupported=true;continue;}uintptr_t mapped_branch=0;if(reinterpret_cast<uintptr_t>(mapped)==candidate&&!add_overflow(reinterpret_cast<uintptr_t>(mapped),relay_offset,mapped_branch)&&branch_reaches(target,mapped_branch))return mapped;munmap(mapped,length);}}
#endif
    // Older Android kernels may not implement MAP_FIXED_NOREPLACE. Treat mmap
    // addresses as hints and accept only a returned mapping within B imm26.
    for(uintptr_t distance=0;distance<=static_cast<uintptr_t>(120u*1024u*1024u);distance+=step){for(int sign=0;sign<(distance?2:1);++sign){uintptr_t hint=center;if(sign==0){if(hint>std::numeric_limits<uintptr_t>::max()-distance)continue;hint+=distance;}else{if(hint<distance)continue;hint-=distance;}void *mapped=mmap(reinterpret_cast<void *>(hint),length,PROT_READ|PROT_WRITE,MAP_PRIVATE|MAP_ANONYMOUS,-1,0);if(mapped==MAP_FAILED)continue;uintptr_t mapped_branch=0;if(!add_overflow(reinterpret_cast<uintptr_t>(mapped),relay_offset,mapped_branch)&&branch_reaches(target,mapped_branch))return mapped;munmap(mapped,length);}}
    if(error)*error="unable to allocate an executable relay within the AArch64 B +/-128MiB range";
    return nullptr;}
static bool make_hook_allocation(uintptr_t target,void *replacement,const uint8_t *original_bytes,bool with_trampoline,HookAllocation &allocation,std::string *error){long raw_page=sysconf(_SC_PAGESIZE);if(raw_page<=0){if(error)*error="cannot query page size for hook relay";return false;}size_t page=static_cast<size_t>(raw_page);size_t relay_offset=with_trampoline?kRelayOffset:0;void *memory=map_near_target(target,page,relay_offset,error);if(!memory)return false;auto *code=static_cast<uint8_t *>(memory);void *trampoline=nullptr;if(with_trampoline){uint32_t original_instruction=0,relocated_instruction=0;std::memcpy(&original_instruction,original_bytes,sizeof(original_instruction));std::string relocation_error;if(!astrae::hooks::arm64::relocate_instruction(original_instruction,target,reinterpret_cast<uintptr_t>(code)+kTrampolineOffset,relocated_instruction,&relocation_error)){munmap(memory,page);if(error)*error="cannot relocate first displaced instruction: "+relocation_error;return false;}std::memcpy(code+kTrampolineOffset,&relocated_instruction,sizeof(relocated_instruction));write_absolute_branch(code+kTrampolineOffset+4,target+4);trampoline=code+kTrampolineOffset;}
    void *relay=code+relay_offset;write_absolute_branch(static_cast<uint8_t *>(relay),reinterpret_cast<uintptr_t>(replacement));flush_instruction_cache(memory,with_trampoline?kRelayOffset+16:16);if(mprotect(memory,page,PROT_READ|PROT_EXEC)!=0){int saved=errno;munmap(memory,page);if(error)*error="mprotect(RX) failed for hook relay: errno="+std::to_string(saved);return false;}uint32_t patch=0;if(!astrae::hooks::arm64::encode_branch(target,reinterpret_cast<uintptr_t>(relay),patch,error)){munmap(memory,page);return false;}allocation.memory=memory;allocation.size=page;allocation.relay=relay;allocation.trampoline=trampoline;allocation.patch_instruction=patch;return true;}
static bool patch_target_atomically(uintptr_t target,uint32_t original_instruction,uint32_t patch_instruction,int original_protection,std::string *error){long raw_page=sysconf(_SC_PAGESIZE);if(raw_page<=0){if(error)*error="cannot query page size for target patch";return false;}uintptr_t page=static_cast<uintptr_t>(raw_page);if((page&(page-1))!=0||(target&3u)!=0){if(error)*error="target patch requires power-of-two pages and 4-byte aligned AArch64 instruction";return false;}uintptr_t page_begin=target&~(page-1);int writable_protection=original_protection|PROT_READ|PROT_WRITE;if(mprotect(reinterpret_cast<void *>(page_begin),static_cast<size_t>(page),writable_protection)!=0){if(error)*error="mprotect(RW+X) failed for target page: errno="+std::to_string(errno);return false;}uint32_t current=0;std::memcpy(&current,reinterpret_cast<const void *>(target),sizeof(current));if(current!=original_instruction){mprotect(reinterpret_cast<void *>(page_begin),static_cast<size_t>(page),original_protection);if(error)*error="target first instruction changed after fingerprint validation; patch aborted";return false;}
    // AArch64 aligned 32-bit stores are single-copy atomic. Only one target
    // instruction is replaced; the remaining original prologue stays intact.
    __atomic_store_n(reinterpret_cast<uint32_t *>(target),patch_instruction,__ATOMIC_RELEASE);flush_instruction_cache(reinterpret_cast<void *>(target),sizeof(uint32_t));if(mprotect(reinterpret_cast<void *>(page_begin),static_cast<size_t>(page),original_protection)!=0){int restore_error=errno;__atomic_store_n(reinterpret_cast<uint32_t *>(target),original_instruction,__ATOMIC_RELEASE);flush_instruction_cache(reinterpret_cast<void *>(target),sizeof(uint32_t));bool rolled_back=mprotect(reinterpret_cast<void *>(page_begin),static_cast<size_t>(page),original_protection)==0;if(error)*error="target page protection restore failed after patch; original instruction rollback "+std::string(rolled_back?"completed":"attempted but protection restore still failed")+"; errno="+std::to_string(restore_error);return false;}return true;}
static void *resolve_target_module(void **out_handle,ModuleScan *out_scan,std::string *error){void *handle=dlopen("libil2cpp.so",RTLD_NOW|RTLD_NOLOAD);if(!handle){if(error)*error="libil2cpp.so is not loaded";return nullptr;}void *symbol=dlsym(handle,"il2cpp_thread_attach");if(!symbol){if(error)*error="il2cpp_thread_attach was not found";dlclose(handle);return nullptr;}Dl_info info{};if(!dladdr(symbol,&info)||!info.dli_fbase){if(error)*error="cannot resolve libil2cpp base address";dlclose(handle);return nullptr;}ModuleScan scan;scan.base=reinterpret_cast<uintptr_t>(info.dli_fbase);dl_iterate_phdr(scan_module,&scan);if(!scan.found){if(error)*error="dl_iterate_phdr could not find libil2cpp";dlclose(handle);return nullptr;}*out_handle=handle;*out_scan=std::move(scan);return info.dli_fbase;}
static const HookSpec *find_spec(uint32_t id){for(const auto &hook:kOriginalHooks)if(hook.id==id)return &hook;return nullptr;}
static bool install_one(const HookSpec &spec,void *replacement,InstallMode mode,std::string *error){std::lock_guard<std::mutex> lock(g_patch_mutex);g_install_attempted.store(true,std::memory_order_release);if(spec.id>=kHookLimit){if(error)*error="hook id exceeds internal state table";return false;}if(!replacement){if(error)*error="replacement callback is null";return false;}if(g_installed[spec.id].load(std::memory_order_acquire)){bool same_mode=g_modes[spec.id].load(std::memory_order_acquire)==static_cast<uint8_t>(mode);bool same_replacement=g_replacements[spec.id].load(std::memory_order_acquire)==replacement;if(same_mode&&same_replacement)return true;if(error)*error="hook id is already installed with a different mode or replacement";return false;}const bool wants_trampoline=mode==InstallMode::Trampoline;if(spec.has_trampoline!=wants_trampoline){if(error)*error=std::string(spec.replacement_symbol)+(spec.has_trampoline?" requires a trampoline binding":" is a direct-replacement hook and must not request an original trampoline");return false;}Dl_info replacement_info{},self_info{};if(!dladdr(replacement,&replacement_info)||!replacement_info.dli_fbase||!dladdr(reinterpret_cast<void *>(&install_one),&self_info)||replacement_info.dli_fbase!=self_info.dli_fbase){if(error)*error="replacement callback must reside in the same loaded ELF image as the hook engine";return false;}void *handle=nullptr;ModuleScan scan;void *base_ptr=resolve_target_module(&handle,&scan,error);if(!base_ptr)return false;uintptr_t base=reinterpret_cast<uintptr_t>(base_ptr),target=0;if(add_overflow(base,spec.target_offset,target)||!executable_contains(scan,target,kFingerprintSize)){if(error)*error="hook target is outside executable libil2cpp ranges";dlclose(handle);return false;}auto *bytes=reinterpret_cast<const uint8_t *>(target);if(std::memcmp(bytes,spec.expected,kFingerprintSize)!=0){if(error)*error=std::string(spec.replacement_symbol)+" target fingerprint mismatch; refusing to patch this game build (actual="+hex_bytes(bytes,kFingerprintSize)+")";dlclose(handle);return false;}const ExecRange *range=find_executable_range(scan,target,kFingerprintSize);uint32_t original_instruction=0;std::memcpy(&original_instruction,bytes,sizeof(original_instruction));HookAllocation allocation;if(!make_hook_allocation(target,replacement,bytes,wants_trampoline,allocation,error)){dlclose(handle);return false;}if(!patch_target_atomically(target,original_instruction,allocation.patch_instruction,range?range->protection:(PROT_READ|PROT_EXEC),error)){munmap(allocation.memory,allocation.size);dlclose(handle);return false;}g_original[spec.id].store(allocation.trampoline,std::memory_order_release);g_allocations[spec.id].store(allocation.memory,std::memory_order_release);g_relays[spec.id].store(allocation.relay,std::memory_order_release);g_replacements[spec.id].store(replacement,std::memory_order_release);g_patch_instructions[spec.id].store(allocation.patch_instruction,std::memory_order_release);g_original_instructions[spec.id].store(original_instruction,std::memory_order_release);g_modes[spec.id].store(static_cast<uint8_t>(mode),std::memory_order_release);g_installed[spec.id].store(true,std::memory_order_release);if(spec.id==5)g_local_request_installed.store(true,std::memory_order_release);if(spec.id==3)g_touch_installed.store(true,std::memory_order_release);dlclose(handle);return true;}
#define ASTRAE_ORIGINAL(type,id) reinterpret_cast<type>(g_original[id].load(std::memory_order_acquire))
static void local_request_replacement(void *network,void *,void *,void *request,int,void *){
    g_calls[5].fetch_add(1,std::memory_order_relaxed);
    if(network&&request&&AstraeOffline_HandleManagedRequest(network,request)){g_managed_request_seen.store(true,std::memory_order_release);return;}
    static constexpr char kFallbackError[]="{\"NotiFlag\":0,\"RefreshProtocols\":[],\"ErrorType\":0,\"Reason\":\"Offline handler could not process this request\"}";
    if(network&&AstraeOffline_RespondManaged(network,"ErrorPacket",kFallbackError)){g_managed_request_seen.store(true,std::memory_order_release);__android_log_write(ANDROID_LOG_WARN,"AstraeOffline","request was not mapped; returned ErrorPacket instead of dropping it");return;}
    __android_log_write(ANDROID_LOG_ERROR,"AstraeOffline","request handling and ErrorPacket fallback both failed");
}
static void touch_replacement(void *controller,void *){
    g_calls[3].fetch_add(1,std::memory_order_relaxed);
    std::string error;
    if(!astrae::lobby::touch_enter_lobby(controller,&error))__android_log_print(ANDROID_LOG_ERROR,"AstraeOffline","minimal touch->EnterLobby dispatch failed: %s",error.c_str());
}
using TextLocaleFn=void(*)(void *,int,int,void *);static void text_locale_hook(void *a,int b,int c,void *d){g_calls[9].fetch_add(1,std::memory_order_relaxed);if(auto f=ASTRAE_ORIGINAL(TextLocaleFn,9))f(a,b,c,d);}
using BannerClickFn=void(*)(void *,int,void *);static void banner_click_hook(void *a,int b,void *c){g_calls[12].fetch_add(1,std::memory_order_relaxed);if(auto f=ASTRAE_ORIGINAL(BannerClickFn,12))f(a,b,c);}
using UiInputTickFn=void(*)(void *,void *);static void ui_input_tick_hook(void *a,void *b){g_calls[15].fetch_add(1,std::memory_order_relaxed);if(auto f=ASTRAE_ORIGINAL(UiInputTickFn,15))f(a,b);}
using UnlockNotificationsFn=void *(*)(void *,int,int,int,void *);static void *unlock_notifications_hook(void *a,int b,int c,int d,void *e){g_calls[16].fetch_add(1,std::memory_order_relaxed);if(auto f=ASTRAE_ORIGINAL(UnlockNotificationsFn,16))return f(a,b,c,d,e);return nullptr;}
using TwoVoidFn=void(*)(void *,void *);static void unlock_popup_hidden_hook(void *a,void *b){g_calls[21].fetch_add(1,std::memory_order_relaxed);if(auto f=ASTRAE_ORIGINAL(TwoVoidFn,21))f(a,b);}
static void unlock_popup_step_hook(void *a,void *b){g_calls[22].fetch_add(1,std::memory_order_relaxed);if(auto f=ASTRAE_ORIGINAL(TwoVoidFn,22))f(a,b);}
using PackShownFn=void(*)(void *,int,void *);static void action_pack_shown_hook(void *a,int b,void *c){g_calls[23].fetch_add(1,std::memory_order_relaxed);if(auto f=ASTRAE_ORIGINAL(PackShownFn,23))f(a,b,c);}
static void action_pack_hidden_hook(void *a,void *b){g_calls[24].fetch_add(1,std::memory_order_relaxed);if(auto f=ASTRAE_ORIGINAL(TwoVoidFn,24))f(a,b);}
static void action_daily_shown_hook(void *a,void *b){g_calls[25].fetch_add(1,std::memory_order_relaxed);if(auto f=ASTRAE_ORIGINAL(TwoVoidFn,25))f(a,b);}
static void action_daily_hidden_hook(void *a,void *b){g_calls[26].fetch_add(1,std::memory_order_relaxed);if(auto f=ASTRAE_ORIGINAL(TwoVoidFn,26))f(a,b);}
static void title_loop_step_hook(void *a,void *b){g_calls[29].fetch_add(1,std::memory_order_relaxed);if(auto f=ASTRAE_ORIGINAL(TwoVoidFn,29))f(a,b);}
static void first_day_step_hook(void *a,void *b){g_calls[30].fetch_add(1,std::memory_order_relaxed);if(auto f=ASTRAE_ORIGINAL(TwoVoidFn,30))f(a,b);}
using FirstDayLoadFn=void(*)(void *,void *,void *,void *);static void first_day_load_hook(void *a,void *b,void *c,void *d){g_calls[31].fetch_add(1,std::memory_order_relaxed);if(auto f=ASTRAE_ORIGINAL(FirstDayLoadFn,31))f(a,b,c,d);}
static void first_day_destroy_hook(void *a,void *b){g_calls[32].fetch_add(1,std::memory_order_relaxed);if(auto f=ASTRAE_ORIGINAL(TwoVoidFn,32))f(a,b);}
static void first_day_button_click_hook(void *a,void *b){g_calls[33].fetch_add(1,std::memory_order_relaxed);if(auto f=ASTRAE_ORIGINAL(TwoVoidFn,33))f(a,b);}
static void first_day_root_update_hook(void *a,void *b){g_calls[34].fetch_add(1,std::memory_order_relaxed);if(auto f=ASTRAE_ORIGINAL(TwoVoidFn,34))f(a,b);}
using NativeFacilityClickFn=void(*)(void *,void *,void *);static void native_facility_click_hook(void *a,void *b,void *c){g_calls[35].fetch_add(1,std::memory_order_relaxed);if(auto f=ASTRAE_ORIGINAL(NativeFacilityClickFn,35))f(a,b,c);}
using ItemSelectionInitializeFn=void(*)(void *,void *,int,int,void *);static void item_selection_initialize_hook(void *a,void *b,int c,int d,void *e){g_calls[38].fetch_add(1,std::memory_order_relaxed);if(auto f=ASTRAE_ORIGINAL(ItemSelectionInitializeFn,38))f(a,b,c,d,e);}
using SignedExchangeRefreshFn=void(*)(void *,void *,int,void *);static void signed_exchange_refresh_hook(void *a,void *b,int c,void *d){g_calls[39].fetch_add(1,std::memory_order_relaxed);if(auto f=ASTRAE_ORIGINAL(SignedExchangeRefreshFn,39))f(a,b,c,d);}
#undef ASTRAE_ORIGINAL
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
#if defined(__ANDROID__) && defined(__aarch64__) && defined(ASTRAE_ENABLE_LOCAL_REQUEST_DIRECT_HOOK)
    const HookSpec *spec=nullptr;for(const auto &hook:kOriginalHooks)if(hook.id==5&&std::strcmp(hook.replacement_symbol,"local_request")==0){spec=&hook;break;}if(!spec){if(error)*error="original hook manifest does not contain hook #5 local_request";return false;}if(spec->has_trampoline){if(error)*error="manifest mismatch: local_request is expected to be a direct replacement";return false;}return install_direct_replacement_hook(5,reinterpret_cast<void *>(&local_request_replacement),error);
#elif defined(__ANDROID__) && defined(__aarch64__)
    if(error)*error="local_request is a direct-replacement hook; installation remains gated until Phase 3.2 restores its complete dispatch and active-message semantics";return false;
#else
    if(error)*error="automatic local_request patching is supported only on Android arm64";return false;
#endif
}
bool install_touch_hook(std::string *error){
#if defined(__ANDROID__) && defined(__aarch64__)
    const HookSpec *spec=find_spec(3);
    if(!spec||std::strcmp(spec->replacement_symbol,"touch")!=0){if(error)*error="original hook manifest does not contain Hook #3 touch";return false;}
    if(spec->has_trampoline){if(error)*error="manifest mismatch: original touch must be a direct replacement";return false;}
    return install_direct_replacement_hook(3,reinterpret_cast<void *>(&touch_replacement),error);
#else
    if(error)*error="touch direct replacement is supported only on Android arm64";return false;
#endif
}
bool install_direct_replacement_hook(uint32_t hook_id,void *replacement,std::string *error){
#if defined(__ANDROID__) && defined(__aarch64__)
    const HookSpec *spec=nullptr;for(const auto &hook:kOriginalHooks)if(hook.id==hook_id){spec=&hook;break;}if(!spec){if(error)*error="hook id is absent from original manifest";return false;}if(spec->has_trampoline){if(error)*error=std::string(spec->replacement_symbol)+" is trampoline-backed, not a direct replacement";return false;}return install_one(*spec,replacement,InstallMode::DirectReplacement,error);
#else
    (void)hook_id;(void)replacement;if(error)*error="direct replacement hooks are supported only on Android arm64";return false;
#endif
}
size_t install_ui_passthrough_hooks(std::string *error){
#if defined(__ANDROID__) && defined(__aarch64__)
    if(error)error->clear();
    size_t installed=0;for(const auto &binding:kUiBindings){const HookSpec *spec=find_spec(binding.id);if(!spec)continue;std::string local_error;if(install_one(*spec,binding.replacement,InstallMode::Trampoline,&local_error)){++installed;}else if(error&&error->empty())*error=std::string(spec->replacement_symbol)+": "+local_error;}g_ui_installed.store(installed,std::memory_order_release);return installed;
#else
    if(error)*error="UI passthrough hooks are supported only on Android arm64";return 0;
#endif
}
bool touch_hook_installed(){
#if defined(__ANDROID__) && defined(__aarch64__)
    return g_touch_installed.load(std::memory_order_acquire);
#else
    return false;
#endif
}
bool local_request_hook_installed(){
#if defined(__ANDROID__) && defined(__aarch64__)
    return g_local_request_installed.load(std::memory_order_acquire);
#else
    return false;
#endif
}
bool managed_request_seen(){
#if defined(__ANDROID__) && defined(__aarch64__)
    return g_managed_request_seen.load(std::memory_order_acquire);
#else
    return false;
#endif
}
std::string validate_original_hooks_json(){json::Value out=json::Value::object();out["platformSupported"]=
#if defined(__ANDROID__) && defined(__aarch64__)
true;
#else
false;
#endif
out["installAttempted"]=
#if defined(__ANDROID__) && defined(__aarch64__)
g_install_attempted.load(std::memory_order_acquire);
#else
false;
#endif
out["totalHooks"]=static_cast<uint64_t>(kOriginalHookCount);out["uiPassthroughInstalled"]=
#if defined(__ANDROID__) && defined(__aarch64__)
static_cast<uint64_t>(g_ui_installed.load(std::memory_order_acquire));
#else
uint64_t(0);
#endif
out["touchHookInstalled"]=
#if defined(__ANDROID__) && defined(__aarch64__)
g_touch_installed.load(std::memory_order_acquire);
#else
false;
#endif
out["localRequestHookInstalled"]=
#if defined(__ANDROID__) && defined(__aarch64__)
g_local_request_installed.load(std::memory_order_acquire);
#else
false;
#endif
out["expectedTrampolines"]=uint64_t(0);out["expectedDirectHooks"]=uint64_t(0);for(const auto &h:kOriginalHooks)if(h.has_trampoline)out["expectedTrampolines"]=out["expectedTrampolines"].as_u64()+1;else out["expectedDirectHooks"]=out["expectedDirectHooks"].as_u64()+1;
#if defined(__ANDROID__) && defined(__aarch64__)
void *handle=nullptr;ModuleScan scan;void *base_ptr=resolve_target_module(&handle,&scan,nullptr);if(!base_ptr){out["moduleLoaded"]=false;out["error"]="libil2cpp.so or il2cpp_thread_attach is unavailable";return json::dump(out,true);}uintptr_t base=reinterpret_cast<uintptr_t>(base_ptr);out["moduleLoaded"]=true;out["modulePath"]=scan.path;json::Value rows=json::Value::array();uint64_t matched=0,mismatched=0,unreadable=0,installed=0,installed_trampoline=0,installed_direct=0;for(const auto &h:kOriginalHooks){json::Value row=json::Value::object();row["id"]=h.id;row["name"]=h.replacement_symbol;row["mode"]=h.has_trampoline?"trampoline":"direct_replacement";row["offset"]="0x"+[] (uintptr_t v){char b[2*sizeof(uintptr_t)+1];static const char *d="0123456789abcdef";char *p=b+sizeof(b);*--p='\0';do{*--p=d[v&15];v>>=4;}while(v);return std::string(p);} (h.target_offset);uintptr_t address=0;if(add_overflow(base,h.target_offset,address)||!executable_contains(scan,address,kFingerprintSize)){row["status"]="not_executable_or_unmapped";++unreadable;}else{const auto *actual=reinterpret_cast<const uint8_t *>(address);bool is_installed=h.id<kHookLimit&&g_installed[h.id].load(std::memory_order_acquire)&&std::memcmp(actual,&g_patch_instructions[h.id],sizeof(uint32_t))==0;row["actualBytes"]=hex_bytes(actual,kFingerprintSize);row["expectedBytes"]=hex_bytes(h.expected,kFingerprintSize);if(is_installed){row["status"]=h.has_trampoline?"installed_trampoline":"installed_direct_replacement";++installed;if(h.has_trampoline)++installed_trampoline;else ++installed_direct;}else if(std::memcmp(actual,h.expected,kFingerprintSize)==0){row["status"]="matched";++matched;}else{row["status"]="mismatch";++mismatched;}}if(h.id<kHookLimit){row["calls"]=g_calls[h.id].load(std::memory_order_relaxed);row["relay"]="0x"+[] (uintptr_t v){char b[2*sizeof(uintptr_t)+1];static const char *d="0123456789abcdef";char *p=b+sizeof(b);*--p='\0';do{*--p=d[v&15];v>>=4;}while(v);return std::string(p);} (reinterpret_cast<uintptr_t>(g_relays[h.id].load(std::memory_order_acquire)));}rows.push_back(std::move(row));}dlclose(handle);out["hooks"]=std::move(rows);out["checked"]=matched+mismatched+installed;out["matched"]=matched;out["installed"]=installed;out["installedTrampolineHooks"]=installed_trampoline;out["installedDirectHooks"]=installed_direct;out["mismatched"]=mismatched;out["unreadable"]=unreadable;out["runtimeHookInstalled"]=installed>0;out["fingerprintCompatible"]=(mismatched==0&&unreadable==0);
#else
out["moduleLoaded"]=false;out["matched"]=uint64_t(0);out["mismatched"]=uint64_t(0);out["unreadable"]=static_cast<uint64_t>(kOriginalHookCount);out["fingerprintCompatible"]=false;out["error"]="Android arm64 process inspection is unavailable on this host platform";
#endif
out["note"]="Hook #3 is a Phase-1 minimal direct replacement that bypasses first-day tutorial and invokes UILoginPageController.EnterLobby. Hook #5 handles mapped requests through the offline backend and returns ErrorPacket when mapping/handling fails. Direct replacements have no original trampoline. UI bindings remain passthrough-only.";return json::dump(out,true);}
}
