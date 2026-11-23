#pragma once
#include "json.hpp"
#include <cerrno>
#include <cstdint>
#include <cstdlib>
#include <limits>
#include <algorithm>
#include <string>

namespace astrae::save_migration {
using J = json::Value;
inline uint64_t parse_u64(const J &value, uint64_t fallback=0) {
    if (value.is_number()) return value.as_u64(fallback);
    if (!value.is_string()) return fallback;
    const std::string text=value.as_string();
    if (text.empty() || text[0]=='-') return fallback;
    errno=0; char *end=nullptr; const unsigned long long parsed=std::strtoull(text.c_str(),&end,10);
    if(errno || end==text.c_str() || !end || *end!='\0') return fallback;
    return static_cast<uint64_t>(parsed);
}
inline int64_t parse_i64(const J &value, int64_t fallback=0) {
    if (value.is_number()) return value.as_i64(fallback);
    if (!value.is_string()) return fallback;
    const std::string text=value.as_string();
    if(text.empty()) return fallback;
    errno=0; char *end=nullptr; const long long parsed=std::strtoll(text.c_str(),&end,10);
    if(errno || end==text.c_str() || !end || *end!='\0') return fallback;
    return static_cast<int64_t>(parsed);
}
inline void normalize_record(J &value) {
    if(value.is_array()) { for(auto &item:value.as_array()) normalize_record(item); return; }
    if(!value.is_object()) return;
    for(auto &entry:value.as_object()) {
        const std::string &key=entry.first; J &child=entry.second;
        if(key=="TableId" || key=="ServerId" || key=="CharacterTableId" || key=="VignetteTableId" || key=="LobbyBackgroundId" || key=="GroupId" || key=="NormalQuestBit" || key=="HardQuestBit" || key=="VeryHardQuestBit" || key=="SubStoryQuestBit" || key=="ChallengeQuestBit") {
            const uint64_t n=parse_u64(child,std::numeric_limits<uint64_t>::max()); if(n!=std::numeric_limits<uint64_t>::max()) child=n;
        } else if(key=="Count" || key=="Level" || key=="Exp" || key=="Star" || key=="Potential" || key=="Index" || key=="Type" || key=="LastRechargeTime" || key=="OpenType" || key=="StoryIndex" || key=="RepeatIndex" || key=="MaxDifficulty" || key=="StoryQuestIndex" || key=="RepeatQuestIndex" || key=="MaxQuestDifficulty") {
            const int64_t n=parse_i64(child,std::numeric_limits<int64_t>::min()); if(n!=std::numeric_limits<int64_t>::min()) child=n;
        } else if(key.size()>=8 && key.compare(key.size()-8,8,"TableIds")==0 && child.is_array()) {
            for(auto &id:child.as_array()) { const uint64_t n=parse_u64(id,std::numeric_limits<uint64_t>::max()); if(n!=std::numeric_limits<uint64_t>::max()) id=n; }
        }
        if(child.is_object() || child.is_array()) normalize_record(child);
    }
}
inline J migrate_pack_records(const J &source) {
    J out=J::object();
    if(!source.is_object()) return out;
    const struct Alias { const char *dst; const char *src; bool unsigned_value; } aliases[]={
        {"GroupId","group_id",true},{"OpenType","open_type",false},{"StoryQuestIndex","story_index",false},
        {"RepeatQuestIndex","repeat_index",false},{"MaxQuestDifficulty","max_difficulty",false},
        {"NormalQuestBit","normal_bits",true},{"HardQuestBit","hard_bits",true},
        {"VeryHardQuestBit","very_hard_bits",true},{"SubStoryQuestBit","substory_bits",true},
        {"ChallengeQuestBit","challenge_bits",true}
    };
    for(const auto &entry:source.as_object()) {
        if(!entry.second.is_object()) continue;
        J pack=entry.second; normalize_record(pack);
        for(const auto &alias:aliases) {
            if(pack.contains(alias.dst)) continue;
            const J &old=entry.second[alias.src];
            if(!old.is_null()) pack[alias.dst]=alias.unsigned_value?J(parse_u64(old)):J(parse_i64(old));
        }
        if(!pack.contains("GroupId")) pack["GroupId"]=parse_u64(J(entry.first));
        out[entry.first]=std::move(pack);
    }
    return out;
}
inline J normalized_array(const J &source) { J out=source.is_array()?source:J::array(); normalize_record(out); return out; }
inline bool is_pristine_new_profile(const J &legacy) {
    if(!legacy.is_object() || legacy["profile_origin"].as_string()!="new_local_profile") return false;
    const J &account=legacy["account"],&tutorial=legacy["tutorial"];
    if(!account.is_object()||!tutorial.is_object()) return false;
    if(account["id"].scalar_string()!="1" || account["name"].as_string()!="Offline Player" || parse_i64(account["level"],1)!=1 || parse_i64(account["exp"],0)!=0) return false;
    if(parse_u64(tutorial["content_bits"],std::numeric_limits<uint64_t>::max())!=0 || parse_i64(tutorial["initial_index"],-1)!=0) return false;
    for(const char *field:{"characters","inventory","currencies","formations","vignettes","artifacts","runes","tickets","costumes","mails","unlocks","quests","rewards"}) if(legacy[field].is_array()&&legacy[field].size()!=0) return false;
    if(legacy["packs"].is_object()&&legacy["packs"].size()!=0) return false;
    return true;
}
inline bool migrate_compat_document(const J &legacy, J &out, std::string *error=nullptr) {
    auto fail=[&](const char *reason){ if(error)*error=reason; return false; };
    if(!legacy.is_object()) return fail("legacy compatibility save root is not an object");
    if(parse_i64(legacy["schema_version"],-1)!=1) return fail("unsupported compatibility save schema_version");
    if(!legacy["account"].is_object()) return fail("legacy compatibility save has no account object");
    if(!legacy["tutorial"].is_object()) return fail("legacy compatibility save has no tutorial object");
    const uint64_t bits=parse_u64(legacy["tutorial"]["content_bits"],std::numeric_limits<uint64_t>::max());
    const int64_t initial=parse_i64(legacy["tutorial"]["initial_index"],-1);
    if(bits==std::numeric_limits<uint64_t>::max() || initial<0 || initial>=128) return fail("legacy tutorial progress is invalid; original save retained");
    J state=J::object(); state["SchemaVersion"]=uint64_t(3); state["LegacySaveImported"]=true;
    state["LegacyCompatSnapshot"]=legacy;
    const J &old_account=legacy["account"]; J account=J::object();
    account["ServerId"]=uint64_t(1); account["GameAccountId"]=old_account["id"].scalar_string();
    const std::string name=old_account["name"].as_string("Offline Player");
    account["UserName"]=name; account["NickName"]=name;
    account["Level"]=parse_i64(old_account["level"],1); account["Exp"]=parse_i64(old_account["exp"],0); account["GameLocale"]=int64_t(0);
    if(old_account.contains("coupon_exp_bank")) account["CouponExpBank"]=parse_i64(old_account["coupon_exp_bank"],0);
    state["Account"]=std::move(account);
    state["TutorialInfo"]=J::object(); state["TutorialInfo"]["ContentBits"]=bits; state["TutorialInfo"]["InitialIndex"]=initial;
    state["inventory"]=J::array();
    if(legacy["inventory"].is_array()) for(const auto &row:legacy["inventory"].as_array()) {
        const int64_t type=parse_i64(row["type"],parse_i64(row["Type"],-1));
        if(type<0 || (type!=5 && !row.contains("TableId") && !row.contains("table_id"))) continue;
        const uint64_t id=parse_u64(row["table_id"],parse_u64(row["TableId"],0));
        const int64_t count=parse_i64(row["count"],parse_i64(row["Count"],-1));
        if((!id && type!=2) || (type==2 && id>=8) || count<0) return fail("legacy inventory contains an invalid item row; original save retained");
        J item=J::object(); item["Type"]=type; item["TableId"]=id; item["Count"]=count; state["inventory"].push_back(std::move(item));
    }
    if(legacy["currencies"].is_array()) for(const auto &row:legacy["currencies"].as_array()) {
        const uint64_t id=parse_u64(row["type"],parse_u64(row["Type"],std::numeric_limits<uint64_t>::max()));
        const int64_t count=parse_i64(row["value"],parse_i64(row["Value"],-1));
        if(id==std::numeric_limits<uint64_t>::max() || id>=8 || count<0) return fail("legacy currency row is invalid; original save retained");
        J item=J::object(); item["Type"]=int64_t(2); item["TableId"]=id; item["Count"]=count; state["inventory"].push_back(std::move(item));
    }
    const char *array_fields[]={"characters","vignettes","artifacts","mails","formations","tickets","costumes","ReceiptInfos","GachaAdjustPointInfos","RuneGrantInfos","ClaimedMailIds","ClaimedGuideMissionTaskIds","ClaimedFavorRewardIds"};
    for(const char *field:array_fields) state[field]=normalized_array(legacy[field]);
    state["runes"]=J::array(); state["rune_stock"]=normalized_array(legacy["runes"]);
    state["RecruitInfos"]=normalized_array(legacy["recruits"]);
    state["quests"]=J::object(); state["gacha_state"]=legacy["gacha_state"].is_object()?legacy["gacha_state"]:J::object(); normalize_record(state["gacha_state"]);
    state["Progress"]=J::object(); state["Progress"]["UnlockInfos"]=normalized_array(legacy["unlocks"]);
    state["PackState"]=J::object(); state["PackState"]["Packs"]=migrate_pack_records(legacy["packs"]);
    state["DailyState"]=legacy["daily_state"].is_object()?legacy["daily_state"]:J::object();
    state["GuideState"]=legacy["guide_state"].is_object()?legacy["guide_state"]:J::object();
    for(const char *field:{"farming_state","raid_state","date_field","active_quest","active_story","active_farming","last_quest_result","last_story_result","activity_counters","custom_item_use","rewards","default_parcels_initialized"}) if(legacy.contains(field)) state[std::string("Legacy_")+field]=legacy[field];
    const uint64_t bg=parse_u64(legacy["lobby_background_id"],30001); state["LobbyBackgroundId"]=bg;
    uint64_t next_id=100000;
    for(const auto &row:state["characters"].as_array()) next_id=std::max(next_id,parse_u64(row["ServerId"],99999)+1);
    for(const auto &row:state["vignettes"].as_array()) next_id=std::max(next_id,parse_u64(row["ServerId"],99999)+1);
    state["NextServerId"]=next_id; state["StoreRevision"]=uint64_t(0);
    out=std::move(state); if(error)error->clear(); return true;
}
}
