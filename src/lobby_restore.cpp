#include "lobby_restore.hpp"
#include "il2cpp_runtime.hpp"
#include "managed_wire.hpp"
#include "embedded_assets.hpp"
#include "json.hpp"
#include "original_store_abi.h"
#include <array>
#include <atomic>
#include <cstring>
#include <ctime>
#include <limits>
#include <mutex>
#include <cstdio>
#include <cstdlib>

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

bool apply_permanent_raid_tables(void *data_manager,std::string *error){
    if(error)error->clear();
    // Exact field path recovered from AArch64: dm+0x10 -> +0x320 -> +0x18 -> +0x10.
    void *root=load_pointer(data_manager,0x10);if(!root){if(error)*error="DataManager root field is null";return false;}
    void *manager=load_pointer(root,0x320);if(!manager){if(error)*error="permanent-raid table manager is null";return false;}
    void *table=load_pointer(manager,0x18);if(!table){if(error)*error="permanent-raid table wrapper is null";return false;}
    void *list=load_pointer(table,0x10);if(!list){if(error)*error="permanent-raid row list is null";return false;}
    const uint64_t count=load_u64(list,0x18);if(count>128){if(error)*error="permanent-raid row count is outside the original 0..128 range";return false;}
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
    const uint32_t result_handle=runtime.retain(result,true);if(!result_handle){if(error)*error="could not pin GetRecentResetDay return value";return -1;}
    const void *raw=api.object_unbox(result);if(!raw){runtime.release(result_handle);if(error)*error="GetRecentResetDay result is not a boxed value type";return -1;}
    uint64_t ticks=0;std::memcpy(&ticks,raw,sizeof(ticks));runtime.release(result_handle);
    return static_cast<int64_t>(ticks&0x3fffffffffffffffULL);
}

