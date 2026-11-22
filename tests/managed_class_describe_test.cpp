#include "il2cpp_runtime.hpp"
#include <cassert>
#include <cstdlib>
#include <cstring>
#include <iostream>
#include <string>

namespace {
struct FakeType{const char *name;};
struct FakeField{const char *name;size_t offset;const FakeType *type;};
struct FakeMethod{const char *name;const FakeType *ret;const FakeType *args[2];uint32_t argc;};
struct FakeClass{const char *ns;const char *name;size_t instance_size;FakeField *fields[2];FakeMethod *methods[2];};
int g_domain=1,g_assembly=2,g_image=3,g_thread=4;
FakeType kInt{"System.Int32"},kString{"System.String"},kVoid{"System.Void"};
FakeField fId{"Id",0x10,&kInt},fName{"Name",0x18,&kString};
FakeMethod mSet{"SetValue",&kVoid,{&kInt,nullptr},1},mFind{"Find",&kString,{&kString,&kInt},2};
FakeClass klass{"_AT.Core.DataSheet","Character_LevelFactorData",0x38,{&fId,&fName},{&mSet,&mFind}};
void **domain_get_assemblies(const void *,size_t *count){static void *items[]={&g_assembly};*count=1;return items;}
const void *assembly_get_image(const void *){return &g_image;}
void *class_from_name(const void *,const char *ns,const char *name){return std::strcmp(ns,klass.ns)==0&&std::strcmp(name,klass.name)==0?&klass:nullptr;}
const char *class_get_name(void *value){return static_cast<FakeClass*>(value)->name;}
const char *class_get_namespace(void *value){return static_cast<FakeClass*>(value)->ns;}
void *class_get_parent(void *){return nullptr;}
size_t class_instance_size(void *value){return static_cast<FakeClass*>(value)->instance_size;}
void *class_get_fields(void *value,void **iter){auto *k=static_cast<FakeClass*>(value);size_t i=reinterpret_cast<size_t>(*iter);if(i>=2)return nullptr;*iter=reinterpret_cast<void*>(i+1);return k->fields[i];}
const char *field_get_name(void *value){return static_cast<FakeField*>(value)->name;}
size_t field_get_offset(void *value){return static_cast<FakeField*>(value)->offset;}
const void *field_get_type(void *value){return static_cast<FakeField*>(value)->type;}
void *class_get_methods(void *value,void **iter){auto *k=static_cast<FakeClass*>(value);size_t i=reinterpret_cast<size_t>(*iter);if(i>=2)return nullptr;*iter=reinterpret_cast<void*>(i+1);return k->methods[i];}
const char *method_get_name(void *value){return static_cast<FakeMethod*>(value)->name;}
uint32_t method_get_param_count(void *value){return static_cast<FakeMethod*>(value)->argc;}
const void *method_get_param(void *value,uint32_t index){auto *m=static_cast<FakeMethod*>(value);return index<m->argc?m->args[index]:nullptr;}
const void *method_get_return_type(void *value){return static_cast<FakeMethod*>(value)->ret;}
char *type_get_name(const void *value){const char *name=static_cast<const FakeType*>(value)->name;size_t n=std::strlen(name)+1;char *copy=static_cast<char*>(std::malloc(n));if(copy)std::memcpy(copy,name,n);return copy;}
void free_mem(void *value){std::free(value);}
void *thread_current(){return &g_thread;}
}
int main(){astrae::il2cpp::Api api{};api.domain_get_assemblies=&domain_get_assemblies;api.assembly_get_image=&assembly_get_image;api.class_from_name=&class_from_name;api.class_get_name=&class_get_name;api.class_get_namespace=&class_get_namespace;api.class_get_parent=&class_get_parent;api.class_instance_size=&class_instance_size;api.class_get_fields=&class_get_fields;api.field_get_name=&field_get_name;api.field_get_offset=&field_get_offset;api.field_get_type=&field_get_type;api.class_get_methods=&class_get_methods;api.method_get_name=&method_get_name;api.method_get_param_count=&method_get_param_count;api.method_get_param=&method_get_param;api.method_get_return_type=&method_get_return_type;api.type_get_name=&type_get_name;api.free_mem=&free_mem;api.thread_current=&thread_current;assert(astrae::il2cpp::Runtime::instance().initialize_for_testing(api,&g_domain));const std::string desc=astrae::il2cpp::Runtime::instance().describe_class("_AT.Core.DataSheet.Character_LevelFactorData");assert(desc.find("class=_AT.Core.DataSheet.Character_LevelFactorData")!=std::string::npos);assert(desc.find("name=Id offset=0x10 type=System.Int32")!=std::string::npos);assert(desc.find("name=Name offset=0x18 type=System.String")!=std::string::npos);assert(desc.find("name=SetValue argc=1 return=System.Void arg0=System.Int32")!=std::string::npos);assert(desc.find("name=Find argc=2 return=System.String arg0=System.String arg1=System.Int32")!=std::string::npos);std::cout<<"managed class metadata enumeration: field offsets and method signatures validated\n";return 0;}
