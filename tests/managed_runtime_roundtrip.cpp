#include "il2cpp_runtime.hpp"
#include "managed_wire.hpp"
#include "astrae_backend.h"
#include <cassert>
#include <cstddef>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <fstream>
#include <map>
#include <string>
#include <atomic>
#include <chrono>
#include <thread>
#include <vector>

using namespace astrae;
namespace {
struct FakeClass;
struct FakeType { FakeClass *klass=nullptr; };
struct FakeField { std::string name; size_t offset=0; FakeType *type=nullptr; };
struct FakeMethod { std::string name; int parameter_count=0; };
struct FakeClass { std::string name,ns; bool value_type=false,is_enum=false; FakeClass *parent=nullptr; std::vector<FakeField> fields; };
struct FakeImage {};
struct FakeAssembly {};
struct FakeDomain {};
struct FakeObject { FakeClass *klass=nullptr; void *monitor=nullptr; alignas(8) uint8_t data[512]{}; };
struct FakeString { FakeClass *klass=nullptr; void *monitor=nullptr; int32_t length=0; char16_t chars[1]; };
struct FakeArray { FakeClass *klass=nullptr; void *monitor=nullptr; void *bounds=nullptr; uintptr_t max_length=0; alignas(8) uint8_t vector[1]; };
FakeDomain g_domain;
FakeImage g_image;
FakeAssembly g_assembly;
void *g_assemblies[]={&g_assembly};
FakeClass g_account{"AccountBaseInfo","",false,false,nullptr,{}};
FakeClass g_string{"String","System",false,false,nullptr,{}};
FakeClass g_u64{"UInt64","System",true,false,nullptr,{}};
FakeClass g_i32{"Int32","System",true,false,nullptr,{}};
FakeClass g_enum{"TestEnum","",true,true,nullptr,{}};
FakeClass g_enum_container{"TestEnumContainer","",false,false,nullptr,{}};
FakeClass g_enum_array{"TestEnum[]","",false,false,nullptr,{}};
FakeClass g_network_handler{"NetworkHandler","_AT.Network",false,false,nullptr,{}};
FakeClass g_test_message_root{"TestMessage","_AT.Messages",false,false,nullptr,{}};
FakeClass g_test_request{"Request","_AT.Messages",false,false,nullptr,{}};
FakeClass g_test_response{"Response","_AT.Messages",false,false,nullptr,{}};
FakeClass g_account_login_root{"AccountLogin","_AT.Messages",false,false,nullptr,{}};
FakeClass g_account_login_request{"Request","_AT.Messages",false,false,nullptr,{}};
FakeClass g_account_login_response{"Response","_AT.Messages",false,false,nullptr,{}};
FakeClass g_error_packet{"ErrorPacket","_AT.Messages",false,false,nullptr,{}};
FakeMethod g_echo_method{"Echo",1},g_test_ctor{".ctor",1},g_dispatch_method{"Dispatch",1},g_response_method{"OnResponseTestMessage",1},g_account_response_method{"OnResponseAccountLogin",1},g_error_response_method{"OnResponseError",1};
std::map<FakeClass*,std::vector<FakeClass*>> g_nested_classes;
void *g_last_dispatched=nullptr,*g_last_response=nullptr;
FakeType g_string_type{&g_string};
FakeType g_u64_type{&g_u64};
FakeType g_i32_type{&g_i32};
FakeClass g_i64{"Int64","System",true,false,nullptr,{}};
FakeType g_i64_type{&g_i64};
FakeType g_enum_array_type{&g_enum_array};
std::map<uint32_t,void*> g_handles;
std::vector<void*> g_allocations;
uint32_t g_next_handle=1;
int g_thread=1;bool g_force_no_current=false,g_fail_attach=false,g_raise_exception=false;int g_mock_exception=1;
void *fake_domain_get(){return &g_domain;}
void **fake_domain_get_assemblies(const void *,size_t *n){if(n)*n=1;return g_assemblies;}
const void *fake_assembly_get_image(const void *){return &g_image;}
void *fake_class_from_name(const void *,const char *ns,const char *name){std::string space=ns?ns:"",n=name?name:"";for(FakeClass *k:{&g_account,&g_string,&g_u64,&g_i32,&g_i64,&g_enum,&g_enum_container,&g_enum_array,&g_network_handler,&g_test_message_root,&g_test_request,&g_test_response,&g_account_login_root,&g_account_login_request,&g_account_login_response,&g_error_packet})if(k->name==n&&k->ns==space)return k;return nullptr;}
const char *fake_class_get_name(void *k){return k?static_cast<FakeClass*>(k)->name.c_str():nullptr;}
const char *fake_class_get_namespace(void *k){return k?static_cast<FakeClass*>(k)->ns.c_str():nullptr;}
void *fake_class_get_parent(void *k){return k?static_cast<FakeClass*>(k)->parent:nullptr;}
void *fake_class_get_nested_types(void *k,void **iter){if(!k||!iter)return nullptr;auto it=g_nested_classes.find(static_cast<FakeClass*>(k));if(it==g_nested_classes.end())return nullptr;uintptr_t i=reinterpret_cast<uintptr_t>(*iter);if(i>=it->second.size())return nullptr;*iter=reinterpret_cast<void*>(i+1);return it->second[i];}
void *fake_class_get_field(void *k,const char *name){if(!k||!name)return nullptr;for(auto &f:static_cast<FakeClass*>(k)->fields)if(f.name==name)return &f;return nullptr;}
size_t fake_field_offset(void *f){return f?static_cast<FakeField*>(f)->offset:0;}
const void *fake_field_type(void *f){return f?static_cast<FakeField*>(f)->type:nullptr;}
void *fake_class_from_type(const void *t){return t?static_cast<const FakeType*>(t)->klass:nullptr;}
void *fake_object_get_class(void *o){return o?static_cast<FakeObject*>(o)->klass:nullptr;}
size_t fake_class_instance_size(void *){return sizeof(FakeObject);}
size_t fake_class_value_size(void *k,uint32_t *){return k==&g_u64||k==&g_i64?sizeof(uint64_t):(k==&g_i32||k==&g_enum?sizeof(uint32_t):0);}
bool fake_class_is_value_type(void *k){return k&&static_cast<FakeClass*>(k)->value_type;}
bool fake_class_is_enum(void *k){return k&&static_cast<FakeClass*>(k)->is_enum;}
size_t fake_field_storage_size(FakeField *f){if(!f||!f->type||!f->type->klass)return sizeof(void*);FakeClass *k=f->type->klass;return k->value_type?fake_class_value_size(k,nullptr):sizeof(void*);}
void fake_field_get_value(void *o,void *f,void *out){if(!o||!f||!out)return;auto *field=static_cast<FakeField*>(f);std::memcpy(out,static_cast<uint8_t*>(o)+field->offset,fake_field_storage_size(field));}
void fake_field_set_value(void *o,void *f,void *in){if(!o||!f||!in)return;auto *field=static_cast<FakeField*>(f);std::memcpy(static_cast<uint8_t*>(o)+field->offset,in,fake_field_storage_size(field));}
void *fake_object_new(void *k){auto *o=static_cast<FakeObject*>(std::calloc(1,sizeof(FakeObject)));if(!o)return nullptr;o->klass=static_cast<FakeClass*>(k);g_allocations.push_back(o);return o;}
void *fake_string_new(const char *s){if(!s)return nullptr;size_t n=std::strlen(s);size_t size=offsetof(FakeString,chars)+(n+1)*sizeof(char16_t);auto *x=static_cast<FakeString*>(std::calloc(1,size));if(!x)return nullptr;x->klass=&g_string;x->length=static_cast<int32_t>(n);for(size_t i=0;i<n;++i)x->chars[i]=static_cast<unsigned char>(s[i]);g_allocations.push_back(x);return x;}
void fake_gc_wbarrier(void *,void **slot,void *value){if(slot)*slot=value;}
uint32_t fake_gchandle_new(void *o,bool){if(!o)return 0;uint32_t h=g_next_handle++;g_handles[h]=o;return h;}
void *fake_gchandle_get_target(uint32_t h){auto i=g_handles.find(h);return i==g_handles.end()?nullptr:i->second;}
void fake_gchandle_free(uint32_t h){g_handles.erase(h);}
void *fake_thread_current(){return g_force_no_current?nullptr:&g_thread;}
void *fake_thread_attach(void *){return g_fail_attach?nullptr:&g_thread;}
void fake_thread_detach(void *){}
void *fake_class_get_method(void *klass,const char *name,int argc){if(!name||argc!=1)return nullptr;std::string n=name;if(klass==&g_network_handler){if(n=="Echo")return &g_echo_method;if(n=="Dispatch")return &g_dispatch_method;if(n=="OnResponseTestMessage")return &g_response_method;if(n=="OnResponseAccountLogin")return &g_account_response_method;if(n=="OnResponseError")return &g_error_response_method;}if(klass==&g_test_message_root&&n==".ctor")return &g_test_ctor;return nullptr;}
void *fake_runtime_invoke(void *method,void *instance,void **args,void **exception){if(exception)*exception=nullptr;if(!instance)return nullptr;if(g_raise_exception){if(exception)*exception=&g_mock_exception;return nullptr;}if(method==&g_echo_method&&args&&args[0])return args[0];if(method==&g_test_ctor&&args&&args[0]){std::memcpy(static_cast<uint8_t*>(instance)+16,args[0],sizeof(int32_t));return nullptr;}if(method==&g_dispatch_method&&args&&args[0]){g_last_dispatched=args[0];return nullptr;}if((method==&g_response_method||method==&g_account_response_method||method==&g_error_response_method)&&args&&args[0]){g_last_response=args[0];return nullptr;}return nullptr;}
void *fake_object_unbox(void *o){return o?static_cast<uint8_t*>(o)+sizeof(void*)*2:nullptr;}
void *fake_value_box(void *,void *klass,void *data){auto *o=static_cast<FakeObject*>(std::calloc(1,sizeof(FakeObject)));if(!o)return nullptr;o->klass=static_cast<FakeClass*>(klass);size_t n=fake_class_value_size(klass,nullptr);if(data&&n)std::memcpy(o->data,data,n);g_allocations.push_back(o);return o;}
void *fake_array_new(void *element,uintptr_t count){(void)element;size_t size=offsetof(FakeArray,vector)+count*fake_class_value_size(&g_enum,nullptr);auto *a=static_cast<FakeArray*>(std::calloc(1,size));if(!a)return nullptr;a->klass=&g_enum_array;a->max_length=count;g_allocations.push_back(a);return a;}
const void *fake_class_get_type(void *){return nullptr;}
void *fake_type_get_object(const void *){return nullptr;}
void *fake_class_from_system_type(void *){return nullptr;}
void *fake_class_get_element_class(void *k){return k==&g_enum_array?&g_enum:nullptr;}
char *fake_format_exception(void *){const char *s="MockInvocationException";char *p=static_cast<char*>(std::malloc(std::strlen(s)+1));if(p)std::memcpy(p,s,std::strlen(s)+1);return p;}
void fake_free(void *p){std::free(p);}
void setup_classes(){g_account.fields={{"ServerId",16,&g_u64_type},{"NickName",24,&g_string_type}};g_enum.fields={{"value__",16,&g_i32_type}};g_enum_container.fields={{"Values",16,&g_enum_array_type}};g_test_response.fields={{"ServerTime",16,&g_i64_type},{"Result",24,&g_i32_type}};g_account_login_request.fields={{"Value",16,&g_i32_type}};g_account_login_response.fields={{"ServerTime",16,&g_i64_type},{"Result",24,&g_i32_type}};g_error_packet.fields={{"ServerTime",16,&g_i64_type},{"ErrorType",24,&g_i32_type}};g_nested_classes[&g_test_message_root]={&g_test_request,&g_test_response};g_nested_classes[&g_account_login_root]={&g_account_login_request,&g_account_login_response};}
void cleanup(){for(void *p:g_allocations)std::free(p);g_allocations.clear();g_handles.clear();}
}
int main(int argc,char **argv){
    assert(argc==4);setup_classes();il2cpp::Api api{};
    api.domain_get=fake_domain_get;api.domain_get_assemblies=fake_domain_get_assemblies;api.assembly_get_image=fake_assembly_get_image;api.class_from_name=fake_class_from_name;api.class_get_name=fake_class_get_name;api.class_get_namespace=fake_class_get_namespace;api.class_get_parent=fake_class_get_parent;api.class_get_nested_types=fake_class_get_nested_types;api.class_get_field_from_name=fake_class_get_field;api.field_get_offset=fake_field_offset;api.field_get_type=fake_field_type;api.class_from_type=fake_class_from_type;api.object_get_class=fake_object_get_class;api.class_instance_size=fake_class_instance_size;api.class_value_size=fake_class_value_size;api.class_is_valuetype=fake_class_is_value_type;api.class_is_enum=fake_class_is_enum;api.field_get_value=fake_field_get_value;api.field_set_value=fake_field_set_value;api.object_new=fake_object_new;api.string_new=fake_string_new;api.gc_wbarrier_set_field=fake_gc_wbarrier;api.gchandle_new=fake_gchandle_new;api.gchandle_get_target=fake_gchandle_get_target;api.gchandle_free=fake_gchandle_free;api.thread_current=fake_thread_current;api.thread_attach=fake_thread_attach;api.thread_detach=fake_thread_detach;api.class_get_method_from_name=fake_class_get_method;api.runtime_invoke=fake_runtime_invoke;api.object_unbox=fake_object_unbox;api.value_box=fake_value_box;api.array_new=fake_array_new;api.class_get_type=fake_class_get_type;api.type_get_object=fake_type_get_object;api.class_from_system_type=fake_class_from_system_type;api.class_get_element_class=fake_class_get_element_class;api.format_exception=fake_format_exception;api.free_mem=fake_free;
    auto &runtime=il2cpp::Runtime::instance();assert(runtime.initialize_for_testing(api,&g_domain));
    std::string error;auto &wire=managed::ManagedWire::instance();assert(wire.load_schema(argv[1],&error));
    assert(runtime.find_class("TestMessage.Response")==&g_test_response);assert(runtime.find_class("_AT.Messages.TestMessage.Response")==&g_test_response);
    auto *controller=static_cast<FakeObject*>(fake_object_new(&g_network_handler));void *invoke_args[]={controller};void *invoke_result=nullptr;void *echo_method=runtime.find_method(&g_network_handler,"Echo",1);assert(echo_method==&g_echo_method);assert(runtime.invoke(echo_method,controller,invoke_args,&invoke_result,&error));assert(error.empty()&&invoke_result==controller);g_raise_exception=true;assert(!runtime.invoke(echo_method,controller,invoke_args,&invoke_result,&error));assert(invoke_result==nullptr&&error.find("MockInvocationException")!=std::string::npos);g_raise_exception=false;
    auto *source=static_cast<FakeObject*>(fake_object_new(&g_account));uint64_t id=0xFEDCBA9876543210ULL;std::memcpy(reinterpret_cast<uint8_t*>(source)+16,&id,sizeof(id));void *nick=fake_string_new("Northstar");std::memcpy(reinterpret_cast<uint8_t*>(source)+24,&nick,sizeof(nick));
    std::string raw=wire.read_object_json("AccountBaseInfo",source,&error);assert(error.empty());assert(raw.find("18364758544493064720")!=std::string::npos);assert(raw.find("Northstar")!=std::string::npos);assert(g_handles.empty());
    uint32_t handle=wire.build_object_handle("AccountBaseInfo",R"({"ServerId":"18446744073709551615","NickName":"RoundTrip"})",&error);assert(handle!=0);assert(error.empty());auto *built=runtime.public_handle_target(handle);assert(built);std::string roundtrip=wire.read_object_json("AccountBaseInfo",built,&error);assert(error.empty());assert(roundtrip.find("18446744073709551615")!=std::string::npos);assert(roundtrip.find("RoundTrip")!=std::string::npos);assert(runtime.release_public_handle(handle));assert(!runtime.release_public_handle(handle));assert(runtime.public_handle_target(handle)==nullptr);assert(g_handles.empty());
    handle=wire.build_object_handle("AccountBaseInfo",R"({"ServerId":"18446744073709551616","NickName":"Overflow"})",&error);assert(handle==0);assert(!error.empty());assert(g_handles.empty());
    error.clear();std::string wrong=wire.read_object_json("ItemInfo",source,&error);assert(wrong.empty());assert(!error.empty());assert(g_handles.empty());
    std::string enum_schema=argv[1]+std::string(".mock-enum.json");{std::ofstream f(enum_schema);f<<R"([{"name":"TestEnumContainer","is_struct":false,"fields":[{"name":"Values","type":"TestEnum[]","element":"TestEnum","offset":16,"kind":"a"}]}])";}assert(wire.load_schema(enum_schema,&error));handle=wire.build_object_handle("TestEnumContainer",R"({"Values":[7,2,255]})",&error);assert(handle!=0);auto *enum_object=runtime.public_handle_target(handle);assert(enum_object);std::string enum_json=wire.read_object_json("TestEnumContainer",enum_object,&error);assert(error.empty());assert(enum_json.find("[7,2,255]")!=std::string::npos);assert(runtime.release_public_handle(handle));std::remove(enum_schema.c_str());assert(g_handles.empty());
    const std::string bridge_schema=argv[1]+std::string(".stage4-message.json");{std::ofstream f(bridge_schema);f<<R"([{"name":"TestMessage.Request","count":1,"is_struct":false,"fields":[{"name":"Value","type":"int","element":"","offset":16,"kind":"i"}]},{"name":"TestMessage.Response","count":2,"is_struct":false,"fields":[{"name":"ServerTime","type":"long","element":"","offset":16,"kind":"l"},{"name":"Result","type":"int","element":"","offset":24,"kind":"i"}]},{"name":"AccountLogin.Request","count":1,"is_struct":false,"fields":[{"name":"Value","type":"int","element":"","offset":16,"kind":"i"}]},{"name":"AccountLogin.Response","count":2,"is_struct":false,"fields":[{"name":"ServerTime","type":"long","element":"","offset":16,"kind":"l"},{"name":"Result","type":"int","element":"","offset":24,"kind":"i"}]},{"name":"ErrorPacket","count":2,"is_struct":false,"fields":[{"name":"ServerTime","type":"long","element":"","offset":16,"kind":"l"},{"name":"ErrorType","type":"int","element":"","offset":24,"kind":"i"}]}])";}assert(wire.load_schema(bridge_schema,&error));
    int32_t ctor_value=37;void *ctor_args[]={&ctor_value};g_last_dispatched=nullptr;assert(AstraeOffline_DispatchManagedMessage(controller,"TestMessage",1,ctor_args)==1);assert(g_last_dispatched&&static_cast<FakeObject*>(g_last_dispatched)->klass==&g_test_message_root);int32_t dispatched_value=0;std::memcpy(&dispatched_value,static_cast<uint8_t*>(g_last_dispatched)+16,sizeof(dispatched_value));assert(dispatched_value==37);
    g_last_response=nullptr;assert(AstraeOffline_RespondManaged(controller,"TestMessage",R"({"Result":42})")==1);assert(g_last_response&&static_cast<FakeObject*>(g_last_response)->klass==&g_test_response);int32_t response_value=0;std::memcpy(&response_value,static_cast<uint8_t*>(g_last_response)+24,sizeof(response_value));assert(response_value==42);int64_t server_ticks=0;std::memcpy(&server_ticks,static_cast<uint8_t*>(g_last_response)+16,sizeof(server_ticks));assert(server_ticks>621355968000000000LL);assert(g_handles.empty());g_last_response=nullptr;assert(AstraeOffline_RespondManaged(controller,"ErrorPacket",R"({"ErrorType":3})")==1);assert(g_last_response&&static_cast<FakeObject*>(g_last_response)->klass==&g_error_packet);int32_t error_type=0;std::memcpy(&error_type,reinterpret_cast<uint8_t*>(g_last_response)+24,sizeof(error_type));assert(error_type==3);assert(g_handles.empty());
    assert(AstraeOffline_Init(argv[3],argv[2])==1);auto *ambiguous_request=fake_object_new(&g_test_request);assert(AstraeOffline_HandleManagedRequest(controller,ambiguous_request)==0);auto *login_request=static_cast<FakeObject*>(fake_object_new(&g_account_login_request));int32_t login_value=9;std::memcpy(reinterpret_cast<uint8_t*>(login_request)+16,&login_value,sizeof(login_value));g_last_response=nullptr;assert(AstraeOffline_HandleManagedRequestForProtocol(controller,"AccountLogin.Request",login_request)==1);assert(g_last_response&&static_cast<FakeObject*>(g_last_response)->klass==&g_account_login_response);int64_t login_ticks=0;std::memcpy(&login_ticks,static_cast<uint8_t*>(g_last_response)+16,sizeof(login_ticks));assert(login_ticks>621355968000000000LL);assert(g_handles.empty());AstraeOffline_Shutdown();
    std::remove(bridge_schema.c_str());assert(wire.load_schema(argv[1],&error));
    handle=wire.build_object_handle("AccountBaseInfo",R"({"ServerId":"42","NickName":"ShutdownRoot"})",&error);assert(handle!=0);assert(g_handles.size()==1);std::atomic<bool> shutdown_started{false},shutdown_finished{false};std::thread shutdown_thread;
    {il2cpp::Runtime::ThreadScope active(runtime);assert(active.ok());shutdown_thread=std::thread([&]{shutdown_started=true;runtime.shutdown();shutdown_finished=true;});while(!shutdown_started.load())std::this_thread::yield();std::this_thread::sleep_for(std::chrono::milliseconds(20));assert(!shutdown_finished.load());}
    shutdown_thread.join();assert(shutdown_finished.load());assert(g_handles.empty());assert(runtime.public_handle_target(handle)==nullptr);
    assert(runtime.initialize_for_testing(api,&g_domain));assert(wire.load_schema(argv[1],&error));handle=wire.build_object_handle("AccountBaseInfo",R"({"ServerId":"99","NickName":"AttachFailure"})",&error);assert(handle!=0);assert(g_handles.size()==1);
    g_force_no_current=true;g_fail_attach=true;runtime.shutdown();assert(runtime.available());assert(g_handles.size()==1);assert(runtime.status().error.find("runtime remains initialized")!=std::string::npos);
    g_fail_attach=false;assert(runtime.public_handle_target(handle)!=nullptr);assert(runtime.release_public_handle(handle));assert(g_handles.empty());g_force_no_current=false;runtime.shutdown();assert(!runtime.available());cleanup();std::puts("managed runtime read/build round-trip, nested type reflection, managed message Dispatch/Response bridge, full managed Request-to-local-Backend-to-managed Response pipeline, ErrorPacket callback, enum arrays, invocation exceptions, handle validation, and shutdown lifecycle passed");return 0;
}
