#include "hook_runtime.hpp"
#include "original_hook_manifest.hpp"
#include "json.hpp"
#include "astrae_backend.h"
#include <atomic>
#include <string>
#include <cstdint>
#include <cstring>
#include <vector>
#include <limits>
#if defined(__ANDROID__)
#include <dlfcn.h>
#include <link.h>
#include <sys/mman.h>
#include <unistd.h>
#include <android/log.h>
#endif
namespace astrae::hooks {
#if defined(__ANDROID__)
struct ExecRange { uintptr_t begin=0,end=0; };
struct ModuleScan { uintptr_t base=0; bool found=false; std::string path; std::vector<ExecRange> executable; };
static int scan_module(dl_phdr_info *info,size_t,void *opaque){auto *scan=static_cast<ModuleScan *>(opaque);if(static_cast<uintptr_t>(info->dlpi_addr)!=scan->base)return 0;scan->found=true;scan->path=info->dlpi_name?info->dlpi_name:"";for(ElfW(Half)i=0;i<info->dlpi_phnum;++i){const auto &ph=info->dlpi_phdr[i];if(ph.p_type!=PT_LOAD||(ph.p_flags&PF_X)==0||ph.p_memsz<16)continue;uintptr_t start=static_cast<uintptr_t>(info->dlpi_addr)+static_cast<uintptr_t>(ph.p_vaddr);if(start>std::numeric_limits<uintptr_t>::max()-static_cast<uintptr_t>(ph.p_memsz))continue;scan->executable.push_back({start,start+static_cast<uintptr_t>(ph.p_memsz)});}return 1;}
static bool executable_contains(const ModuleScan &scan,uintptr_t address,size_t size){if(address>std::numeric_limits<uintptr_t>::max()-size)return false;for(const auto &r:scan.executable)if(address>=r.begin&&address+size<=r.end)return true;return false;}
static std::string hex_bytes(const uint8_t *p,size_t n){static const char *d="0123456789abcdef";std::string s;s.reserve(n*2);for(size_t i=0;i<n;++i){s.push_back(d[p[i]>>4]);s.push_back(d[p[i]&15]);}return s;}
#endif
#if defined(__ANDROID__) && defined(__aarch64__)
namespace {
std::atomic<bool> g_local_request_installed{false};
void local_request_replacement(void *network,void *,void *,void *request,int,void *){
    if(AstraeOffline_HandleManagedRequest(network,request))return;
    char *last_error=AstraeOffline_Il2Cpp_GetLastError();
    std::string reason=last_error?last_error:"offline request dispatch failed";
    AstraeOffline_Free(last_error);
    // A failed protocol must still complete the managed request; otherwise the
    // game's pending request task can remain unresolved indefinitely.
    if(!AstraeOffline_RespondManaged(network,"ErrorPacket","{\"ErrorType\":1}")){
        char *response_error=AstraeOffline_Il2Cpp_GetLastError();
        std::string message="request dispatch failed: "+reason+"; ErrorPacket callback failed: "+(response_error?response_error:"unknown");
        __android_log_write(ANDROID_LOG_ERROR,"AstraeOffline",message.c_str());
        AstraeOffline_Free(response_error);
    }else __android_log_write(ANDROID_LOG_WARN,"AstraeOffline",("request rejected: "+reason).c_str());
}
bool patch_direct_hook(const HookSpec &spec,void *replacement,std::string *error){
    if(error){error->clear();}void *handle=dlopen("libil2cpp.so",RTLD_NOW|RTLD_NOLOAD);if(!handle){if(error)*error="libil2cpp.so is not loaded";return false;}
    void *symbol=dlsym(handle,"il2cpp_thread_attach");if(!symbol){if(error)*error="il2cpp_thread_attach was not found";dlclose(handle);return false;}
    Dl_info info{};if(!dladdr(symbol,&info)||!info.dli_fbase){if(error)*error="cannot resolve libil2cpp base address";dlclose(handle);return false;}
    uintptr_t base=reinterpret_cast<uintptr_t>(info.dli_fbase);ModuleScan scan;scan.base=base;dl_iterate_phdr(scan_module,&scan);uintptr_t target=base+spec.target_offset;
    if(target<base||!scan.found||!executable_contains(scan,target,16)){if(error)*error="local_request target is not inside an executable libil2cpp segment";dlclose(handle);return false;}
    auto *code=reinterpret_cast<uint8_t *>(target);if(std::memcmp(code,spec.expected,16)!=0){if(error)*error="local_request target fingerprint mismatch; refusing to patch unknown game binary";dlclose(handle);return false;}
    const long page_size=sysconf(_SC_PAGESIZE);if(page_size<=0){if(error)*error="cannot query process page size";dlclose(handle);return false;}
    uintptr_t page_mask=static_cast<uintptr_t>(page_size-1),page_begin=target&~page_mask,page_end=(target+16+page_mask)&~page_mask;if(page_end<=page_begin){if(error)*error="invalid code patch page range";dlclose(handle);return false;}
    const size_t page_length=static_cast<size_t>(page_end-page_begin);if(mprotect(reinterpret_cast<void *>(page_begin),page_length,PROT_READ|PROT_WRITE|PROT_EXEC)!=0){if(error)*error="mprotect(RWX) failed for local_request";dlclose(handle);return false;}
    alignas(16) uint8_t patch[16]={0x50,0x00,0x00,0x58,0x00,0x02,0x1f,0xd6};uintptr_t destination=reinterpret_cast<uintptr_t>(replacement);std::memcpy(patch+8,&destination,sizeof(destination));
    std::atomic_thread_fence(std::memory_order_seq_cst);std::memcpy(code,patch,sizeof(patch));__builtin___clear_cache(reinterpret_cast<char *>(code),reinterpret_cast<char *>(code+sizeof(patch)));std::atomic_thread_fence(std::memory_order_seq_cst);
    bool restored=mprotect(reinterpret_cast<void *>(page_begin),page_length,PROT_READ|PROT_EXEC)==0;dlclose(handle);
    if(!restored&&error)*error="hook bytes were installed but executable page protection could not be restored";
    return true;
}
}
#endif
bool install_local_request_hook(std::string *error){
#if defined(__ANDROID__) && defined(__aarch64__)
    if(g_local_request_installed.load()){return true;}const HookSpec *spec=nullptr;for(const auto &hook:kOriginalHooks)if(hook.id==5&&std::strcmp(hook.replacement_symbol,"local_request")==0){spec=&hook;break;}if(!spec){if(error)*error="original hook manifest does not contain hook #5 local_request";return false;}
    if(!patch_direct_hook(*spec,reinterpret_cast<void *>(&local_request_replacement),error)){return false;}g_local_request_installed.store(true);return true;
#else
    if(error)*error="automatic local_request patching is supported only on Android arm64";return false;
#endif
}
bool local_request_hook_installed(){
#if defined(__ANDROID__) && defined(__aarch64__)
    return g_local_request_installed.load();
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
out["installAttempted"]=false;out["totalHooks"]=static_cast<uint64_t>(kOriginalHookCount);out["expectedTrampolines"]=uint64_t(0);out["expectedDirectHooks"]=uint64_t(0);for(const auto &h:kOriginalHooks)if(h.has_trampoline)out["expectedTrampolines"]=out["expectedTrampolines"].as_u64()+1;else out["expectedDirectHooks"]=out["expectedDirectHooks"].as_u64()+1;
#if defined(__ANDROID__)
void *handle=dlopen("libil2cpp.so",RTLD_NOW|RTLD_NOLOAD);if(!handle){out["moduleLoaded"]=false;out["error"]="libil2cpp.so is not loaded (RTLD_NOLOAD)";return json::dump(out,true);}out["moduleLoaded"]=true;void *symbol=dlsym(handle,"il2cpp_thread_attach");if(!symbol){out["error"]="il2cpp_thread_attach is not exported by libil2cpp.so";dlclose(handle);return json::dump(out,true);}Dl_info dl_info{};if(!dladdr(symbol,&dl_info)||!dl_info.dli_fbase){out["error"]="dladdr could not resolve the libil2cpp load base";dlclose(handle);return json::dump(out,true);}uintptr_t base=reinterpret_cast<uintptr_t>(dl_info.dli_fbase);ModuleScan scan;scan.base=base;dl_iterate_phdr(scan_module,&scan);out["moduleBase"]="0x"+[] (uintptr_t v){char b[2*sizeof(uintptr_t)+1];static const char *d="0123456789abcdef";char *p=b+sizeof(b);*--p='\0';do{*--p=d[v&15];v>>=4;}while(v);return std::string(p);} (base);out["modulePath"]=scan.path;if(!scan.found){out["error"]="dl_iterate_phdr could not find the module containing il2cpp_thread_attach";dlclose(handle);return json::dump(out,true);}json::Value rows=json::Value::array();uint64_t matched=0,mismatched=0,unreadable=0,installed=0;for(const auto &h:kOriginalHooks){json::Value row=json::Value::object();row["id"]=h.id;row["name"]=h.replacement_symbol;row["offset"]="0x"+[] (uintptr_t v){char b[2*sizeof(uintptr_t)+1];static const char *d="0123456789abcdef";char *p=b+sizeof(b);*--p='\0';do{*--p=d[v&15];v>>=4;}while(v);return std::string(p);} (h.target_offset);uintptr_t address=base+h.target_offset;if(address<base||!executable_contains(scan,address,16)){row["status"]="not_executable_or_unmapped";++unreadable;}else{const auto *actual=reinterpret_cast<const uint8_t *>(address);bool equal=std::memcmp(actual,h.expected,16)==0;bool is_installed=false;
#if defined(__ANDROID__) && defined(__aarch64__)
if(h.id==5&&g_local_request_installed.load()){uint8_t patch[16]={0x50,0x00,0x00,0x58,0x00,0x02,0x1f,0xd6};uintptr_t destination=reinterpret_cast<uintptr_t>(&local_request_replacement);std::memcpy(patch+8,&destination,sizeof(destination));is_installed=std::memcmp(actual,patch,16)==0;}
#endif
row["actualBytes"]=hex_bytes(actual,16);row["expectedBytes"]=hex_bytes(h.expected,16);if(is_installed){row["status"]="installed";++installed;}else{row["status"]=equal?"matched":"mismatch";if(equal)++matched;else ++mismatched;}}rows.push_back(std::move(row));}dlclose(handle);out["hooks"]=std::move(rows);out["checked"]=matched+mismatched+installed;out["matched"]=matched;out["installed"]=installed;out["mismatched"]=mismatched;out["unreadable"]=unreadable;out["runtimeHookInstalled"]=installed>0;out["fingerprintCompatible"]=(matched+installed==kOriginalHookCount&&mismatched==0&&unreadable==0);out["note"]="Read-only validation. Hook #5 is reported as installed only when its 16-byte AArch64 branch stub matches this library's local_request replacement pointer.";
#else
out["moduleLoaded"]=false;out["matched"]=uint64_t(0);out["mismatched"]=uint64_t(0);out["unreadable"]=static_cast<uint64_t>(kOriginalHookCount);out["fingerprintCompatible"]=false;out["error"]="Android process inspection is unavailable on this host platform";
#endif
return json::dump(out,true);}
}
