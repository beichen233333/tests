#pragma once
#include "json.hpp"
#include <cstddef>
#include <cstdint>
#include <string>
namespace astrae::lobby {
// Raw 24-byte reward accumulator row recovered from gacha_converted_batch.
struct GachaRewardEntry {uint64_t descriptor=0;uint64_t table_id=0;int32_t count=0;int32_t auxiliary=0;};
// Recreates gacha_visual@0x252a3c's cJSON payload shape from the reference ELF.
json::Value build_gacha_visual(int32_t type,uint64_t table_id,int32_t amount,bool is_new);
// Recreates the observed (low-32-bit type, id) merge / overflow / append rule.
bool merge_gacha_reward_entry(GachaRewardEntry *entries,size_t *count,size_t capacity,const GachaRewardEntry &incoming,std::string *error=nullptr);
}
