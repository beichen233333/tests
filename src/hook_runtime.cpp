#include "hook_runtime.hpp"
#include "original_hook_manifest.hpp"
#include "json.hpp"
#include "astrae_backend.h"
#include <atomic>
#include <array>
#include <cstdint>
#include <cstring>
#include <limits>
#include <mutex>
#include <string>
#include <vector>
#if defined(__ANDROID__)
#include <dlfcn.h>
#include <link.h>
#include <sys/mman.h>
#include <unistd.h>
#include <android/log.h>
#endif

namespace astrae::hooks {
#if defined(__ANDROID__)
namespace {
constexpr size_t kPatchSize=16;
constexpr uint32_t kHookLimit=40;
struct ExecRange { uintptr_t begin=0,end=0; };
struct ModuleScan { uintptr_t base=0; bool found=false; std::string path; std::vector<ExecRange> executable; };
static int scan_module(dl_phdr_info *info,size_t,void *opaque){auto *scan=static_cast<ModuleScan *>(opaque);if(static_cast<uintptr_t>(info->dlpi_addr)!=scan->base)return 0;scan->found=true;scan->path=info->dlpi_name?info->dlpi_name:"";for(ElfW(Half)i=0;i<info->dlpi_phnum;++i){const auto &ph=info->dlpi_phdr[i];if(ph.p_type!=PT_LOAD||(ph.p_flags&PF_X)==0||ph.p_memsz<kPatchSize)continue;uintptr_t start=static_cast<uintptr_t>(info->dlpi_addr)+static_cast<uintptr_t>(ph.p_vaddr);if(start>std::numeric_limits<uintptr_t>::max()-static_cast<uintptr_t>(ph.p_memsz))continue;scan->executable.push_back({start,start+static_cast<uintptr_t>(ph.p_memsz)});}return 1;}
static bool executable_contains(const ModuleScan &scan,uintptr_t address,size_t size){if(address>std::numeric_limits<uintptr_t>::max()-size)return false;for(const auto &r:scan.executable)if(address>=r.begin&&address+size<=r.end)return true;return false;}
static std::string hex_bytes(const uint8_t *p,size_t n){static const char *d="0123456789abcdef";std::string s;s.reserve(n*2);for(size_t i=0;i<n;++i){s.push_back(d[p[i]>>4]);s.push_back(d[p[i]&15]);}return s;}
static int64_t sign_extend(uint64_t value,unsigned bits){const uint64_t sign=uint64_t(1)<<(bits-1);return (value&sign)?static_cast<int64_t>(value)-static_cast<int64_t>(uint64_t(1)<<bits):static_cast<int64_t>(value);}
static int64_t address_delta(uintptr_t target,uintptr_t pc){return target>=pc?static_cast<int64_t>(target-pc):-static_cast<int64_t>(pc-target);}
static bool encode_scaled(int64_t delta,unsigned shift,unsigned bits,uint64_t &imm){const int64_t scale=int64_t(1)<<shift;if((delta&(scale-1))!=0)return false;const int64_t scaled=delta/scale;const int64_t min=-(int64_t(1)<<(bits-1)),max=(int64_t(1)<<(bits-1))-1;if(scaled<min||scaled>max)return false;imm=static_cast<uint64_t>(scaled)&((uint64_t(1)<<bits)-1);return true;}
static bool relocate_instruction(uint32_t insn,uintptr_t old_pc,uintptr_t new_pc,uint32_t &out){out=insn;uint64_t imm=0;
    // ADR / ADRP: recompute their PC-relative immediate for the trampoline location.
    if((insn&0x9f000000u)==0x90000000u){uint64_t raw=((uint64_t(insn>>5)&0x7ffffu)<<2)|((insn>>29)&3u);int64_t pages=sign_extend(raw,21);uintptr_t target_page=static_cast<uintptr_t>(static_cast<int64_t>(old_pc&~uintptr_t(0xfff))+pages*4096);int64_t delta=address_delta(target_page,new_pc&~uintptr_t(0xfff));if(!encode_scaled(delta,12,21,imm))return false;out=(insn&~((3u<<29)|(0x7ffffu<<5)))|(static_cast<uint32_t>(imm&3u)<<29)|(static_cast<uint32_t>((imm>>2)&0x7ffffu)<<5);return true;}
    if((insn&0x9f000000u)==0x10000000u){uint64_t raw=((uint64_t(insn>>5)&0x7ffffu)<<2)|((insn>>29)&3u);int64_t delta=sign_extend(raw,21);uintptr_t target=old_pc+delta;int64_t moved=address_delta(target,new_pc);if(moved<-(int64_t(1)<<20)||moved>((int64_t(1)<<20)-1))return false;imm=static_cast<uint64_t>(moved)&0x1fffffu;out=(insn&~((3u<<29)|(0x7ffffu<<5)))|(static_cast<uint32_t>(imm&3u)<<29)|(static_cast<uint32_t>((imm>>2)&0x7ffffu)<<5);return true;}
    // Unconditional B / BL.
    if((insn&0x7c000000u)==0x14000000u){int64_t delta=sign_extend(insn&0x03ffffffu,26)*4;uintptr_t target=old_pc+delta;int64_t moved=address_delta(target,new_pc);if(!encode_scaled(moved,2,26,imm))return false;out=(insn&0xfc000000u)|static_cast<uint32_t>(imm);return true;}
    // B.cond.
    if((insn&0xff000010u)==0x54000000u){int64_t delta=sign_extend((insn>>5)&0x7ffffu,19)*4;uintptr_t target=old_pc+delta;int64_t moved=address_delta(target,new_pc);if(!encode_scaled(moved,2,19,imm))return false;out=(insn&~(0x7ffffu<<5))|(static_cast<uint32_t>(imm)<<5);return true;}
    // CBZ / CBNZ.
    if((insn&0x7e000000u)==0x34000000u){int64_t delta=sign_extend((insn>>5)&0x7ffffu,19)*4;uintptr_t target=old_pc+delta;int64_t moved=address_delta(target,new_pc);if(!encode_scaled(moved,2,19,imm))return false;out=(insn&~(0x7ffffu<<5))|(static_cast<uint32_t>(imm)<<5);return true;}
    // TBZ / TBNZ.
    if((insn&0x7e000000u)==0x36000000u){int64_t delta=sign_extend((insn>>5)&0x3fffu,14)*4;uintptr_t target=old_pc+delta;int64_t moved=address_delta(target,new_pc);if(!encode_scaled(moved,2,14,imm))return false;out=(insn&~(0x3fffu<<5))|(static_cast<uint32_t>(imm)<<5);return true;}
    // Literal loads (integer/SIMD/PRFM). A far literal is rejected rather than copied incorrectly.
    if((insn&0x3b000000u)==0x18000000u){int64_t delta=sign_extend((insn>>5)&0x7ffffu,19)*4;uintptr_t target=old_pc+delta;int64_t moved=address_delta(target,new_pc);if(!encode_scaled(moved,2,19,imm))return false;out=(insn&~(0x7ffffu<<5))|(static_cast<uint32_t>(imm)<<5);return true;}
    return true;
}
static void write_absolute_branch(uint8_t *where,uintptr_t destination){const uint8_t op[8]={0x50,0x00,0x00,0x58,0x00,0x02,0x1f,0xd6};std::memcpy(where,op,sizeof(op));std::memcpy(where+8,&destination,sizeof(destination));}
static void *make_trampoline(uintptr_t target,const uint8_t original[kPatchSize],std::string *error){long page_size=sysconf(_SC_PAGESIZE);if(page_size<=0){if(error)*error="cannot query page size for trampoline";return nullptr;}size_t length=static_cast<size_t>(page_size);void *memory=mmap(nullptr,length,PROT_READ|PROT_WRITE,MAP_PRIVATE|MAP_ANONYMOUS,-1,0);if(memory==MAP_FAILED){if(error)*error="mmap failed while allocating hook trampoline";return nullptr;}auto *code=static_cast<uint8_t *>(memory);for(size_t i=0;i<4;++i){uint32_t instruction=0,relocated=0;std::memcpy(&instruction,original+i*4,4);if(!relocate_instruction(instruction,target+i*4,reinterpret_cast<uintptr_t>(code)+i*4,relocated)){munmap(memory,length);if(error)*error="unsupported/out-of-range PC-relative instruction in 16-byte hook prologue";return nullptr;}std::memcpy(code+i*4,&relocated,4);}write_absolute_branch(code+16,target+kPatchSize);__builtin___clear_cache(reinterpret_cast<char *>(code),reinterpret_cast<char *>(code+32));if(mprotect(memory,length,PROT_READ|PROT_EXEC)!=0){munmap(memory,length);if(error)*error="mprotect failed while sealing trampoline RX";return nullptr;}return memory;}
static bool write_hook_patch(uintptr_t target,void *replacement,std::string *error){long page_size=sysconf(_SC_PAGESIZE);if(page_size<=0){if(error)*error="cannot query process page size";return false;}uintptr_t size=static_cast<uintptr_t>(page_size),begin=target&~(size-1),end=(target+kPatchSize+size-1)&~(size-1);if(end<=begin){if(error)*error="invalid code patch page range";return false;}size_t length=static_cast<size_t>(end-begin);if(mprotect(reinterpret_cast<void *>(begin),length,PROT_READ|PROT_WRITE|PROT_EXEC)!=0){if(error)*error="mprotect(RWX) failed for hook target";return false;}uint8_t patch[kPatchSize]{};write_absolute_branch(patch,reinterpret_cast<uintptr_t>(replacement));std::memcpy(reinterpret_cast<void *>(target),patch,sizeof(patch));__builtin___clear_cache(reinterpret_cast<char *>(target),reinterpret_cast<char *>(target+kPatchSize));bool restored=mprotect(reinterpret_cast<void *>(begin),length,PROT_READ|PROT_EXEC)==0;if(!restored&&error)*error="hook bytes installed, but target page protection could not be restored to RX";return true;}
static std::mutex g_patch_mutex;
static std::array<std::atomic<void *>,kHookLimit> g_original{};
static std::array<std::atomic<bool>,kHookLimit> g_installed{};
static std::array<std::atomic<uint64_t>,kHookLimit> g_calls{};
static std::atomic<bool> g_install_attempted{false};
static std::atomic<size_t> g_ui_installed{0};
static std::atomic<bool> g_local_request_installed{false};
static std::atomic<bool> g_managed_request_seen{false};
static void *resolve_target_module(void **out_handle,ModuleScan *out_scan,std::string *error){void *handle=dlopen("libil2cpp.so",RTLD_NOW|RTLD_NOLOAD);if(!handle){if(error)*error="libil2cpp.so is not loaded";return nullptr;}void *symbol=dlsym(handle,"il2cpp_thread_attach");if(!symbol){if(error)*error="il2cpp_thread_attach was not found";dlclose(handle);return nullptr;}Dl_info info{};if(!dladdr(symbol,&info)||!info.dli_fbase){if(error)*error="cannot resolve libil2cpp base address";dlclose(handle);return nullptr;}ModuleScan scan;scan.base=reinterpret_cast<uintptr_t>(info.dli_fbase);dl_iterate_phdr(scan_module,&scan);if(!scan.found){if(error)*error="dl_iterate_phdr could not find libil2cpp";dlclose(handle);return nullptr;}*out_handle=handle;*out_scan=std::move(scan);return info.dli_fbase;}
static const HookSpec *find_spec(uint32_t id){for(const auto &hook:kOriginalHooks)if(hook.id==id)return &hook;return nullptr;}
static bool install_one(const HookSpec &spec,void *replacement,std::string *error){std::lock_guard<std::mutex> lock(g_patch_mutex);g_install_attempted.store(true);if(spec.id>=kHookLimit){if(error)*error="hook id exceeds internal state table";return false;}if(g_installed[spec.id].load())return true;if(!spec.has_trampoline){if(error)*error=std::string(spec.replacement_symbol)+" has no original trampoline in the source ELF manifest; refusing to synthesize an unverified fallback";return false;}void *handle=nullptr;ModuleScan scan;void *base_ptr=resolve_target_module(&handle,&scan,error);if(!base_ptr)return false;uintptr_t base=reinterpret_cast<uintptr_t>(base_ptr),target=base+spec.target_offset;if(target<base||!executable_contains(scan,target,kPatchSize)){if(error)*error="hook target is outside executable libil2cpp ranges";dlclose(handle);return false;}auto *bytes=reinterpret_cast<const uint8_t *>(target);if(std::memcmp(bytes,spec.expected,kPatchSize)!=0){if(error)*error=std::string(spec.replacement_symbol)+" target fingerprint mismatch; refusing to patch this game build";dlclose(handle);return false;}void *trampoline=make_trampoline(target,bytes,error);if(!trampoline){dlclose(handle);return false;}g_original[spec.id].store(trampoline,std::memory_order_release);if(!write_hook_patch(target,replacement,error)){g_original[spec.id].store(nullptr,std::memory_order_release);long page_size=sysconf(_SC_PAGESIZE);if(page_size>0)munmap(trampoline,static_cast<size_t>(page_size));dlclose(handle);return false;}g_installed[spec.id].store(true,std::memory_order_release);if(spec.id==5)g_local_request_installed.store(true);dlclose(handle);return true;}
#define ASTRAE_ORIGINAL(type,id) reinterpret_cast<type>(g_original[id].load(std::memory_order_acquire))
using LocalRequestFn=void(*)(void *,void *,void *,void *,int,void *);
static void local_request_replacement(void *network,void *server,void *url,void *request,int refresh,void *method){g_calls[5].fetch_add(1,std::memory_order_relaxed);if(network&&request&&AstraeOffline_HandleManagedRequest(network,request)){g_managed_request_seen.store(true,std::memory_order_release);return;}LocalRequestFn original=ASTRAE_ORIGINAL(LocalRequestFn,5);if(original){original(network,server,url,request,refresh,method);return;}__android_log_write(ANDROID_LOG_ERROR,"AstraeOffline","local_request fallback unavailable; request left unhandled");}
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
#if defined(__ANDROID__) && defined(__aarch64__)
    const HookSpec *spec=nullptr;for(const auto &hook:kOriginalHooks)if(hook.id==5&&std::strcmp(hook.replacement_symbol,"local_request")==0){spec=&hook;break;}if(!spec){if(error)*error="original hook manifest does not contain hook #5 local_request";return false;}if(!spec->has_trampoline){if(error)*error="hook #5 local_request has no original trampoline in the source ELF; automatic installation is disabled until its original control flow is recovered";return false;}return install_one(*spec,reinterpret_cast<void *>(&local_request_replacement),error);
#else
    if(error)*error="automatic local_request patching is supported only on Android arm64";return false;
#endif
}
size_t install_ui_passthrough_hooks(std::string *error){
#if defined(__ANDROID__) && defined(__aarch64__)
    if(error)error->clear();size_t installed=0;for(const auto &binding:kUiBindings){const HookSpec *spec=find_spec(binding.id);if(!spec)continue;std::string local_error;if(install_one(*spec,binding.replacement,&local_error)){++installed;}else if(error&&error->empty())*error=std::string(spec->replacement_symbol)+": "+local_error;}g_ui_installed.store(installed);return installed;
#else
    if(error)*error="UI passthrough hooks are supported only on Android arm64";return 0;
#endif
}
bool local_request_hook_installed(){
#if defined(__ANDROID__) && defined(__aarch64__)
    return g_local_request_installed.load();
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
#if defined(__ANDROID__)
true;
#else
false;
#endif
out["installAttempted"]=
#if defined(__ANDROID__) && defined(__aarch64__)
g_install_attempted.load();
#else
false;
#endif
out["totalHooks"]=static_cast<uint64_t>(kOriginalHookCount);out["uiPassthroughInstalled"]=
#if defined(__ANDROID__) && defined(__aarch64__)
static_cast<uint64_t>(g_ui_installed.load());
#else
uint64_t(0);
#endif
out["expectedTrampolines"]=uint64_t(0);out["expectedDirectHooks"]=uint64_t(0);for(const auto &h:kOriginalHooks)if(h.has_trampoline)out["expectedTrampolines"]=out["expectedTrampolines"].as_u64()+1;else out["expectedDirectHooks"]=out["expectedDirectHooks"].as_u64()+1;
#if defined(__ANDROID__) && defined(__aarch64__)
void *handle=nullptr;ModuleScan scan;void *base_ptr=resolve_target_module(&handle,&scan,nullptr);if(!base_ptr){out["moduleLoaded"]=false;out["error"]="libil2cpp.so or il2cpp_thread_attach is unavailable";return json::dump(out,true);}uintptr_t base=reinterpret_cast<uintptr_t>(base_ptr);out["moduleLoaded"]=true;out["modulePath"]=scan.path;json::Value rows=json::Value::array();uint64_t matched=0,mismatched=0,unreadable=0,installed=0;for(const auto &h:kOriginalHooks){json::Value row=json::Value::object();row["id"]=h.id;row["name"]=h.replacement_symbol;row["offset"]="0x"+[] (uintptr_t v){char b[2*sizeof(uintptr_t)+1];static const char *d="0123456789abcdef";char *p=b+sizeof(b);*--p='\0';do{*--p=d[v&15];v>>=4;}while(v);return std::string(p);} (h.target_offset);uintptr_t address=base+h.target_offset;if(address<base||!executable_contains(scan,address,kPatchSize)){row["status"]="not_executable_or_unmapped";++unreadable;}else{const auto *actual=reinterpret_cast<const uint8_t *>(address);bool is_installed=false;if(h.id<kHookLimit&&g_installed[h.id].load()){uint8_t patch[kPatchSize]{};write_absolute_branch(patch,reinterpret_cast<uintptr_t>(h.id==5?reinterpret_cast<void *>(&local_request_replacement):nullptr));if(h.id!=5){for(const auto &b:kUiBindings)if(b.id==h.id){write_absolute_branch(patch,reinterpret_cast<uintptr_t>(b.replacement));break;}}is_installed=std::memcmp(actual,patch,kPatchSize)==0;}row["actualBytes"]=hex_bytes(actual,kPatchSize);row["expectedBytes"]=hex_bytes(h.expected,kPatchSize);if(is_installed){row["status"]="installed_passthrough";++installed;}else if(std::memcmp(actual,h.expected,kPatchSize)==0){row["status"]="matched";++matched;}else{row["status"]="mismatch";++mismatched;}}if(h.id<kHookLimit)row["calls"]=g_calls[h.id].load();rows.push_back(std::move(row));}dlclose(handle);out["hooks"]=std::move(rows);out["checked"]=matched+mismatched+installed;out["matched"]=matched;out["installed"]=installed;out["mismatched"]=mismatched;out["unreadable"]=unreadable;out["runtimeHookInstalled"]=installed>0;out["fingerprintCompatible"]=(mismatched==0&&unreadable==0);
#else
out["moduleLoaded"]=false;out["matched"]=uint64_t(0);out["mismatched"]=uint64_t(0);out["unreadable"]=static_cast<uint64_t>(kOriginalHookCount);out["fingerprintCompatible"]=false;out["error"]="Android arm64 process inspection is unavailable on this host platform";
#endif
out["note"]="Hook #5 calls the original trampoline whenever offline handling declines/fails. ABI-known UI pass-through wrappers are deferred until the first offline request completes; UI side effects from the old library are not yet reproduced. Hook #37 is excluded because the original manifest has no trampoline for it.";return json::dump(out,true);}
}
