#pragma once
#include <cstddef>
#include <cstdint>
#include <string>
#include <mutex>
#include <condition_variable>
#include <vector>
#include <unordered_set>

namespace astrae::il2cpp {
struct Api {
    void *(*domain_get)()=nullptr;
    void **(*domain_get_assemblies)(const void *, size_t *)=nullptr;
    const void *(*assembly_get_image)(const void *)=nullptr;
    const char *(*image_get_name)(const void *)=nullptr;
    size_t (*image_get_class_count)(const void *)=nullptr;
    void *(*image_get_class)(const void *, size_t)=nullptr;
    void *(*class_from_name)(const void *, const char *, const char *)=nullptr;
    const char *(*class_get_name)(void *)=nullptr;
    const char *(*class_get_namespace)(void *)=nullptr;
    void *(*class_get_parent)(void *)=nullptr;
    void *(*class_get_declaring_type)(void *)=nullptr;
    void *(*class_get_nested_types)(void *,void **)=nullptr;
    const void *(*class_get_type)(void *)=nullptr;
    void *(*class_from_type)(const void *)=nullptr;
    void *(*class_from_system_type)(void *)=nullptr;
    void *(*type_get_object)(const void *)=nullptr;
    const char *(*type_get_name)(const void *)=nullptr;
    void *(*class_get_field_from_name)(void *, const char *)=nullptr;
    size_t (*field_get_offset)(void *)=nullptr;
    const void *(*field_get_type)(void *)=nullptr;
    void *(*class_get_method_from_name)(void *, const char *, int)=nullptr;
    const char *(*method_get_name)(void *)=nullptr;
    uint32_t (*method_get_param_count)(void *)=nullptr;
    void *(*runtime_invoke)(void *, void *, void **, void **)=nullptr;
    void *(*object_new)(void *)=nullptr;
    void *(*object_unbox)(void *)=nullptr;
    void *(*value_box)(void *,void *,void *)=nullptr;
    void *(*object_get_class)(void *)=nullptr;
    void *(*string_new)(const char *)=nullptr;
    void *(*array_new)(void *, uintptr_t)=nullptr;
    size_t (*class_value_size)(void *, uint32_t *)=nullptr;
    size_t (*class_instance_size)(void *)=nullptr;
    bool (*class_is_valuetype)(void *)=nullptr;
    bool (*class_is_enum)(void *)=nullptr;
    void *(*class_get_element_class)(void *)=nullptr;
    void (*field_get_value)(void *, void *, void *)=nullptr;
    void (*field_set_value)(void *, void *, void *)=nullptr;
    void (*gc_wbarrier_set_field)(void *, void **, void *)=nullptr;
    uint32_t (*gchandle_new)(void *, bool)=nullptr;
    void *(*gchandle_get_target)(uint32_t)=nullptr;
    void (*gchandle_free)(uint32_t)=nullptr;
    void *(*thread_attach)(void *)=nullptr;
    void (*thread_detach)(void *)=nullptr;
    void *(*thread_current)()=nullptr;
    char *(*format_exception)(void *)=nullptr;
    void (*free_mem)(void *)=nullptr;
};
struct Status {
    bool loaded=false;
    bool initialized=false;
    bool core_network_class_ready=false;
    bool attached=false;
    bool can_read=false;
    bool can_build=false;
    bool can_generic_reflection=false;
    bool can_enumerate=false;
    std::string error;
    std::string library_path;
    std::string domain_name;
    size_t assembly_count=0;
    size_t resolved_symbols=0;
    size_t missing_required=0;
};
class Runtime {
public:
    static Runtime &instance();
    bool initialize(std::string *error=nullptr);
#ifndef __ANDROID__
    bool initialize_for_testing(const Api &api,void *domain);
#endif
    bool initialize_wait(uint32_t timeout_ms=120000,uint32_t poll_interval_ms=100,std::string *error=nullptr);
    void shutdown();
    bool available() const;
    Status status() const;
    const Api &api() const { return api_; }
    void *domain() const { return domain_; }
    void *find_class(const std::string &full_name, std::string *error=nullptr) const;
    void *find_field(void *klass, const std::string &name) const;
    void *find_method(void *klass, const std::string &name, int argc) const;
    void *generic_argument_class(void *closed_generic_class,size_t index,std::string *error=nullptr) const;
    bool invoke(void *method,void *instance,void **arguments,void **result,std::string *error=nullptr) const;
    uint32_t retain(void *object, bool pinned=false) const;
    void *target(uint32_t handle) const;
    void release(uint32_t handle) const;
    bool publish_handle(uint32_t handle);
    void *public_handle_target(uint32_t handle) const;
    bool release_public_handle(uint32_t handle);
    class ThreadScope {
    public:
        explicit ThreadScope(Runtime &runtime);
        ~ThreadScope();
        ThreadScope(const ThreadScope &)=delete;
        ThreadScope &operator=(const ThreadScope &)=delete;
        bool ok() const { return ok_; }
        const std::string &error() const { return error_; }
    private:
        Runtime &runtime_;
        void *thread_=nullptr;
        bool attached_here_=false;
        bool ok_=false;
        std::string error_;
    };
private:
    Runtime()=default;
    void *handle_=nullptr;
    void *domain_=nullptr;
    Api api_{};
    Status status_{};
    mutable std::mutex mutex_;
    std::condition_variable scope_cv_;
    size_t active_scopes_=0;
    bool shutting_down_=false;
    mutable std::mutex public_handles_mutex_;
    std::unordered_set<uint32_t> public_handles_;
    void resolve_symbols();
};
}
