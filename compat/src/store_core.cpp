#include "original_store_abi.h"
#include <algorithm>
#include <cerrno>
#include <cinttypes>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <limits>
#include <mutex>
#include <string>
#include <sys/stat.h>
#include <unistd.h>
#include <vector>

namespace {
constexpr int64_t kLevelExp[99] = {32, 36, 40, 42, 48, 52, 56, 60, 64, 68, 72, 76, 80, 84, 88, 92, 96, 100, 104, 108, 112, 116, 120, 124, 128, 132, 136, 140, 144, 150, 150, 150, 150, 150, 150, 150, 150, 150, 150, 150, 150, 150, 150, 150, 150, 150, 150, 150, 500000, 500000, 500000, 500000, 500000, 500000, 500000, 500000, 500000, 500000, 500000, 500000, 500000, 500000, 500000, 500000, 500000, 500000, 500000, 500000, 500000, 500000, 500000, 500000, 500000, 500000, 500000, 500000, 500000, 500000, 500000, 500000, 500000, 500000, 500000, 500000, 500000, 500000, 500000, 500000, 500000, 500000, 500000, 500000, 500000, 500000, 500000, 500000, 500000, 500000, 500000};
constexpr size_t kSavePathCapacity = 768;
constexpr size_t kPathCapacity = 780;
constexpr size_t kErrorCapacity = 160;
constexpr size_t kMaxSaveBytes = 0x100000;
cJSON *g_document = nullptr;
char g_save_path[kSavePathCapacity]{};
char g_backup_path[kPathCapacity]{};
char g_temp_path[kPathCapacity]{};
char g_error[kErrorCapacity]{};
std::recursive_mutex g_store_mutex;
int64_t (*g_activity_period)(void) = nullptr;

void set_error(const char *message) {
    const int saved_errno = errno;
    std::snprintf(g_error, sizeof(g_error), "%s (errno=%d)", message ? message : "store error", saved_errno);
}

cJSON *object_item(cJSON *object, const char *key) {
    return cJSON_GetObjectItemCaseSensitive(object, key);
}

bool put_item(cJSON *object, const char *key, cJSON *value) {
    if (!object || !key || !value) { cJSON_Delete(value); return false; }
    if (cJSON_HasObjectItem(object, key)) return cJSON_ReplaceItemInObjectCaseSensitive(object, key, value) != 0;
    return cJSON_AddItemToObject(object, key, value) != 0;
}

bool put_string(cJSON *object, const char *key, const char *value) {
    return put_item(object, key, cJSON_CreateString(value ? value : ""));
}

bool put_number(cJSON *object, const char *key, double value) {
    return put_item(object, key, cJSON_CreateNumber(value));
}

bool put_bool(cJSON *object, const char *key, bool value) {
    return put_item(object, key, cJSON_CreateBool(value ? 1 : 0));
}

cJSON *read_document(const char *path, int *exists) {
    if (exists) *exists = 0;
    FILE *file = std::fopen(path, "rb");
    if (!file) {
        if (exists) *exists = errno != ENOENT;
        return nullptr;
    }
    if (exists) *exists = 1;
    if (std::fseek(file, 0, SEEK_END) != 0) { std::fclose(file); return nullptr; }
    const long size = std::ftell(file);
    if (size < 0 || static_cast<unsigned long>(size) >= kMaxSaveBytes) { std::fclose(file); return nullptr; }
    std::rewind(file);
    char *buffer = static_cast<char *>(std::malloc(static_cast<size_t>(size) + 1));
    if (!buffer) { std::fclose(file); return nullptr; }
    const size_t count = std::fread(buffer, 1, static_cast<size_t>(size), file);
    std::fclose(file);
    buffer[count] = '\0';
    cJSON *document = nullptr;
    if (count == static_cast<size_t>(size)) document = cJSON_ParseWithLengthOpts(buffer, static_cast<size_t>(size) + 1, nullptr, 1);
    std::free(buffer);
    if (!cJSON_IsObject(document)) { cJSON_Delete(document); return nullptr; }
    return document;
}

cJSON *make_default_document() {
    cJSON *root = cJSON_CreateObject();
    if (!root) return nullptr;
    bool ok = put_number(root, "schema_version", 1) && put_bool(root, "substory_direct_index", true) && put_string(root, "profile_origin", "new_local_profile");
    cJSON *account = cJSON_AddObjectToObject(root, "account");
    ok = ok && account && put_string(account, "id", "1") && put_string(account, "name", "Offline Player") && put_number(account, "level", 1) && put_number(account, "exp", 0);
    cJSON *tutorial = cJSON_AddObjectToObject(root, "tutorial");
    ok = ok && tutorial && put_string(tutorial, "content_bits", "0") && put_number(tutorial, "initial_index", 0);
    const char *arrays[] = {"characters", "inventory", "currencies", "formations", "quests", "rewards"};
    for (const char *name : arrays) ok = ok && cJSON_AddArrayToObject(root, name);
    if (!ok) { cJSON_Delete(root); return nullptr; }
    return root;
}

bool valid_unsigned_decimal(const char *text, uint64_t *value) {
    if (!text || !*text || *text == '-') return false;
    errno = 0;
    char *end = nullptr;
    const unsigned long long parsed = std::strtoull(text, &end, 10);
    if (errno || end == text || !end || *end != '\0') return false;
    if (value) *value = static_cast<uint64_t>(parsed);
    return true;
}

cJSON *balance_entry(int type, uint64_t id, bool create) {
    if (!g_document) return nullptr;
    const char *array_name = type == 2 ? "currencies" : "inventory";
    cJSON *array = object_item(g_document, array_name);
    if (!cJSON_IsArray(array)) return nullptr;
    char id_text[32];
    std::snprintf(id_text, sizeof(id_text), "%llu", static_cast<unsigned long long>(id));
    for (cJSON *row = array->child; row; row = row->next) {
        cJSON *type_item = object_item(row, "type");
        if (!cJSON_IsNumber(type_item)) continue;
        if (type == 2) {
            if (type_item->valueint == static_cast<int>(id)) return row;
        } else {
            if (type_item->valueint != 5) continue;
            cJSON *table_id = object_item(row, "table_id");
            if (cJSON_IsString(table_id) && table_id->valuestring && std::strcmp(table_id->valuestring, id_text) == 0) return row;
        }
    }
    if (!create) return nullptr;
    cJSON *row = cJSON_CreateObject();
    if (!row) return nullptr;
    bool ok = false;
    if (type == 2) ok = put_number(row, "type", static_cast<int>(id)) && put_string(row, "value", "0");
    else ok = put_number(row, "type", 5) && put_string(row, "table_id", id_text) && put_number(row, "count", 0);
    if (!ok || !cJSON_AddItemToArray(array, row)) { cJSON_Delete(row); return nullptr; }
    return row;
}

cJSON *counted_entry(int type, uint64_t id, bool create) {
    if (!g_document) return nullptr;
    const char *array_name = type == 7 ? "tickets" : (type == 8 ? "costumes" : (type == 11 ? "runes" : nullptr));
    if (!array_name) return nullptr;
    cJSON *array = object_item(g_document, array_name);
    if (!array && create) array = cJSON_AddArrayToObject(g_document, array_name);
    if (!cJSON_IsArray(array)) return nullptr;
    char id_text[32];
    std::snprintf(id_text, sizeof(id_text), "%llu", static_cast<unsigned long long>(id));
    for (cJSON *row = array->child; row; row = row->next) {
        cJSON *table_id = object_item(row, "TableId");
        if (cJSON_IsString(table_id) && table_id->valuestring && std::strcmp(table_id->valuestring, id_text) == 0) return row;
    }
    if (!create) return nullptr;
    cJSON *row = cJSON_CreateObject();
    if (!row) return nullptr;
    bool ok = put_string(row, "TableId", id_text);
    if (type == 8) ok = ok && put_bool(row, "IsEquipped", false);
    else {
        ok = ok && put_number(row, "Count", 0);
        if (type == 7) ok = ok && put_string(row, "LastRechargeTime", "0");
    }
    if (!ok || !cJSON_AddItemToArray(array, row)) { cJSON_Delete(row); return nullptr; }
    return row;
}

bool parse_nonnegative_int64(const char *text, int64_t *value) {
    if (!text || !*text || *text == '-') return false;
    errno = 0;
    char *end = nullptr;
    const long long parsed = std::strtoll(text, &end, 10);
    if (errno || end == text || !end || *end != '\0' || parsed < 0) return false;
    *value = static_cast<int64_t>(parsed);
    return true;
}

bool read_counter(cJSON *object, const char *key, uint64_t *value) {
    cJSON *item = object_item(object, key);
    if (cJSON_IsString(item) && item->valuestring) { *value = static_cast<uint64_t>(std::strtoull(item->valuestring, nullptr, 10)); return true; }
    if (cJSON_IsNumber(item) && item->valuedouble >= 0.0 && item->valuedouble < 18446744073709551616.0) { *value = static_cast<uint64_t>(item->valuedouble); return true; }
    *value = 0;
    return true;
}

bool activity_counter_add(cJSON *object, const char *key, uint64_t amount) {
    uint64_t old_value = 0;
    if (!read_counter(object, key, &old_value) || old_value > std::numeric_limits<uint64_t>::max() - amount) { set_error("activity counter overflow"); return false; }
    char text[32];
    std::snprintf(text, sizeof(text), "%llu", static_cast<unsigned long long>(old_value + amount));
    if (!put_string(object, key, text)) { set_error("invalid activity ledger"); return false; }
    return true;
}

bool activity_add(const char *kind, uint64_t id, uint64_t amount) {
    if (!amount) return true;
    if (!g_document || !kind || !*kind) { set_error("invalid activity ledger"); return false; }
    const int64_t period = g_activity_period ? g_activity_period() : 0;
    if (period < 0) { set_error("invalid activity ledger"); return false; }
    cJSON *counters = object_item(g_document, "activity_counters");
    if (!counters) counters = cJSON_AddObjectToObject(g_document, "activity_counters");
    if (!cJSON_IsObject(counters)) { set_error("invalid activity ledger"); return false; }
    cJSON *bucket = object_item(counters, kind);
    if (!bucket) bucket = cJSON_AddObjectToObject(counters, kind);
    if (!cJSON_IsObject(bucket)) { set_error("invalid activity ledger"); return false; }
    cJSON *lifetime = object_item(bucket, "lifetime");
    if (!lifetime) lifetime = cJSON_AddObjectToObject(bucket, "lifetime");
    if (!cJSON_IsObject(lifetime)) { set_error("invalid activity ledger"); return false; }
    cJSON *daily = object_item(bucket, "daily");
    cJSON *period_item = object_item(daily, "Period");
    uint64_t stored_period = 0;
    bool period_valid = false;
    if (cJSON_IsString(period_item) && period_item->valuestring) { stored_period = static_cast<uint64_t>(std::strtoull(period_item->valuestring, nullptr, 10)); period_valid = true; }
    else if (cJSON_IsNumber(period_item) && period_item->valuedouble >= 0 && period_item->valuedouble < 18446744073709551616.0) { stored_period = static_cast<uint64_t>(period_item->valuedouble); period_valid = true; }
    if (!cJSON_IsObject(daily) || !period_valid || stored_period != static_cast<uint64_t>(period)) {
        cJSON *fresh = cJSON_CreateObject();
        if (!fresh) { set_error("invalid activity ledger"); return false; }
        char period_text[32];
        std::snprintf(period_text, sizeof(period_text), "%llu", static_cast<unsigned long long>(period));
        if (!put_string(fresh, "Period", period_text)) { cJSON_Delete(fresh); set_error("invalid activity ledger"); return false; }
        if (cJSON_HasObjectItem(bucket, "daily")) cJSON_ReplaceItemInObjectCaseSensitive(bucket, "daily", fresh);
        else cJSON_AddItemToObject(bucket, "daily", fresh);
        daily = fresh;
    }
    char key[80];
    std::snprintf(key, sizeof(key), "%s:%llu", kind, static_cast<unsigned long long>(id));
    return activity_counter_add(lifetime, key, amount) && activity_counter_add(daily, key, amount);
}

bool deduct_costs(const struct store_cost *costs, unsigned int count) {
    if (count > 64 || (count && !costs)) { set_error("invalid cost transaction"); return false; }
    constexpr unsigned int allowed = (1u << 2) | (1u << 5) | (1u << 7) | (1u << 11);
    for (unsigned int i = 0; i < count; ++i) {
        const store_cost &cost = costs[i];
        if (cost.type < 0 || cost.type > 11 || !(allowed & (1u << cost.type)) || cost.id == 0 || cost.amount < 1) { set_error("insufficient or invalid local cost"); return false; }
        const int64_t balance = store_reward_balance(cost.type, cost.id);
        if (balance < cost.amount) { set_error("insufficient or invalid local cost"); return false; }
        if (cost.type == 7 && cost.id == 7 && !activity_add("ticket", 7, static_cast<uint64_t>(cost.amount))) return false;
        cJSON *entry = (cost.type == 2 || cost.type == 5) ? balance_entry(cost.type, cost.id, false) : counted_entry(cost.type, cost.id, false);
        if (!cJSON_IsObject(entry)) { set_error("insufficient or invalid local cost"); return false; }
        const int64_t remaining = balance - cost.amount;
        if (cost.type == 2) {
            char text[32];
            std::snprintf(text, sizeof(text), "%llu", static_cast<unsigned long long>(remaining));
            if (!put_string(entry, "value", text)) { set_error("insufficient or invalid local cost"); return false; }
        } else {
            const char *field = cost.type == 5 ? "count" : "Count";
            if (!put_number(entry, field, static_cast<double>(remaining))) { set_error("insufficient or invalid local cost"); return false; }
        }
        if (cost.type == 7 && !activity_add("ticket", cost.id, static_cast<uint64_t>(cost.amount))) return false;
    }
    return true;
}

uint64_t activity_value_unlocked(const char *kind, uint64_t id, int daily) {
    if (!g_document || !kind || !*kind) return 0;
    cJSON *counters = object_item(g_document, "activity_counters");
    cJSON *bucket = object_item(counters, kind);
    cJSON *scope = object_item(bucket, daily ? "daily" : "lifetime");
    if (!cJSON_IsObject(scope)) return 0;
    if (daily) {
        const int64_t current_period = g_activity_period ? g_activity_period() : 0;
        if (current_period < 0) return 0;
        cJSON *period_item = object_item(scope, "Period");
        uint64_t stored_period = 0;
        if (cJSON_IsString(period_item) && period_item->valuestring) stored_period = static_cast<uint64_t>(std::strtoull(period_item->valuestring, nullptr, 10));
        else if (cJSON_IsNumber(period_item) && period_item->valuedouble >= 0 && period_item->valuedouble < 18446744073709551616.0) stored_period = static_cast<uint64_t>(period_item->valuedouble);
        else return 0;
        if (stored_period != static_cast<uint64_t>(current_period)) return 0;
    }
    char key[80];
    std::snprintf(key, sizeof(key), "%s:%llu", kind, static_cast<unsigned long long>(id));
    uint64_t value = 0;
    return read_counter(scope, key, &value) ? value : 0;
}

bool apply_account_exp(int64_t bonus) {
    cJSON *account = object_item(g_document, "account");
    cJSON *level_item = object_item(account, "level");
    cJSON *exp_item = object_item(account, "exp");
    if (!cJSON_IsNumber(level_item) || !cJSON_IsNumber(exp_item) || level_item->valueint < 1 || level_item->valueint > 99 || exp_item->valuedouble != static_cast<double>(exp_item->valueint) || exp_item->valueint < 0) { set_error("invalid account experience"); return false; }
    int level = level_item->valueint;
    int64_t exp = static_cast<int64_t>(exp_item->valueint) + bonus;
    cJSON *unlocks = object_item(g_document, "unlocks");
    if (unlocks) {
        char expected_id[32];
        std::snprintf(expected_id, sizeof(expected_id), "%llu", 200ULL);
        for (cJSON *row = unlocks->child; row; row = row->next) {
            cJSON *table_id = object_item(row, "TableId");
            if (!cJSON_IsString(table_id) || !table_id->valuestring || std::strcmp(table_id->valuestring, expected_id) != 0) continue;
            cJSON *value_item = object_item(row, "Value");
            if (!cJSON_IsString(value_item) || !value_item->valuestring) continue;
            errno = 0;
            char *end = nullptr;
            const long long parsed = std::strtoll(value_item->valuestring, &end, 10);
            if (errno || !end || *end != '\0' || parsed < 10) { set_error("invalid account unlock value"); return false; }
        }
    }
    if (exp < 0 || exp > std::numeric_limits<int>::max()) { set_error("account experience overflow"); return false; }
    while (level < 99 && exp >= kLevelExp[level - 1]) { exp -= kLevelExp[level - 1]; ++level; }
    if (!put_number(account, "level", level) || !put_number(account, "exp", static_cast<double>(exp))) { set_error("invalid account experience"); return false; }

    return true;
}

struct RuneStockRule { uint64_t id; int32_t limit; };
#include "rune_stock_rules.inc"

const RuneStockRule *rune_stock_rule(uint64_t id) {
    for (const auto &rule : kRuneStockRules) if (rule.id == id) return &rule;
    return nullptr;
}

bool add_string_number(cJSON *object, const char *key, uint64_t value) {
    char text[32];
    std::snprintf(text, sizeof(text), "%llu", static_cast<unsigned long long>(value));
    return put_string(object, key, text);
}

bool grant_reward_inner(const store_reward &reward) {
    if (!g_document || !reward.id || reward.amount < 1) { set_error("invalid reward"); return false; }
    const int type = reward.type;
    if (type == 3) return apply_account_exp(reward.amount);
    if (type == 10) return reward.id == 1 && apply_account_exp(reward.amount);
    if (type == 1 || type == 4) {
        if (reward.amount != 1 || store_reward_balance(type, reward.id) != 0) { set_error("duplicate or invalid unique reward"); return false; }
        const char *array_name = type == 1 ? "characters" : "vignettes";
        cJSON *array = object_item(g_document, array_name);
        if (!cJSON_IsArray(array)) { set_error("invalid unique reward collection"); return false; }
        const unsigned int array_size = static_cast<unsigned int>(cJSON_GetArraySize(array));
        if (array_size >= 9999) { set_error("unique reward inventory limit exceeded"); return false; }
        cJSON *row = cJSON_CreateObject();
        if (!row || !add_string_number(row, "TableId", reward.id) || !add_string_number(row, "ServerId", static_cast<uint64_t>(array_size) + 1)) { cJSON_Delete(row); set_error("create unique reward"); return false; }
        bool ok = true;
        if (type == 4) ok = put_number(row, "Star", 0);
        else {
            ok = put_number(row, "Level", 1) && put_number(row, "Exp", 0) && put_number(row, "Star", 0) && put_bool(row, "Locked", false) && put_bool(row, "Signed", false);
            cJSON *buffs = ok ? cJSON_AddArrayToObject(row, "Buffs") : nullptr;
            cJSON *runes = ok ? cJSON_AddArrayToObject(row, "Runes") : nullptr;
            cJSON *skills = ok ? cJSON_AddArrayToObject(row, "Skills") : nullptr;
            ok = ok && buffs && runes && skills;
            for (int i = 0; ok && i < 8; ++i) {
                cJSON *skill = cJSON_CreateObject();
                ok = skill && put_number(skill, "Level", 1) && cJSON_AddItemToArray(skills, skill);
                if (!ok) cJSON_Delete(skill);
            }
        }
        if (!ok || !cJSON_AddItemToArray(array, row)) { cJSON_Delete(row); set_error("create unique reward"); return false; }
        return true;
    }
    if (type == 2 || type == 5) {
        if (type == 2 && reward.id >= 8) { set_error("invalid currency reward"); return false; }
        const int64_t balance = store_reward_balance(type, reward.id);
        if (balance < 0 || balance > std::numeric_limits<int64_t>::max() - reward.amount) { set_error("invalid or overflowing reward balance"); return false; }
        if (type == 5 && balance > std::numeric_limits<int>::max() - reward.amount) { set_error("inventory reward overflow"); return false; }
        cJSON *entry = balance_entry(type, reward.id, true);
        if (!cJSON_IsObject(entry)) { set_error("invalid reward balance entry"); return false; }
        if (type == 2) return add_string_number(entry, "value", static_cast<uint64_t>(balance + reward.amount));
        return put_number(entry, "count", static_cast<double>(balance + reward.amount));
    }
    if (type == 7 || type == 8 || type == 11) {
        if (type == 11) {
            const RuneStockRule *rule = rune_stock_rule(reward.id);
            if (!rule || rule->limit < 2) { set_error("unknown rune or original stock maximum exceeded"); return false; }
        }
        const int64_t balance = store_reward_balance(type, reward.id);
        if (balance < 0 || balance > std::numeric_limits<int>::max() - reward.amount) { set_error("invalid or overflowing counted reward"); return false; }
        if (type == 11) {
            const RuneStockRule *rule = rune_stock_rule(reward.id);
            if (!rule || balance + reward.amount > rule->limit) { set_error("rune stock limit exceeded"); return false; }
        }
        cJSON *entry = counted_entry(type, reward.id, true);
        if (!cJSON_IsObject(entry)) { set_error("invalid counted reward entry"); return false; }
        if (type == 8) return true;
        return put_number(entry, "Count", static_cast<double>(balance + reward.amount));
    }
    set_error("unsupported reward type");
    return false;
}

bool grant_reward(const store_reward &reward) {
    if (!grant_reward_inner(reward)) return false;
    if (reward.type == 2) return activity_add("currency", reward.id, static_cast<uint64_t>(reward.amount));
    return true;
}

bool farming_activity(const cJSON *incoming) {
    cJSON *current_farming = object_item(g_document, "farming_state");
    cJSON *current_active = object_item(current_farming, "active");
    cJSON *incoming_last = object_item(const_cast<cJSON *>(incoming), "last_result");
    if (!current_active || cJSON_HasObjectItem(const_cast<cJSON *>(incoming), "active")) return true;
    cJSON *result = object_item(incoming_last, "result");
    if (!cJSON_IsNumber(result) || result->valueint != 1) return true;
    cJSON *count = object_item(current_active, "count");
    if (!cJSON_IsNumber(count)) return true;
    if (count->valueint < 1 || count->valueint > 99) return false;
    return activity_add("quest", 0, static_cast<uint64_t>(count->valueint));
}

bool commit_or_restore(cJSON *snapshot) {
    if (!snapshot) { set_error("missing transaction snapshot"); return false; }
    if (store_commit()) { cJSON_Delete(snapshot); return true; }
    cJSON_Delete(g_document);
    g_document = snapshot;
    return false;
}

bool parse_id_string(cJSON *object, const char *key, uint64_t value) {
    cJSON *item = object_item(object, key);
    if (!cJSON_IsString(item) || !item->valuestring) return false;
    char expected[32];
    std::snprintf(expected, sizeof(expected), "%llu", static_cast<unsigned long long>(value));
    return std::strcmp(item->valuestring, expected) == 0;
}

bool int_matches(cJSON *object, const char *key, int value) {
    cJSON *item = object_item(object, key);
    return cJSON_IsNumber(item) && item->valueint == value;
}

struct GiftAttachment { int type; uint64_t table_id; uint32_t count; };
#include "prologue_gift_tables.inc"

bool queue_gift_uncommitted(const char *flag, uint64_t server_id, const char *title, const char *comment, const GiftAttachment *attachments, size_t attachment_count) {
    if (!g_document || !flag || !*flag || !title || !comment || !attachments) { set_error("invalid queued gift arguments"); return false; }
    if (cJSON_IsTrue(object_item(g_document, flag))) return true;
    cJSON *pack = store_pack(1000);
    cJSON *story_index = object_item(pack, "story_index");
    if (!cJSON_IsNumber(story_index) || story_index->valueint < 9) return true;
    if (attachment_count < 1 || attachment_count > 512) { set_error("invalid gift size"); return false; }
    cJSON *mails = object_item(g_document, "mails");
    if (!mails) mails = cJSON_AddArrayToObject(g_document, "mails");
    if (!cJSON_IsArray(mails)) { set_error("invalid mail collection"); return false; }
    char id_text[32];
    std::snprintf(id_text, sizeof(id_text), "%llu", static_cast<unsigned long long>(server_id));
    for (cJSON *mail = mails->child; mail; mail = mail->next) {
        cJSON *existing_id = object_item(mail, "ServerId");
        if (cJSON_IsString(existing_id) && existing_id->valuestring && std::strcmp(existing_id->valuestring, id_text) == 0) { set_error("gift ID collision"); return false; }
    }
    cJSON *mail = cJSON_CreateObject();
    cJSON *attach_infos = cJSON_CreateArray();
    bool ok = mail && attach_infos && put_string(mail, "ServerId", id_text) && put_number(mail, "ParcelType", 2) && put_number(mail, "Icon", 0) && put_number(mail, "TextType", 1) && put_string(mail, "Title", title) && put_string(mail, "Comment", comment) && put_string(mail, "Sender", "离线版") && put_number(mail, "Receipt", 0) && put_string(mail, "ExpireTime", "3155378975999999999");
    for (size_t i = 0; ok && i < attachment_count; ++i) {
        char table_id[32], count[32];
        std::snprintf(table_id, sizeof(table_id), "%llu", static_cast<unsigned long long>(attachments[i].table_id));
        std::snprintf(count, sizeof(count), "%u", attachments[i].count);
        cJSON *entry = cJSON_CreateObject();
        ok = entry && put_number(entry, "Type", attachments[i].type) && put_string(entry, "TableId", table_id) && put_string(entry, "Count", count) && cJSON_AddItemToArray(attach_infos, entry);
        if (!ok) cJSON_Delete(entry);
    }
    if (ok) { ok = cJSON_AddItemToObject(mail, "AttachInfos", attach_infos) != 0; if (ok) attach_infos = nullptr; }
    if (ok) { ok = cJSON_AddItemToArray(mails, mail) != 0; if (ok) mail = nullptr; }
    if (ok) ok = put_bool(g_document, flag, true);
    cJSON_Delete(attach_infos);
    cJSON_Delete(mail);
    if (!ok) { set_error("create queued gift mail"); return false; }
    return true;
}

bool queue_gift_atomic(const char *flag, uint64_t server_id, const char *title, const char *comment, const GiftAttachment *attachments, size_t attachment_count) {
    if (!g_document) { set_error("store not open"); return false; }
    if (cJSON_IsTrue(object_item(g_document, flag))) return true;
    cJSON *snapshot = cJSON_Duplicate(g_document, 1);
    if (!snapshot) { set_error("snapshot queued gift"); return false; }
    if (!queue_gift_uncommitted(flag, server_id, title, comment, attachments, attachment_count)) { cJSON_Delete(g_document); g_document = snapshot; return false; }
    return commit_or_restore(snapshot);
}

bool migrate_rune_tier_mail_comments() {
    cJSON *mails = object_item(g_document, "mails");
    if (!cJSON_IsArray(mails)) return true;
    static const char *ids[] = {"900014", "900015", "900016"};
    static const char *comments[] = {
        "T1符石按三角形、四邊形、五邊形、六邊形、八邊形分箱，每箱7種。各箱99999個。",
        "T2符石按三角形、四邊形、五邊形、六邊形、八邊形分箱，每箱20至22種。各箱99999個。",
        "T3符石按三角形、四邊形、五邊形、六邊形、八邊形、十字星分箱，每箱7至32種。各箱99999個。"
    };
    cJSON *snapshot = nullptr;
    for (cJSON *mail = mails->child; mail; mail = mail->next) {
        cJSON *server_id = object_item(mail, "ServerId");
        if (!cJSON_IsString(server_id) || !server_id->valuestring) continue;
        for (size_t i = 0; i < 3; ++i) if (std::strcmp(server_id->valuestring, ids[i]) == 0) {
            cJSON *current = object_item(mail, "Comment");
            if (cJSON_IsString(current) && current->valuestring && std::strcmp(current->valuestring, comments[i]) == 0) break;
            if (!snapshot) snapshot = cJSON_Duplicate(g_document, 1);
            if (!snapshot) { set_error("snapshot rune gift text migration"); return false; }
            if (!put_string(mail, "Comment", comments[i])) { cJSON_Delete(g_document); g_document = snapshot; set_error("update rune gift text"); return false; }
            break;
        }
    }
    if (!snapshot) return true;
    return commit_or_restore(snapshot);
}

bool restore_prologue_vignette_once() {
    if (!cJSON_IsTrue(object_item(g_document, "facility_gift_retired_v021"))) return true;
    cJSON *pack = store_pack(1000);
    cJSON *story_index = object_item(pack, "story_index");
    if (!cJSON_IsNumber(story_index) || story_index->valueint < 9 || cJSON_IsTrue(object_item(g_document, "prologue_vignette_30026_restored"))) return true;
    cJSON *snapshot = cJSON_Duplicate(g_document, 1);
    if (!snapshot) { set_error("snapshot restored prologue vignette"); return false; }
    if (store_reward_balance(4, 30026) == 0) {
        const store_reward reward{4, 30026, 1};
        if (!grant_reward_inner(reward)) { cJSON_Delete(g_document); g_document = snapshot; return false; }
    }
    if (!put_bool(g_document, "prologue_vignette_30026_restored", true)) { cJSON_Delete(g_document); g_document = snapshot; set_error("mark restored prologue vignette"); return false; }
    return commit_or_restore(snapshot);
}

static constexpr GiftAttachment kDrawTicketGift[] = {
    {5, 2ULL, 99999U},
    {5, 3ULL, 99999U},
    {5, 4ULL, 99999U},
};
cJSON *find_mail_by_server_id(uint64_t server_id) {
    cJSON *mails = object_item(g_document, "mails");
    if (!cJSON_IsArray(mails)) return nullptr;
    char id_text[32];
    std::snprintf(id_text, sizeof(id_text), "%llu", static_cast<unsigned long long>(server_id));
    for (cJSON *mail = mails->child; mail; mail = mail->next) {
        cJSON *id = object_item(mail, "ServerId");
        if (cJSON_IsString(id) && id->valuestring && std::strcmp(id->valuestring, id_text) == 0) return mail;
    }
    return nullptr;
}

bool set_mail_comment(cJSON *mail, const char *comment) {
    if (!mail || !comment) return true;
    cJSON *current = object_item(mail, "Comment");
    if (cJSON_IsString(current) && current->valuestring && std::strcmp(current->valuestring, comment) == 0) return true;
    return put_string(mail, "Comment", comment);
}

bool gift_expand_one(cJSON *mail, const GiftAttachment *definitions, size_t definition_count, uint32_t missing_mail_count, std::vector<GiftAttachment> *compensation) {
    if (!definitions || !compensation || definition_count > 512) { set_error("invalid gift expansion arguments"); return false; }
    compensation->clear();
    if (!mail) {
        if (!missing_mail_count) return true;
        for (size_t i = 0; i < definition_count; ++i) compensation->push_back({definitions[i].type, definitions[i].table_id, missing_mail_count});
        return true;
    }
    cJSON *attachments = object_item(mail, "AttachInfos");
    cJSON *receipt = object_item(mail, "Receipt");
    if (!cJSON_IsArray(attachments) || !cJSON_IsNumber(receipt) || receipt->valueint < 0) { set_error("invalid historical gift mail"); return false; }
    const bool already_claimed = receipt->valueint > 1;
    for (size_t i = 0; i < definition_count; ++i) {
        const GiftAttachment &definition = definitions[i];
        char id_text[32], target_text[32];
        std::snprintf(id_text, sizeof(id_text), "%llu", static_cast<unsigned long long>(definition.table_id));
        std::snprintf(target_text, sizeof(target_text), "%u", definition.count);
        cJSON *matching = nullptr;
        for (cJSON *row = attachments->child; row; row = row->next) {
            cJSON *type = object_item(row, "Type");
            cJSON *table_id = object_item(row, "TableId");
            if (cJSON_IsNumber(type) && type->valueint == definition.type && cJSON_IsString(table_id) && table_id->valuestring && std::strcmp(table_id->valuestring, id_text) == 0) { matching = row; break; }
        }
        uint64_t current_count = 0;
        if (matching) {
            cJSON *count = object_item(matching, "Count");
            int64_t parsed = 0;
            if (!cJSON_IsString(count) || !parse_nonnegative_int64(count->valuestring, &parsed)) { set_error("invalid historical gift attachment count"); return false; }
            current_count = static_cast<uint64_t>(parsed);
        }
        if (current_count > definition.count) { set_error("historical gift exceeds current attachment target"); return false; }
        if (already_claimed && current_count < definition.count) compensation->push_back({definition.type, definition.table_id, static_cast<uint32_t>(definition.count - current_count)});
        if (current_count == definition.count) continue;
        if (matching) {
            if (!put_string(matching, "Count", target_text)) { set_error("update historical gift attachment"); return false; }
        } else {
            cJSON *row = cJSON_CreateObject();
            bool ok = row && put_number(row, "Type", definition.type) && put_string(row, "TableId", id_text) && put_string(row, "Count", target_text) && cJSON_AddItemToArray(attachments, row);
            if (!ok) { cJSON_Delete(row); set_error("append historical gift attachment"); return false; }
        }
    }
    return true;
}

bool expanded_gift_v3() {
    if (cJSON_IsTrue(object_item(g_document, "expanded_gift_v3"))) return true;
    if (!cJSON_IsTrue(object_item(g_document, "growth_gift_queued"))) return true;
    cJSON *snapshot = cJSON_Duplicate(g_document, 1);
    if (!snapshot) { set_error("snapshot expanded gift migration"); return false; }
    cJSON *growth_mail = find_mail_by_server_id(900002);
    cJSON *custom_mail = find_mail_by_server_id(900003);
    std::vector<GiftAttachment> draw_compensation, card_compensation;
    if (!gift_expand_one(growth_mail, kDrawTicketGift, sizeof(kDrawTicketGift) / sizeof(kDrawTicketGift[0]), 9999, &draw_compensation) ||
        !gift_expand_one(custom_mail, kCharacterCardsGift, sizeof(kCharacterCardsGift) / sizeof(kCharacterCardsGift[0]), 0, &card_compensation)) {
        cJSON_Delete(g_document); g_document = snapshot; return false;
    }
    if (!draw_compensation.empty() && !queue_gift_uncommitted("draw_gift_compensation_v3", 900007, "扭蛋補給追加", "三種扭蛋道具補足至各99999；只發更新前已領取的差額。", draw_compensation.data(), draw_compensation.size())) {
        cJSON_Delete(g_document); g_document = snapshot; return false;
    }
    if (!card_compensation.empty() && !queue_gift_uncommitted("bond_card_compensation_v3", 900008, "全角色羈絆卡片補給追加", "每位角色魔法師卡牌補足至999999；不贈送奧爾晶球，每個存檔一次。", card_compensation.data(), card_compensation.size())) {
        cJSON_Delete(g_document); g_document = snapshot; return false;
    }
    if (!set_mail_comment(growth_mail, "流星石99999，精選扭蛋幣、扭蛋幣、星光底片各99999。已領取舊版的差額另發郵件。") ||
        !set_mail_comment(custom_mail, "魔法銀幣99999900、赫卡忒的火種99999999、魔法師EXP999999、每位角色魔法師卡牌999999、通用養成素材與體力藥。不贈送奧爾晶球。") ||
        !put_bool(g_document, "expanded_gift_v3", true)) {
        cJSON_Delete(g_document); g_document = snapshot; set_error("complete expanded gift migration"); return false;
    }
    return commit_or_restore(snapshot);
}

bool expanded_character_exp_gift_v5() {
    if (cJSON_IsTrue(object_item(g_document, "character_exp_gift_v5"))) return true;
    if (!cJSON_IsTrue(object_item(g_document, "growth_gift_queued"))) return true;
    cJSON *snapshot = cJSON_Duplicate(g_document, 1);
    if (!snapshot) { set_error("snapshot character exp gift migration"); return false; }
    cJSON *growth_mail = find_mail_by_server_id(900002);
    std::vector<GiftAttachment> compensation;
    if (!gift_expand_one(growth_mail, kCharacterExpGift, sizeof(kCharacterExpGift) / sizeof(kCharacterExpGift[0]), 999999, &compensation)) {
        cJSON_Delete(g_document); g_document = snapshot; return false;
    }
    if (!compensation.empty() && !queue_gift_uncommitted("character_exp_compensation_v5", 900010, "魔法師EXP追加補給", "角色養成郵件的魔法師EXP提高至999999000；此郵件只補已領取舊版的差額，每個帳號一次。", compensation.data(), compensation.size())) {
        cJSON_Delete(g_document); g_document = snapshot; return false;
    }
    if (!set_mail_comment(growth_mail, "魔法銀幣99999900、赫卡忒的火種99999999、魔法師EXP999999000、魔法師卡牌999999、通用養成素材與體力藥。不贈送奧爾晶球。") ||
        !put_bool(g_document, "character_exp_gift_v5", true)) {
        cJSON_Delete(g_document); g_document = snapshot; set_error("complete character exp gift migration"); return false;
    }
    return commit_or_restore(snapshot);
}

static constexpr GiftAttachment kRuneTier1Gift[] = {
    {5, 900010111ULL, 0U}, {5, 900010112ULL, 0U}, {5, 900010113ULL, 0U}, {5, 900010114ULL, 0U}, {5, 900010115ULL, 0U},
};
static constexpr GiftAttachment kRuneTier2Gift[] = {
    {5, 900010121ULL, 0U}, {5, 900010122ULL, 0U}, {5, 900010123ULL, 0U}, {5, 900010124ULL, 0U}, {5, 900010125ULL, 0U},
};
static constexpr GiftAttachment kRuneTier3Gift[] = {
    {5, 900010131ULL, 0U}, {5, 900010132ULL, 0U}, {5, 900010133ULL, 0U}, {5, 900010134ULL, 0U}, {5, 900010135ULL, 0U}, {5, 900010136ULL, 0U},
};

bool migrate_rune_tier_gifts() {
    if (!cJSON_IsTrue(object_item(g_document, "facility_gift_retired_v021")) || cJSON_IsTrue(object_item(g_document, "rune_tier_gifts_v021"))) return true;
    cJSON *pack = store_pack(1000);
    cJSON *story_index = object_item(pack, "story_index");
    if (!cJSON_IsNumber(story_index) || story_index->valueint < 9) return true;
    const int64_t old_remaining = store_reward_balance(5, 900010005ULL);
    if (old_remaining < 0) { set_error("invalid retired rune parcel balance"); return false; }
    const uint32_t new_each_category = static_cast<uint32_t>(std::min<int64_t>(old_remaining, 99999));
    cJSON *snapshot = cJSON_Duplicate(g_document, 1);
    if (!snapshot) { set_error("snapshot rune tier migration"); return false; }
    GiftAttachment tier1[sizeof(kRuneTier1Gift) / sizeof(kRuneTier1Gift[0])];
    GiftAttachment tier2[sizeof(kRuneTier2Gift) / sizeof(kRuneTier2Gift[0])];
    GiftAttachment tier3[sizeof(kRuneTier3Gift) / sizeof(kRuneTier3Gift[0])];
    for (size_t i = 0; i < sizeof(tier1) / sizeof(tier1[0]); ++i) { tier1[i] = kRuneTier1Gift[i]; tier1[i].count = new_each_category; }
    for (size_t i = 0; i < sizeof(tier2) / sizeof(tier2[0]); ++i) { tier2[i] = kRuneTier2Gift[i]; tier2[i].count = new_each_category; }
    for (size_t i = 0; i < sizeof(tier3) / sizeof(tier3[0]); ++i) { tier3[i] = kRuneTier3Gift[i]; tier3[i].count = new_each_category; }
    if (!queue_gift_uncommitted("rune_tier1_gift_v021", 900014, "T1符石自選箱補給", "T1符石按三角形、四邊形、五邊形、六邊形、八邊形分箱，每箱7種。各箱99999個。", tier1, sizeof(tier1) / sizeof(tier1[0])) ||
        !queue_gift_uncommitted("rune_tier2_gift_v021", 900015, "T2符石自選箱補給", "T2符石按三角形、四邊形、五邊形、六邊形、八邊形分箱，每箱20至22種。各箱99999個。", tier2, sizeof(tier2) / sizeof(tier2[0])) ||
        !queue_gift_uncommitted("rune_tier3_gift_v021", 900016, "T3符石自選箱補給", "T3符石按三角形、四邊形、五邊形、六邊形、八邊形、十字星分箱，每箱7至32種。各箱99999個。", tier3, sizeof(tier3) / sizeof(tier3[0]))) {
        cJSON_Delete(g_document); g_document = snapshot; return false;
    }
    cJSON *inventory = object_item(g_document, "inventory");
    if (inventory && !cJSON_IsArray(inventory)) { cJSON_Delete(g_document); g_document = snapshot; set_error("invalid inventory in rune tier migration"); return false; }
    if (cJSON_IsArray(inventory)) {
        for (int i = cJSON_GetArraySize(inventory) - 1; i >= 0; --i) {
            cJSON *row = cJSON_GetArrayItem(inventory, i);
            cJSON *type = object_item(row, "type");
            cJSON *table_id = object_item(row, "table_id");
            if (cJSON_IsNumber(type) && type->valueint == 5 && cJSON_IsString(table_id) && table_id->valuestring && std::strcmp(table_id->valuestring, "900010005") == 0) cJSON_DeleteItemFromArray(inventory, i);
        }
    }
    cJSON *receipt = cJSON_CreateObject();
    bool ok = receipt && put_number(receipt, "old_remaining", static_cast<double>(old_remaining)) && put_number(receipt, "new_each_category", new_each_category) && put_item(g_document, "rune_tier_migration_receipt", receipt);
    if (ok) receipt = nullptr;
    if (ok) ok = put_bool(g_document, "rune_selection_gift_v02", true) && put_bool(g_document, "rune_tier_gifts_v021", true);
    cJSON_Delete(receipt);
    if (!ok) { cJSON_Delete(g_document); g_document = snapshot; set_error("write rune tier migration receipt"); return false; }
    return commit_or_restore(snapshot);
}

}

