#include "unlock_sync.hpp"
#include <cassert>
#include <cstdint>
#include <string>
#include <vector>
using astrae::json::Value;
using namespace astrae::lobby;
int main(){
    std::vector<UnlockGroupRecord> groups={{1000,1,1},{200,2,3},{300,0,7}};
    std::vector<ScenarioUnlockRecord> rows={
        {1001,0,4,{1000,200,300},{4,5,6}},
        {1001,1,8,{1000,200},{63,10}},
        {1000,0,2,{1000,200},{2,8}},
        {1002,2,1,{1000,200},{9,15}},
        {1003,0,1,{1000},{64}},
        {1004,0,1,{1000,200},{1}}
    };
    Value stored=Value::array();Value old=Value::object();old["TableId"]="1000";old["Value"]="8";stored.push_back(old);Value numeric=Value::object();numeric["TableId"]=uint64_t(200);numeric["Value"]=uint64_t(20);stored.push_back(numeric);
    Value result;std::string error;auto rank=[](uint64_t pack,int32_t kind,int32_t bit){return pack==1001&&kind==1&&bit==8;};
    assert(build_earned_unlocks(groups,rows,stored,0,rank,&result,&error));assert(result.size()==3);
    assert(result[0]["TableId"].as_string()=="1000");assert(result[0]["Value"].as_string()=="9223372036854775816");
    assert(result[1]["TableId"].as_string()=="200");assert(result[1]["Value"].as_string()=="99");
    assert(result[2]["TableId"].as_string()=="300");assert(result[2]["Value"].as_string()=="7");
    Value seed;std::vector<UnlockGroupRecord> one={{1000,1,0}};std::vector<ScenarioUnlockRecord> seed_rows={{1000,0,3,{1000},{5}}};assert(build_earned_unlocks(one,seed_rows,Value::array(),3,{},&seed,&error));assert(seed[0]["Value"].as_string()=="32");
    std::vector<ScenarioUnlockRecord> invalid={{1000,0,1,{1000,1000},{1}}};assert(build_earned_unlocks(one,invalid,Value::array(),1,{},&seed,&error));assert(seed[0]["Value"].as_string()=="0");
    assert(!build_earned_unlocks({},rows,stored,0,rank,&result,&error)&&!error.empty());
    std::vector<UnlockGroupRecord> two_groups={{1000,1,0},{200,2,0}};std::vector<ScenarioUnlockRecord> malformed_stop={{1000,0,1,{1000,200},{2}},{1000,0,2,{1000},{4}}};assert(build_earned_unlocks(two_groups,malformed_stop,Value::array(),1,{},&result,&error));assert(result.size()==1&&result[0]["TableId"].as_string()=="1000");
    Value packs=Value::object(),pack=Value::object();pack["StoryQuestIndex"]=int64_t(7);pack["NormalQuestBit"]="56";pack["HardQuestBit"]="8";pack["VeryHardQuestBit"]="0";pack["ChallengeQuestBit"]="64";pack["SubStoryQuestBit"]="16";packs["Packs"]["1001"]=pack;
    assert(pack_state_quest_rank(packs,1001,0,7));assert(!pack_state_quest_rank(packs,1001,0,8));assert(pack_state_quest_rank(packs,1001,8,7));assert(pack_state_quest_rank(packs,1001,1,2));assert(pack_state_quest_rank(packs,1001,2,2));assert(!pack_state_quest_rank(packs,1001,3,2));assert(pack_state_quest_rank(packs,1001,5,3));assert(pack_state_quest_rank(packs,1001,12,4));assert(!pack_state_quest_rank(packs,1001,12,5));pack["NormalQuestBit"]=uint64_t(56);packs["Packs"]["1001"]=pack;assert(pack_state_quest_rank(packs,1001,1,2));
    assert(build_earned_unlocks(one,{},Value::array(),0,{},&result,&error));assert(result[0]["Value"].as_string()=="0");
    std::vector<UnlockGroupRecord> too_many(129);assert(!build_earned_unlocks(too_many,rows,stored,0,rank,&result,&error));
    std::vector<ScenarioUnlockRecord> too_many_rows(4097);assert(!build_earned_unlocks(one,too_many_rows,stored,0,rank,&result,&error));
    return 0;
}
