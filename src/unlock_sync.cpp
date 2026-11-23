#include "unlock_sync.hpp"
#include <algorithm>
#include <cstdlib>
#include <limits>

namespace astrae::lobby {
namespace {
uint64_t cjson_u64(const json::Value &value){
    if(value.is_string()){const std::string s=value.as_string();char *end=nullptr;unsigned long long n=std::strtoull(s.c_str(),&end,10);return end&&end!=s.c_str()?static_cast<uint64_t>(n):0;}
    if(value.is_bool())return value.as_bool()?1ULL:0ULL;
    if(std::holds_alternative<double>(value.data())){double d=value.as_double();if(!(d>0.0))return 0;if(d>=static_cast<double>(std::numeric_limits<uint64_t>::max()))return std::numeric_limits<uint64_t>::max();return static_cast<uint64_t>(d);}
    return value.as_u64(0);
}
bool group_id_matches(const json::Value &entry,uint64_t id){return entry.is_object()&&entry.contains("TableId")&&cjson_u64(entry["TableId"])==id;}
uint64_t stored_value(const json::Value &entry){if(!entry.is_object()||!entry.contains("Value"))return 0;return cjson_u64(entry["Value"]);}
}
bool build_earned_unlocks(const std::vector<UnlockGroupRecord> &groups,const std::vector<ScenarioUnlockRecord> &scenario_rows,const json::Value &stored_unlocks,uint32_t max_bit,const QuestRankPredicate &quest_rank,json::Value *result,std::string *error){
    if(error)error->clear();if(!result){if(error)*error="earned_unlocks result pointer is null";return false;}
    if(groups.empty()||groups.size()>128){if(error)*error="earned_unlocks Unlock table count must be 1..128";return false;}
    if(scenario_rows.size()>4096){if(error)*error="earned_unlocks ScenarioPack table count must be <=4096";return false;}
    json::Value output=stored_unlocks.is_array()?stored_unlocks:json::Value::array();
    for(const auto &group:groups){
        bool stop_after_group=false;size_t found=output.size();
        for(size_t i=0;i<output.size();++i)if(group_id_matches(output[i],group.table_id))found=i;
        uint64_t value=found<output.size()?stored_value(output[found]):group.default_value;
        for(const auto &scenario:scenario_rows){
            bool eligible=quest_rank?quest_rank(scenario.scenario_pack_id,scenario.mode,scenario.bit_index):false;
            if(!eligible)eligible=scenario.scenario_pack_id==1000&&scenario.mode==0&&scenario.bit_index>=1&&static_cast<uint32_t>(scenario.bit_index)<=max_bit;
            if(!eligible)continue;
            const size_t ids=scenario.group_ids.size(),values=scenario.group_values.size();
            if(ids>32||ids!=values){stop_after_group=true;break;}
            for(size_t i=0;i<ids;++i){
                if(scenario.group_ids[i]!=group.table_id)continue;
                const int32_t candidate=scenario.group_values[i];
                if(group.mode==1){if(candidate>=0&&candidate<=63)value|=(uint64_t(1)<<static_cast<unsigned>(candidate));}
                else if(group.mode==2)value=std::max<uint64_t>(value,static_cast<uint32_t>(candidate));
            }
        }
        if(group.mode==2&&group.table_id==200)value=99;
        json::Value entry=found<output.size()?output[found]:json::Value::object();
        entry["TableId"]=std::to_string(group.table_id);
        entry["Value"]=std::to_string(value);
        if(found<output.size())output[found]=std::move(entry);else output.push_back(std::move(entry));
        if(stop_after_group)break;
    }
    *result=std::move(output);return true;
}
bool pack_state_quest_rank(const json::Value &pack_state,uint64_t group,int32_t kind,int32_t bit){
    if(bit<1||!pack_state.is_object())return false;const json::Value &packs=pack_state["Packs"];if(!packs.is_object())return false;const json::Value &pack=packs[std::to_string(group)];if(!pack.is_object())return false;
    if(kind==0||kind==8){const json::Value &index=pack["StoryQuestIndex"];return index.is_number()&&index.as_i64()>=bit;}
    const char *field=nullptr;int max_bit=21;bool bit_flag=false;switch(kind){case 1:field="NormalQuestBit";break;case 2:field="HardQuestBit";break;case 3:field="VeryHardQuestBit";break;case 5:field="ChallengeQuestBit";break;case 12:field="SubStoryQuestBit";max_bit=63;bit_flag=true;break;default:return false;}
    const json::Value &stored_bits=pack[field];if(bit>max_bit||(!stored_bits.is_string()&&!stored_bits.is_number()))return false;uint64_t bits=cjson_u64(stored_bits);if(bit_flag)return ((bits>>static_cast<unsigned>(bit))&1ULL)!=0;const unsigned shift=static_cast<unsigned>((bit-1)*3);return ((bits>>shift)&7ULL)!=0;
}

}