extern "C" {
void astrae_store_set_pending_error(const char *message) { std::snprintf(g_error, sizeof(g_error), "%s", message ? message : "store function pending"); }
const char *store_error(void) { return g_error; }
cJSON *store_document(void) { std::lock_guard<std::recursive_mutex> lock(g_store_mutex); return g_document; }
uint64_t store_content_bits(void) {
    std::lock_guard<std::recursive_mutex> lock(g_store_mutex);
    cJSON *tutorial = object_item(g_document, "tutorial");
    cJSON *bits = object_item(tutorial, "content_bits");
    if (!cJSON_IsString(bits) || !bits->valuestring) return 0;
    return static_cast<uint64_t>(std::strtoull(bits->valuestring, nullptr, 10));
}
int store_initial_index(void) {
    std::lock_guard<std::recursive_mutex> lock(g_store_mutex);
    cJSON *tutorial = object_item(g_document, "tutorial");
    cJSON *index = object_item(tutorial, "initial_index");
    return cJSON_IsNumber(index) ? index->valueint : -1;
}
int store_commit(void) {
    std::lock_guard<std::recursive_mutex> lock(g_store_mutex);
    if (!g_document) { set_error("save not loaded"); return 0; }
    char *serialized = cJSON_PrintUnformatted(g_document);
    if (!serialized) { set_error("serialize"); return 0; }
    FILE *file = std::fopen(g_temp_path, "wb");
    if (!file) { std::free(serialized); set_error("open temporary"); return 0; }
    const size_t length = std::strlen(serialized);
    const size_t written = std::fwrite(serialized, 1, length, file);
    std::free(serialized);
    if (written != length || std::fflush(file) != 0) {
        std::fclose(file); std::remove(g_temp_path); set_error("flush temporary"); return 0;
    }
    const int fd = fileno(file);
    const int sync_result = fd >= 0 ? fsync(fd) : -1;
    const int close_result = std::fclose(file);
    if (sync_result != 0 || close_result != 0) { std::remove(g_temp_path); set_error("flush temporary"); return 0; }
    int temp_exists = 0;
    cJSON *verified = read_document(g_temp_path, &temp_exists);
    if (!verified) { std::remove(g_temp_path); set_error("serialize"); return 0; }
    cJSON_Delete(verified);
    const bool had_primary = access(g_save_path, F_OK) == 0;
    if (had_primary) {
        std::remove(g_backup_path);
        if (std::rename(g_save_path, g_backup_path) != 0) { std::remove(g_temp_path); set_error("backup previous"); return 0; }
    } else if (errno != ENOENT) {
        std::remove(g_temp_path); set_error("inspect previous save"); return 0;
    }
    if (std::rename(g_temp_path, g_save_path) != 0) {
        if (had_primary) (void)std::rename(g_backup_path, g_save_path);
        std::remove(g_temp_path); set_error("replace primary"); return 0;
    }
    return 1;
}
int store_open(const char *directory) {
    std::lock_guard<std::recursive_mutex> lock(g_store_mutex);
    if (g_document) { cJSON_Delete(g_document); g_document = nullptr; }
    if (!directory || !*directory) { errno = EINVAL; set_error("create save directory"); return 0; }
    if (mkdir(directory, 0700) != 0 && errno != EEXIST) { set_error("create save directory"); return 0; }
    std::snprintf(g_save_path, sizeof(g_save_path), "%s/save.json", directory);
    std::snprintf(g_backup_path, sizeof(g_backup_path), "%s.bak", g_save_path);
    std::snprintf(g_temp_path, sizeof(g_temp_path), "%s.tmp", g_save_path);
    int primary_exists = 0, backup_exists = 0;
    g_document = read_document(g_save_path, &primary_exists);
    if (!g_document) g_document = read_document(g_backup_path, &backup_exists);
    if (!g_document) {
        if (primary_exists || backup_exists) { set_error("existing save invalid; retained for recovery"); return 0; }
        g_document = make_default_document();
        if (!g_document) { set_error("create default save"); return 0; }
        return store_commit();
    }
    cJSON *schema = object_item(g_document, "schema_version");
    cJSON *tutorial = object_item(g_document, "tutorial");
    cJSON *content_bits = object_item(tutorial, "content_bits");
    cJSON *initial_index = object_item(tutorial, "initial_index");
    if (!cJSON_IsNumber(schema) || schema->valueint != 1 || !cJSON_IsObject(tutorial) || !cJSON_IsString(content_bits) || !content_bits->valuestring || !cJSON_IsNumber(initial_index)) {
        set_error("unsupported save schema; retained"); return 0;
    }
    uint64_t bits = 0;
    if (!valid_unsigned_decimal(content_bits->valuestring, &bits) || initial_index->valueint < 0 || initial_index->valueint >= 128) {
        set_error("invalid tutorial values; retained"); return 0;
    }
    (void)bits;
    cJSON *direct_index = object_item(g_document, "substory_direct_index");
    if (cJSON_IsTrue(direct_index)) return 1;
    cJSON *snapshot = cJSON_Duplicate(g_document, 1);
    if (!snapshot) { set_error("substory migration snapshot"); return 0; }
    cJSON *packs = object_item(g_document, "packs");
    if (packs) {
        for (cJSON *pack = packs->child; pack; pack = pack->next) {
            cJSON *substory_bits = object_item(pack, "substory_bits");
            if (!cJSON_IsString(substory_bits) || !substory_bits->valuestring) continue;
            const uint64_t old_bits = static_cast<uint64_t>(std::strtoull(substory_bits->valuestring, nullptr, 10));
            if ((old_bits & (uint64_t(1) << 63)) != 0) {
                cJSON_Delete(g_document); g_document = snapshot; set_error("substory migration overflow; retained"); return 0;
            }
            char value[32];
            std::snprintf(value, sizeof(value), "%llu", static_cast<unsigned long long>(old_bits << 1));
            if (!put_string(pack, "substory_bits", value)) { cJSON_Delete(g_document); g_document = snapshot; set_error("substory migration snapshot"); return 0; }
        }
    }
    if (!put_bool(g_document, "substory_direct_index", true)) { cJSON_Delete(g_document); g_document = snapshot; set_error("substory migration snapshot"); return 0; }
    if (!store_commit()) { cJSON_Delete(g_document); g_document = snapshot; return 0; }
    cJSON_Delete(snapshot);
    return 1;
}
int store_mark_content(unsigned int bit) {
    std::lock_guard<std::recursive_mutex> lock(g_store_mutex);
    if (bit > 63 || !g_document) { set_error("invalid content bit"); return 0; }
    cJSON *tutorial = object_item(g_document, "tutorial");
    cJSON *bits_item = object_item(tutorial, "content_bits");
    const uint64_t old_bits = cJSON_IsString(bits_item) && bits_item->valuestring ? static_cast<uint64_t>(std::strtoull(bits_item->valuestring, nullptr, 10)) : 0;
    const uint64_t new_bits = old_bits | (uint64_t(1) << bit);
    char old_text[32], new_text[32];
    std::snprintf(old_text, sizeof(old_text), "%llu", static_cast<unsigned long long>(old_bits));
    std::snprintf(new_text, sizeof(new_text), "%llu", static_cast<unsigned long long>(new_bits));
    if (!cJSON_IsObject(tutorial)) { set_error("invalid content bit"); return 0; }
    if (!put_string(tutorial, "content_bits", new_text)) { set_error("invalid content bit"); return 0; }
    if (store_commit()) return 1;
    put_string(tutorial, "content_bits", old_text);
    return 0;
}
int store_tutorial_progress(int group) {
    std::lock_guard<std::recursive_mutex> lock(g_store_mutex);
    if (group < 0 || group > 127 || !g_document) { set_error("invalid tutorial group"); return 0; }
    cJSON *tutorial = object_item(g_document, "tutorial");
    cJSON *index = object_item(tutorial, "initial_index");
    const int old_index = cJSON_IsNumber(index) ? index->valueint : 0;
    if (group < old_index) return 1;
    if (!cJSON_IsObject(tutorial) || !put_number(tutorial, "initial_index", group)) { set_error("invalid tutorial group"); return 0; }
    if (store_commit()) return 1;
    put_number(tutorial, "initial_index", old_index);
    return 0;
}
cJSON *store_pack(uint64_t group) {
    std::lock_guard<std::recursive_mutex> lock(g_store_mutex);
    if (!g_document) return nullptr;
    cJSON *packs = object_item(g_document, "packs");
    if (!packs) packs = cJSON_AddObjectToObject(g_document, "packs");
    if (!cJSON_IsObject(packs)) return nullptr;
    char key[32];
    std::snprintf(key, sizeof(key), "%llu", static_cast<unsigned long long>(group));
    cJSON *pack = object_item(packs, key);
    if (pack) {
        if (!cJSON_IsObject(pack)) return nullptr;
        if (group == 1000) {
            cJSON *story_index = object_item(pack, "story_index");
            cJSON *open_type = object_item(pack, "open_type");
            if (cJSON_IsNumber(story_index) && story_index->valueint >= 9 && (!cJSON_IsNumber(open_type) || open_type->valueint != 3)) {
                cJSON *previous = cJSON_DetachItemFromObjectCaseSensitive(pack, "open_type");
                if (!put_number(pack, "open_type", 3)) { if (previous) cJSON_AddItemToObject(pack, "open_type", previous); return nullptr; }
                if (!store_commit()) {
                    cJSON_DeleteItemFromObjectCaseSensitive(pack, "open_type");
                    if (previous) cJSON_AddItemToObject(pack, "open_type", previous);
                    return nullptr;
                }
                cJSON_Delete(previous);
            }
        }
        return pack;
    }
    pack = cJSON_AddObjectToObject(packs, key);
    if (!pack) return nullptr;
    bool ok = put_string(pack, "group_id", key) && put_number(pack, "open_type", 1) && put_number(pack, "story_index", 0) && put_number(pack, "repeat_index", 0) && put_number(pack, "max_difficulty", 0) && put_string(pack, "normal_bits", "0") && put_string(pack, "hard_bits", "0") && put_string(pack, "very_hard_bits", "0") && put_string(pack, "substory_bits", "0") && put_string(pack, "challenge_bits", "0");
    if (!ok || !store_commit()) { cJSON_DeleteItemFromObjectCaseSensitive(packs, key); return nullptr; }
    return pack;
}
uint64_t store_background(void) {
    std::lock_guard<std::recursive_mutex> lock(g_store_mutex);
    cJSON *item = object_item(g_document, "lobby_background_id");
    if (!cJSON_IsString(item) || !item->valuestring) return 30001;
    return static_cast<uint64_t>(std::strtoull(item->valuestring, nullptr, 10));
}
int store_set_background(uint64_t id) {
    std::lock_guard<std::recursive_mutex> lock(g_store_mutex);
    if (!id || !g_document) { set_error("invalid lobby background id"); return 0; }
    cJSON *old = object_item(g_document, "lobby_background_id");
    if (cJSON_IsString(old) && old->valuestring) {
        if (static_cast<uint64_t>(std::strtoull(old->valuestring, nullptr, 10)) == id) return 1;
    } else {
        if (id != 30001) { set_error("invalid lobby background id"); return 0; }
        if (old) return 1;
    }
    cJSON *previous = cJSON_DetachItemFromObjectCaseSensitive(g_document, "lobby_background_id");
    char text[32];
    std::snprintf(text, sizeof(text), "%llu", static_cast<unsigned long long>(id));
    if (!put_string(g_document, "lobby_background_id", text)) {
        if (previous) cJSON_AddItemToObject(g_document, "lobby_background_id", previous);
        set_error("invalid lobby background id"); return 0;
    }
    if (store_commit()) { cJSON_Delete(previous); return 1; }
    cJSON_DeleteItemFromObjectCaseSensitive(g_document, "lobby_background_id");
    if (previous) cJSON_AddItemToObject(g_document, "lobby_background_id", previous);
    return 0;
}
int64_t store_reward_balance(int type, uint64_t id) {
    std::lock_guard<std::recursive_mutex> lock(g_store_mutex);
    if (type == 10 && id == 1) {
        cJSON *account = object_item(g_document, "account");
        cJSON *exp = object_item(account, "exp");
        return cJSON_IsNumber(exp) && exp->valueint >= 0 ? exp->valueint : -1;
    }
    if (type < 0 || type > 11) return -1;
    if (type == 7 || type == 8 || type == 11) {
        cJSON *entry = counted_entry(type, id, false);
        if (type == 8) return entry ? 1 : 0;
        if (!entry) return 0;
        cJSON *count = object_item(entry, "Count");
        return cJSON_IsNumber(count) && count->valuedouble == static_cast<double>(count->valueint) && count->valueint >= 0 ? count->valueint : -1;
    }
    if (type == 1 || type == 4) {
        const char *array_name = type == 1 ? "characters" : "vignettes";
        cJSON *array = object_item(g_document, array_name);
        if (!array) return -1;
        char id_text[32];
        std::snprintf(id_text, sizeof(id_text), "%llu", static_cast<unsigned long long>(id));
        for (cJSON *row = array->child; row; row = row->next) {
            cJSON *table_id = object_item(row, "TableId");
            if (cJSON_IsString(table_id) && table_id->valuestring && std::strcmp(table_id->valuestring, id_text) == 0) return 1;
        }
        return 0;
    }
    if (type == 2 || type == 5) {
        cJSON *collection = object_item(g_document, type == 2 ? "currencies" : "inventory");
        if (!cJSON_IsArray(collection)) return -1;
        cJSON *entry = balance_entry(type, id, false);
        if (!entry) return 0;
        cJSON *value = object_item(entry, type == 2 ? "value" : "count");
        if (type == 5) return cJSON_IsNumber(value) && value->valuedouble == static_cast<double>(value->valueint) && value->valueint >= 0 ? value->valueint : -1;
        int64_t parsed = -1;
        return cJSON_IsString(value) && parse_nonnegative_int64(value->valuestring, &parsed) ? parsed : -1;
    }
    return -1;
}
void store_set_activity_period(int64_t (*period)(void)) {
    std::lock_guard<std::recursive_mutex> lock(g_store_mutex);
    g_activity_period = period;
}
uint64_t store_activity_value(const char *kind, uint64_t id, int daily) {
    std::lock_guard<std::recursive_mutex> lock(g_store_mutex);
    return activity_value_unlocked(kind, id, daily);
}
int store_replace_value(const char *key, const cJSON *value) {
    std::lock_guard<std::recursive_mutex> lock(g_store_mutex);
    if (!key || !value || !g_document) { set_error("invalid local state update"); return 0; }
    cJSON *snapshot = cJSON_Duplicate(g_document, 1);
    cJSON *replacement = cJSON_Duplicate(value, 1);
    if (!snapshot || !replacement) { cJSON_Delete(snapshot); cJSON_Delete(replacement); set_error("snapshot local state update"); return 0; }
    if (cJSON_HasObjectItem(g_document, key)) cJSON_ReplaceItemInObjectCaseSensitive(g_document, key, replacement);
    else cJSON_AddItemToObject(g_document, key, replacement);
    if (store_commit()) { cJSON_Delete(snapshot); return 1; }
    cJSON_Delete(g_document);
    g_document = snapshot;
    return 0;
}
int store_skip_first_day(int *changed) {
    std::lock_guard<std::recursive_mutex> lock(g_store_mutex);
    if (!changed) { set_error("missing first day result"); return 0; }
    *changed = 0;
    if (!g_document) { set_error("save not loaded"); return 0; }
    cJSON *tutorial = object_item(g_document, "tutorial");
    cJSON *bits_item = object_item(tutorial, "content_bits");
    uint64_t bits = cJSON_IsString(bits_item) && bits_item->valuestring ? static_cast<uint64_t>(std::strtoull(bits_item->valuestring, nullptr, 10)) : 0;
    if (bits & 1u) return 1;
    cJSON *snapshot = cJSON_Duplicate(g_document, 1);
    if (!snapshot || !cJSON_IsObject(tutorial)) { cJSON_Delete(snapshot); set_error("snapshot first day state"); return 0; }
    char bits_text[32];
    std::snprintf(bits_text, sizeof(bits_text), "%llu", static_cast<unsigned long long>(bits | 1u));
    if (!put_string(tutorial, "content_bits", bits_text) || !put_bool(g_document, "first_day_skipped", true)) {
        cJSON_Delete(g_document); g_document = snapshot; set_error("invalid first day state"); return 0;
    }
    if (store_commit()) { cJSON_Delete(snapshot); *changed = 1; return 1; }
    cJSON_Delete(g_document); g_document = snapshot;
    return 0;
}
int store_update_with_cost(const char *key, const cJSON *value, const struct store_cost *costs, unsigned int count) {
    std::lock_guard<std::recursive_mutex> lock(g_store_mutex);
    if (!key || !value || !g_document || count > 64 || (count && !costs)) { set_error("invalid cost transaction"); return 0; }
    cJSON *snapshot = cJSON_Duplicate(g_document, 1);
    cJSON *replacement = cJSON_Duplicate(value, 1);
    if (!snapshot || !replacement) { cJSON_Delete(snapshot); cJSON_Delete(replacement); set_error("snapshot cost transaction"); return 0; }
    if (!deduct_costs(costs, count)) { cJSON_Delete(replacement); cJSON_Delete(g_document); g_document = snapshot; return 0; }
    if (cJSON_HasObjectItem(g_document, key)) cJSON_ReplaceItemInObjectCaseSensitive(g_document, key, replacement);
    else cJSON_AddItemToObject(g_document, key, replacement);
    if (store_commit()) { cJSON_Delete(snapshot); return 1; }
    cJSON_Delete(g_document); g_document = snapshot;
    return 0;
}
int store_story_start(uint64_t group, uint64_t quest, int bit, int difficulty, int count) {
    std::lock_guard<std::recursive_mutex> lock(g_store_mutex);
    if (!g_document || !group || !quest || bit < 1 || bit > 127 || difficulty != 0 || count < 0 || count > 1) { set_error("invalid story start"); return 0; }
    cJSON *pack = store_pack(group);
    if (!cJSON_IsObject(pack)) { set_error("missing pack"); return 0; }
    cJSON *story_index = object_item(pack, "story_index");
    if (!cJSON_IsNumber(story_index) || bit > story_index->valueint + 1) { set_error("story prerequisite incomplete"); return 0; }
    cJSON *snapshot = cJSON_Duplicate(g_document, 1);
    cJSON *story = cJSON_CreateObject();
    if (!snapshot || !story) { cJSON_Delete(snapshot); cJSON_Delete(story); set_error("snapshot story start"); return 0; }
    char group_text[32], quest_text[32];
    std::snprintf(group_text, sizeof(group_text), "%llu", static_cast<unsigned long long>(group));
    std::snprintf(quest_text, sizeof(quest_text), "%llu", static_cast<unsigned long long>(quest));
    bool ok = put_string(story, "group_id", group_text) && put_string(story, "quest_id", quest_text) && put_number(story, "bit", bit) && put_number(story, "difficulty", 0) && put_number(story, "count", 1);
    if (!ok || !put_item(g_document, "active_story", story) || !store_commit()) { cJSON_Delete(g_document); g_document = snapshot; if (!ok) set_error("invalid story start"); return 0; }
    cJSON_Delete(snapshot);
    return 1;
}
int store_use_account_level_items(int count, int *added_exp) {
    std::lock_guard<std::recursive_mutex> lock(g_store_mutex);
    if (count < 1 || count > 99 || !added_exp || !g_document) { set_error("invalid account coupon request"); return 0; }
    cJSON *account = object_item(g_document, "account");
    cJSON *level_item = object_item(account, "level");
    cJSON *exp_item = object_item(account, "exp");
    cJSON *bank_item = object_item(account, "coupon_exp_bank");
    if (!cJSON_IsNumber(level_item) || !cJSON_IsNumber(exp_item)) { set_error("invalid account coupon state"); return 0; }
    const int level = level_item->valueint;
    const int exp = exp_item->valueint;
    const int bank = cJSON_IsNumber(bank_item) ? bank_item->valueint : 0;
    if (level < 1 || level > 99 - count || exp < 0) { set_error("invalid account coupon level or experience"); return 0; }
    const int target = level + count;
    const int32_t total_exp = static_cast<int32_t>(static_cast<uint32_t>(exp) + static_cast<uint32_t>(bank));
    if (total_exp < 0) { set_error("account coupon experience overflow"); return 0; }
    int64_t required = 0;
    for (int current = level; current < target; ++current) {
        required += kLevelExp[current - 1];
        if (required > std::numeric_limits<int32_t>::max()) { set_error("account coupon experience overflow"); return 0; }
    }
    const int64_t next_requirement = kLevelExp[target - 1];
    if (next_requirement < 1 || next_requirement - 1 > std::numeric_limits<int32_t>::max()) { set_error("account coupon experience overflow"); return 0; }
    const int new_exp = (total_exp >= next_requirement && target <= 98) ? static_cast<int>(next_requirement - 1) : total_exp;
    const int new_bank = total_exp - new_exp;
    cJSON *snapshot = cJSON_Duplicate(g_document, 1);
    if (!snapshot) { set_error("unable to snapshot account coupon transaction"); return 0; }
    const struct store_cost cost{5, 900010004ULL, count};
    if (!deduct_costs(&cost, 1) || !put_number(account, "level", target) || !put_number(account, "exp", new_exp) || !put_number(account, "coupon_exp_bank", new_bank)) {
        cJSON_Delete(g_document); g_document = snapshot; set_error("unable to apply account coupon transaction"); return 0;
    }
    cJSON *custom_use = object_item(g_document, "custom_item_use");
    if (!custom_use) custom_use = cJSON_AddObjectToObject(g_document, "custom_item_use");
    cJSON *used_item = object_item(custom_use, "account_coupons_used");
    const int used = cJSON_IsNumber(used_item) ? used_item->valueint : 0;
    if (!cJSON_IsObject(custom_use) || used < 0 || used > std::numeric_limits<int>::max() - count || !put_number(custom_use, "account_coupons_used", used + count)) {
        cJSON_Delete(g_document); g_document = snapshot; set_error("invalid account coupon ledger"); return 0;
    }
    *added_exp = static_cast<int>(required);
    if (!store_commit()) { cJSON_Delete(g_document); g_document = snapshot; return 0; }
    cJSON_Delete(snapshot);
    return 1;
}
int store_apply_account_unlocks(const cJSON *unlocks) {
    std::lock_guard<std::recursive_mutex> lock(g_store_mutex);
    if (!g_document || !cJSON_IsArray(unlocks)) { set_error("invalid account unlocks"); return 0; }
    cJSON *snapshot = cJSON_Duplicate(g_document, 1);
    cJSON *copy = cJSON_Duplicate(unlocks, 1);
    if (!snapshot || !copy) { cJSON_Delete(snapshot); cJSON_Delete(copy); set_error("unable to snapshot account unlocks"); return 0; }
    if (!put_item(g_document, "unlocks", copy) || !apply_account_exp(0) || !store_commit()) { cJSON_Delete(g_document); g_document = snapshot; return 0; }
    cJSON_Delete(snapshot);
    return 1;
}
int store_quest_rank(uint64_t group, int kind, int bit) {
    std::lock_guard<std::recursive_mutex> lock(g_store_mutex);
    if (bit < 1 || !g_document) return 0;
    char group_text[32];
    std::snprintf(group_text, sizeof(group_text), "%llu", static_cast<unsigned long long>(group));
    cJSON *packs = object_item(g_document, "packs");
    cJSON *pack = object_item(packs, group_text);
    if (!cJSON_IsObject(pack)) return 0;
    if (kind == 0 || kind == 8) {
        cJSON *story_index = object_item(pack, "story_index");
        return cJSON_IsNumber(story_index) && story_index->valueint >= bit ? 1 : 0;
    }
    const char *field = nullptr;
    int max_bit = 21;
    switch (kind) {
        case 1: field = "normal_bits"; break;
        case 2: field = "hard_bits"; break;
        case 3: field = "very_hard_bits"; break;
        case 5: field = "challenge_bits"; break;
        case 12: field = "substory_bits"; max_bit = 63; break;
        default: return 0;
    }
    if (bit > max_bit) return 0;
    cJSON *bits_item = object_item(pack, field);
    uint64_t bits = 0;
    if (!cJSON_IsString(bits_item) || !bits_item->valuestring) return 0;
    bits = static_cast<uint64_t>(std::strtoull(bits_item->valuestring, nullptr, 10));
    const unsigned int shift = kind == 12 ? static_cast<unsigned int>(bit) : static_cast<unsigned int>((bit - 1) * 3);
    return kind == 12 ? static_cast<int>((bits >> shift) & 1u) : static_cast<int>((bits >> shift) & 7u);
}
int store_farming_rank(uint64_t group, int type, int bit) {
    std::lock_guard<std::recursive_mutex> lock(g_store_mutex);
    if (!g_document || bit < 1 || bit > 21) return 0;
    const char *field = type == 1 ? "GoldBit" : (type == 2 ? "CharacterExpBit" : (type == 3 ? "ArtifactExpBit" : nullptr));
    if (!field) return 0;
    char group_text[32];
    std::snprintf(group_text, sizeof(group_text), "%llu", static_cast<unsigned long long>(group));
    cJSON *state = object_item(g_document, "farming_state");
    cJSON *groups = object_item(state, "groups");
    cJSON *group_state = object_item(groups, group_text);
    cJSON *bits_item = object_item(group_state, field);
    uint64_t bits = 0;
    if (!cJSON_IsString(bits_item) || !bits_item->valuestring) return 0;
    bits = static_cast<uint64_t>(std::strtoull(bits_item->valuestring, nullptr, 10));
    const unsigned int shift = static_cast<unsigned int>((bit - 1) * 3);
    return static_cast<int>((bits >> shift) & 7u);
}
int store_gained_exp(int before_level, int before_exp, int target_level, int target_exp) {
    if (target_level < before_level || before_level < 1 || before_exp < 0 || target_exp < 0 || target_exp < before_exp) return 0;
    int64_t total = static_cast<int64_t>(target_exp) - static_cast<int64_t>(before_exp);
    bool found_before = false, found_target = false;
    for (int level = 1; level <= 99; ++level) {
        if (level == before_level) found_before = true;
        if (level == target_level) found_target = true;
        if (level < before_level || level >= target_level) continue;
        const int64_t required = kLevelExp[level - 1];
        if (required < 1) return 0;
        total += required;
        if (total > std::numeric_limits<int32_t>::max()) return 0;
    }
    return found_before && found_target ? static_cast<int>(total) : 0;
}
int store_story_end_rewards(uint64_t group, uint64_t quest, int bit, int difficulty, int result, const struct store_reward *rewards, unsigned int count, int *changed) {
    std::lock_guard<std::recursive_mutex> lock(g_store_mutex);
    if (changed) *changed = 0;
    if (!g_document || !changed || !group || !quest || bit < 1 || bit > 127 || difficulty != 0 || (result != 1 && result != 2) || count > 16 || (count && !rewards)) { set_error("invalid story end"); return 0; }
    cJSON *active = object_item(g_document, "active_story");
    if (!cJSON_IsObject(active)) {
        cJSON *last = object_item(g_document, "last_story_result");
        if (cJSON_IsObject(last) && parse_id_string(last, "group_id", group) && parse_id_string(last, "quest_id", quest) && int_matches(last, "bit", bit) && int_matches(last, "difficulty", difficulty) && int_matches(last, "result", result)) return 1;
        set_error("missing or mismatched active story"); return 0;
    }
    if (!parse_id_string(active, "group_id", group) || !parse_id_string(active, "quest_id", quest) || !int_matches(active, "bit", bit) || !int_matches(active, "difficulty", difficulty)) { set_error("active story receipt mismatch"); return 0; }
    cJSON *pack = store_pack(group);
    cJSON *story_index = object_item(pack, "story_index");
    if (!cJSON_IsObject(pack) || !cJSON_IsNumber(story_index)) { set_error("missing story pack"); return 0; }
    cJSON *snapshot = cJSON_Duplicate(g_document, 1);
    if (!snapshot) { set_error("snapshot story end"); return 0; }
    if (result == 1 && bit > story_index->valueint) {
        for (unsigned int i = 0; i < count; ++i) if (!grant_reward(rewards[i])) { cJSON_Delete(g_document); g_document = snapshot; return 0; }
        if (!put_number(pack, "story_index", bit)) { cJSON_Delete(g_document); g_document = snapshot; set_error("update story index"); return 0; }
        if (group == 1000 && bit >= 9 && !put_number(pack, "open_type", 3)) { cJSON_Delete(g_document); g_document = snapshot; return 0; }
        *changed = 1;
    }
    cJSON *last = cJSON_Duplicate(active, 1);
    if (!last || !put_number(last, "result", result) || !put_item(g_document, "last_story_result", last)) { cJSON_Delete(last); cJSON_Delete(g_document); g_document = snapshot; set_error("store last story result"); return 0; }
    cJSON_DeleteItemFromObjectCaseSensitive(g_document, "active_story");
    if (!commit_or_restore(snapshot)) { *changed = 0; return 0; }
    return 1;
}
int store_story_end(uint64_t group, uint64_t quest, int bit, int difficulty, int result, int *changed) {
    return store_story_end_rewards(group, quest, bit, difficulty, result, nullptr, 0, changed);
}
int store_seed_full_runes(void) {
    std::lock_guard<std::recursive_mutex> lock(g_store_mutex);
    if (!g_document) { set_error("save not loaded for rune defaults"); return 0; }
    if (cJSON_IsTrue(object_item(g_document, "runes_seeded"))) return 1;
    if (!cJSON_IsArray(object_item(g_document, "runes"))) { set_error("invalid rune collection"); return 0; }
    cJSON *snapshot = cJSON_Duplicate(g_document, 1);
    if (!snapshot) { set_error("snapshot rune defaults"); return 0; }
    for (const auto &rule : kRuneStockRules) {
        cJSON *entry = counted_entry(11, rule.id, false);
        if (!entry) continue;
        cJSON *count_item = object_item(entry, "Count");
        if (!cJSON_IsNumber(count_item) || count_item->valueint < 0) { cJSON_Delete(g_document); g_document = snapshot; set_error("invalid rune stock"); return 0; }
        const int64_t existing = count_item->valueint;
        if (rule.limit >= 100 && existing <= 99) {
            const store_reward reward{11, rule.id, static_cast<int>(100 - existing)};
            if (!grant_reward_inner(reward)) { cJSON_Delete(g_document); g_document = snapshot; return 0; }
        }
    }
    if (!put_bool(g_document, "runes_seeded", true)) { cJSON_Delete(g_document); g_document = snapshot; return 0; }
    if (!commit_or_restore(snapshot)) return 0;
    return 1;
}
bool attach_battle_roster_snapshot(cJSON *receipt, int formation) {
    if (!cJSON_IsObject(receipt) || !g_document) return false;
    cJSON *formations = object_item(g_document, "formations");
    if (!cJSON_IsArray(formations)) return true;
    for (cJSON *row = formations->child; row; row = row->next) {
        cJSON *index = object_item(row, "Index");
        if (!cJSON_IsNumber(index) || index->valueint != formation) continue;
        cJSON *character_ids = object_item(row, "CharacterTableIds");
        if (!cJSON_IsArray(character_ids)) return true;
        cJSON *copy = cJSON_Duplicate(character_ids, 1);
        if (!copy) { set_error("snapshot quest roster"); return false; }
        return put_item(receipt, "battle_roster_snapshot", copy);
    }
    return true;
}

int store_quest_start_multiple(uint64_t group, uint64_t quest, int kind, int bit, int difficulty, int formation, int play_count, const struct store_cost *costs, unsigned int cost_count) {
    std::lock_guard<std::recursive_mutex> lock(g_store_mutex);
    const bool kind_valid = kind == 12 || (kind >= 1 && kind <= 5 && ((1u << kind) & 0x2eu));
    if (!g_document || !group || !quest || !kind_valid || bit < 1 || bit > (kind == 12 ? 63 : 21) || difficulty < 0 || difficulty > 3 || play_count < 1 || play_count > 100 || formation < 0 || cost_count > 64 || (cost_count && !costs)) { set_error("invalid quest start"); return 0; }
    cJSON *pack = store_pack(group);
    cJSON *open_type = object_item(pack, "open_type");
    if (!cJSON_IsObject(pack) || !cJSON_IsNumber(open_type) || open_type->valueint == 0) { set_error("pack not unlocked"); return 0; }
    cJSON *active = object_item(g_document, "active_quest");
    if (cJSON_IsObject(active) && parse_id_string(active, "group_id", group) && parse_id_string(active, "quest_id", quest) && int_matches(active, "kind", kind) && int_matches(active, "bit", bit) && int_matches(active, "difficulty", difficulty) && int_matches(active, "formation", formation) && int_matches(active, "play_count", play_count)) return 1;
    cJSON *snapshot = cJSON_Duplicate(g_document, 1);
    if (!snapshot) { set_error("snapshot quest start"); return 0; }
    if (!deduct_costs(costs, cost_count)) { cJSON_Delete(g_document); g_document = snapshot; return 0; }
    cJSON *receipt = cJSON_CreateObject();
    if (!receipt) { cJSON_Delete(g_document); g_document = snapshot; return 0; }
    const bool ok = add_string_number(receipt, "group_id", group) && add_string_number(receipt, "quest_id", quest) && put_number(receipt, "kind", kind) && put_number(receipt, "bit", bit) && put_number(receipt, "difficulty", difficulty) && put_number(receipt, "formation", formation) && put_number(receipt, "play_count", play_count);
    if (!ok || !attach_battle_roster_snapshot(receipt, formation) || !put_item(g_document, "active_quest", receipt) || !commit_or_restore(snapshot)) { if (g_document != snapshot) { cJSON_Delete(g_document); g_document = snapshot; } set_error("Unable to restore local quest rewards and unlocks"); return 0; }
    return 1;
}
int store_quest_start(uint64_t group, uint64_t quest, int kind, int bit, int difficulty, int formation, const struct store_cost *costs, unsigned int cost_count) {
    return store_quest_start_multiple(group, quest, kind, bit, difficulty, formation, 1, costs, cost_count);
}

int store_quest_end_favor(uint64_t group, uint64_t quest, int kind, int bit, int difficulty, int play_count, int result, int rank, int complete_pack, const struct store_reward *rewards, unsigned int reward_count, uint64_t enemy_kills, const cJSON *favor_state, int *changed) {
    std::lock_guard<std::recursive_mutex> lock(g_store_mutex);
    if (changed) *changed = 0;
    const bool story_kind = kind == 0 || kind == 8;
    const bool ranked_kind = kind == 1 || kind == 2 || kind == 3 || kind == 5 || kind == 12;
    const int max_bit = kind == 12 ? 63 : (story_kind ? 127 : 21);
    if (!changed || !g_document || !group || !quest || (!story_kind && !ranked_kind) || bit < 1 || bit > max_bit || difficulty < 0 || difficulty > 3 || play_count < 1 || play_count > 100 || result < 1 || result > 2 || (result == 1 && (rank < 1 || rank > 3)) || reward_count > 512 || (reward_count && !rewards)) {
        set_error("invalid quest result");
        return 0;
    }
    auto receipt_count = [](const cJSON *receipt) -> int {
        cJSON *item = object_item(const_cast<cJSON *>(receipt), "play_count");
        if (!item) return 1;
        return cJSON_IsNumber(item) ? item->valueint : 0;
    };
    cJSON *active = object_item(g_document, "active_quest");
    if (!active) {
        cJSON *last = object_item(g_document, "last_quest_result");
        if (cJSON_IsObject(last) && parse_id_string(last, "group_id", group) && parse_id_string(last, "quest_id", quest) && int_matches(last, "difficulty", difficulty) && int_matches(last, "result", result) && int_matches(last, "rank", rank) && receipt_count(last) == play_count) return 1;
        set_error("active quest receipt mismatch");
        return 0;
    }
    if (!cJSON_IsObject(active) || !parse_id_string(active, "group_id", group) || !parse_id_string(active, "quest_id", quest) || !int_matches(active, "kind", kind) || !int_matches(active, "bit", bit) || !int_matches(active, "difficulty", difficulty) || receipt_count(active) != play_count) {
        set_error("active quest receipt mismatch");
        return 0;
    }
    cJSON *pack = store_pack(group);
    if (!cJSON_IsObject(pack)) { set_error("missing quest pack"); return 0; }
    cJSON *snapshot = cJSON_Duplicate(g_document, 1);
    if (!snapshot) { set_error("snapshot quest result"); return 0; }
    if (result == 1) {
        for (unsigned int i = 0; i < reward_count; ++i) {
            if (!grant_reward(rewards[i])) { cJSON_Delete(g_document); g_document = snapshot; return 0; }
        }
        const int old_rank = store_quest_rank(group, kind, bit);
        if (story_kind) {
            if (old_rank == 0 && !put_number(pack, "story_index", bit)) { cJSON_Delete(g_document); g_document = snapshot; set_error("update quest story index"); return 0; }
        } else {
            const char *field = kind == 1 ? "normal_bits" : (kind == 2 ? "hard_bits" : (kind == 3 ? "very_hard_bits" : (kind == 5 ? "challenge_bits" : "substory_bits")));
            cJSON *bits_item = object_item(pack, field);
            uint64_t old_bits = 0;
            if (cJSON_IsString(bits_item) && bits_item->valuestring) {
                if (!valid_unsigned_decimal(bits_item->valuestring, &old_bits)) { cJSON_Delete(g_document); g_document = snapshot; set_error("invalid quest progress format"); return 0; }
            } else if (bits_item) { cJSON_Delete(g_document); g_document = snapshot; set_error("invalid quest progress format"); return 0; }
            if (kind == 12) old_bits |= uint64_t(1) << static_cast<unsigned int>(bit);
            else if (rank > old_rank) {
                const unsigned int shift = static_cast<unsigned int>((bit - 1) * 3);
                const uint64_t mask = uint64_t(7) << shift;
                old_bits = (old_bits & ~mask) | (static_cast<uint64_t>(rank) << shift);
            }
            char text[32];
            std::snprintf(text, sizeof(text), "%llu", static_cast<unsigned long long>(old_bits));
            if (!put_string(pack, field, text)) { cJSON_Delete(g_document); g_document = snapshot; set_error("update quest progress"); return 0; }
        }
        if (!activity_add("quest", 0, static_cast<uint64_t>(play_count)) || !activity_add("pack_clear", group, static_cast<uint64_t>(play_count)) || !activity_add("pack_kill", group, enemy_kills)) { cJSON_Delete(g_document); g_document = snapshot; return 0; }
        for (unsigned int i = 0; i < reward_count; ++i) {
            if (rewards[i].type == 11 && !activity_add("pack_rune", group, static_cast<uint64_t>(rewards[i].amount))) { cJSON_Delete(g_document); g_document = snapshot; return 0; }
        }
        if (favor_state) {
            if (!cJSON_IsObject(favor_state)) { cJSON_Delete(g_document); g_document = snapshot; set_error("invalid date field state"); return 0; }
            cJSON *favor_copy = cJSON_Duplicate(const_cast<cJSON *>(favor_state), 1);
            if (!favor_copy || !put_item(g_document, "date_field", favor_copy)) { cJSON_Delete(favor_copy); cJSON_Delete(g_document); g_document = snapshot; set_error("copy date field state"); return 0; }
        }
        if (complete_pack && !put_number(pack, "open_type", 3)) { cJSON_Delete(g_document); g_document = snapshot; set_error("complete quest pack"); return 0; }
    }
    cJSON *last_result = cJSON_Duplicate(active, 1);
    if (!last_result || !put_number(last_result, "result", result) || !put_number(last_result, "rank", rank) || !put_item(g_document, "last_quest_result", last_result)) {
        cJSON_Delete(last_result); cJSON_Delete(g_document); g_document = snapshot; set_error("create quest result receipt"); return 0;
    }
    cJSON_DeleteItemFromObjectCaseSensitive(g_document, "active_quest");
    if (!commit_or_restore(snapshot)) return 0;
    *changed = result == 1 ? 1 : 0;
    return 1;
}
int store_quest_end(uint64_t group, uint64_t quest, int kind, int bit, int difficulty, int result, int rank, int complete_pack, const struct store_reward *rewards, unsigned int reward_count, int *changed) {
    return store_quest_end_favor(group, quest, kind, bit, difficulty, 1, result, rank, complete_pack, rewards, reward_count, 0, nullptr, changed);
}
int store_quest_end_multiple(uint64_t group, uint64_t quest, int kind, int bit, int difficulty, int play_count, int result, int rank, int complete_pack, const struct store_reward *rewards, unsigned int reward_count, int *changed) {
    return store_quest_end_favor(group, quest, kind, bit, difficulty, play_count, result, rank, complete_pack, rewards, reward_count, 0, nullptr, changed);
}
int store_quest_end_report(uint64_t group, uint64_t quest, int kind, int bit, int difficulty, int play_count, int result, int rank, int complete_pack, const struct store_reward *rewards, unsigned int reward_count, uint64_t enemy_kills, int *changed) {
    return store_quest_end_favor(group, quest, kind, bit, difficulty, play_count, result, rank, complete_pack, rewards, reward_count, enemy_kills, nullptr, changed);
}

int store_purchase_values(const cJSON *updates, const struct store_cost *costs, unsigned int cost_count, const struct store_reward *rewards, unsigned int reward_count) {
    std::lock_guard<std::recursive_mutex> lock(g_store_mutex);
    if (!g_document || !cJSON_IsObject(updates) || cJSON_GetArraySize(updates) > 8 || cost_count > 64 || reward_count > 64 || (cost_count && !costs) || (reward_count && !rewards)) { set_error("invalid purchase values"); return 0; }
    cJSON *snapshot = cJSON_Duplicate(g_document, 1);
    if (!snapshot) { set_error("snapshot purchase values"); return 0; }
    if (!deduct_costs(costs, cost_count)) { cJSON_Delete(g_document); g_document = snapshot; return 0; }
    for (const cJSON *item = updates->child; item; item = item->next) {
        if (!item->string || !*item->string) { cJSON_Delete(g_document); g_document = snapshot; set_error("invalid purchase update key"); return 0; }
        cJSON *copy = cJSON_Duplicate(item, 1);
        if (!copy) { cJSON_Delete(g_document); g_document = snapshot; set_error("snapshot purchase update"); return 0; }
        if (std::strcmp(item->string, "farming_state") == 0) {
            if (!farming_activity(copy)) { cJSON_Delete(copy); cJSON_Delete(g_document); g_document = snapshot; return 0; }
        } else if (std::strcmp(item->string, "raid_state") == 0) {
            cJSON *old_state = object_item(g_document, "raid_state");
            cJSON *last = object_item(copy, "last");
            if (!cJSON_HasObjectItem(old_state, "active") && !cJSON_HasObjectItem(copy, "active") && cJSON_IsNumber(last) && last->valueint == 1 && !activity_add("quest", 0, 1)) { cJSON_Delete(copy); cJSON_Delete(g_document); g_document = snapshot; return 0; }
        }
        if (!put_item(g_document, item->string, copy)) { cJSON_Delete(g_document); g_document = snapshot; set_error("replace purchase value"); return 0; }
    }
    for (unsigned int i = 0; i < reward_count; ++i) {
        if (!grant_reward_inner(rewards[i]) || (rewards[i].type == 2 && !activity_add("currency", rewards[i].id, static_cast<uint64_t>(rewards[i].amount)))) { cJSON_Delete(g_document); g_document = snapshot; return 0; }
    }
    return commit_or_restore(snapshot);
}
int store_purchase_rewards(const char *key, const cJSON *state, const struct store_cost *costs, unsigned int cost_count, const struct store_reward *rewards, unsigned int reward_count) {
    std::lock_guard<std::recursive_mutex> lock(g_store_mutex);
    if (!g_document || !key || !*key || !state || cost_count > 64 || reward_count > 512 || (cost_count && !costs) || (reward_count && !rewards)) { set_error("invalid purchase rewards"); return 0; }
    cJSON *snapshot = cJSON_Duplicate(g_document, 1);
    cJSON *state_copy = cJSON_Duplicate(state, 1);
    if (!snapshot || !state_copy) { cJSON_Delete(snapshot); cJSON_Delete(state_copy); set_error("snapshot purchase rewards"); return 0; }
    if (!deduct_costs(costs, cost_count)) { cJSON_Delete(state_copy); cJSON_Delete(g_document); g_document = snapshot; return 0; }
    for (unsigned int i = 0; i < reward_count; ++i) {
        if (!grant_reward_inner(rewards[i]) || (rewards[i].type == 2 && !activity_add("currency", rewards[i].id, static_cast<uint64_t>(rewards[i].amount)))) { cJSON_Delete(state_copy); cJSON_Delete(g_document); g_document = snapshot; return 0; }
    }
    if (std::strcmp(key, "custom_item_use") == 0 || std::strcmp(key, "gacha_state") == 0) {
        cJSON_Delete(state_copy); cJSON_Delete(g_document); g_document = snapshot;
        set_error("purchase reward special state requires signed artifact or gacha metadata");
        return 0;
    }
    if (std::strcmp(key, "farming_state") == 0 && !farming_activity(state_copy)) { cJSON_Delete(state_copy); cJSON_Delete(g_document); g_document = snapshot; return 0; }
    if (!put_item(g_document, key, state_copy)) { cJSON_Delete(g_document); g_document = snapshot; set_error("replace purchase reward state"); return 0; }
    return commit_or_restore(snapshot);
}

int store_farming_end_favor(uint64_t group, uint64_t quest, int type, int bit, int count, int result, int rank, const struct store_reward *rewards, unsigned int reward_count, const cJSON *favor_state, int *changed) {
    std::lock_guard<std::recursive_mutex> lock(g_store_mutex);
    if (changed) *changed = 0;
    if (!changed || !g_document || !group || !quest || type < 1 || type > 3 || bit < 1 || bit > 21 || count < 1 || count > 99 || result < 1 || result > 2 || (result == 1 && rank < 1) || reward_count > 512 || (reward_count && !rewards)) { set_error("invalid farming result"); return 0; }
    cJSON *current_state = object_item(g_document, "farming_state");
    if (!cJSON_IsObject(current_state)) { set_error("missing farming state"); return 0; }
    cJSON *active = object_item(current_state, "active");
    cJSON *active_type = object_item(active, "type"), *active_bit = object_item(active, "bit"), *active_count = object_item(active, "count");
    if (!parse_id_string(active, "group_id", group) || !parse_id_string(active, "quest_id", quest) || !cJSON_IsNumber(active_type) || active_type->valueint != type || !cJSON_IsNumber(active_bit) || active_bit->valueint != bit || !cJSON_IsNumber(active_count) || active_count->valueint != count) { set_error("farming result does not match active start"); return 0; }
    cJSON *state = cJSON_Duplicate(current_state, 1);
    if (!cJSON_IsObject(state)) { cJSON_Delete(state); set_error("snapshot farming result"); return 0; }
    char group_text[32];
    std::snprintf(group_text, sizeof(group_text), "%llu", static_cast<unsigned long long>(group));
    cJSON *groups = object_item(state, "groups");
    if (!groups) groups = cJSON_AddObjectToObject(state, "groups");
    cJSON *group_state = cJSON_IsObject(groups) ? object_item(groups, group_text) : nullptr;
    if (!cJSON_IsObject(groups)) { cJSON_Delete(state); set_error("invalid farming group ledger"); return 0; }
    if (!group_state) {
        group_state = cJSON_AddObjectToObject(groups, group_text);
        if (!cJSON_IsObject(group_state) || !put_string(group_state, "GroupId", group_text) || !put_string(group_state, "GoldBit", "0") || !put_string(group_state, "CharacterExpBit", "0") || !put_string(group_state, "ArtifactExpBit", "0")) { cJSON_Delete(state); set_error("create farming group ledger"); return 0; }
    }
    if (!cJSON_IsObject(group_state)) { cJSON_Delete(state); set_error("invalid farming group ledger"); return 0; }
    if (result == 1) {
        const char *rank_key = type == 1 ? "GoldBit" : (type == 2 ? "CharacterExpBit" : "ArtifactExpBit");
        cJSON *rank_value = object_item(group_state, rank_key);
        if (!cJSON_IsString(rank_value) || !rank_value->valuestring) { cJSON_Delete(state); set_error("invalid farming rank ledger"); return 0; }
        errno = 0;
        char *end = nullptr;
        const unsigned long long packed = std::strtoull(rank_value->valuestring, &end, 10);
        if (errno || end == rank_value->valuestring || !end || *end != '\0') { cJSON_Delete(state); set_error("invalid farming rank ledger"); return 0; }
        const unsigned int shift = static_cast<unsigned int>((bit - 1) * 3);
        const unsigned int old_rank = static_cast<unsigned int>((packed >> shift) & 7u);
        if (static_cast<unsigned int>(rank) > old_rank) {
            const uint64_t mask = uint64_t(7) << shift;
            const uint64_t updated = (static_cast<uint64_t>(packed) & ~mask) | (static_cast<uint64_t>(rank) << shift);
            char text[32];
            std::snprintf(text, sizeof(text), "%llu", static_cast<unsigned long long>(updated));
            if (!put_string(group_state, rank_key, text)) { cJSON_Delete(state); set_error("update farming rank ledger"); return 0; }
        }
    }
    cJSON *last_result = cJSON_Duplicate(active, 1);
    if (!cJSON_IsObject(last_result) || !put_number(last_result, "result", result) || !put_number(last_result, "rank", rank) || !put_item(state, "last_result", last_result)) { cJSON_Delete(last_result); cJSON_Delete(state); set_error("create farming result receipt"); return 0; }
    cJSON_DeleteItemFromObjectCaseSensitive(state, "active");
    int ok = 0;
    if (result == 1 && favor_state) {
        cJSON *updates = cJSON_CreateObject();
        cJSON *state_copy = cJSON_Duplicate(state, 1);
        cJSON *favor_copy = cJSON_Duplicate(const_cast<cJSON *>(favor_state), 1);
        if (updates && state_copy && favor_copy && put_item(updates, "farming_state", state_copy) && put_item(updates, "favor_state", favor_copy)) ok = store_purchase_values(updates, nullptr, 0, rewards, reward_count);
        else { cJSON_Delete(state_copy); cJSON_Delete(favor_copy); }
        cJSON_Delete(updates);
    } else {
        ok = store_purchase_rewards("farming_state", state, nullptr, 0, result == 1 ? rewards : nullptr, result == 1 ? reward_count : 0);
    }
    cJSON_Delete(state);
    if (!ok) return 0;
    *changed = result == 1 ? 1 : 0;
    return 1;
}

int store_farming_end(uint64_t group, uint64_t quest, int type, int bit, int count, int result, int rank, const struct store_reward *rewards, unsigned int reward_count, int *changed) {
    return store_farming_end_favor(group, quest, type, bit, count, result, rank, rewards, reward_count, nullptr, changed);
}

int store_farming_start(uint64_t group, uint64_t quest, int type, int bit, int formation, int count, const struct store_cost *costs, unsigned int cost_count) {
    std::lock_guard<std::recursive_mutex> lock(g_store_mutex);
    if (!g_document || !group || !quest || type < 1 || type > 3 || bit < 1 || bit > 21 || formation < 1 || formation > 10 || count < 1 || count > 99 || cost_count > 64 || (cost_count && !costs)) { set_error("invalid farming start"); return 0; }
    cJSON *old_state = object_item(g_document, "farming_state");
    cJSON *state = old_state ? cJSON_Duplicate(old_state, 1) : cJSON_CreateObject();
    if (!cJSON_IsObject(state)) { cJSON_Delete(state); set_error("invalid farming state"); return 0; }
    cJSON *old_active = object_item(state, "active");
    auto receipt_matches = [&](cJSON *receipt) {
        cJSON *type_item = object_item(receipt, "type");
        cJSON *bit_item = object_item(receipt, "bit");
        cJSON *count_item = object_item(receipt, "count");
        return parse_id_string(receipt, "group_id", group) && parse_id_string(receipt, "quest_id", quest) && cJSON_IsNumber(type_item) && type_item->valueint == type && cJSON_IsNumber(bit_item) && bit_item->valueint == bit && cJSON_IsNumber(count_item) && count_item->valueint == count;
    };
    if (receipt_matches(old_active)) {
        cJSON *old_formation = object_item(old_active, "formation");
        if (cJSON_IsNumber(old_formation) && old_formation->valueint == formation) { cJSON_Delete(state); return 1; }
    }
    cJSON *receipt = cJSON_CreateObject();
    char group_text[32], quest_text[32];
    std::snprintf(group_text, sizeof(group_text), "%llu", static_cast<unsigned long long>(group));
    std::snprintf(quest_text, sizeof(quest_text), "%llu", static_cast<unsigned long long>(quest));
    bool ok = receipt && put_string(receipt, "group_id", group_text) && put_string(receipt, "quest_id", quest_text) && put_number(receipt, "type", type) && put_number(receipt, "bit", bit) && put_number(receipt, "formation", formation) && put_number(receipt, "count", count);
    cJSON *formations = object_item(g_document, "formations");
    for (cJSON *row = ok && cJSON_IsArray(formations) ? formations->child : nullptr; row; row = row->next) {
        cJSON *index = object_item(row, "Index");
        if (!cJSON_IsNumber(index) || index->valueint != formation) continue;
        cJSON *roster = object_item(row, "CharacterTableIds");
        if (cJSON_IsArray(roster)) {
            cJSON *roster_copy = cJSON_Duplicate(roster, 1);
            if (!roster_copy || !put_item(receipt, "roster", roster_copy)) { cJSON_Delete(roster_copy); ok = false; }
        }
        break;
    }
    if (ok) ok = put_item(state, "active", receipt);
    else cJSON_Delete(receipt);
    if (ok) {
        cJSON *groups = object_item(state, "groups");
        if (!groups) groups = cJSON_AddObjectToObject(state, "groups");
        if (!cJSON_IsObject(groups)) ok = false;
        cJSON *group_state = ok ? object_item(groups, group_text) : nullptr;
        if (ok && !group_state) {
            group_state = cJSON_AddObjectToObject(groups, group_text);
            ok = cJSON_IsObject(group_state) && put_string(group_state, "GroupId", group_text) && put_string(group_state, "GoldBit", "0") && put_string(group_state, "CharacterExpBit", "0") && put_string(group_state, "ArtifactExpBit", "0");
        } else if (ok && !cJSON_IsObject(group_state)) ok = false;
    }
    if (!ok) { cJSON_Delete(state); set_error("create farming receipt"); return 0; }
    const int result = store_update_with_cost("farming_state", state, costs, cost_count);
    cJSON_Delete(state);
    return result;
}

int store_complete_prologue(int expected_index, const struct store_reward *rewards, unsigned int count, const cJSON *unlocks, int *changed) {
    std::lock_guard<std::recursive_mutex> lock(g_store_mutex);
    if (changed) *changed = 0;
    if (!changed || !g_document || count > 64 || (count && !rewards) || !cJSON_IsArray(const_cast<cJSON *>(unlocks))) { set_error("invalid prologue completion"); return 0; }
    cJSON *pack = store_pack(1000);
    if (!pack) { set_error("prologue pack unavailable"); return 0; }
    cJSON *story_index = object_item(pack, "story_index");
    if (!cJSON_IsNumber(story_index) || story_index->valueint >= 10) { set_error("invalid prologue story index"); return 0; }
    if (story_index->valueint == 9) return 1;
    if (story_index->valueint != expected_index) { set_error("prologue index mismatch"); return 0; }
    cJSON *tutorial = object_item(g_document, "tutorial");
    cJSON *content_bits = object_item(tutorial, "content_bits");
    if (!cJSON_IsString(content_bits) || !content_bits->valuestring || (std::strtoull(content_bits->valuestring, nullptr, 10) & 1u) == 0) { set_error("prologue tutorial bit not complete"); return 0; }
    if (cJSON_HasObjectItem(g_document, "active_story") || cJSON_HasObjectItem(g_document, "active_quest") || cJSON_HasObjectItem(g_document, "active_farming")) { set_error("prologue completion blocked by active activity"); return 0; }
    cJSON *snapshot = cJSON_Duplicate(g_document, 1);
    cJSON *unlock_copy = cJSON_Duplicate(const_cast<cJSON *>(unlocks), 1);
    if (!snapshot || !unlock_copy) { cJSON_Delete(snapshot); cJSON_Delete(unlock_copy); set_error("snapshot prologue completion"); return 0; }
    for (unsigned int i = 0; i < count; ++i) {
        if (!grant_reward(rewards[i])) { cJSON_Delete(unlock_copy); cJSON_Delete(g_document); g_document = snapshot; return 0; }
    }
    if (!put_number(pack, "story_index", 9) || !put_number(pack, "open_type", 3) || !put_item(g_document, "unlocks", unlock_copy)) {
        cJSON_Delete(g_document); g_document = snapshot; set_error("update prologue completion"); return 0;
    }
    cJSON *receipt = cJSON_CreateObject();
    if (!receipt || !put_number(receipt, "from_index", expected_index) || !put_number(receipt, "to_index", 9) || !put_string(receipt, "quest_id", "1000008") || !put_item(g_document, "prologue_skip_receipt", receipt)) {
        cJSON_Delete(receipt); cJSON_Delete(g_document); g_document = snapshot; set_error("create prologue receipt"); return 0;
    }
    if (!commit_or_restore(snapshot)) return 0;
    *changed = 1;
    return 1;
}

int store_equip_artifact(uint64_t artifact_server_id, uint64_t character_table_id, uint64_t character_server_id) {
    std::lock_guard<std::recursive_mutex> lock(g_store_mutex);
    if (!g_document || !artifact_server_id || ((character_table_id == 0) != (character_server_id == 0))) { set_error("invalid equipment key"); return 0; }
    cJSON *artifacts = object_item(g_document, "artifacts");
    if (!cJSON_IsArray(artifacts)) { set_error("equipment or character not owned"); return 0; }
    cJSON *target = nullptr;
    for (cJSON *row = artifacts->child; row; row = row->next) {
        if (parse_id_string(row, "ServerId", artifact_server_id)) { target = row; break; }
    }
    if (!target) { set_error("equipment or character not owned"); return 0; }
    if (character_table_id != 0) {
        cJSON *characters = object_item(g_document, "characters");
        bool owned = false;
        for (cJSON *row = cJSON_IsArray(characters) ? characters->child : nullptr; row; row = row->next) {
            if (parse_id_string(row, "TableId", character_table_id) && parse_id_string(row, "ServerId", character_server_id)) { owned = true; break; }
        }
        if (!owned) { set_error("equipment or character not owned"); return 0; }
    }
    cJSON *snapshot = cJSON_Duplicate(g_document, 1);
    if (!snapshot) { set_error("snapshot equipment"); return 0; }
    char character_id[32];
    std::snprintf(character_id, sizeof(character_id), "%llu", static_cast<unsigned long long>(character_table_id));
    artifacts = object_item(g_document, "artifacts");
    for (cJSON *row = artifacts ? artifacts->child : nullptr; row; row = row->next) {
        bool update_target = row == target;
        if (!update_target && character_table_id != 0) {
            cJSON *equipped = object_item(row, "CharacterTableId");
            update_target = cJSON_IsString(equipped) && equipped->valuestring && std::strcmp(equipped->valuestring, character_id) == 0;
        }
        if (update_target && !put_string(row, "CharacterTableId", row == target ? character_id : "0")) {
            cJSON_Delete(g_document); g_document = snapshot; set_error("update artifact equipment"); return 0;
        }
    }
    return commit_or_restore(snapshot) ? 1 : 0;
}

int store_exchange_gacha_points(const cJSON *state, uint64_t point_id, int cost, const struct store_reward *rewards, unsigned int reward_count) {
    std::lock_guard<std::recursive_mutex> lock(g_store_mutex);
    if (!g_document || !state || !point_id || cost < 1 || !rewards || reward_count < 1 || reward_count > 64) { set_error("invalid gacha point exchange"); return 0; }
    cJSON *current_gacha = object_item(g_document, "gacha_state");
    cJSON *current_points = object_item(current_gacha, "points");
    if (!cJSON_IsArray(current_points)) { set_error("invalid gacha points collection"); return 0; }
    char id_text[32];
    std::snprintf(id_text, sizeof(id_text), "%llu", static_cast<unsigned long long>(point_id));
    cJSON *current_entry = nullptr;
    for (cJSON *row = current_points->child; row; row = row->next) {
        cJSON *table_id = object_item(row, "TableId");
        if (cJSON_IsString(table_id) && table_id->valuestring && std::strcmp(table_id->valuestring, id_text) == 0) { current_entry = row; break; }
    }
    cJSON *current_value = object_item(current_entry, "Value");
    if (!cJSON_IsNumber(current_value) || current_value->valuedouble != static_cast<double>(current_value->valueint) || current_value->valueint < cost) { set_error("insufficient or invalid gacha points"); return 0; }
    const int old_value = current_value->valueint;
    cJSON *snapshot = cJSON_Duplicate(g_document, 1);
    cJSON *state_copy = cJSON_Duplicate(state, 1);
    if (!snapshot || !state_copy) { cJSON_Delete(snapshot); cJSON_Delete(state_copy); set_error("snapshot gacha point exchange"); return 0; }
    cJSON *copy_points = object_item(state_copy, "points");
    cJSON *copy_entry = nullptr;
    for (cJSON *row = copy_points ? copy_points->child : nullptr; row; row = row->next) {
        cJSON *table_id = object_item(row, "TableId");
        if (cJSON_IsString(table_id) && table_id->valuestring && std::strcmp(table_id->valuestring, id_text) == 0) { copy_entry = row; break; }
    }
    if (!cJSON_IsObject(copy_entry) || !put_number(copy_entry, "Value", old_value - cost)) { cJSON_Delete(state_copy); cJSON_Delete(g_document); g_document = snapshot; set_error("stale point exchange state"); return 0; }
    for (unsigned int i = 0; i < reward_count; ++i) {
        if (!grant_reward_inner(rewards[i]) || (rewards[i].type == 2 && !activity_add("currency", rewards[i].id, static_cast<uint64_t>(rewards[i].amount)))) { cJSON_Delete(state_copy); cJSON_Delete(g_document); g_document = snapshot; return 0; }
    }
    if (!put_item(g_document, "gacha_state", state_copy)) { cJSON_Delete(g_document); g_document = snapshot; set_error("replace gacha exchange state"); return 0; }
    return commit_or_restore(snapshot);
}

int store_grant_once(const char *key, const cJSON *result, const struct store_reward *rewards, unsigned int count, int *changed) {
    std::lock_guard<std::recursive_mutex> lock(g_store_mutex);
    if (changed) *changed = 0;
    if (!g_document || !key || !*key || !result || !rewards || count > 64 || !changed) { set_error("invalid grant once"); return 0; }
    if (cJSON_HasObjectItem(g_document, key)) return 1;
    cJSON *snapshot = cJSON_Duplicate(g_document, 1);
    if (!snapshot) { set_error("snapshot grant once"); return 0; }
    for (unsigned int i = 0; i < count; ++i) {
        if (!grant_reward_inner(rewards[i]) || (rewards[i].type == 2 && !activity_add("currency", rewards[i].id, static_cast<uint64_t>(rewards[i].amount)))) { cJSON_Delete(g_document); g_document = snapshot; return 0; }
    }
    if (std::strcmp(key, "tutorial_gacha_result") == 0) { cJSON_Delete(g_document); g_document = snapshot; set_error("tutorial gacha signed-result path requires gacha roster metadata"); return 0; }
    cJSON *copy = cJSON_Duplicate(result, 1);
    if (!copy || !put_item(g_document, key, copy)) { cJSON_Delete(copy); cJSON_Delete(g_document); g_document = snapshot; set_error("store one-time result"); return 0; }
    if (!commit_or_restore(snapshot)) return 0;
    *changed = 1;
    return 1;
}
int store_claim_mail(uint64_t server_id, int all, struct store_reward *rewards, unsigned int *count, int *changed) {
    std::lock_guard<std::recursive_mutex> lock(g_store_mutex);
    if (count) *count = 0;
    if (changed) *changed = 0;
    if (!g_document || !rewards || !count || !changed || (!server_id && !all)) { set_error("invalid mail claim request"); return 0; }
    cJSON *snapshot = cJSON_Duplicate(g_document, 1);
    if (!snapshot) { set_error("snapshot mail claim"); return 0; }
    cJSON *mails = object_item(g_document, "mails");
    if (!cJSON_IsArray(mails)) { cJSON_Delete(snapshot); set_error("mail collection missing"); return 0; }
    char wanted_id[32];
    std::snprintf(wanted_id, sizeof(wanted_id), "%llu", static_cast<unsigned long long>(server_id));
    bool found = false;
    for (cJSON *mail = mails->child; mail; mail = mail->next) {
        if (!cJSON_IsObject(mail)) { cJSON_Delete(g_document); g_document = snapshot; *count = 0; *changed = 0; set_error("invalid mail row"); return 0; }
        if (!all) {
            cJSON *id_item = object_item(mail, "TableId");
            if (!cJSON_IsString(id_item) || !id_item->valuestring || std::strcmp(id_item->valuestring, wanted_id) != 0) continue;
        }
        cJSON *receipt = object_item(mail, "Receipt");
        if (!cJSON_IsNumber(receipt) || receipt->valueint < 0 || receipt->valueint > 3) { cJSON_Delete(g_document); g_document = snapshot; *count = 0; *changed = 0; set_error("invalid mail receipt"); return 0; }
        found = true;
        if (receipt->valueint > 1) continue;
        cJSON *attachments = object_item(mail, "AttachInfos");
        if (!cJSON_IsArray(attachments)) { cJSON_Delete(g_document); g_document = snapshot; *count = 0; *changed = 0; set_error("invalid mail attachment list"); return 0; }
        for (cJSON *attachment = attachments->child; attachment; attachment = attachment->next) {
            cJSON *type_item = object_item(attachment, "Type");
            cJSON *table_item = object_item(attachment, "TableId");
            cJSON *amount_item = object_item(attachment, "Count");
            if (!cJSON_IsNumber(type_item) || !cJSON_IsString(table_item) || !table_item->valuestring || !cJSON_IsString(amount_item) || !amount_item->valuestring || *count >= 512) {
                cJSON_Delete(g_document); g_document = snapshot; *count = 0; *changed = 0; set_error("invalid mail attachment"); return 0;
            }
            const int type = type_item->valueint;
            if (type < 0 || type > 7 || (((1u << static_cast<unsigned int>(type)) & 0xa4u) == 0)) {
                cJSON_Delete(g_document); g_document = snapshot; *count = 0; *changed = 0; set_error("unsupported mail attachment type"); return 0;
            }
            uint64_t table_id = 0;
            if (!valid_unsigned_decimal(table_item->valuestring, &table_id) || !table_id) {
                cJSON_Delete(g_document); g_document = snapshot; *count = 0; *changed = 0; set_error("invalid mail attachment table id"); return 0;
            }
            int64_t amount = 0;
            if (!parse_nonnegative_int64(amount_item->valuestring, &amount) || amount < 1 || amount > std::numeric_limits<int>::max()) {
                cJSON_Delete(g_document); g_document = snapshot; *count = 0; *changed = 0; set_error("invalid mail attachment count"); return 0;
            }
            const store_reward reward{type, table_id, static_cast<int>(amount)};
            if (!grant_reward(reward)) { cJSON_Delete(g_document); g_document = snapshot; *count = 0; *changed = 0; return 0; }
            rewards[*count] = reward;
            ++(*count);
        }
        if (!put_number(mail, "Receipt", 2)) { cJSON_Delete(g_document); g_document = snapshot; *count = 0; *changed = 0; set_error("update mail receipt"); return 0; }
        *changed = 1;
        if (!all) break;
    }
    if (!found) { cJSON_Delete(g_document); g_document = snapshot; *count = 0; *changed = 0; set_error("mail not found"); return 0; }
    if (*changed) {
        if (!commit_or_restore(snapshot)) { *count = 0; *changed = 0; return 0; }
    } else {
        cJSON_Delete(snapshot);
    }
    return 1;
}

int store_queue_prologue_gift(void) {
    std::lock_guard<std::recursive_mutex> lock(g_store_mutex);
    if (!g_document) { set_error("save not loaded for prologue gifts"); return 0; }
    if (!migrate_rune_tier_mail_comments()) return 0;
    if (!queue_gift_atomic("free_time_gifts_v03", 900017, "自由時間禮物補給", "全部6種通用禮物與24種專屬禮物，各999999個。", kFreeTimeGift, sizeof(kFreeTimeGift) / sizeof(kFreeTimeGift[0]))) return 0;
    if (!migrate_rune_tier_gifts() || !restore_prologue_vignette_once()) return 0;
    if (!queue_gift_atomic("prologue_gift_queued", 900001, "序章通关赠礼", "99999 流星石、精選扭蛋幣与扭蛋幣各 99999、星光底片 99999。每个本地账号领取一次。", kPrologueGift, sizeof(kPrologueGift) / sizeof(kPrologueGift[0]))) return 0;
    if (!queue_gift_atomic("growth_gift_queued", 900002, "角色养成与体力补给", "通用養成素材與魔法師卡牌各999999，魔法師EXP999999000，體力藥9999，不贈送奧爾晶球。", kGrowthGift, sizeof(kGrowthGift) / sizeof(kGrowthGift[0]))) return 0;
    if (!queue_gift_atomic("custom_boxes_gift_queued", 900003, "全角色・全神器・全膠片自選補給", "三種自選箱各9999個。背包中使用後選擇物件；重複角色與膠片沿用扭蛋轉化規則。", kCustomBoxesGift, sizeof(kCustomBoxesGift) / sizeof(kCustomBoxesGift[0]))) return 0;
    if (!queue_gift_atomic("account_level_items_gift_queued", 900004, "帳號升級經驗券補給", "帳號升級經驗券999張。每張提升帳號一級並保留已有經驗，最高99級；99級不能使用。", kAccountCouponsGift, sizeof(kAccountCouponsGift) / sizeof(kAccountCouponsGift[0]))) return 0;
    if (!queue_gift_atomic("oracle_wheel_gift_queued", 900008, "大神殿補給：神託機的菊輪", "贈送神託機的菊輪1000個，用於大神殿強化。每個存檔領取一次。", kOracleWheelGift, sizeof(kOracleWheelGift) / sizeof(kOracleWheelGift[0]))) return 0;
    if (!expanded_gift_v3()) return 0;
    if (!queue_gift_atomic("character_cards_gift_v4", 900009, "全角色魔法師卡牌專項補給", "原道具表全部22種魔法師卡牌各999999張，依角色原始映射用於魔力節點。與其他初始郵件同時自動出現；舊存檔也可領取一次。不贈送奧爾晶球。", kCharacterCardsGift, sizeof(kCharacterCardsGift) / sizeof(kCharacterCardsGift[0]))) return 0;
    if (!expanded_character_exp_gift_v5()) return 0;
    if (!queue_gift_atomic("shop_voucher_gift_v01", 900011, "商店憑證補給", "游界憑證、上級游界憑證、記憶憑證各999999。與其他初始補給同時出現，每個帳號僅可領取一次。", kShopVoucherGift, sizeof(kShopVoucherGift) / sizeof(kShopVoucherGift[0]))) return 0;
    return 1;
}

int store_seed_defaults(const struct store_reward *rewards, unsigned int count) {
    std::lock_guard<std::recursive_mutex> lock(g_store_mutex);
    if (!g_document || !rewards || count > 64) { set_error("invalid default parcel rewards"); return 0; }
    if (cJSON_IsTrue(object_item(g_document, "default_parcels_initialized"))) return 1;
    cJSON *snapshot = cJSON_Duplicate(g_document, 1);
    if (!snapshot) { set_error("snapshot default parcel initialization"); return 0; }
    for (unsigned int i = 0; i < count; ++i) if (!grant_reward_inner(rewards[i])) { cJSON_Delete(g_document); g_document = snapshot; return 0; }
    cJSON *recruits = object_item(g_document, "recruits");
    cJSON *characters = object_item(g_document, "characters");
    if (cJSON_IsArray(recruits) && cJSON_GetArraySize(recruits) == 0 && cJSON_IsArray(characters) && cJSON_GetArraySize(characters) >= 1) {
        cJSON *recruit = cJSON_CreateObject();
        cJSON *char_ids = cJSON_CreateArray();
        cJSON *vignette_ids = cJSON_CreateArray();
        cJSON *first = cJSON_GetArrayItem(characters, 0);
        cJSON *table_id = object_item(first, "TableId");
        cJSON *first_copy = cJSON_IsString(table_id) && table_id->valuestring ? cJSON_Duplicate(table_id, 1) : cJSON_CreateString("");
        bool ok = recruit && char_ids && vignette_ids && first_copy && put_number(recruit, "Index", 1) && put_string(recruit, "Name", "");
        if (ok) { ok = cJSON_AddItemToArray(char_ids, first_copy) != 0; if (ok) first_copy = nullptr; }
        for (int i = 0; ok && i < 3; ++i) { cJSON *empty = cJSON_CreateString(""); ok = empty && cJSON_AddItemToArray(char_ids, empty); if (!ok) cJSON_Delete(empty); }
        for (int i = 0; ok && i < 3; ++i) { cJSON *empty = cJSON_CreateString(""); ok = empty && cJSON_AddItemToArray(vignette_ids, empty); if (!ok) cJSON_Delete(empty); }
        if (ok) { ok = cJSON_AddItemToObject(recruit, "CharacterTableIds", char_ids) != 0; if (ok) char_ids = nullptr; }
        if (ok) { ok = cJSON_AddItemToObject(recruit, "VignetteTableIds", vignette_ids) != 0; if (ok) vignette_ids = nullptr; }
        if (ok) ok = cJSON_AddItemToArray(recruits, recruit) != 0;
        if (!ok) { cJSON_Delete(first_copy); cJSON_Delete(char_ids); cJSON_Delete(vignette_ids); cJSON_Delete(recruit); cJSON_Delete(g_document); g_document = snapshot; set_error("create default recruit parcel"); return 0; }
    }
    if (!put_bool(g_document, "default_parcels_initialized", true)) { cJSON_Delete(g_document); g_document = snapshot; return 0; }
    return commit_or_restore(snapshot);
}
}
