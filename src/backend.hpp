#pragma once
#include "json.hpp"
#include <map>
#include <mutex>
#include <set>
#include <string>
#include <vector>

namespace astrae {
using json::Value;
class Backend {
public:
    static Backend &instance();
    int init(const std::string &save_dir, const std::string &table_dir);
    void shutdown();
    std::string handle(const std::string &protocol, const std::string &request_json);
    std::string state_json();
    int replace_state(const std::string &state_json);
    std::string diagnostics_json();
    std::string protocol_catalog_json();
    std::string validate_request_json(const std::string &protocol, const std::string &request_json);
private:
    Backend()=default;
    bool initialized_=false;
    std::string save_dir_, table_dir_, save_path_;
    Value state_=Value::object(), rules_=Value::object();
    std::map<std::string,Value> schemas_, tables_;
    std::set<std::string> request_names_;
    std::map<std::string,std::string> protocol_routes_;
    std::vector<std::string> response_names_;
    size_t table_row_count_=0;
    std::string last_store_recovery_;
    bool save_data_present_but_unusable_=false;
    std::mutex mutex_;
    bool load_tables();
    bool load_schemas();
    bool load_protocols();
    std::string classify_protocol(const std::string &name) const;
    bool normalize_state();
    bool valid_state(const Value &candidate, std::string *reason=nullptr) const;
    bool load_rules();
    bool load_store();
    bool commit_store();
    void seed_defaults();
    Value schema_default(const std::string &name, int depth=0) const;
    Value response_base(const std::string &name) const;
    Value updated_parcels(const Value &changed) const;
    Value &arr(const std::string &key);
    Value &obj(const std::string &key);
    Value *find_id(Value &list, const std::string &field, uint64_t id);
    const Value *table_row(const std::string &table, const std::string &id_field, uint64_t id) const;
    std::vector<Value> rewards_from(const Value &req, const std::string &operation) const;
    bool grant(const Value &reward, Value *changed=nullptr);
    bool spend(const Value &cost, Value *changed=nullptr);
    bool spend_by_table_id(uint64_t table_id, int64_t count, Value *changed=nullptr);
    int64_t common_i64(const std::string &key, int64_t fallback) const;
    bool grant_batch(const std::vector<Value> &rewards, Value *changed=nullptr);
    bool spend_batch(const std::vector<Value> &costs, Value *changed=nullptr);
    Value handle_account(const std::string &op, const Value &req, Value response, bool &changed);
    Value handle_growth(const std::string &op, const Value &req, Value response, bool &changed);
    Value handle_artifact(const std::string &op, const Value &req, Value response, bool &changed);
    Value handle_gacha(const std::string &op, const Value &req, Value response, bool &changed);
    Value handle_runes(const std::string &op, const Value &req, Value response, bool &changed);
    Value handle_mail(const std::string &op, const Value &req, Value response, bool &changed);
    Value handle_quest(const std::string &op, const Value &req, Value response, bool &changed);
    Value handle_farming(const std::string &op, const Value &req, Value response, bool &changed);
    Value handle_raid(const std::string &op, const Value &req, Value response, bool &changed);
    Value handle_missions(const std::string &op, const Value &req, Value response, bool &changed);
    Value handle_date(const std::string &op, const Value &req, Value response, bool &changed);
    Value handle_installation(const std::string &op, const Value &req, Value response, bool &changed);
    Value handle_shop(const std::string &op, const Value &req, Value response, bool &changed);
    Value handle_misc(const std::string &op, const Value &req, Value response, bool &changed);
    void fill_account_response(Value &response) const;
    void fill_updated_parcels(Value &response, const Value &changed) const;
    Value reward_total(const std::vector<Value> &rewards) const;
    std::string make_key(uint64_t a, uint64_t b=0) const;
};
}
