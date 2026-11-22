#include "il2cpp_runtime.hpp"
#include <algorithm>
#include <cstring>
#include <sstream>
#include <cstdlib>
#include <chrono>
#include <thread>
#if defined(__ANDROID__)
#include <dlfcn.h>
#endif

namespace astrae::il2cpp {
namespace {
class TemporaryRoot {
public:
    TemporaryRoot(const Runtime &runtime,void *object):runtime_(runtime){if(object)handle_=runtime_.retain(object,true);}
    ~TemporaryRoot(){if(handle_)runtime_.release(handle_);}
    TemporaryRoot(const TemporaryRoot&)=delete;
    TemporaryRoot& operator=(const TemporaryRoot&)=delete;
    bool ok() const{return handle_!=0;}
private:
    const Runtime &runtime_;
    uint32_t handle_=0;
};
template<class T> void bind_symbol(T &dst, void *handle, const char *name, size_t &resolved) {
#if defined(__ANDROID__)
    dst=reinterpret_cast<T>(dlsym(handle,name));
    if(dst)++resolved;
#else
    (void)handle;(void)name;(void)resolved;dst=nullptr;
#endif
}

}
Runtime &Runtime::instance(){static Runtime runtime;return runtime;}
void Runtime::resolve_symbols(){
    size_t n=0;
#define ASTRAE_BIND(field,name) bind_symbol(api_.field,handle_,name,n)
    ASTRAE_BIND(domain_get,"il2cpp_domain_get");
    ASTRAE_BIND(domain_get_assemblies,"il2cpp_domain_get_assemblies");
    ASTRAE_BIND(assembly_get_image,"il2cpp_assembly_get_image");
    ASTRAE_BIND(image_get_name,"il2cpp_image_get_name");
    ASTRAE_BIND(image_get_class_count,"il2cpp_image_get_class_count");
    ASTRAE_BIND(image_get_class,"il2cpp_image_get_class");
    ASTRAE_BIND(class_from_name,"il2cpp_class_from_name");
    ASTRAE_BIND(class_get_name,"il2cpp_class_get_name");
    ASTRAE_BIND(class_get_namespace,"il2cpp_class_get_namespace");
    ASTRAE_BIND(class_get_parent,"il2cpp_class_get_parent");
    ASTRAE_BIND(class_get_nested_types,"il2cpp_class_get_nested_types");
    ASTRAE_BIND(class_get_type,"il2cpp_class_get_type");
    ASTRAE_BIND(class_from_type,"il2cpp_class_from_type");
    ASTRAE_BIND(class_from_system_type,"il2cpp_class_from_system_type");
    ASTRAE_BIND(type_get_object,"il2cpp_type_get_object");
    ASTRAE_BIND(type_get_name,"il2cpp_type_get_name");
    ASTRAE_BIND(class_get_field_from_name,"il2cpp_class_get_field_from_name");
    ASTRAE_BIND(field_get_offset,"il2cpp_field_get_offset");
    ASTRAE_BIND(field_get_type,"il2cpp_field_get_type");
    ASTRAE_BIND(class_get_method_from_name,"il2cpp_class_get_method_from_name");
    ASTRAE_BIND(method_get_name,"il2cpp_method_get_name");
    ASTRAE_BIND(method_get_param_count,"il2cpp_method_get_param_count");
    ASTRAE_BIND(runtime_invoke,"il2cpp_runtime_invoke");
    ASTRAE_BIND(object_new,"il2cpp_object_new");
    ASTRAE_BIND(object_unbox,"il2cpp_object_unbox");
    ASTRAE_BIND(value_box,"il2cpp_value_box");
    ASTRAE_BIND(object_get_class,"il2cpp_object_get_class");
    ASTRAE_BIND(string_new,"il2cpp_string_new");
    ASTRAE_BIND(array_new,"il2cpp_array_new");
    ASTRAE_BIND(class_value_size,"il2cpp_class_value_size");
    ASTRAE_BIND(class_instance_size,"il2cpp_class_instance_size");
    ASTRAE_BIND(class_is_valuetype,"il2cpp_class_is_valuetype");
    ASTRAE_BIND(class_is_enum,"il2cpp_class_is_enum");
    ASTRAE_BIND(class_get_element_class,"il2cpp_class_get_element_class");
    ASTRAE_BIND(field_get_value,"il2cpp_field_get_value");
    ASTRAE_BIND(field_set_value,"il2cpp_field_set_value");
    ASTRAE_BIND(gc_wbarrier_set_field,"il2cpp_gc_wbarrier_set_field");
    ASTRAE_BIND(gchandle_new,"il2cpp_gchandle_new");
    ASTRAE_BIND(gchandle_get_target,"il2cpp_gchandle_get_target");
    ASTRAE_BIND(gchandle_free,"il2cpp_gchandle_free");
    ASTRAE_BIND(thread_attach,"il2cpp_thread_attach");
    ASTRAE_BIND(thread_detach,"il2cpp_thread_detach");
    ASTRAE_BIND(thread_current,"il2cpp_thread_current");
    ASTRAE_BIND(format_exception,"il2cpp_format_exception");
    ASTRAE_BIND(free_mem,"il2cpp_free");
#undef ASTRAE_BIND
    status_.resolved_symbols=n;
    status_.missing_required=0;
    const void *required[]={reinterpret_cast<const void*>(api_.domain_get),reinterpret_cast<const void*>(api_.domain_get_assemblies),reinterpret_cast<const void*>(api_.assembly_get_image),reinterpret_cast<const void*>(api_.class_from_name),reinterpret_cast<const void*>(api_.class_get_field_from_name),reinterpret_cast<const void*>(api_.field_get_offset),reinterpret_cast<const void*>(api_.field_get_type),reinterpret_cast<const void*>(api_.class_from_type),reinterpret_cast<const void*>(api_.class_get_method_from_name),reinterpret_cast<const void*>(api_.runtime_invoke),reinterpret_cast<const void*>(api_.object_new),reinterpret_cast<const void*>(api_.object_get_class),reinterpret_cast<const void*>(api_.class_is_valuetype),reinterpret_cast<const void*>(api_.field_get_value),reinterpret_cast<const void*>(api_.class_get_name),reinterpret_cast<const void*>(api_.thread_attach),reinterpret_cast<const void*>(api_.thread_detach)};
    for(auto p:required)if(!p)++status_.missing_required;
    status_.can_read=api_.object_get_class&&api_.class_from_type&&api_.field_get_type&&api_.field_get_offset&&api_.field_get_value&&api_.class_is_valuetype&&api_.gchandle_new&&api_.gchandle_free;
    status_.can_build=api_.gchandle_new&&api_.gchandle_get_target&&api_.gchandle_free&&api_.gc_wbarrier_set_field&&api_.array_new&&api_.string_new&&api_.value_box&&api_.class_value_size&&api_.field_get_type&&api_.class_from_type&&api_.object_new&&api_.runtime_invoke;
    status_.can_generic_reflection=api_.class_get_type&&api_.type_get_object&&api_.class_from_system_type&&api_.object_get_class&&api_.runtime_invoke;
    status_.can_enumerate=api_.object_get_class&&api_.class_get_method_from_name&&api_.runtime_invoke&&api_.class_get_type&&api_.type_get_object&&api_.class_from_system_type;
}
#ifndef __ANDROID__
bool Runtime::initialize_for_testing(const Api &api,void *domain){
    std::lock_guard<std::mutex> lock(mutex_);
    api_=api;domain_=domain;handle_=nullptr;status_={};status_.loaded=true;status_.initialized=domain_!=nullptr;status_.core_network_class_ready=domain_!=nullptr;status_.library_path="mock-il2cpp-runtime";status_.assembly_count=1;
    const void *required[]={reinterpret_cast<const void*>(api_.domain_get),reinterpret_cast<const void*>(api_.domain_get_assemblies),reinterpret_cast<const void*>(api_.assembly_get_image),reinterpret_cast<const void*>(api_.class_from_name),reinterpret_cast<const void*>(api_.class_get_field_from_name),reinterpret_cast<const void*>(api_.field_get_offset),reinterpret_cast<const void*>(api_.field_get_type),reinterpret_cast<const void*>(api_.class_from_type),reinterpret_cast<const void*>(api_.class_get_method_from_name),reinterpret_cast<const void*>(api_.runtime_invoke),reinterpret_cast<const void*>(api_.object_new),reinterpret_cast<const void*>(api_.object_get_class),reinterpret_cast<const void*>(api_.class_is_valuetype),reinterpret_cast<const void*>(api_.field_get_value),reinterpret_cast<const void*>(api_.class_get_name),reinterpret_cast<const void*>(api_.thread_attach),reinterpret_cast<const void*>(api_.thread_detach)};
    for(auto p:required)if(!p)++status_.missing_required;
    status_.can_read=api_.object_get_class&&api_.class_from_type&&api_.field_get_type&&api_.field_get_offset&&api_.field_get_value&&api_.class_is_valuetype&&api_.gchandle_new&&api_.gchandle_free;
    status_.can_build=api_.gchandle_new&&api_.gchandle_get_target&&api_.gchandle_free&&api_.gc_wbarrier_set_field&&api_.array_new&&api_.string_new&&api_.value_box&&api_.class_value_size&&api_.field_get_type&&api_.class_from_type&&api_.object_new&&api_.runtime_invoke;
    status_.can_generic_reflection=api_.class_get_type&&api_.type_get_object&&api_.class_from_system_type&&api_.object_get_class&&api_.runtime_invoke;
    status_.can_enumerate=api_.object_get_class&&api_.class_get_method_from_name&&api_.runtime_invoke&&api_.class_get_type&&api_.type_get_object&&api_.class_from_system_type;
    status_.error=domain_?std::string():std::string("mock IL2CPP domain is null");
    return status_.initialized;
}
#endif
bool Runtime::initialize(std::string *error){
    std::lock_guard<std::mutex> lock(mutex_);
    if(status_.initialized)return true;
#if defined(__ANDROID__)
    if(handle_){dlclose(handle_);handle_=nullptr;}
    handle_=dlopen("libil2cpp.so",RTLD_NOW|RTLD_NOLOAD);
    if(!handle_){status_.error="libil2cpp.so is not loaded in this process";if(error)*error=status_.error;return false;}
    status_.loaded=true;status_.library_path="libil2cpp.so";resolve_symbols();
    if(status_.missing_required){status_.error="required IL2CPP exports are missing";if(error)*error=status_.error;return false;}
    domain_=api_.domain_get();
    if(!domain_){status_.error="il2cpp_domain_get returned null";if(error)*error=status_.error;return false;}
    size_t count=0;api_.domain_get_assemblies(domain_,&count);status_.assembly_count=count;
    status_.initialized=true;status_.error.clear();return true;
#else
    status_.error="IL2CPP runtime binding is available only inside Android game process";
    if(error)*error=status_.error;return false;
#endif
}
bool Runtime::initialize_wait(uint32_t timeout_ms,uint32_t poll_interval_ms,std::string *error){
#if defined(__ANDROID__)
    if(timeout_ms==0)timeout_ms=120000;if(poll_interval_ms==0)poll_interval_ms=100;auto deadline=std::chrono::steady_clock::now()+std::chrono::milliseconds(timeout_ms);std::string last;
    bool ready=false;
    do{if(initialize(&last)){ready=true;break;}if(last.find("required IL2CPP exports are missing")!=std::string::npos){if(error)*error=last;return false;}auto now=std::chrono::steady_clock::now();if(now>=deadline)break;auto left=std::chrono::duration_cast<std::chrono::milliseconds>(deadline-now).count();std::this_thread::sleep_for(std::chrono::milliseconds(std::min<uint32_t>(poll_interval_ms,static_cast<uint32_t>(std::max<int64_t>(1,left)))));}while(std::chrono::steady_clock::now()<deadline);
    if(ready){while(std::chrono::steady_clock::now()<deadline){ThreadScope scope(*this);if(scope.ok()){std::string class_error;if(find_class("_AT.Network.NetworkHandler",&class_error)){std::lock_guard<std::mutex> lock(mutex_);status_.core_network_class_ready=true;status_.error.clear();if(error)error->clear();return true;}last=class_error;}else last=scope.error();auto now=std::chrono::steady_clock::now();if(now>=deadline)break;auto left=std::chrono::duration_cast<std::chrono::milliseconds>(deadline-now).count();std::this_thread::sleep_for(std::chrono::milliseconds(std::min<uint32_t>(poll_interval_ms,static_cast<uint32_t>(std::max<int64_t>(1,left)))));}}
    last="timed out waiting for libil2cpp.so / _AT.Network.NetworkHandler"+(last.empty()?std::string():std::string("; last error: ")+last);{std::lock_guard<std::mutex> lock(mutex_);status_.error=last;}if(error)*error=last;return false;
#else
    (void)timeout_ms;(void)poll_interval_ms;return initialize(error);
#endif
}
void Runtime::shutdown(){
    std::unique_lock<std::mutex> lock(mutex_);shutting_down_=true;scope_cv_.wait(lock,[this]{return active_scopes_==0;});
    std::vector<uint32_t> outstanding;{std::lock_guard<std::mutex> handle_lock(public_handles_mutex_);outstanding.assign(public_handles_.begin(),public_handles_.end());}
    if(!outstanding.empty()){
        if(!api_.gchandle_free){status_.error="cannot shut down IL2CPP runtime while public GCHandles exist: gchandle_free is unavailable";shutting_down_=false;lock.unlock();scope_cv_.notify_all();return;}
        void *thread=api_.thread_current?api_.thread_current():nullptr;bool attached_here=false;
        if(!thread&&api_.thread_attach&&domain_){thread=api_.thread_attach(domain_);attached_here=thread!=nullptr;}
        if(!thread){status_.error="cannot shut down IL2CPP runtime: failed to attach current thread to release public GCHandles; runtime remains initialized";shutting_down_=false;lock.unlock();scope_cv_.notify_all();return;}
        for(uint32_t token:outstanding)api_.gchandle_free(token);
        if(attached_here&&api_.thread_detach)api_.thread_detach(thread);
        {std::lock_guard<std::mutex> handle_lock(public_handles_mutex_);public_handles_.clear();}
    }
#if defined(__ANDROID__)
    if(handle_){dlclose(handle_);handle_=nullptr;}
#endif
    domain_=nullptr;api_={};status_={};shutting_down_=false;lock.unlock();scope_cv_.notify_all();
}
bool Runtime::available() const {std::lock_guard<std::mutex> lock(mutex_);return status_.initialized&&domain_;}
Status Runtime::status() const {std::lock_guard<std::mutex> lock(mutex_);Status snapshot=status_;snapshot.attached=api_.thread_current&&api_.thread_current()!=nullptr;return snapshot;}
void *Runtime::find_class(const std::string &raw_name,std::string *error) const {
    if(error)error->clear();
    if(!available()){if(error)*error="IL2CPP runtime is not initialized";return nullptr;}
    std::string name=raw_name;
    const std::pair<const char*,const char*> aliases[]={
        {"string","String"},{"bool","Boolean"},{"byte","Byte"},{"sbyte","SByte"},{"short","Int16"},{"ushort","UInt16"},{"int","Int32"},{"uint","UInt32"},{"long","Int64"},{"ulong","UInt64"},{"float","Single"},{"double","Double"},{"object","Object"},{"char","Char"}
    };
    for(const auto &alias:aliases)if(name==alias.first)name=std::string("System.")+alias.second;
    std::vector<std::string> parts;size_t pos=0;while(pos<=name.size()){size_t dot=name.find('.',pos);parts.push_back(name.substr(pos,dot==std::string::npos?std::string::npos:dot-pos));if(dot==std::string::npos)break;pos=dot+1;}
    if(parts.empty()||parts.back().empty()){if(error)*error="invalid managed type name: "+raw_name;return nullptr;}
    size_t count=0;void **assemblies=api_.domain_get_assemblies(domain_,&count);if(!assemblies){if(error)*error="assembly list is unavailable";return nullptr;}
    const std::vector<std::string> default_namespaces={"","System","AstraeOratio","Astrae","NC","Game","Game.Data","NC.Game","NC.AstraeOratio","_AT","_AT.Messages","_AT.Network"};
    auto find_root=[&](const std::string &root_name,const std::string &ns,bool any_namespace)->void*{
        for(size_t i=0;i<count;++i){const void *image=api_.assembly_get_image(assemblies[i]);if(!image)continue;
            if(api_.class_from_name){void *k=api_.class_from_name(image,ns.c_str(),root_name.c_str());if(k)return k;}
            if(api_.image_get_class_count&&api_.image_get_class){size_t n=api_.image_get_class_count(image);if(n>500000)n=500000;for(size_t j=0;j<n;++j){void *k=api_.image_get_class(image,j);if(!k)continue;const char *cn=api_.class_get_name(k),*cns=api_.class_get_namespace?api_.class_get_namespace(k):nullptr;if(cn&&root_name==cn&&(any_namespace||(cns&&ns==cns)))return k;}}
        }return nullptr;
    };
    // Try every namespace/type split. Remaining path segments are nested CLR/IL2CPP types.
    for(size_t split=parts.size();split-->0;){
        if(parts[split].empty())continue;
        std::string ns;for(size_t i=0;i<split;++i){if(i)ns.push_back('.');ns+=parts[i];}
        std::vector<std::string> candidates;
        if(split==0)candidates=default_namespaces;else candidates.push_back(ns);
        for(const auto &candidate_ns:candidates){
            void *klass=find_root(parts[split],candidate_ns,split==0);if(!klass)continue;
            bool path_ok=true;
            for(size_t j=split+1;j<parts.size();++j){if(!api_.class_get_nested_types){path_ok=false;break;}void *iter=nullptr,*nested=nullptr;while((nested=api_.class_get_nested_types(klass,&iter))!=nullptr){const char *nested_name=api_.class_get_name?api_.class_get_name(nested):nullptr;if(nested_name&&parts[j]==nested_name)break;}if(!nested){path_ok=false;break;}klass=nested;}
            if(path_ok)return klass;
        }
    }
    if(error)*error="managed class or nested type not found: "+raw_name;return nullptr;
}
void *Runtime::find_field(void *klass,const std::string &name) const {for(void *k=klass;k;k=api_.class_get_parent?api_.class_get_parent(k):nullptr){void *f=api_.class_get_field_from_name?api_.class_get_field_from_name(k,name.c_str()):nullptr;if(f)return f;}return nullptr;}
void *Runtime::find_method(void *klass,const std::string &name,int argc) const {for(void *k=klass;k;k=api_.class_get_parent?api_.class_get_parent(k):nullptr){void *m=api_.class_get_method_from_name?api_.class_get_method_from_name(k,name.c_str(),argc):nullptr;if(m)return m;}return nullptr;}
void *Runtime::generic_argument_class(void *closed_generic_class,size_t index,std::string *error) const {
    if(sizeof(void*)!=8||!available()||!closed_generic_class||!api_.class_get_type||!api_.type_get_object||!api_.class_from_system_type||!api_.object_get_class||!api_.runtime_invoke){if(error)*error="generic reflection exports are unavailable or ABI is not 64-bit";return nullptr;}
    ThreadScope scope(const_cast<Runtime&>(*this));if(!scope.ok()){if(error)*error=scope.error();return nullptr;}
    const Api &a=api_;
    const void *closed_type=a.class_get_type(closed_generic_class);if(!closed_type){if(error)*error="il2cpp_class_get_type returned null for closed generic class";return nullptr;}
    void *type_object=a.type_get_object(closed_type);if(!type_object){if(error)*error="cannot obtain System.Type for managed generic class";return nullptr;}
    TemporaryRoot type_root(*this,type_object);if(!type_root.ok()){if(error)*error="cannot root System.Type during generic reflection";return nullptr;}
    void *type_class=a.object_get_class(type_object);void *method=find_method(type_class,"GetGenericArguments",0);if(!method){if(error)*error="System.Type.GetGenericArguments is unavailable";return nullptr;}
    void *exception=nullptr;void *array=a.runtime_invoke(method,type_object,nullptr,&exception);
    if(exception){std::string detail="GetGenericArguments invocation failed";if(a.format_exception){char *formatted=a.format_exception(exception);if(formatted&&*formatted)detail+=std::string(": ")+formatted;if(formatted&&a.free_mem)a.free_mem(formatted);}if(error)*error=detail;return nullptr;}
    if(!array){if(error)*error="GetGenericArguments returned null";return nullptr;}
    TemporaryRoot array_root(*this,array);if(!array_root.ok()){if(error)*error="cannot root generic argument array";return nullptr;}
    struct ArrayView{void *klass;void *monitor;void *bounds;uintptr_t length;alignas(8) void *items[1];};auto *view=static_cast<ArrayView*>(array);
    if(view->length>64){if(error)*error="generic argument array length exceeds safety limit";return nullptr;}
    if(index>=view->length){if(error)*error="generic type argument index is out of range";return nullptr;}
    void *arg_type=view->items[index];if(!arg_type){if(error)*error="generic argument System.Type is null";return nullptr;}
    TemporaryRoot arg_root(*this,arg_type);if(!arg_root.ok()){if(error)*error="cannot root generic argument System.Type";return nullptr;}
    void *result=a.class_from_system_type(arg_type);if(!result&&error)*error="cannot resolve closed generic type argument";return result;
}
bool Runtime::invoke(void *method,void *instance,void **arguments,void **result,std::string *error) const {
    if(error)error->clear();if(result)*result=nullptr;if(!method){if(error)*error="managed method pointer is null";return false;}
    ThreadScope scope(const_cast<Runtime&>(*this));if(!scope.ok()){if(error)*error=scope.error();return false;}if(!api_.runtime_invoke){if(error)*error="il2cpp_runtime_invoke is unavailable";return false;}
    void *exception=nullptr;void *value=api_.runtime_invoke(method,instance,arguments,&exception);
    if(exception){std::string detail="managed method invocation raised an exception";if(api_.format_exception){char *formatted=api_.format_exception(exception);if(formatted&&*formatted)detail+=std::string(": ")+formatted;if(formatted&&api_.free_mem)api_.free_mem(formatted);}if(error)*error=detail;return false;}
    if(result)*result=value;return true;
}
uint32_t Runtime::retain(void *object,bool pinned) const {return object&&api_.gchandle_new?api_.gchandle_new(object,pinned):0;}
void *Runtime::target(uint32_t handle) const {return handle&&api_.gchandle_get_target?api_.gchandle_get_target(handle):nullptr;}
void Runtime::release(uint32_t handle) const {if(handle&&api_.gchandle_free)api_.gchandle_free(handle);}
bool Runtime::publish_handle(uint32_t handle){if(!handle)return false;std::lock_guard<std::mutex> runtime_lock(mutex_);if(!status_.initialized||shutting_down_)return false;std::lock_guard<std::mutex> handle_lock(public_handles_mutex_);return public_handles_.insert(handle).second;}
void *Runtime::public_handle_target(uint32_t handle) const {if(!handle)return nullptr;ThreadScope scope(const_cast<Runtime&>(*this));if(!scope.ok())return nullptr;std::lock_guard<std::mutex> lock(public_handles_mutex_);if(!public_handles_.count(handle))return nullptr;return target(handle);}
bool Runtime::release_public_handle(uint32_t handle){if(!handle)return false;ThreadScope scope(*this);if(!scope.ok())return false;std::lock_guard<std::mutex> lock(public_handles_mutex_);if(public_handles_.erase(handle)==0)return false;release(handle);return true;}
Runtime::ThreadScope::ThreadScope(Runtime &runtime):runtime_(runtime){
    std::lock_guard<std::mutex> lock(runtime_.mutex_);
    if(!runtime_.status_.initialized||!runtime_.domain_||runtime_.shutting_down_){error_="IL2CPP runtime is not initialized or is shutting down";return;}
    const Api &a=runtime_.api_;
    if(a.thread_current&&(thread_=a.thread_current())){++runtime_.active_scopes_;ok_=true;return;}
    thread_=a.thread_attach? a.thread_attach(runtime_.domain_):nullptr;
    if(!thread_){error_="il2cpp_thread_attach returned null";return;}
    attached_here_=true;++runtime_.active_scopes_;ok_=true;
}
Runtime::ThreadScope::~ThreadScope(){
    if(!ok_)return;if(attached_here_&&thread_&&runtime_.api_.thread_detach)runtime_.api_.thread_detach(thread_);
    {std::lock_guard<std::mutex> lock(runtime_.mutex_);if(runtime_.active_scopes_>0)--runtime_.active_scopes_;if(runtime_.active_scopes_==0)runtime_.scope_cv_.notify_all();}
}
}
