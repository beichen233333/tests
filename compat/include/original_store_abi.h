#ifndef ASTRAE_ORIGINAL_STORE_ABI_H
#define ASTRAE_ORIGINAL_STORE_ABI_H
/* Public store ABI reconstructed from the original libastraeoffline.so DWARF.
 * Struct sizes, fields and offsets are verified from DWARF. */
#include <stdint.h>
#include "cJSON.h"
#ifdef __cplusplus
extern "C" {
#endif
#if defined(__GNUC__) || defined(__clang__)
#define ASTRAE_ORIGINAL_EXPORT __attribute__((visibility("default")))
#else
#define ASTRAE_ORIGINAL_EXPORT
#endif
struct store_reward { int type; uint64_t id; int amount; };
struct store_cost { int type; uint64_t id; int64_t amount; };
#if defined(__cplusplus)
static_assert(sizeof(struct store_reward)==24 && offsetof(struct store_reward,type)==0 && offsetof(struct store_reward,id)==8 && offsetof(struct store_reward,amount)==16,"store_reward ABI mismatch");
static_assert(sizeof(struct store_cost)==24 && offsetof(struct store_cost,type)==0 && offsetof(struct store_cost,id)==8 && offsetof(struct store_cost,amount)==16,"store_cost ABI mismatch");
#endif
ASTRAE_ORIGINAL_EXPORT const char *store_error(void);
ASTRAE_ORIGINAL_EXPORT cJSON *store_document(void);
ASTRAE_ORIGINAL_EXPORT uint64_t store_content_bits(void);
ASTRAE_ORIGINAL_EXPORT int store_initial_index(void);
ASTRAE_ORIGINAL_EXPORT int store_commit(void);
ASTRAE_ORIGINAL_EXPORT int store_open(const char *directory);
ASTRAE_ORIGINAL_EXPORT int store_mark_content(unsigned int bit);
ASTRAE_ORIGINAL_EXPORT int store_tutorial_progress(int group);
ASTRAE_ORIGINAL_EXPORT cJSON *store_pack(uint64_t group);
ASTRAE_ORIGINAL_EXPORT uint64_t store_background(void);
ASTRAE_ORIGINAL_EXPORT int store_set_background(uint64_t id);
ASTRAE_ORIGINAL_EXPORT int64_t store_reward_balance(int type, uint64_t id);
ASTRAE_ORIGINAL_EXPORT int store_equip_artifact(uint64_t artifact_server_id, uint64_t character_table_id, uint64_t character_server_id);
ASTRAE_ORIGINAL_EXPORT int store_story_start(uint64_t group, uint64_t quest, int bit, int difficulty, int count);
ASTRAE_ORIGINAL_EXPORT int store_apply_account_unlocks(const cJSON *unlocks);
ASTRAE_ORIGINAL_EXPORT void store_set_activity_period(int64_t (*period)(void));
ASTRAE_ORIGINAL_EXPORT uint64_t store_activity_value(const char *kind, uint64_t id, int daily);
ASTRAE_ORIGINAL_EXPORT int store_seed_full_runes(void);
ASTRAE_ORIGINAL_EXPORT int store_story_end_rewards(uint64_t group, uint64_t quest, int bit, int difficulty, int result, const struct store_reward *rewards, unsigned int count, int *changed);
ASTRAE_ORIGINAL_EXPORT int store_story_end(uint64_t group, uint64_t quest, int bit, int difficulty, int result, int *changed);
ASTRAE_ORIGINAL_EXPORT int store_replace_value(const char *key, const cJSON *value);
ASTRAE_ORIGINAL_EXPORT int store_skip_first_day(int *changed);
ASTRAE_ORIGINAL_EXPORT int store_complete_prologue(int expected_index, const struct store_reward *rewards, unsigned int count, const cJSON *unlocks, int *changed);
ASTRAE_ORIGINAL_EXPORT int store_update_with_cost(const char *key, const cJSON *value, const struct store_cost *costs, unsigned int count);
ASTRAE_ORIGINAL_EXPORT int store_purchase_rewards(const char *key, const cJSON *state, const struct store_cost *costs, unsigned int cost_count, const struct store_reward *rewards, unsigned int reward_count);
ASTRAE_ORIGINAL_EXPORT int store_farming_rank(uint64_t group, int type, int bit);
ASTRAE_ORIGINAL_EXPORT int store_farming_start(uint64_t group, uint64_t quest, int type, int bit, int formation, int count, const struct store_cost *costs, unsigned int cost_count);
ASTRAE_ORIGINAL_EXPORT int store_farming_end_favor(uint64_t group, uint64_t quest, int type, int bit, int count, int result, int rank, const struct store_reward *rewards, unsigned int reward_count, const cJSON *favor_state, int *changed);
ASTRAE_ORIGINAL_EXPORT int store_purchase_values(const cJSON *updates, const struct store_cost *costs, unsigned int cost_count, const struct store_reward *rewards, unsigned int reward_count);
ASTRAE_ORIGINAL_EXPORT int store_farming_end(uint64_t group, uint64_t quest, int type, int bit, int count, int result, int rank, const struct store_reward *rewards, unsigned int reward_count, int *changed);
ASTRAE_ORIGINAL_EXPORT int store_quest_rank(uint64_t group, int kind, int bit);
ASTRAE_ORIGINAL_EXPORT int store_quest_start(uint64_t group, uint64_t quest, int kind, int bit, int difficulty, int formation, const struct store_cost *costs, unsigned int cost_count);
ASTRAE_ORIGINAL_EXPORT int store_quest_start_multiple(uint64_t group, uint64_t quest, int kind, int bit, int difficulty, int formation, int play_count, const struct store_cost *costs, unsigned int cost_count);
ASTRAE_ORIGINAL_EXPORT int store_quest_end(uint64_t group, uint64_t quest, int kind, int bit, int difficulty, int result, int rank, int complete_pack, const struct store_reward *rewards, unsigned int reward_count, int *changed);
ASTRAE_ORIGINAL_EXPORT int store_quest_end_multiple(uint64_t group, uint64_t quest, int kind, int bit, int difficulty, int play_count, int result, int rank, int complete_pack, const struct store_reward *rewards, unsigned int reward_count, int *changed);
ASTRAE_ORIGINAL_EXPORT int store_quest_end_report(uint64_t group, uint64_t quest, int kind, int bit, int difficulty, int play_count, int result, int rank, int complete_pack, const struct store_reward *rewards, unsigned int reward_count, uint64_t enemy_kills, int *changed);
ASTRAE_ORIGINAL_EXPORT int store_quest_end_favor(uint64_t group, uint64_t quest, int kind, int bit, int difficulty, int play_count, int result, int rank, int complete_pack, const struct store_reward *rewards, unsigned int reward_count, uint64_t enemy_kills, const cJSON *favor_state, int *changed);
ASTRAE_ORIGINAL_EXPORT int store_exchange_gacha_points(const cJSON *state, uint64_t point_id, int cost, const struct store_reward *rewards, unsigned int reward_count);
ASTRAE_ORIGINAL_EXPORT int store_use_account_level_items(int count, int *added_exp);
ASTRAE_ORIGINAL_EXPORT int store_queue_prologue_gift(void);
ASTRAE_ORIGINAL_EXPORT int store_grant_once(const char *key, const cJSON *result, const struct store_reward *rewards, unsigned int count, int *changed);
ASTRAE_ORIGINAL_EXPORT int store_claim_mail(uint64_t server_id, int all, struct store_reward *rewards, unsigned int *count, int *changed);
ASTRAE_ORIGINAL_EXPORT int store_seed_defaults(const struct store_reward *rewards, unsigned int count);
ASTRAE_ORIGINAL_EXPORT int store_gained_exp(int before_level, int before_exp, int target_level, int target_exp);
/* JNI entry points recovered from the original AArch64 ELF. */
ASTRAE_ORIGINAL_EXPORT int Java_com_nc_astraeoffline_OfflineBridge_getActions(void *env, void *clazz);
ASTRAE_ORIGINAL_EXPORT void Java_com_nc_astraeoffline_OfflineBridge_requestAction(void *env, void *clazz, int action);
#ifdef __cplusplus
}
#endif
#endif
