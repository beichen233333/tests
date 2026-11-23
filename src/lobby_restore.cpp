#include "lobby_restore.hpp"
#include "character99_object_graph.hpp"
#include "il2cpp_runtime.hpp"
#include "managed_wire.hpp"
#include "embedded_assets.hpp"
#include "json.hpp"
#include "original_store_abi.h"
#include "debug_log.hpp"
#include <array>
#include <atomic>
#include <cstring>
#include <ctime>
#include <limits>
#include <mutex>
#include <sstream>
#include <cstdio>
#include <cstdlib>
#include <unordered_set>
#include <vector>

namespace astrae::lobby {
namespace {
constexpr uint64_t kPermanentRaidRowValue=0x2bca2875f4373fffULL;
void *load_pointer(const void *base,size_t offset){void *value=nullptr;if(base)std::memcpy(&value,static_cast<const uint8_t*>(base)+offset,sizeof(value));return value;}
uint64_t load_u64(const void *base,size_t offset){uint64_t value=0;if(base)std::memcpy(&value,static_cast<const uint8_t*>(base)+offset,sizeof(value));return value;}
}
cJSON *guide_state_from_document(const cJSON *document){
    const cJSON *stored=document?cJSON_GetObjectItemCaseSensitive(document,"guidebook"):nullptr;
    cJSON *state=stored?cJSON_Duplicate(stored,1):cJSON_CreateObject();if(!state)return nullptr;
    constexpr const char *keys[]={"offerings","claims","progress"};
    for(const char *key:keys)if(!cJSON_HasObjectItem(state,key))cJSON_AddObjectToObject(state,key);
    return state;
}
cJSON *guide_state(){return guide_state_from_document(store_document());}

bool artifact_capacity_tables(void *data_manager,std::string *error){
    if(error)error->clear();
    if(!data_manager){if(error)*error="DataManager is null for artifact_capacity_tables";return false;}
    // Exact object path from artifact_capacity_tables@0x24c2ec:
    // DataManager+0x10 -> root+0x140 -> table+0x18 -> wrapper+0x10 -> Common.
    void *root=load_pointer(data_manager,0x10);if(!root){if(error)*error="DataManager root is null for artifact capacity";return false;}
    void *table=load_pointer(root,0x140);if(!table){if(error)*error="Common DataManager slot 0x140 is null";return false;}
    void *wrapper=load_pointer(table,0x18);if(!wrapper){if(error)*error="Common table wrapper is null";return false;}
    void *common=load_pointer(wrapper,0x10);if(!common){if(error)*error="Common managed object is null";return false;}
    auto &runtime=il2cpp::Runtime::instance();const auto &api=runtime.api();if(!api.object_get_class||!api.object_unbox){if(error)*error="IL2CPP object reflection/unboxing API unavailable for artifact capacity";return false;}
    il2cpp::Runtime::ThreadScope thread(runtime);if(!thread.ok()){if(error)*error=thread.error();return false;}
    std::string class_error;void *secure_int=runtime.find_class("_AT.Shared.SecureInt",&class_error);if(!secure_int){if(error)*error="_AT.Shared.SecureInt resolution failed: "+class_error;return false;}
    void *setter=runtime.find_method(secure_int,"set_Value",1);if(!setter){if(error)*error="SecureInt.set_Value(Int32) method not found";return false;}
    void *common_class=api.object_get_class(common);if(!common_class){if(error)*error="Common object has no runtime class";return false;}
    void *getter=runtime.find_method(common_class,"get_Inventory_Artifact_Max_Slots",0);if(!getter){if(error)*error="Common.get_Inventory_Artifact_Max_Slots() method not found";return false;}
    int32_t capacity=9999;void *arguments[1]={&capacity};std::string invoke_error;
    // The reference invokes SecureInt.set_Value on the inline field at Common+0xc8.
    // This value-type field is not a separately allocated Managed object.
    void *secure_int_field=static_cast<uint8_t*>(common)+0xc8;
    if(!runtime.invoke(setter,secure_int_field,arguments,nullptr,&invoke_error)){if(error)*error="SecureInt.set_Value(9999) failed: "+invoke_error;return false;}
    void *boxed=nullptr;if(!runtime.invoke(getter,common,nullptr,&boxed,&invoke_error)){if(error)*error="Common.get_Inventory_Artifact_Max_Slots() failed: "+invoke_error;return false;}
    if(!boxed){if(error)*error="Common.get_Inventory_Artifact_Max_Slots() returned null";return false;}
    const void *raw=api.object_unbox(boxed);if(!raw){if(error)*error="artifact inventory capacity getter did not return a boxed Int32";return false;}
    int32_t actual=0;std::memcpy(&actual,raw,sizeof(actual));
    ASTRAE_LOGI("reference artifact capacity applied: requested=%d actual=%d common=%p field_offset=0xc8",capacity,actual,common);
    if(actual!=capacity){if(error)*error="artifact inventory capacity verification failed: expected 9999, got "+std::to_string(actual);return false;}
    return true;
}

bool apply_permanent_raid_tables(void *data_manager,std::string *error){
    if(error)error->clear();
    // Exact field path recovered from AArch64: dm+0x10 -> +0x320 -> +0x18 -> +0x10.
    void *root=load_pointer(data_manager,0x10);if(!root){if(error)*error="DataManager root field is null";return false;}
    void *manager=load_pointer(root,0x320);if(!manager){if(error)*error="permanent-raid table manager is null";return false;}
    void *table=load_pointer(manager,0x18);if(!table){if(error)*error="permanent-raid table wrapper is null";return false;}
    void *list=load_pointer(table,0x10);if(!list){if(error)*error="permanent-raid row list is null";return false;}
    const uint64_t count=load_u64(list,0x18);if(count==0||count>128){if(error)*error="permanent-raid row count is outside the original 1..128 range";return false;}
    for(uint64_t i=0;i<count;++i){void *row=load_pointer(list,0x20+static_cast<size_t>(i)*sizeof(void*));if(!row){if(error)*error="permanent-raid row pointer is null at index "+std::to_string(i);return false;}std::array<uint64_t,2> value{kPermanentRaidRowValue,kPermanentRaidRowValue};std::memcpy(static_cast<uint8_t*>(row)+0x38,value.data(),sizeof(value));}
    return true;
}
namespace { int64_t local_activity_period_provider(){return local_activity_period(nullptr);} }
void register_local_activity_period_provider(){store_set_activity_period(&local_activity_period_provider);}
int64_t local_activity_period(std::string *error){
    if(error)error->clear();
    auto &runtime=il2cpp::Runtime::instance();const auto &api=runtime.api();
    il2cpp::Runtime::ThreadScope thread(runtime);if(!thread.ok()){if(error)*error=thread.error();return -1;}
    if(!api.object_unbox){if(error)*error="IL2CPP object_unbox API is unavailable";return -1;}
    std::string type_error;void *extension_class=runtime.find_class("_AT.Shared.DateTimeExtensions",&type_error);if(!extension_class){if(error)*error="_AT.Shared.DateTimeExtensions type resolution failed: "+type_error;return -1;}
    void *method=runtime.find_method(extension_class,"GetRecentResetDay",1);if(!method){if(error)*error="DateTimeExtensions.GetRecentResetDay(DateTime) not found";return -1;}
    // Original stack layout is { args[0] -> now, now = time(NULL)*10,000,000 + DateTime epoch ticks }.
    constexpr int64_t kTicksPerSecond=10000000LL;constexpr int64_t kUnixEpochTicks=621355968000000000LL;
    int64_t now_ticks=static_cast<int64_t>(std::time(nullptr))*kTicksPerSecond+kUnixEpochTicks;void *arguments[1]={&now_ticks};void *result=nullptr;std::string invoke_error;
    if(!runtime.invoke(method,nullptr,arguments,&result,&invoke_error)){if(error)*error="GetRecentResetDay invocation failed: "+invoke_error;return -1;}
    if(!result){if(error)*error="GetRecentResetDay returned null";return -1;}
    // The boxed DateTime is unboxed immediately; no Managed allocation occurs between
    // runtime_invoke and object_unbox, so a transient GCHandle is unnecessary here.
    const void *raw=api.object_unbox(result);if(!raw){if(error)*error="GetRecentResetDay result is not a boxed value type";return -1;}
    uint64_t ticks=0;std::memcpy(&ticks,raw,sizeof(ticks));
    return static_cast<int64_t>(ticks&0x3fffffffffffffffULL);
}

void *table_get_key_borrowed(void *data_manager,uint32_t table_offset,const void *key,std::string *error){
    if(error)error->clear();if(!data_manager||!key){if(error)*error="DataManager or table key is null";return nullptr;}
    if(table_offset<0x10||table_offset>0x10000||(table_offset&7U)){if(error)*error="DataManager table offset is invalid";return nullptr;}
    void *root=load_pointer(data_manager,0x10);if(!root){if(error)*error="DataManager table root is null";return nullptr;}
    void *table=load_pointer(root,table_offset);if(!table){if(error)*error="DataManager table slot is null";return nullptr;}
    void *dictionary=load_pointer(table,0x18);if(!dictionary){if(error)*error="DataManager table dictionary is null";return nullptr;}
    auto &runtime=il2cpp::Runtime::instance();const auto &api=runtime.api();if(!api.object_get_class){if(error)*error="il2cpp_object_get_class is unavailable";return nullptr;}
    il2cpp::Runtime::ThreadScope scope(runtime);if(!scope.ok()){if(error)*error=scope.error();return nullptr;}
    void *klass=api.object_get_class(dictionary);void *method=runtime.find_method(klass,"get_Item",1);if(!method){if(error)*error="managed table dictionary get_Item(key) method not found";return nullptr;}
    void *arguments[1]={const_cast<void*>(key)};void *result=nullptr;std::string invoke_error;if(!runtime.invoke(method,dictionary,arguments,&result,&invoke_error)){if(error)*error="managed table lookup failed: "+invoke_error;return nullptr;}
    if(!result&&error)*error="managed table lookup returned null row";return result;
}
void *table_get_borrowed(void *data_manager,uint32_t table_offset,uint64_t key,std::string *error){return table_get_key_borrowed(data_manager,table_offset,&key,error);}
uint32_t table_get_key_pinned(void *data_manager,uint32_t table_offset,const void *key,std::string *error){
    void *row=table_get_key_borrowed(data_manager,table_offset,key,error);if(!row)return 0;const uint32_t handle=il2cpp::Runtime::instance().retain(row,true);if(!handle&&error)*error="could not pin managed table row";return handle;
}
uint32_t table_get_pinned(void *data_manager,uint32_t table_offset,uint64_t key,std::string *error){return table_get_key_pinned(data_manager,table_offset,&key,error);}
uint32_t table_get_i32_pinned(void *data_manager,uint32_t table_offset,int32_t key,std::string *error){return table_get_key_pinned(data_manager,table_offset,&key,error);}
namespace {
template<typename T> bool read_row_scalar(void *row,const std::string &getter,T *value,std::string *error){
    if(error)error->clear();if(value)*value=T{};if(!row||!value){if(error)*error="row getter received a null argument";return false;}
    auto &runtime=il2cpp::Runtime::instance();const auto &api=runtime.api();if(!api.object_get_class||!api.object_unbox){if(error)*error="IL2CPP object reflection or unboxing API is unavailable";return false;}
    il2cpp::Runtime::ThreadScope scope(runtime);if(!scope.ok()){if(error)*error=scope.error();return false;}
    // Callers pass rows owned by a live DataManager collection/dictionary, and this
    // getter is synchronous. Read the boxed scalar immediately, before another Managed
    // allocation or safepoint; do not create a temporary GCHandle whose free path is
    // known to abort in the target IL2CPP build (observed in ReleaseRoots::~ReleaseRoots).
    void *klass=api.object_get_class(row);if(!klass){if(error)*error="managed row has no runtime class: "+getter;return false;}
    void *method=runtime.find_method(klass,getter,0);if(!method){if(error)*error="managed row getter not found: "+getter;return false;}
    void *boxed=nullptr;std::string invoke_error;if(!runtime.invoke(method,row,nullptr,&boxed,&invoke_error)){if(error)*error="managed row getter invocation failed ("+getter+"): "+invoke_error;return false;}
    if(!boxed){if(error)*error="managed row getter returned null: "+getter;return false;}
    const void *raw=api.object_unbox(boxed);if(!raw){if(error)*error="managed row getter result is not a boxed value: "+getter;return false;}
    std::memcpy(value,raw,sizeof(T));return true;
}
}
bool row_u64(void *row,const std::string &getter,uint64_t *value,std::string *error){return read_row_scalar<uint64_t>(row,getter,value,error);}
bool row_i32(void *row,const std::string &getter,int32_t *value,std::string *error){return read_row_scalar<int32_t>(row,getter,value,error);}


bool office_wire_probe(std::string *error){
    if(error)error->clear();
    struct Probe{const char *type;const char *field;const char *json;};
    // Exact five payload/type/property triplets referenced by the original ELF's
    // relative-string table and relocation-backed type/property arrays.
    static constexpr Probe probes[]={
        {"DateFieldEventPresent.Request","PresentItems",R"({"NodeEventId":"100001","PresentItems":[{"Key":"920001","Value":3}]})"},
        {"BarracksInstallationRunTask.Response","ResultGrade",R"({"ResultGrade":[{"Key":1,"Value":2}]})"},
        {"BuffInstallationInfo","OpenSlots",R"({"OpenSlots":[{"Key":"9001","Value":5}],"Slots":[]})"},
        {"BuffInstallationCharacterBuff.Request","Apply",R"({"InstallationServerId":"9901","BuffTableId":"9001","Apply":[{"ApplyCharacterTableId":"10001","UpdateType":0}]})"},
        {"BarracksInstallationRegisterAgent.Request","RegisterAgents",R"({"InstallationServerId":"9902","RegisterAgents":[{"TableId":"1","GroupId":1,"Agents":[]}]})"}
    };
    // Original office_wire_probe sets its one-shot guard before the first JSON/wire operation;
    // a failed probe is not retried by later touch calls.
    static std::atomic<bool> started{false};bool expected=false;if(!started.compare_exchange_strong(expected,true,std::memory_order_acq_rel))return true;
    auto &wire=managed::ManagedWire::instance();if(!wire.schema_loaded()){if(error)*error="ManagedWire schema must be loaded before the original office probe";return false;}
    auto &runtime=il2cpp::Runtime::instance();il2cpp::Runtime::ThreadScope thread(runtime);if(!thread.ok()){if(error)*error=thread.error();return false;}
    std::vector<std::string> failures;
    for(const auto &probe:probes){std::string local_error;uint32_t token=wire.build_object_handle(probe.type,probe.json,&local_error);if(!token){failures.push_back(std::string(probe.type)+": build failed: "+local_error);continue;}
        void *object=runtime.public_handle_target(token);if(!object){runtime.release_public_handle(token);failures.push_back(std::string(probe.type)+": built object handle has no target");continue;}
        std::string roundtrip=wire.read_object_json(probe.type,object,&local_error);runtime.release_public_handle(token);if(!local_error.empty()){failures.push_back(std::string(probe.type)+": read failed: "+local_error);continue;}
        try{const json::Value original=json::parse(probe.json),decoded=json::parse(roundtrip);if(!original.contains(probe.field)||!decoded.contains(probe.field)||json::dump(original[probe.field],false)!=json::dump(decoded[probe.field],false))failures.push_back(std::string(probe.type)+": round-trip field mismatch: "+probe.field);}catch(const std::exception &e){failures.push_back(std::string(probe.type)+": JSON compare failed: "+e.what());}
    }
    if(!failures.empty()){if(error){for(size_t i=0;i<failures.size();++i){if(i)error->append("; ");error->append(failures[i]);}}return false;}return true;
}


namespace {
struct InstallationField { const char *name; enum class Kind : uint8_t { U64String, Int32, Boolean } kind; };
static constexpr InstallationField kInstallationFields[]={
    {"Id",InstallationField::Kind::U64String},
    {"GroupId",InstallationField::Kind::Int32},
    {"CurrentLevel",InstallationField::Kind::Int32},
    {"NextBuffId",InstallationField::Kind::U64String},
    {"AutoGive",InstallationField::Kind::Boolean},
    {"ReqInstallationLevel",InstallationField::Kind::Int32},
    {"BuffElementType",InstallationField::Kind::Int32},
    {"NextLevelReqItemId",InstallationField::Kind::Int32},
    {"NextLevelReqItemAmount",InstallationField::Kind::Int32}
};
std::mutex g_installation_mutex;
json::Value g_installation_config;
bool g_installation_config_loaded=false;
bool g_installation_tables_ready=false;
bool ensure_installation_config(std::string *error){
    if(g_installation_config_loaded)return true;
    std::string source;if(!assets::read_embedded_asset("assets/tables/embedded_01_raid_datefield_character.json",source)){if(error)*error="embedded installation seed asset is unavailable";return false;}
    try{g_installation_config=json::parse(source);}catch(const std::exception &e){if(error)*error=std::string("embedded installation seed JSON is invalid: ")+e.what();return false;}
    if(!g_installation_config.is_object()){if(error)*error="embedded installation seed root is not an object";return false;}
    g_installation_config_loaded=true;return true;
}
uint64_t json_u64(const json::Value &value){
    if(value.is_string()){const std::string text=value.as_string();char *end=nullptr;unsigned long long parsed=std::strtoull(text.c_str(),&end,10);return end&&end!=text.c_str()&&*end=='\0'?static_cast<uint64_t>(parsed):0;}
    if(value.is_bool())return value.as_bool()?1ULL:0ULL;return value.as_u64();
}
bool find_seed_installation(uint64_t id,json::Value *row){
    if(!row)return false;const auto &table=g_installation_config["Ins_GreatTemple_Buff"];if(!table.is_array())return false;
    for(const auto &candidate:table.as_array()){if(candidate.is_object()&&candidate.contains("Id")&&json_u64(candidate["Id"])==id){*row=candidate;return true;}}
    return false;
}
bool managed_property_value(void *row,const InstallationField &field,json::Value *value,std::string *error){
    if(!row||!value){if(error)*error="installation property getter received a null argument";return false;}
    auto &runtime=il2cpp::Runtime::instance();const auto &api=runtime.api();if(!api.object_get_class||!api.object_unbox){if(error)*error="IL2CPP reflection/unbox API is unavailable for installation table";return false;}
    void *klass=api.object_get_class(row);if(!klass){if(error)*error="installation row class is null";return false;}
    const std::string getter=std::string("get_")+field.name;void *method=runtime.find_method(klass,getter,0);if(!method){if(error)*error="installation row getter not found: "+getter;return false;}
    void *boxed=nullptr;std::string invoke_error;if(!runtime.invoke(method,row,nullptr,&boxed,&invoke_error)){if(error)*error="installation row getter failed ("+getter+"): "+invoke_error;return false;}
    if(!boxed){if(error)*error="installation row getter returned null: "+getter;return false;}
    const void *raw=api.object_unbox(boxed);if(!raw){if(error)*error="installation row getter result is not a boxed value: "+getter;return false;}
    switch(field.kind){
        case InstallationField::Kind::U64String:{uint64_t v=0;std::memcpy(&v,raw,sizeof(v));*value=std::to_string(v);break;}
        case InstallationField::Kind::Int32:{int32_t v=0;std::memcpy(&v,raw,sizeof(v));*value=v;break;}
        case InstallationField::Kind::Boolean:{uint8_t v=0;std::memcpy(&v,raw,sizeof(v));*value=(v!=0);break;}
    }
    return true;
}
}
bool installation_native_tables(void *data_manager,std::string *error){
    if(error)error->clear();std::lock_guard<std::mutex> lock(g_installation_mutex);if(g_installation_tables_ready)return true;
    // The original helper treats a null DataManager as a no-op success; it does not set its one-shot flag.
    if(!data_manager)return true;
    void *root=load_pointer(data_manager,0x10);if(!root){if(error)*error="DataManager root is null for installation_native_tables";return false;}
    void *table=load_pointer(root,0x238);if(!table){if(error)*error="installation DataManager table slot 0x238 is null";return false;}
    void *wrapper=load_pointer(table,0x18);if(!wrapper){if(error)*error="installation table wrapper is null";return false;}
    void *list=load_pointer(wrapper,0x10);if(!list){if(error)*error="installation row list is null";return false;}
    const uint64_t count=load_u64(list,0x18);if(count==0||count>4096){if(error)*error="installation row count is outside the original 1..4096 range";return false;}
    if(!ensure_installation_config(error))return false;
    auto &runtime=il2cpp::Runtime::instance();il2cpp::Runtime::ThreadScope thread(runtime);if(!thread.ok()){if(error)*error=thread.error();return false;}
    json::Value converted=json::Value::array();converted.as_array().reserve(static_cast<size_t>(count));
    for(uint64_t i=0;i<count;++i){
        void *row_object=load_pointer(list,0x20+static_cast<size_t>(i)*sizeof(void*));if(!row_object){if(error)*error="installation row pointer is null at index "+std::to_string(i);return false;}
        uint64_t id=0;std::string row_error;if(!row_u64(row_object,"get_Id",&id,&row_error)){if(error)*error="installation row Id lookup failed at index "+std::to_string(i)+": "+row_error;return false;}
        json::Value output=json::Value::object();(void)find_seed_installation(id,&output);
        // The original duplicates the static seed row (when found), deletes these nine managed fields,
        // and then repopulates them from live Managed getters.
        for(const auto &field:kInstallationFields)output.erase(field.name);
        for(const auto &field:kInstallationFields){json::Value value;if(!managed_property_value(row_object,field,&value,&row_error)){if(error)*error="installation field "+std::string(field.name)+" failed at row "+std::to_string(id)+": "+row_error;return false;}output[field.name]=std::move(value);}
        converted.push_back(std::move(output));
    }
    g_installation_config["Ins_GreatTemple_Buff"]=std::move(converted);g_installation_tables_ready=true;return true;
}
bool battle_multiple_tables(void *data_manager,std::string *error){
    if(error)error->clear();
    if(!data_manager){if(error)*error="DataManager is null for battle_multiple_tables";return false;}
    void *root=load_pointer(data_manager,0x10);if(!root){if(error)*error="DataManager root is null for battle_multiple_tables";return false;}
    void *table=load_pointer(root,0x140);if(!table){if(error)*error="battle/common DataManager slot 0x140 is null";return false;}
    void *wrapper=load_pointer(table,0x18);if(!wrapper){if(error)*error="battle/common table wrapper is null";return false;}
    void *common=load_pointer(wrapper,0x10);if(!common){if(error)*error="battle/common data object is null";return false;}
    auto &runtime=il2cpp::Runtime::instance();il2cpp::Runtime::ThreadScope thread(runtime);if(!thread.ok()){if(error)*error=thread.error();return false;}
    std::string class_error;void *secure_int=runtime.find_class("_AT.Shared.SecureInt",&class_error);if(!secure_int){if(error)*error="_AT.Shared.SecureInt type resolution failed: "+class_error;return false;}
    void *setter=runtime.find_method(secure_int,"set_Value",1);if(!setter){if(error)*error="SecureInt.set_Value(Int32) method not found";return false;}
    int32_t cap=99;void *arguments[1]={&cap};
    for(size_t offset:{size_t(0x3b0),size_t(0x3b8)}){void *instance=static_cast<uint8_t*>(common)+offset;std::string invoke_error;if(!runtime.invoke(setter,instance,arguments,nullptr,&invoke_error)){if(error)*error="SecureInt.set_Value(99) failed at common+0x"+std::to_string(offset)+": "+invoke_error;return false;}}
    int32_t quest_cap=0,farming_cap=0;std::string getter_error;
    if(!row_i32(common,"get_QuestMaxMultipleRewardCount",&quest_cap,&getter_error)){if(error)*error="QuestMaxMultipleRewardCount verification failed: "+getter_error;return false;}
    if(quest_cap!=99){if(error)*error="QuestMaxMultipleRewardCount did not retain the original value 99";return false;}
    if(!row_i32(common,"get_FarmingMaxMultipleRewardCount",&farming_cap,&getter_error)){if(error)*error="FarmingMaxMultipleRewardCount verification failed: "+getter_error;return false;}
    if(farming_cap!=99){if(error)*error="FarmingMaxMultipleRewardCount did not retain the original value 99";return false;}
    return true;
}

bool installation_native_get_row(const std::string &table,uint64_t id,json::Value *row,std::string *error){
    if(error)error->clear();if(!row){if(error)*error="installation row output is null";return false;}
    std::lock_guard<std::mutex> lock(g_installation_mutex);if(!ensure_installation_config(error))return false;
    const json::Value &rows=g_installation_config[table];if(!rows.is_array()){if(error)*error="installation configuration table is not an array: "+table;return false;}
    for(const auto &candidate:rows.as_array())if(candidate.is_object()&&candidate.contains("Id")&&json_u64(candidate["Id"])==id){*row=candidate;return true;}
    if(error)*error="installation configuration row not found: "+table+" id="+std::to_string(id);return false;
}

bool run_initial_lobby_route(void *login_controller,void *data_manager,uint64_t content_bits,InitialLobbyAction *action,std::string *error){
    ASTRAE_LOGI("initial lobby route started: controller=%p data_manager=%p content_bits=0x%llx",login_controller,data_manager,static_cast<unsigned long long>(content_bits));
    if(error)error->clear();if(action)*action=InitialLobbyAction::None;
    if(!login_controller){if(error)*error="UILoginPageController instance is null";return false;}
    if(!data_manager){if(error)*error="DataManager is null; refusing to enter lobby without the controller's managed DataManager";ASTRAE_LOGE("lobby route rejected: DataManager is null; controller+0x10 must be resolved before EnterLobby");return false;}
    auto &runtime=il2cpp::Runtime::instance();const auto &api=runtime.api();if(!api.object_get_class){if(error)*error="il2cpp_object_get_class is unavailable";return false;}
    il2cpp::Runtime::ThreadScope scope(runtime);if(!scope.ok()){if(error)*error=scope.error();return false;}
    std::string class_error;void *login_class=runtime.find_class("_AT.UI.UILoginPageController",&class_error);if(!login_class){if(error)*error="UILoginPageController type resolution failed: "+class_error;ASTRAE_LOGE("lobby route cannot resolve controller class: %s",class_error.c_str());return false;}
    // Hook #3's first ABI argument is named `controller` in the original DWARF, but do not
    // assume its runtime class: reject unexpected objects before invoking any Managed method.
    void *actual_class=api.object_get_class(login_controller);if(!actual_class){if(error)*error="touch controller has no Managed runtime class";return false;}
    bool compatible=false;void *candidate=actual_class;for(size_t depth=0;candidate&&depth<64;++depth){if(candidate==login_class){compatible=true;break;}candidate=api.class_get_parent?api.class_get_parent(candidate):nullptr;}
    if(!compatible){const char *actual_name=api.class_get_name?api.class_get_name(actual_class):nullptr;const char *actual_ns=api.class_get_namespace?api.class_get_namespace(actual_class):nullptr;if(error)*error=std::string("touch controller type mismatch: received ")+(actual_ns&&*actual_ns?std::string(actual_ns)+".":std::string())+(actual_name?actual_name:"<unknown>")+", expected _AT.UI.UILoginPageController";ASTRAE_LOGE("touch controller type mismatch: actual=%s.%s class=%p expected=%p",actual_ns?actual_ns:"",actual_name?actual_name:"<unknown>",actual_class,login_class);return false;}
    ASTRAE_LOGI("touch controller type validated: instance=%p class=%p",login_controller,actual_class);
    auto invoke_login_method=[&](const char *name,void **arguments)->bool{void *method=runtime.find_method(login_class,name,arguments?1:0);if(!method){if(error)*error=std::string("UILoginPageController method not found: ")+name;return false;}void *result=nullptr;std::string invoke_error;ASTRAE_LOGI("invoking UILoginPageController.%s on instance=%p method=%p",name,login_controller,method);if(!runtime.invoke(method,login_controller,arguments,&result,&invoke_error)){if(error)*error=std::string("UILoginPageController.")+name+" invocation failed: "+invoke_error;ASTRAE_LOGE("UILoginPageController.%s invocation failed: %s",name,invoke_error.c_str());return false;}ASTRAE_LOGI("UILoginPageController.%s invocation returned successfully",name);if(action)*action=std::strcmp(name,"EnterLobby")==0?InitialLobbyAction::EnterLobby:InitialLobbyAction::PlayFirstDay;return true;};
    bool ok=true;
    // This route is invoked synchronously from touch() while the controller and its
    // DataManager remain rooted by the active Managed call stack. The quest row stays
    // owned by its DataManager dictionary; borrowed access avoids temporary GCHandles.
    if((content_bits&1ULL)!=0){ok=invoke_login_method("EnterLobby",nullptr);return ok;}
    std::string lookup_error;void *row=table_get_borrowed(data_manager,0x3c8,0x0700a185ULL,&lookup_error);if(!row){if(error&&!lookup_error.empty())*error="initial quest lookup unavailable: "+lookup_error;return true;}
    int32_t required_level=0;uint64_t reward_id=0;std::string getter_error;
    if(row_i32(row,"get_ReqAccountLevel",&required_level,&getter_error)&&required_level==8&&row_u64(row,"get_OneTimeReward_S",&reward_id,&getter_error)&&reward_id==0x0700a185ULL){void *arguments[1]={row};ok=invoke_login_method("PlayFirstDay",arguments);}
    return ok;
}
bool run_initial_lobby_route_from_store(void *login_controller,void *data_manager,InitialLobbyAction *action,std::string *error){return run_initial_lobby_route(login_controller,data_manager,store_content_bits(),action,error);}

namespace {
std::atomic<bool> g_touch_lobby_entered{false};
std::mutex g_character99_metadata_mutex;
std::unordered_set<std::string> g_character99_metadata_emitted;
std::atomic<bool> g_character99_metadata_complete{false};
std::mutex g_character99_xml_param_mutex;
std::unordered_set<std::string> g_character99_xml_param_emitted;
std::mutex g_character99_loader_metadata_mutex;
std::unordered_set<std::string> g_character99_loader_metadata_emitted;
std::atomic<bool> g_character99_loader_metadata_complete{false};
std::atomic<bool> g_artifact_capacity_applied{false};
std::atomic<bool> g_permanent_raid_tables_applied{false};
std::atomic<bool> g_battle_multiple_tables_applied{false};
std::atomic_flag g_touch_lobby_running=ATOMIC_FLAG_INIT;

std::string managed_class_full_name(const il2cpp::Api &api,void *object){
    if(!object||!api.object_get_class)return {};
    void *klass=api.object_get_class(object);if(!klass)return {};
    const char *name=api.class_get_name?api.class_get_name(klass):nullptr;
    const char *ns=api.class_get_namespace?api.class_get_namespace(klass):nullptr;
    if(!name||!*name)return {};
    return (ns&&*ns?std::string(ns)+".":std::string())+name;
}
void emit_managed_object_metadata(il2cpp::Runtime &runtime,const il2cpp::Api &api,const char *tag,void *object,std::unordered_set<std::string> &emitted,std::mutex &mutex){
    if(!object){ASTRAE_LOGW("character99 runtime object probe: %s=null",tag);return;}
    const std::string type=managed_class_full_name(api,object);
    if(type.empty()){ASTRAE_LOGW("character99 runtime object probe: %s=%p has no Managed class",tag,object);return;}
    ASTRAE_LOGI("character99 runtime object probe: %s=%p class=%s",tag,object,type.c_str());
    if(type.size()>=2&&type.compare(type.size()-2,2,"[]")==0){
        const uint64_t length=load_u64(object,0x18);
        ASTRAE_LOGI("character99 runtime array probe: %s type=%s length=%llu",tag,type.c_str(),static_cast<unsigned long long>(length));
        return;
    }
    {std::lock_guard<std::mutex> lock(mutex);if(!emitted.insert(type).second)return;}
    const std::string description=runtime.describe_class(type,256,256);
    ASTRAE_LOGI("character99 loader metadata snapshot begin: tag=%s type=%s",tag,type.c_str());
    std::istringstream lines(description);std::string line;size_t index=0;
    while(std::getline(lines,line))ASTRAE_LOGI("character99 loader metadata type=%s line=%zu: %s",type.c_str(),index++,line.c_str());
    ASTRAE_LOGI("character99 loader metadata snapshot end: tag=%s type=%s",tag,type.c_str());
}
void emit_fromxml_parameter_metadata(il2cpp::Runtime &runtime,const il2cpp::Api &api,const char *root_type){
    std::string class_error;void *root_class=runtime.find_class(root_type,&class_error);
    if(!root_class){ASTRAE_LOGW("character99 XML root parameter probe: root=%s unavailable: %s",root_type,class_error.c_str());return;}
    void *from_xml=runtime.find_method(root_class,"FromXml",1);
    if(!from_xml||!api.method_get_param||!api.class_from_type){ASTRAE_LOGW("character99 XML root parameter probe: root=%s FromXml/1 or type reflection unavailable",root_type);return;}
    const void *parameter_type=api.method_get_param(from_xml,0);void *parameter_class=parameter_type?api.class_from_type(parameter_type):nullptr;
    if(!parameter_class){ASTRAE_LOGW("character99 XML root parameter probe: root=%s FromXml argument class could not be resolved",root_type);return;}
    const char *name=api.class_get_name?api.class_get_name(parameter_class):nullptr;const char *ns=api.class_get_namespace?api.class_get_namespace(parameter_class):nullptr;
    if(!name||!*name){ASTRAE_LOGW("character99 XML root parameter probe: root=%s parameter class has no name",root_type);return;}
    const std::string full=(ns&&*ns?std::string(ns)+".":std::string())+name;
    {std::lock_guard<std::mutex> lock(g_character99_xml_param_mutex);if(!g_character99_xml_param_emitted.insert(full).second)return;}
    char *type_name=api.type_get_name?api.type_get_name(parameter_type):nullptr;
    ASTRAE_LOGI("character99 XML root parameter resolved: root=%s method=FromXml/1 argument=%s class=%p",root_type,type_name?type_name:full.c_str(),parameter_class);
    if(type_name&&api.free_mem)api.free_mem(type_name);
    const std::string description=runtime.describe_class(full,256,256);
    ASTRAE_LOGI("character99 XML root metadata snapshot begin: %s",full.c_str());
    std::istringstream lines(description);std::string line;size_t index=0;
    while(std::getline(lines,line))ASTRAE_LOGI("character99 XML root metadata type=%s line=%zu: %s",full.c_str(),index++,line.c_str());
    ASTRAE_LOGI("character99 XML root metadata snapshot end: %s",full.c_str());
}
}
bool touch_enter_lobby(void *login_controller,std::string *error){
    ASTRAE_LOGI("touch_enter_lobby called: controller=%p",login_controller);
    if(error)error->clear();
    if(!login_controller){if(error)*error="touch replacement received a null UILoginPageController";return false;}
    if(g_touch_lobby_entered.load(std::memory_order_acquire))return true;
    if(g_touch_lobby_running.test_and_set(std::memory_order_acq_rel)){if(error)*error="lobby entry is already being dispatched";return false;}
    struct ClearFlag{std::atomic_flag &flag;~ClearFlag(){flag.clear(std::memory_order_release);}} clear{g_touch_lobby_running};
    auto &runtime=il2cpp::Runtime::instance();const auto &api=runtime.api();
    il2cpp::Runtime::ThreadScope thread(runtime);if(!thread.ok()){if(error)*error="cannot attach touch callback to IL2CPP: "+thread.error();ASTRAE_LOGE("touch preflight cannot attach to IL2CPP: %s",thread.error().c_str());return false;}
    // These offsets are directly confirmed by the reference ELF's touch(controller, method):
    // account = controller + 0xb0; DataManager = controller + 0x10.
    void *data_manager=load_pointer(login_controller,0x10);void *account=load_pointer(login_controller,0xb0);
    ASTRAE_LOGI("touch managed fields recovered: controller=%p data_manager=%p account=%p source_offsets={DataManager:0x10,Account:0xb0}",login_controller,data_manager,account);
    if(!data_manager||!account){if(error)*error=std::string("touch controller is missing required managed state: ")+(data_manager?"Account(controller+0xb0)":"DataManager(controller+0x10)");ASTRAE_LOGE("touch preflight rejected: DataManager=%p Account=%p; no lobby method will be invoked",data_manager,account);return false;}
    if(!api.object_get_class){if(error)*error="il2cpp_object_get_class is unavailable while validating lobby state";return false;}
    void *dm_class=api.object_get_class(data_manager);void *account_class=api.object_get_class(account);
    if(!dm_class||!account_class){if(error)*error="controller DataManager or Account is not a valid managed object";ASTRAE_LOGE("touch preflight rejected invalid Managed objects: DataManager.class=%p Account.class=%p",dm_class,account_class);return false;}
    const char *dm_name=api.class_get_name?api.class_get_name(dm_class):nullptr;const char *account_name=api.class_get_name?api.class_get_name(account_class):nullptr;
    ASTRAE_LOGI("touch managed state validated: DataManager.class=%s Account.class=%s",dm_name?dm_name:"<unknown>",account_name?account_name:"<unknown>");
    // The reference character99_tables helper traverses CustomDataLoader at these
    // exact offsets. Inspect the live objects so the next reconstruction step uses the
    // game's actual table-root classes and generic collection declarations, rather than
    // guessing from the static factor/recipe record layouts alone.
    if(!g_character99_loader_metadata_complete.load(std::memory_order_acquire)){
        const Character99ObjectGraph graph=resolve_character99_object_graph(data_manager);
        ASTRAE_LOGI("character99 loader graph (DWARF-verified offsets): data_loader=%p db_loader=%p level_loader=%p level_root=%p level_data=%p level_dictionary=%p growth_loader=%p growth_error_message=%p growth_root=loader+0x18:%p growth_data=root+0x10:%p growth_dictionary=root+0x18:%p",graph.data_loader,graph.db_loader,graph.level_factor_loader,graph.level_factor_root,graph.level_factor_data,graph.level_factor_dictionary,graph.growth_recipe_loader,graph.growth_recipe_error_message,graph.growth_recipe_root,graph.growth_recipe_data,graph.growth_recipe_dictionary);
        emit_managed_object_metadata(runtime,api,"DataManager._dataLoader",graph.data_loader,g_character99_loader_metadata_emitted,g_character99_loader_metadata_mutex);
        emit_managed_object_metadata(runtime,api,"DataManager._dbLoader",graph.db_loader,g_character99_loader_metadata_emitted,g_character99_loader_metadata_mutex);
        emit_managed_object_metadata(runtime,api,"Character_LevelFactor.loader",graph.level_factor_loader,g_character99_loader_metadata_emitted,g_character99_loader_metadata_mutex);
        emit_managed_object_metadata(runtime,api,"Character_LevelFactor.root",graph.level_factor_root,g_character99_loader_metadata_emitted,g_character99_loader_metadata_mutex);
        emit_managed_object_metadata(runtime,api,"Character_LevelFactor.data",graph.level_factor_data,g_character99_loader_metadata_emitted,g_character99_loader_metadata_mutex);
        emit_managed_object_metadata(runtime,api,"Character_LevelFactor.dictionary",graph.level_factor_dictionary,g_character99_loader_metadata_emitted,g_character99_loader_metadata_mutex);
        emit_managed_object_metadata(runtime,api,"GrowthRecipe.loader",graph.growth_recipe_loader,g_character99_loader_metadata_emitted,g_character99_loader_metadata_mutex);
        emit_managed_object_metadata(runtime,api,"GrowthRecipe.errorMessage",graph.growth_recipe_error_message,g_character99_loader_metadata_emitted,g_character99_loader_metadata_mutex);
        emit_managed_object_metadata(runtime,api,"GrowthRecipe.root",graph.growth_recipe_root,g_character99_loader_metadata_emitted,g_character99_loader_metadata_mutex);
        emit_managed_object_metadata(runtime,api,"GrowthRecipe.data",graph.growth_recipe_data,g_character99_loader_metadata_emitted,g_character99_loader_metadata_mutex);
        emit_managed_object_metadata(runtime,api,"GrowthRecipe.dictionary",graph.growth_recipe_dictionary,g_character99_loader_metadata_emitted,g_character99_loader_metadata_mutex);
        const bool level_factor_graph_ok=graph.level_factor_loader&&graph.level_factor_root&&graph.level_factor_data&&graph.level_factor_dictionary;
        const bool growth_recipe_graph_ok=graph.growth_recipe_loader&&graph.growth_recipe_root&&graph.growth_recipe_data&&graph.growth_recipe_dictionary;
        if(level_factor_graph_ok&&growth_recipe_graph_ok){g_character99_loader_metadata_complete.store(true,std::memory_order_release);ASTRAE_LOGI("character99 loader graph complete: LevelFactor DataList/Dictionary and GrowthRecipe DataList/Dictionary resolved");}
        else ASTRAE_LOGW("character99 loader graph partial: LevelFactor={loader:%s root:%s data:%s dictionary:%s} GrowthRecipe={loader:%s root:%s data:%s dictionary:%s}; null GrowthRecipe.Root requires reconstruction, not an offset fallback",graph.level_factor_loader?"ready":"missing",graph.level_factor_root?"ready":"missing",graph.level_factor_data?"ready":"missing",graph.level_factor_dictionary?"ready":"missing",graph.growth_recipe_loader?"ready":"missing",graph.growth_recipe_root?"ready":"missing",graph.growth_recipe_data?"ready":"missing",graph.growth_recipe_dictionary?"ready":"missing");
    }
    if(!g_character99_metadata_complete.load(std::memory_order_acquire)){
        const char *dm_ns=api.class_get_namespace?api.class_get_namespace(dm_class):nullptr;
        const char *account_ns=api.class_get_namespace?api.class_get_namespace(account_class):nullptr;
        const char *dm_type=api.class_get_name?api.class_get_name(dm_class):nullptr;
        const char *account_type=api.class_get_name?api.class_get_name(account_class):nullptr;
        const std::string dm_full=(dm_ns&&*dm_ns?std::string(dm_ns)+".":std::string())+(dm_type?dm_type:"DataManager");
        const std::string account_full=(account_ns&&*account_ns?std::string(account_ns)+".":std::string())+(account_type?account_type:"AccountManager");
        const std::string types[]={dm_full,account_full,"_AT.Core.DataSheet.Character_LevelFactorLoader","_AT.Core.DataSheet.Character_LevelFactorRoot","_AT.Core.DataSheet.Character_LevelFactorXmlRoot","_AT.Core.DataSheet.Character_LevelFactorData","_AT.Core.DataSheet.Character_LevelFactorXmlData","_AT.Core.DataSheet.GrowthRecipeLoader","_AT.Core.DataSheet.GrowthRecipeRoot","_AT.Core.DataSheet.GrowthRecipeXmlRoot","_AT.Core.DataSheet.GrowthRecipeData","_AT.Core.DataSheet.GrowthRecipeXmlData"};
        for(const auto &type:types){
            {std::lock_guard<std::mutex> lock(g_character99_metadata_mutex);if(g_character99_metadata_emitted.count(type))continue;}
            const std::string description=runtime.describe_class(type,192,192);
            ASTRAE_LOGI("character99 IL2CPP metadata snapshot begin: %s",type.c_str());
            std::istringstream metadata_lines(description);std::string metadata_line;size_t metadata_line_index=0;while(std::getline(metadata_lines,metadata_line)){ASTRAE_LOGI("character99 metadata type=%s line=%zu: %s",type.c_str(),metadata_line_index++,metadata_line.c_str());}
            ASTRAE_LOGI("character99 IL2CPP metadata snapshot end: %s",type.c_str());
            if(description.rfind("class_lookup_failed",0)!=0&&description.rfind("thread_attach_failed",0)!=0){std::lock_guard<std::mutex> lock(g_character99_metadata_mutex);g_character99_metadata_emitted.insert(type);}
        }
        // Resolve XML Root classes from the actual FromXml parameter types. Some IL2CPP
        // builds do not expose these nested/deserialization classes reliably through a
        // name-only lookup, while method_get_param + class_from_type is authoritative.
        emit_fromxml_parameter_metadata(runtime,api,"_AT.Core.DataSheet.Character_LevelFactorRoot");
        emit_fromxml_parameter_metadata(runtime,api,"_AT.Core.DataSheet.GrowthRecipeRoot");
        bool complete=false;{std::lock_guard<std::mutex> lock(g_character99_metadata_mutex);complete=g_character99_metadata_emitted.size()>=sizeof(types)/sizeof(types[0]);}
        if(complete){g_character99_metadata_complete.store(true,std::memory_order_release);ASTRAE_LOGI("character99 IL2CPP metadata snapshot complete; DataManager, Account, loaders, XML roots, data records and progression roots resolved");}
        else ASTRAE_LOGW("character99 IL2CPP metadata snapshot incomplete; only unresolved classes will be retried on the next touch callback");
    }
    // The hook runs synchronously inside the managed UILoginPageController.touch call on
    // Unity's already-attached main thread. The controller is still strongly reachable from
    // that active managed invocation, and DataManager/Account are fields reachable from it.
    // Do not create/free GCHandles here: this target Unity build's gchandle_free path crashes
    // when these temporary handles are released (observed at libil2cpp+0x23a6574). Keeping
    // these borrowed references within this stack frame is sufficient; no managed allocation
    // or asynchronous hand-off occurs in this preflight path.
    ASTRAE_LOGI("touch preflight using borrowed managed roots from active invocation; GCHandle retain/free intentionally skipped");
    std::string helper_error;
    if(!g_artifact_capacity_applied.load(std::memory_order_acquire)){
        if(!artifact_capacity_tables(data_manager,&helper_error)){if(error)*error="reference touch step 1/15 artifact_capacity_tables failed: "+helper_error;ASTRAE_LOGE("reference touch step 1/15 artifact_capacity_tables failed: %s",helper_error.c_str());return false;}
        g_artifact_capacity_applied.store(true,std::memory_order_release);
        ASTRAE_LOGI("reference touch step 1/15 completed: artifact_capacity_tables");
    }
    if(!character99_tables(data_manager,&helper_error)){if(error)*error="reference touch step 2/15 character99_tables failed: "+helper_error;ASTRAE_LOGE("reference touch step 2/15 character99_tables failed: %s",helper_error.c_str());return false;}
    ASTRAE_LOGI("reference touch step 2/15 completed: character99_tables");
    if(!g_permanent_raid_tables_applied.load(std::memory_order_acquire)){
        if(!apply_permanent_raid_tables(data_manager,&helper_error)){if(error)*error="reference touch step 3/15 permanent_raid_tables failed: "+helper_error;ASTRAE_LOGE("reference touch step 3/15 permanent_raid_tables failed: %s",helper_error.c_str());return false;}
        g_permanent_raid_tables_applied.store(true,std::memory_order_release);
        ASTRAE_LOGI("reference touch step 3/15 completed: permanent_raid_tables");
    }
    // Reference touch calls seed_table_defaults at step 4, but its false return is explicitly
    // non-fatal: the original branches to the log block and continues to battle_multiple_tables.
    // The seed helper itself is still not reconstructed, so report the omission honestly and
    // preserve the reference control-flow instead of incorrectly stopping before step 5.
    ASTRAE_LOGW("reference touch step 4/15 seed_table_defaults not reconstructed; reference false-return path is non-fatal, continuing without claiming seed defaults were applied");
    if(!g_battle_multiple_tables_applied.load(std::memory_order_acquire)){
        if(!battle_multiple_tables(data_manager,&helper_error)){if(error)*error="reference touch step 5/15 battle_multiple_tables failed: "+helper_error;ASTRAE_LOGE("reference touch step 5/15 battle_multiple_tables failed: %s",helper_error.c_str());return false;}
        g_battle_multiple_tables_applied.store(true,std::memory_order_release);
        ASTRAE_LOGI("reference touch step 5/15 completed: battle_multiple_tables");
    }
    // custom_resource_tables is the next mandatory gate in the original touch function.
    constexpr const char *missing="custom_resource_tables, custom_item_tables, signed_exchange_tables, earned_unlocks/sync_unlocks, bootstrap(account){restore_balances,restore_foundation,pack_objects_cache,local_mail_count,restore_gacha,restore_farming_cache,restore_raid_cache,date_restore,installation_restore,guide_restore_cache,restore_daily_cache,restore_pack_missions,restore_puzzles}, local_account_event_login, guide_restore_cache(account,true)/initialize_guide_cache, office_wire_probe, installation_native_tables";
    if(error)*error=std::string("reference touch sequence stopped safely at step 6/15; missing: ")+missing;
    ASTRAE_LOGE("custom offline dispatcher not ready; reference touch sequence stopped at step 6/15: %s",missing);
    ASTRAE_LOGI("completed reference steps: artifact_capacity_tables, character99_tables, permanent_raid_tables, battle_multiple_tables; seed_table_defaults remains omitted on its documented non-fatal path; DataManager=%p Account=%p",data_manager,account);
    return false;
}

}
