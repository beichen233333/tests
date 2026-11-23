#include "unlock_sync.hpp"
#include "backend.hpp"
#include "il2cpp_runtime.hpp"
#include "lobby_restore.hpp"
#include "debug_log.hpp"
#include <algorithm>
#include <cstring>
#include <string>
#include <vector>
namespace astrae::lobby {
namespace {
constexpr size_t kManagedArrayLength=0x18;
constexpr size_t kManagedArrayVector=0x20;
void *load_pointer(const void *base,size_t offset){void *value=nullptr;if(base)std::memcpy(&value,static_cast<const uint8_t*>(base)+offset,sizeof(value));return value;}
uint64_t load_length(const void *array){uint64_t value=0;if(array)std::memcpy(&value,static_cast<const uint8_t*>(array)+kManagedArrayLength,sizeof(value));return value;}
int32_t load_i32(const void *row,size_t offset){int32_t value=0;if(row)std::memcpy(&value,static_cast<const uint8_t*>(row)+offset,sizeof(value));return value;}
std::string managed_type_name(const il2cpp::Api &api,void *klass){if(!klass||!api.class_get_type||!api.type_get_name)return "<unknown>";const void *type=api.class_get_type(klass);char *name=type?api.type_get_name(type):nullptr;std::string result=name?name:"<unknown>";if(name&&api.free_mem)api.free_mem(name);return result;}
bool array_element_is(il2cpp::Runtime &runtime,void *array,const char *expected,std::string *error,const char *tag){const auto &api=runtime.api();if(!array||!api.object_get_class||!api.class_get_element_class){if(error)*error=std::string("earned_unlocks ")+tag+" array element metadata unavailable";return false;}void *array_class=api.object_get_class(array);void *element=array_class?api.class_get_element_class(array_class):nullptr;const std::string actual=managed_type_name(api,element);if(actual!=expected){if(error)*error=std::string("earned_unlocks ")+tag+" array element mismatch: "+actual+" expected "+expected;return false;}return true;}
}
bool extract_earned_unlocks(void *data_manager,uint32_t max_bit,const QuestRankPredicate &quest_rank,const json::Value &stored_unlocks,json::Value *result,std::string *error){
    if(error)error->clear();if(!data_manager||!result){if(error)*error="earned_unlocks DataManager/result is null";return false;}
    auto &runtime=il2cpp::Runtime::instance();const auto &api=runtime.api();if(!api.object_get_class||!api.class_get_element_class||!api.class_get_type||!api.type_get_name){if(error)*error="earned_unlocks IL2CPP array metadata API unavailable";return false;}
    il2cpp::Runtime::ThreadScope thread(runtime);if(!thread.ok()){if(error)*error=thread.error();return false;}
    void *loader=load_pointer(data_manager,0x10);void *unlock_table=load_pointer(loader,0x4a0);void *scenario_table=load_pointer(loader,0x3c8);void *unlock_root=load_pointer(unlock_table,0x18);void *scenario_root=load_pointer(scenario_table,0x18);void *unlock_list=load_pointer(unlock_root,0x10);void *scenario_list=load_pointer(scenario_root,0x10);
    if(!loader||!unlock_table||!scenario_table||!unlock_root||!scenario_root||!unlock_list||!scenario_list){if(error)*error="earned_unlocks missing CustomDataLoader 0x4a0/0x3c8 Root/DataList chain";return false;}
    const uint64_t unlock_count=load_length(unlock_list),scenario_count=load_length(scenario_list);if(unlock_count==0||unlock_count>128||scenario_count>4096){if(error)*error="earned_unlocks list count guard failed: Unlock="+std::to_string(unlock_count)+" ScenarioPack="+std::to_string(scenario_count);return false;}
    if(!array_element_is(runtime,unlock_list,"_AT.Core.DataSheet.Unlock",error,"Unlock DataList"))return false;
    if(!array_element_is(runtime,scenario_list,"_AT.Core.DataSheet.ScenarioPack",error,"ScenarioPack DataList"))return false;
    std::vector<UnlockGroupRecord> groups;groups.reserve(static_cast<size_t>(unlock_count));
    for(uint64_t i=0;i<unlock_count;++i){void *row=load_pointer(unlock_list,kManagedArrayVector+static_cast<size_t>(i)*sizeof(void*));UnlockGroupRecord record;int32_t default_value=0;if(!row||!row_u64(row,"get_UnlockGroupId",&record.table_id,error)||!row_i32(row,"get_DefaultValue",&default_value,error))return false;record.default_value=static_cast<uint32_t>(default_value);record.mode=load_i32(row,0x20);groups.push_back(record);}
    std::vector<ScenarioUnlockRecord> scenarios;scenarios.reserve(static_cast<size_t>(scenario_count));
    for(uint64_t i=0;i<scenario_count;++i){void *row=load_pointer(scenario_list,kManagedArrayVector+static_cast<size_t>(i)*sizeof(void*));if(!row){if(error)*error="earned_unlocks contains a null ScenarioPack row at index "+std::to_string(i);return false;}ScenarioUnlockRecord record;if(!row_u64(row,"get_ScenarioPackId",&record.scenario_pack_id,error)||!row_i32(row,"get_BitIndex",&record.bit_index,error))return false;record.mode=load_i32(row,0x30);void *ids=load_pointer(row,0x178),*values=load_pointer(row,0x180);if(ids&&values){const uint64_t id_count=load_length(ids),value_count=load_length(values);if(!array_element_is(runtime,ids,"System.UInt64",error,"ReqUnlockGroupId")||!array_element_is(runtime,values,"System.Int32",error,"ReqUnlockGroupValue"))return false;if(id_count>32||value_count>32||id_count!=value_count){record.group_ids.resize(static_cast<size_t>(std::min<uint64_t>(id_count,33)));record.group_values.resize(static_cast<size_t>(std::min<uint64_t>(value_count,33)));}else{record.group_ids.resize(static_cast<size_t>(id_count));record.group_values.resize(static_cast<size_t>(value_count));for(uint64_t j=0;j<id_count;++j){std::memcpy(&record.group_ids[static_cast<size_t>(j)],static_cast<const uint8_t*>(ids)+kManagedArrayVector+static_cast<size_t>(j)*sizeof(uint64_t),sizeof(uint64_t));std::memcpy(&record.group_values[static_cast<size_t>(j)],static_cast<const uint8_t*>(values)+kManagedArrayVector+static_cast<size_t>(j)*sizeof(int32_t),sizeof(int32_t));}}}scenarios.push_back(std::move(record));}
    if(!build_earned_unlocks(groups,scenarios,stored_unlocks,max_bit,quest_rank,result,error))return false;ASTRAE_LOGI("earned_unlocks extracted: Unlock=%llu ScenarioPack=%llu output=%zu",static_cast<unsigned long long>(unlock_count),static_cast<unsigned long long>(scenario_count),result->size());return true;
}
bool sync_unlocks_runtime(void *data_manager,std::string *error){
    if(error)error->clear();auto &backend=Backend::instance();if(!backend.is_initialized()){if(error)*error="sync_unlocks requires an initialized offline Backend";return false;}
    json::Value state;try{state=json::parse(backend.state_json());}catch(...){if(error)*error="sync_unlocks could not parse Backend state";return false;}
    const json::Value &progress=state["Progress"];const json::Value &stored=progress["UnlockInfos"];const json::Value &pack_state=state["PackState"];
    QuestRankPredicate predicate=[&pack_state](uint64_t pack,int32_t kind,int32_t bit){return pack_state_quest_rank(pack_state,pack,kind,bit);};
    json::Value earned;if(!extract_earned_unlocks(data_manager,0,predicate,stored,&earned,error))return false;
    const json::Value original=stored.is_array()?stored:json::Value::array();if(json::dump(original,false)==json::dump(earned,false)){ASTRAE_LOGI("sync_unlocks: UnlockInfos unchanged (%zu rows)",earned.size());return true;}
    if(!backend.apply_account_unlocks(earned)){if(error)*error="sync_unlocks Backend account-unlock transaction failed";return false;}
    ASTRAE_LOGI("sync_unlocks: committed %zu UnlockInfos rows",earned.size());return true;
}

}
