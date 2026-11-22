#include "hook_runtime.hpp"
#include "original_hook_manifest.hpp"
#include "json.hpp"
#include <cstdint>
#include <cstring>
#include <vector>
#include <limits>
#if defined(__ANDROID__)
#include <dlfcn.h>
#include <link.h>
#endif
namespace astrae::hooks {
#if defined(__ANDROID__)
struct ExecRange { uintptr_t begin=0,end=0; };
struct ModuleScan { uintptr_t base=0; bool found=false; std::string path; std::vector<ExecRange> executable; };
static int scan_module(dl_phdr_info *info,size_t,void *opaque){auto *scan=static_cast<ModuleScan *>(opaque);if(static_cast<uintptr_t>(info->dlpi_addr)!=scan->base)return 0;scan->found=true;scan->path=info->dlpi_name?info->dlpi_name:"";for(ElfW(Half)i=0;i<info->dlpi_phnum;++i){const auto &ph=info->dlpi_phdr[i];if(ph.p_type!=PT_LOAD||(ph.p_flags&PF_X)==0||ph.p_memsz<16)continue;uintptr_t start=static_cast<uintptr_t>(info->dlpi_addr)+static_cast<uintptr_t>(ph.p_vaddr);if(start>std::numeric_limits<uintptr_t>::max()-static_cast<uintptr_t>(ph.p_memsz))continue;scan->executable.push_back({start,start+static_cast<uintptr_t>(ph.p_memsz)});}return 1;}
static bool executable_contains(const ModuleScan &scan,uintptr_t address,size_t size){if(address>std::numeric_limits<uintptr_t>::max()-size)return false;for(const auto &r:scan.executable)if(address>=r.begin&&address+size<=r.end)return true;return false;}
static std::string hex_bytes(const uint8_t *p,size_t n){static const char *d="0123456789abcdef";std::string s;s.reserve(n*2);for(size_t i=0;i<n;++i){s.push_back(d[p[i]>>4]);s.push_back(d[p[i]&15]);}return s;}
#endif
std::string validate_original_hooks_json(){json::Value out=json::Value::object();out["platformSupported"]=
#if defined(__ANDROID__)
true;
#else
false;
#endif
out["installAttempted"]=false;out["totalHooks"]=static_cast<uint64_t>(kOriginalHookCount);out["expectedTrampolines"]=uint64_t(0);out["expectedDirectHooks"]=uint64_t(0);for(const auto &h:kOriginalHooks)if(h.has_trampoline)out["expectedTrampolines"]=out["expectedTrampolines"].as_u64()+1;else out["expectedDirectHooks"]=out["expectedDirectHooks"].as_u64()+1;
#if defined(__ANDROID__)
void *handle=dlopen("libil2cpp.so",RTLD_NOW|RTLD_NOLOAD);if(!handle){out["moduleLoaded"]=false;out["error"]="libil2cpp.so is not loaded (RTLD_NOLOAD)";return json::dump(out,true);}out["moduleLoaded"]=true;void *symbol=dlsym(handle,"il2cpp_thread_attach");if(!symbol){out["error"]="il2cpp_thread_attach is not exported by libil2cpp.so";dlclose(handle);return json::dump(out,true);}Dl_info dl_info{};if(!dladdr(symbol,&dl_info)||!dl_info.dli_fbase){out["error"]="dladdr could not resolve the libil2cpp load base";dlclose(handle);return json::dump(out,true);}uintptr_t base=reinterpret_cast<uintptr_t>(dl_info.dli_fbase);ModuleScan scan;scan.base=base;dl_iterate_phdr(scan_module,&scan);out["moduleBase"]="0x"+[] (uintptr_t v){char b[2*sizeof(uintptr_t)+1];static const char *d="0123456789abcdef";char *p=b+sizeof(b);*--p='\0';do{*--p=d[v&15];v>>=4;}while(v);return std::string(p);} (base);out["modulePath"]=scan.path;if(!scan.found){out["error"]="dl_iterate_phdr could not find the module containing il2cpp_thread_attach";dlclose(handle);return json::dump(out,true);}json::Value rows=json::Value::array();uint64_t matched=0,mismatched=0,unreadable=0;for(const auto &h:kOriginalHooks){json::Value row=json::Value::object();row["id"]=h.id;row["name"]=h.replacement_symbol;row["offset"]="0x"+[] (uintptr_t v){char b[2*sizeof(uintptr_t)+1];static const char *d="0123456789abcdef";char *p=b+sizeof(b);*--p='\0';do{*--p=d[v&15];v>>=4;}while(v);return std::string(p);} (h.target_offset);uintptr_t address=base+h.target_offset;if(address<base||!executable_contains(scan,address,16)){row["status"]="not_executable_or_unmapped";++unreadable;}else{const auto *actual=reinterpret_cast<const uint8_t *>(address);bool equal=std::memcmp(actual,h.expected,16)==0;row["actualBytes"]=hex_bytes(actual,16);row["expectedBytes"]=hex_bytes(h.expected,16);row["status"]=equal?"matched":"mismatch";if(equal)++matched;else ++mismatched;}rows.push_back(std::move(row));}dlclose(handle);out["hooks"]=std::move(rows);out["checked"]=matched+mismatched;out["matched"]=matched;out["mismatched"]=mismatched;out["unreadable"]=unreadable;out["fingerprintCompatible"]=(matched==kOriginalHookCount&&mismatched==0&&unreadable==0);out["note"]="Read-only 16-byte target fingerprint validation only. No target memory is modified and no Hook is installed. Fingerprint match does not prove replacement callback ABI or trampoline safety.";
#else
out["moduleLoaded"]=false;out["matched"]=uint64_t(0);out["mismatched"]=uint64_t(0);out["unreadable"]=static_cast<uint64_t>(kOriginalHookCount);out["fingerprintCompatible"]=false;out["error"]="Android process inspection is unavailable on this host platform";
#endif
return json::dump(out,true);}
}
