#include "lobby_restore.hpp"
#include "il2cpp_runtime.hpp"
#include <cassert>
#include <cstdint>
#include <cstring>
#include <iostream>
#include <string>
#include <vector>

namespace {
int g_common_class=1,g_secure_class=2,g_setter_method=3,g_getter_method=4,g_assembly=5,g_image=6,g_thread=7,g_boxed_value=0;bool g_write_enabled=true;
template<class T> void put(void *base,size_t offset,T value){std::memcpy(static_cast<uint8_t*>(base)+offset,&value,sizeof(value));}
struct Fixture {std::vector<uint8_t> dm,root,table,wrapper,common;Fixture():dm(0x18),root(0x148),table(0x20),wrapper(0x18),common(0xd0){put(dm.data(),0x10,root.data());put(root.data(),0x140,table.data());put(table.data(),0x18,wrapper.data());put(wrapper.data(),0x10,common.data());put(common.data(),0x00,&g_common_class);}};
void **domain_get_assemblies(const void*,size_t *count){static void *assemblies[]={&g_assembly};*count=1;return assemblies;}
const void *assembly_get_image(const void*){return &g_image;}
void *class_from_name(const void*,const char *ns,const char *name){return ns&&name&&std::strcmp(ns,"_AT.Shared")==0&&std::strcmp(name,"SecureInt")==0?&g_secure_class:nullptr;}
void *object_get_class(void *object){return object?&g_common_class:nullptr;}
const char *class_get_name(void *klass){return klass==&g_secure_class?"SecureInt":"CommonData";}
void *class_get_method_from_name(void *klass,const char *name,int argc){if(klass==&g_secure_class&&name&&std::strcmp(name,"set_Value")==0&&argc==1)return &g_setter_method;if(klass==&g_common_class&&name&&std::strcmp(name,"get_Inventory_Artifact_Max_Slots")==0&&argc==0)return &g_getter_method;return nullptr;}
void *runtime_invoke(void *method,void *instance,void **arguments,void **exception){if(exception)*exception=nullptr;if(method==&g_setter_method){if(!instance||!arguments||!arguments[0]){if(exception)*exception=reinterpret_cast<void*>(1);return nullptr;}if(g_write_enabled)*static_cast<int32_t*>(instance)=*static_cast<int32_t*>(arguments[0]);return nullptr;}if(method==&g_getter_method){if(!instance){if(exception)*exception=reinterpret_cast<void*>(1);return nullptr;}g_boxed_value=*reinterpret_cast<int32_t*>(static_cast<uint8_t*>(instance)+0xc8);return &g_boxed_value;}if(exception)*exception=reinterpret_cast<void*>(1);return nullptr;}
void *object_unbox(void *object){return object;}
void *thread_current(){return &g_thread;}
void verify_success(){Fixture f;g_write_enabled=true;g_boxed_value=0;std::string error;bool ok=astrae::lobby::artifact_capacity_tables(f.dm.data(),&error);assert(ok);assert(error.empty());int32_t actual=0;std::memcpy(&actual,f.common.data()+0xc8,sizeof(actual));assert(actual==9999);}
void verify_readback_failure(){Fixture f;g_write_enabled=false;int32_t old=37;std::memcpy(f.common.data()+0xc8,&old,sizeof(old));std::string error;bool ok=astrae::lobby::artifact_capacity_tables(f.dm.data(),&error);assert(!ok);assert(error.find("expected 9999, got 37")!=std::string::npos);}
void verify_missing_common(){Fixture f;void *null_object=nullptr;put(f.wrapper.data(),0x10,null_object);std::string error;assert(!astrae::lobby::artifact_capacity_tables(f.dm.data(),&error));assert(error.find("Common managed object is null")!=std::string::npos);}
}
int main(){astrae::il2cpp::Api api{};api.domain_get_assemblies=&domain_get_assemblies;api.assembly_get_image=&assembly_get_image;api.class_from_name=&class_from_name;api.class_get_name=&class_get_name;api.class_get_method_from_name=&class_get_method_from_name;api.object_get_class=&object_get_class;api.object_unbox=&object_unbox;api.runtime_invoke=&runtime_invoke;api.thread_current=&thread_current;int domain=1;assert(astrae::il2cpp::Runtime::instance().initialize_for_testing(api,&domain));verify_success();verify_readback_failure();verify_missing_common();std::cout<<"artifact_capacity_tables: exact path, setter/getter verification, missing-object and mismatch cases validated\n";return 0;}
