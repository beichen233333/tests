#include "legacy_save_migration.hpp"
#include <cassert>
#include <cstdint>
#include <string>
using astrae::json::Value;
int main(){
    const std::string text=R"({"schema_version":1,"substory_direct_index":true,"profile_origin":"old_profile","account":{"id":"98765","name":"Beichen","level":42,"exp":1234},"tutorial":{"content_bits":"725367","initial_index":7},"characters":[{"TableId":"10001","ServerId":"100000","Level":80,"Exp":321,"Star":3,"Skills":[{"Level":5}]}],"inventory":[{"type":5,"table_id":"20001","count":17}],"currencies":[{"type":0,"value":"7"},{"type":1,"value":"123456"},{"type":2,"value":"99"}],"vignettes":[{"TableId":"30001","ServerId":"100001","Star":2}],"artifacts":[],"runes":[{"TableId":"40001","Count":8}],"tickets":[{"TableId":"50001","Count":3,"LastRechargeTime":"0"}],"costumes":[],"mails":[],"formations":[{"Index":0,"CharacterTableIds":["10001"],"VignetteTableIds":[]}],"unlocks":[{"TableId":"200","Value":"12"}],"packs":{"1000":{"group_id":"1000","open_type":3,"story_index":8,"repeat_index":2,"max_difficulty":3,"normal_bits":"9","hard_bits":"2","very_hard_bits":"1","substory_bits":"4","challenge_bits":"5"}},"gacha_state":{"WishlistInfo":{"GroupId":1}},"farming_state":{"active":{"quest_id":"10"}},"raid_state":{"cleared":true},"lobby_background_id":"30002"})";
    Value legacy=astrae::json::parse(text), state; std::string error;
    assert(astrae::save_migration::migrate_compat_document(legacy,state,&error));
    assert(state["SchemaVersion"].as_u64()==3);
    assert(state["Account"]["GameAccountId"].as_string()=="98765");
    assert(state["Account"]["UserName"].as_string()=="Beichen");
    assert(state["Account"]["Level"].as_i64()==42 && state["Account"]["Exp"].as_i64()==1234);
    assert(state["TutorialInfo"]["ContentBits"].as_u64()==725367 && state["TutorialInfo"]["InitialIndex"].as_i64()==7);
    assert(state["characters"].size()==1 && state["characters"][0]["TableId"].as_u64()==10001 && state["characters"][0]["ServerId"].as_u64()==100000);
    assert(state["inventory"].size()==4);
    assert(state["inventory"][0]["Type"].as_i64()==5 && state["inventory"][0]["TableId"].as_u64()==20001 && state["inventory"][0]["Count"].as_i64()==17);
    assert(state["inventory"][1]["Type"].as_i64()==2 && state["inventory"][1]["TableId"].as_u64()==0 && state["inventory"][1]["Count"].as_i64()==7);
    assert(state["inventory"][2]["Type"].as_i64()==2 && state["inventory"][2]["TableId"].as_u64()==1 && state["inventory"][2]["Count"].as_i64()==123456);
    assert(state["rune_stock"][0]["TableId"].as_u64()==40001 && state["rune_stock"][0]["Count"].as_i64()==8);
    assert(state["Progress"]["UnlockInfos"].size()==1);
    assert(state["PackState"]["Packs"]["1000"]["GroupId"].as_u64()==1000);
    assert(state["PackState"]["Packs"]["1000"]["OpenType"].as_i64()==3);
    assert(state["PackState"]["Packs"]["1000"]["StoryQuestIndex"].as_i64()==8);
    assert(state["PackState"]["Packs"]["1000"]["RepeatQuestIndex"].as_i64()==2);
    assert(state["PackState"]["Packs"]["1000"]["MaxQuestDifficulty"].as_i64()==3);
    assert(state["PackState"]["Packs"]["1000"]["NormalQuestBit"].as_u64()==9);
    assert(state["PackState"]["Packs"]["1000"]["SubStoryQuestBit"].as_u64()==4);
    assert(state["Legacy_farming_state"]["active"]["quest_id"].as_string()=="10");
    assert(state["LegacyCompatSnapshot"]["tutorial"]["content_bits"].as_string()=="725367");
    Value untouched=legacy; assert(untouched["account"]["id"].as_string()=="98765");
    Value blank=astrae::json::parse(R"({"schema_version":1,"substory_direct_index":true,"profile_origin":"new_local_profile","account":{"id":"1","name":"Offline Player","level":1,"exp":0},"tutorial":{"content_bits":"0","initial_index":0},"characters":[],"inventory":[],"currencies":[],"formations":[],"vignettes":[],"artifacts":[],"runes":[],"tickets":[],"costumes":[],"mails":[],"unlocks":[],"quests":[],"rewards":[],"packs":{}})");
    assert(astrae::save_migration::is_pristine_new_profile(blank));
    blank["tutorial"]["content_bits"]="1"; assert(!astrae::save_migration::is_pristine_new_profile(blank));
    Value bad=legacy; bad["tutorial"]["content_bits"]="nope"; Value ignored; assert(!astrae::save_migration::migrate_compat_document(bad,ignored,&error));
    Value bad_currency=legacy; bad_currency["currencies"][0]["type"]=99; assert(!astrae::save_migration::migrate_compat_document(bad_currency,ignored,&error));
    return 0;
}
