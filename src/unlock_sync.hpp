#pragma once
#include "json.hpp"
#include <cstdint>
#include <functional>
#include <string>
#include <vector>

namespace astrae::lobby {
struct UnlockGroupRecord {
    uint64_t table_id=0;
    int32_t mode=0;
    uint32_t default_value=0;
};
struct ScenarioUnlockRecord {
    uint64_t scenario_pack_id=0;
    int32_t mode=0;
    int32_t bit_index=0;
    std::vector<uint64_t> group_ids;
    std::vector<int32_t> group_values;
};
using QuestRankPredicate=std::function<bool(uint64_t scenario_pack_id,int32_t mode,int32_t bit_index)>;
// Pure reconstruction of earned_unlocks@0x2531ac. The predicate corresponds to the
// external Store ABI store_quest_rank(group, kind, bit). The Backend keeps packed
// quest bitfields as normalized JSON numbers; legacy Store values are decimal strings.
bool pack_state_quest_rank(const json::Value &pack_state,uint64_t group,int32_t kind,int32_t bit);
bool sync_unlocks_runtime(void *data_manager,std::string *error=nullptr);
bool extract_earned_unlocks(void *data_manager,uint32_t max_bit,const QuestRankPredicate &quest_rank,const json::Value &stored_unlocks,json::Value *result,std::string *error=nullptr);
bool build_earned_unlocks(const std::vector<UnlockGroupRecord> &groups,const std::vector<ScenarioUnlockRecord> &scenario_rows,const json::Value &stored_unlocks,uint32_t max_bit,const QuestRankPredicate &quest_rank,json::Value *result,std::string *error=nullptr);
}