uint32_t table_get_key_pinned(void *data_manager,uint32_t table_offset,const void *key,std::string *error){
    if(error)error->clear();if(!data_manager||!key){if(error)*error="DataManager or table key is null";return 0;}
    if(table_offset<0x10||table_offset>0x10000||(table_offset&7U)){if(error)*error="DataManager table offset is invalid";return 0;}
    void *root=load_pointer(data_manager,0x10);if(!root){if(error)*error="DataManager table root is null";return 0;}
    void *table=load_pointer(root,table_offset);if(!table){if(error)*error="DataManager table slot is null";return 0;}
    void *dictionary=load_pointer(table,0x18);if(!dictionary){if(error)*error="DataManager table dictionary is null";return 0;}
    auto &runtime=il2cpp::Runtime::instance();const auto &api=runtime.api();if(!api.object_get_class){if(error)*error="il2cpp_object_get_class is unavailable";return 0;}
    il2cpp::Runtime::ThreadScope scope(runtime);if(!scope.ok()){if(error)*error=scope.error();return 0;}
    void *klass=api.object_get_class(dictionary);void *method=runtime.find_method(klass,"get_Item",1);if(!method){if(error)*error="managed table dictionary get_Item(key) method not found";return 0;}
    void *arguments[1]={const_cast<void*>(key)};void *result=nullptr;std::string invoke_error;if(!runtime.invoke(method,dictionary,arguments,&result,&invoke_error)){if(error)*error="managed table lookup failed: "+invoke_error;return 0;}
    if(!result){if(error)*error="managed table lookup returned null row";return 0;}
    const uint32_t handle=runtime.retain(result,true);if(!handle&&error)*error="could not pin managed table row";return handle;
}
uint32_t table_get_pinned(void *data_manager,uint32_t table_offset,uint64_t key,std::string *error){return table_get_key_pinned(data_manager,table_offset,&key,error);}
uint32_t table_get_i32_pinned(void *data_manager,uint32_t table_offset,int32_t key,std::string *error){return table_get_key_pinned(data_manager,table_offset,&key,error);}
namespace {
template<typename T> bool read_row_scalar(void *row,const std::string &getter,T *value,std::string *error){
    if(error)error->clear();if(value)*value=T{};if(!row||!value){if(error)*error="row getter received a null argument";return false;}
    auto &runtime=il2cpp::Runtime::instance();const auto &api=runtime.api();if(!api.object_get_class||!api.object_unbox){if(error)*error="IL2CPP object reflection or unboxing API is unavailable";return false;}
    il2cpp::Runtime::ThreadScope scope(runtime);if(!scope.ok()){if(error)*error=scope.error();return false;}
    const uint32_t row_handle=runtime.retain(row,true);if(!row_handle){if(error)*error="could not pin managed row during getter invocation";return false;}
    void *klass=api.object_get_class(row);void *method=runtime.find_method(klass,getter,0);if(!method){runtime.release(row_handle);if(error)*error="managed row getter not found: "+getter;return false;}
    void *boxed=nullptr;std::string invoke_error;if(!runtime.invoke(method,row,nullptr,&boxed,&invoke_error)){runtime.release(row_handle);if(error)*error="managed row getter invocation failed ("+getter+"): "+invoke_error;return false;}
    if(!boxed){runtime.release(row_handle);if(error)*error="managed row getter returned null: "+getter;return false;}
    const uint32_t value_handle=runtime.retain(boxed,true);if(!value_handle){runtime.release(row_handle);if(error)*error="could not pin boxed row getter result: "+getter;return false;}
    const void *raw=api.object_unbox(boxed);if(!raw){runtime.release(value_handle);runtime.release(row_handle);if(error)*error="managed row getter result is not a boxed value: "+getter;return false;}
    std::memcpy(value,raw,sizeof(T));runtime.release(value_handle);runtime.release(row_handle);return true;
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
    const uint32_t handle=runtime.retain(boxed,true);if(!handle){if(error)*error="cannot pin installation row getter result: "+getter;return false;}
    const void *raw=api.object_unbox(boxed);if(!raw){runtime.release(handle);if(error)*error="installation row getter result is not a boxed value: "+getter;return false;}
    switch(field.kind){
        case InstallationField::Kind::U64String:{uint64_t v=0;std::memcpy(&v,raw,sizeof(v));*value=std::to_string(v);break;}
        case InstallationField::Kind::Int32:{int32_t v=0;std::memcpy(&v,raw,sizeof(v));*value=v;break;}
        case InstallationField::Kind::Boolean:{uint8_t v=0;std::memcpy(&v,raw,sizeof(v));*value=(v!=0);break;}
    }
    runtime.release(handle);return true;
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
        const uint32_t row_handle=runtime.retain(row_object,true);if(!row_handle){if(error)*error="cannot pin installation row at index "+std::to_string(i);return false;}
        uint64_t id=0;std::string row_error;if(!row_u64(row_object,"get_Id",&id,&row_error)){runtime.release(row_handle);if(error)*error="installation row Id lookup failed at index "+std::to_string(i)+": "+row_error;return false;}
        json::Value output=json::Value::object();(void)find_seed_installation(id,&output);
        // The original duplicates the static seed row (when found), deletes these nine managed fields,
        // and then repopulates them from live Managed getters.
        for(const auto &field:kInstallationFields)output.erase(field.name);
        for(const auto &field:kInstallationFields){json::Value value;if(!managed_property_value(row_object,field,&value,&row_error)){runtime.release(row_handle);if(error)*error="installation field "+std::string(field.name)+" failed at row "+std::to_string(id)+": "+row_error;return false;}output[field.name]=std::move(value);}
        runtime.release(row_handle);converted.push_back(std::move(output));
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
    if(error)error->clear();if(action)*action=InitialLobbyAction::None;
    if(!login_controller){if(error)*error="UILoginPageController instance is null";return false;}
    auto &runtime=il2cpp::Runtime::instance();const auto &api=runtime.api();if(!api.object_get_class){if(error)*error="il2cpp_object_get_class is unavailable";return false;}
    il2cpp::Runtime::ThreadScope scope(runtime);if(!scope.ok()){if(error)*error=scope.error();return false;}
    std::string class_error;void *login_class=runtime.find_class("_AT.UI.UILoginPageController",&class_error);if(!login_class){if(error)*error="UILoginPageController type resolution failed: "+class_error;return false;}
    const uint32_t controller_handle=runtime.retain(login_controller,true);if(!controller_handle){if(error)*error="could not pin UILoginPageController instance";return false;}
    auto invoke_login_method=[&](const char *name,void **arguments)->bool{void *method=runtime.find_method(login_class,name,arguments?1:0);if(!method){if(error)*error=std::string("UILoginPageController method not found: ")+name;return false;}void *result=nullptr;std::string invoke_error;if(!runtime.invoke(method,login_controller,arguments,&result,&invoke_error)){if(error)*error=std::string("UILoginPageController.")+name+" invocation failed: "+invoke_error;return false;}if(action)*action=std::strcmp(name,"EnterLobby")==0?InitialLobbyAction::EnterLobby:InitialLobbyAction::PlayFirstDay;return true;};
    bool ok=true;
    if((content_bits&1ULL)!=0){ok=invoke_login_method("EnterLobby",nullptr);runtime.release(controller_handle);return ok;}
    std::string lookup_error;const uint32_t row_handle=table_get_pinned(data_manager,0x3c8,0x0700a185ULL,&lookup_error);if(!row_handle){runtime.release(controller_handle);if(error&&!lookup_error.empty())*error="initial quest lookup unavailable: "+lookup_error;return true;}
    void *row=runtime.target(row_handle);int32_t required_level=0;uint64_t reward_id=0;std::string getter_error;
    if(row&&row_i32(row,"get_ReqAccountLevel",&required_level,&getter_error)&&required_level==8&&row_u64(row,"get_OneTimeReward_S",&reward_id,&getter_error)&&reward_id==0x0700a185ULL){void *arguments[1]={row};ok=invoke_login_method("PlayFirstDay",arguments);}
    runtime.release(row_handle);runtime.release(controller_handle);return ok;
}
bool run_initial_lobby_route_from_store(void *login_controller,void *data_manager,InitialLobbyAction *action,std::string *error){return run_initial_lobby_route(login_controller,data_manager,store_content_bits(),action,error);}

namespace {
std::atomic<bool> g_touch_lobby_entered{false};
std::atomic_flag g_touch_lobby_running=ATOMIC_FLAG_INIT;
}
bool touch_enter_lobby(void *login_controller,std::string *error){
    if(error)error->clear();
    if(!login_controller){if(error)*error="touch replacement received a null UILoginPageController";return false;}
    if(g_touch_lobby_entered.load(std::memory_order_acquire))return true;
    if(g_touch_lobby_running.test_and_set(std::memory_order_acq_rel)){if(error)*error="lobby entry is already being dispatched";return false;}
    struct ClearFlag{std::atomic_flag &flag;~ClearFlag(){flag.clear(std::memory_order_release);}} clear{g_touch_lobby_running};
    // This build intentionally bypasses the original first-day tutorial branch. The bit is
    // also persisted in both local stores during bootstrap; using 1 here protects against
    // an older or not-yet-opened compatibility store without blocking the lobby route.
    int changed=0;
    (void)store_skip_first_day(&changed);
    InitialLobbyAction action=InitialLobbyAction::None;
    if(!run_initial_lobby_route(login_controller,nullptr,UINT64_C(1),&action,error))return false;
    if(action!=InitialLobbyAction::EnterLobby){if(error)*error="minimal touch replacement did not dispatch EnterLobby";return false;}
    g_touch_lobby_entered.store(true,std::memory_order_release);
    return true;
}

}
