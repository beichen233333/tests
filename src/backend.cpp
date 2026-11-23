#include "backend.hpp"
#include "embedded_assets.hpp"
#include "legacy_save_migration.hpp"
#include "debug_log.hpp"
#include <algorithm>
#include <cerrno>
#include <chrono>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <ctime>
#include <dirent.h>
#include <fcntl.h>
#include <sys/stat.h>
#include <sys/types.h>
#include <unistd.h>
#include <unordered_set>

namespace astrae {
using J=json::Value;
static std::string join_path(const std::string &a,const std::string &b){return a.empty()?b:(a.back()=='/'?a+b:a+"/"+b);}
static bool mkdirs(const std::string &p){
    if(p.empty())return false;
    std::string cur;
    for(size_t i=0;i<p.size();++i){cur.push_back(p[i]);if(p[i]=='/'&&cur.size()>1){if(mkdir(cur.c_str(),0700)!=0&&errno!=EEXIST)return false;}}
    if(mkdir(p.c_str(),0700)!=0&&errno!=EEXIST)return false;
    struct stat st{};
    return stat(p.c_str(),&st)==0&&S_ISDIR(st.st_mode);
}
static bool read_file(const std::string &p,std::string &out,size_t limit=32*1024*1024){FILE *f=std::fopen(p.c_str(),"rb");if(!f){if(!assets::read_embedded_asset(p,out))return false;return out.size()<=limit;}if(std::fseek(f,0,SEEK_END)!=0){std::fclose(f);return false;}long n=std::ftell(f);if(n<0||size_t(n)>limit){std::fclose(f);return false;}std::rewind(f);out.resize(size_t(n));size_t got=out.empty()?0:std::fread(out.data(),1,out.size(),f);std::fclose(f);if(got!=out.size()){out.resize(got);return false;}return true;}
static std::string trim_protocol(std::string p){auto pos=p.rfind(".Request");if(pos!=std::string::npos&&pos+8==p.size())p.resize(pos);return p;}
static std::string str(const J &v,const char *k,const std::string &d={}){return v[k].as_string(d);}
static uint64_t u64(const J &v,const char *k,uint64_t d=0){return v[k].as_u64(d);} static uint64_t u64(const J &v,uint64_t d=0){return v.as_u64(d);}
static int64_t i64(const J &v,const char *k,int64_t d=0){return v[k].as_i64(d);} static bool boolean(const J &v,const char *k,bool d=false){return v.contains(k)?v[k].as_bool(d):d;}
static J j_obj(){return J::object();} static J j_arr(){return J::array();}
static bool id_equal(const J &v,uint64_t id){return v.as_u64(UINT64_MAX)==id||v.as_string()==std::to_string(id);}
static J make_reward(int type,uint64_t id,int64_t count){J r=j_obj();r["Type"]=type;r["TableId"]=id;r["Count"]=count;r["AddedCount"]=count;r["RewardFlag"]=0;return r;}
static int64_t now_sec(){return static_cast<int64_t>(std::time(nullptr));}
static J &ensure_obj(J &v,const std::string &k){if(!v[k].is_object())v[k]=j_obj();return v[k];}
static J &ensure_arr(J &v,const std::string &k){if(!v[k].is_array())v[k]=j_arr();return v[k];}
static J default_account(){J a=j_obj();a["ServerId"]=uint64_t(1);a["UserName"]="Offline Player";a["NickName"]="Offline Player";a["Level"]=1;a["Exp"]=0;a["GameAccountId"]="";a["GameLocale"]=0;return a;}
struct RuneStockRule { uint64_t id; int32_t limit; };
#include "../compat/src/rune_stock_rules.inc"
static int32_t rune_stock_limit(uint64_t id){for(const auto &rule:kRuneStockRules)if(rule.id==id)return rule.limit;return 0;}
static constexpr int64_t kAccountLevelExp[99]={32,36,40,42,48,52,56,60,64,68,72,76,80,84,88,92,96,100,104,108,112,116,120,124,128,132,136,140,144,150,150,150,150,150,150,150,150,150,150,150,150,150,150,150,150,150,150,500000,500000,500000,500000,500000,500000,500000,500000,500000,500000,500000,500000,500000,500000,500000,500000,500000,500000,500000,500000,500000,500000,500000,500000,500000,500000,500000,500000,500000,500000,500000,500000,500000,500000,500000,500000,500000,500000,500000,500000,500000,500000,500000,500000,500000,500000,500000,500000,500000,500000,500000};
static J make_default_character(uint64_t id,uint64_t serverId){J c=j_obj();c["TableId"]=id;c["ServerId"]=serverId;c["Level"]=1;c["Exp"]=0;c["Star"]=0;c["Potential"]=uint64_t(0);c["Favorite"]=false;c["Locked"]=false;c["Signed"]=false;c["Buffs"]=j_arr();c["Runes"]=j_arr();c["Skills"]=j_arr();for(int i=0;i<8;i++){J skill=j_obj();skill["Level"]=1;c["Skills"].push_back(skill);}return c;}
static J make_default_vignette(uint64_t id,uint64_t serverId){J v=j_obj();v["TableId"]=id;v["ServerId"]=serverId;v["Star"]=0;return v;}
static bool list_has_id(const J &a,const std::string &key,uint64_t id){for(const auto &x:a.as_array())if(id_equal(x[key],id))return true;return false;}
static uint64_t make_uid(const J &state){uint64_t n=u64(state,"NextServerId",100000);return n;}
Backend &Backend::instance(){static Backend b;return b;}
int Backend::init(const std::string &save_dir,const std::string &table_dir){
    std::lock_guard<std::mutex> lk(mutex_);
    if(initialized_){ASTRAE_LOGD("Backend::init skipped; backend is already initialized");return 1;}
    save_dir_=save_dir.empty()?"/storage/emulated/0/Android/data/com.nc.astraeoratio.singleplayer/files/AstraeOffline/SinglePlayer":save_dir;
    table_dir_=table_dir.empty()?"assets":table_dir;
    save_path_=join_path(save_dir_,"save.json");
    ASTRAE_LOGI("Backend init: save_dir=%s table_dir=%s",save_dir_.c_str(),table_dir_.c_str());
    if(!mkdirs(save_dir_)){ASTRAE_LOGE("Backend cannot create save directory %s: errno=%d (%s)",save_dir_.c_str(),errno,std::strerror(errno));return 0;}
    ASTRAE_LOGI("Backend save directory ready");
    if(!load_schemas()){ASTRAE_LOGE("Backend load_schemas failed from %s/schema/wire_shapes.json or embedded assets",table_dir_.c_str());return 0;}
    ASTRAE_LOGI("Backend schemas loaded: %zu",schemas_.size());
    if(!load_protocols()){ASTRAE_LOGE("Backend load_protocols failed from %s/schema/protocol_index.json or embedded assets",table_dir_.c_str());return 0;}
    ASTRAE_LOGI("Backend protocol catalog loaded: %zu",protocol_routes_.size());
    load_tables();ASTRAE_LOGI("Backend embedded/external tables loaded");
    load_rules();ASTRAE_LOGI("Backend rules loaded");
    if(!load_store()){
        if(save_data_present_but_unusable_){ASTRAE_LOGE("Backend found existing save data but could not safely load or migrate it; refusing to create a new account. Existing files were retained. recovery=%s",last_store_recovery_.c_str());return 0;}
        ASTRAE_LOGW("Backend found no existing save; creating the first local profile at %s",save_path_.c_str());seed_defaults();if(!commit_store()){ASTRAE_LOGE("Backend could not commit new default save at %s: errno=%d (%s)",save_path_.c_str(),errno,std::strerror(errno));return 0;}
    }
    // Do not synthesize a starter character or tutorial completion for an imported
    // compatibility profile. New profiles already receive defaults in seed_defaults().
    // A legacy profile with no characters is a valid pre-tutorial state and must stay empty.
    bool startup_changed=false;
    if(!state_["TutorialInfo"].is_object())state_["TutorialInfo"]=j_obj();
    if(!state_["characters"].is_array())state_["characters"]=j_arr();
    const bool has_characters=state_["characters"].size()>0;
    if(!state_["LobbyCharacterInfo"].is_object())state_["LobbyCharacterInfo"]=schema_default("LobbyCharacterInfo");
    for(const char *field:{"TableIds_Day","TableIds_Night"}){
        if(!state_["LobbyCharacterInfo"][field].is_array()){state_["LobbyCharacterInfo"][field]=j_arr();startup_changed=true;}
        if(has_characters&&state_["LobbyCharacterInfo"][field].size()==0){state_["LobbyCharacterInfo"][field].push_back(state_["characters"][0]["TableId"].as_u64());startup_changed=true;}
    }
    if(has_characters&&!state_.contains("LobbyCharacterId")){state_["LobbyCharacterId"]=state_["characters"][0]["TableId"].as_u64();startup_changed=true;}
    if(!state_["formations"].is_array()){state_["formations"]=j_arr();startup_changed=true;}
    if(has_characters&&state_["formations"].size()==0){
        const uint64_t lobby_character_id=state_["characters"][0]["TableId"].as_u64();
        J formation=j_obj();formation["Index"]=int64_t(0);formation["CharacterTableIds"]=j_arr();formation["CharacterTableIds"].push_back(lobby_character_id);formation["VignetteTableIds"]=j_arr();formation["Name"]="Formation 1";state_["formations"].push_back(std::move(formation));startup_changed=true;
    }
    if(startup_changed&&!commit_store())return 0;
    initialized_=true;ASTRAE_LOGI("Backend initialized: save=%s characters=%zu formations=%zu",save_path_.c_str(),state_["characters"].size(),state_["formations"].size());
    return 1;
}
void Backend::shutdown(){std::lock_guard<std::mutex> lk(mutex_);if(initialized_)commit_store();initialized_=false;}
bool Backend::load_schemas(){
    schemas_.clear();
    std::string s,p=join_path(join_path(table_dir_,"schema"),"wire_shapes.json");
    if(!read_file(p,s))p=join_path(table_dir_,"wire_shapes.json");
    if(!read_file(p,s))return false;
    try{J root=json::parse(s);if(!root.is_array())return false;for(const auto &shape:root.as_array()){std::string name=str(shape,"name");if(!name.empty()&&shape["fields"].is_array())schemas_[name]=shape;}return !schemas_.empty();}catch(...){return false;}
}
std::string Backend::classify_protocol(const std::string &op) const {
    if(op.rfind("Account",0)==0||op.rfind("Dev",0)==0||op=="RegisterAccountName"||op=="Encryption"||op=="TutorialProgress")return "account";
    if(op.rfind("Character",0)==0||op=="CostumeEquip"||op=="VignetteStarRankUp")return "growth";
    if(op.rfind("Artifact",0)==0)return "artifact";
    if(op.rfind("Gacha",0)==0)return "gacha";
    if(op.rfind("Rune",0)==0||op=="RunesGrant")return "rune";
    if(op.find("Mail")!=std::string::npos||op=="ReceiveAllMail"||op=="ReceiveMailByUids"||op=="ReceivedMail")return "mail";
    if(op.rfind("ScenarioPack",0)==0)return "quest";
    if(op.rfind("FarmingDungeon",0)==0)return "farming";
    if(op.rfind("Raid",0)==0)return "raid";
    if(op.find("Mission")!=std::string::npos||op.rfind("GuideBook",0)==0||op.rfind("Daily",0)==0||op.rfind("Weekly",0)==0||op.rfind("Limited",0)==0||op.rfind("Challenge",0)==0)return "mission";
    if(op.rfind("DateField",0)==0)return "date";
    if(op.rfind("Installation",0)==0||op.rfind("Barracks",0)==0||op.rfind("BuffInstallation",0)==0)return "installation";
    if(op.rfind("Shop",0)==0)return "shop";
    if(op=="AttendanceReward"||op=="CardBookReward"||op=="ItemUseConsumable"||op=="ItemUseSelectable"||op=="FormationChange"||op=="FormationChangeName"||op=="LobbySetBackground"||op=="LobbySetCharacter"||op=="ViewHelpBook"||op=="TutorialContentView"||op=="LogTest")return "misc";
    return "unmapped";
}
bool Backend::load_protocols(){
    request_names_.clear();response_names_.clear();protocol_routes_.clear();
    std::string s,p=join_path(join_path(table_dir_,"schema"),"protocol_index.json");
    if(!read_file(p,s))p=join_path(table_dir_,"protocol_index.json");
    if(!read_file(p,s))return false;
    try{
        J root=json::parse(s);
        if(!root["requests"].is_array()||!root["responses"].is_array())return false;
        for(const auto &x:root["requests"].as_array()){
            std::string name=x.as_string();if(name.empty())continue;
            request_names_.insert(name);
            protocol_routes_[name]=classify_protocol(name);
        }
        for(const auto &x:root["responses"].as_array()){std::string name=x.as_string();if(!name.empty())response_names_.push_back(name);}
        return !request_names_.empty();
    }catch(...){return false;}
}
bool Backend::load_tables(){
    tables_.clear();table_row_count_=0;
    const char *files[]={"embedded_01_raid_datefield_character.json","embedded_02_guide_quests_missions.json","embedded_03_scenario_pack_puzzle.json","embedded_04_installation_seed.json","embedded_05_node_event_seed.json","embedded_06_result_grade.json","embedded_07_buff_installation_seed.json","embedded_08_open_slots.json","embedded_09_empty.json"};
    std::map<std::string,std::unordered_set<std::string>> seen_rows;
    for(auto f:files){std::string s;if(!read_file(join_path(join_path(table_dir_,"tables"),f),s))continue;try{J root=json::parse(s);if(!root.is_object())continue;for(auto &kv:root.as_object()){if(!kv.second.is_array())continue;if(!tables_[kv.first].is_array())tables_[kv.first]=j_arr();auto &seen=seen_rows[kv.first];for(const auto &row:kv.second.as_array()){const std::string fingerprint=json::dump(row,false);if(!seen.insert(fingerprint).second)continue;tables_[kv.first].push_back(row);++table_row_count_;}}}catch(...){}}
    return !tables_.empty();
}
bool Backend::load_rules(){rules_=j_obj();std::string s;if(read_file(join_path(table_dir_,"rules.json"),s)){try{J v=json::parse(s);if(v.is_object()){rules_=std::move(v);return true;}}catch(...){}}return false;}
void Backend::seed_defaults(){state_=j_obj();state_["SchemaVersion"]=3;state_["Account"]=default_account();state_["inventory"]=j_arr();state_["characters"]=j_arr();state_["vignettes"]=j_arr();state_["artifacts"]=j_arr();state_["runes"]=j_arr();state_["rune_stock"]=j_arr();state_["tickets"]=j_arr();state_["costumes"]=j_arr();state_["mails"]=j_arr();state_["ReceiptInfos"]=j_arr();state_["quests"]=j_obj();state_["formations"]=j_arr();state_["gacha_state"]=j_obj();state_["GachaAdjustPointInfos"]=j_arr();state_["RuneGrantInfos"]=j_arr();state_["ClaimedMailIds"]=j_arr();state_["ClaimedGuideMissionTaskIds"]=j_arr();state_["ClaimedFavorRewardIds"]=j_arr();state_["TutorialInfo"]=j_obj();state_["TutorialInfo"]["InitialIndex"]=int64_t(0);state_["TutorialInfo"]["ContentBits"]=uint64_t(0);state_["Progress"]=j_obj();state_["Installations"]=j_arr();state_["DateFieldState"]=j_obj();state_["DateFieldInfo"]=schema_default("DateFieldInfo");state_["DateFieldNodeInfos"]=j_arr();state_["FavorInfos"]=j_arr();state_["DateFieldNodeEventInfos"]=j_arr();state_["CollectionArtifactInfo"]=schema_default("CollectionArtifactInfo");state_["LobbyCharacterInfo"]=schema_default("LobbyCharacterInfo");state_["LobbyBackgroundId"]=uint64_t(30001);state_["LobbyCharacterInfo"]["TableIds_Day"]=j_arr();state_["LobbyCharacterInfo"]["TableIds_Day"].push_back(uint64_t(10001));state_["LobbyCharacterInfo"]["TableIds_Night"]=j_arr();state_["LobbyCharacterInfo"]["TableIds_Night"].push_back(uint64_t(10001));state_["LobbyCharacterId"]=uint64_t(10001);state_["WishlistInfo"]=schema_default("WishlistInfo");state_["RecruitInfos"]=j_arr();state_["ScenarioPackMiniInfos"]=j_arr();state_["ScenarioPackDetailInfos"]=j_arr();state_["ShopItemInfos"]=j_arr();state_["ServerOptionInfo"]=schema_default("ServerOptionInfo");state_["DailyState"]=j_obj();state_["GuideState"]=j_obj();state_["PackState"]=j_obj();state_["formations"]=j_arr();J starter=make_default_character(10001,100000);state_["characters"].push_back(std::move(starter));J formation=j_obj();formation["Index"]=int64_t(0);formation["CharacterTableIds"]=j_arr();formation["CharacterTableIds"].push_back(uint64_t(10001));formation["VignetteTableIds"]=j_arr();formation["Name"]="Formation 1";state_["formations"].push_back(std::move(formation));state_["NextServerId"]=uint64_t(100001);state_["StoreRevision"]=uint64_t(0);}
bool Backend::normalize_state(){
    if(!state_.is_object())return false;
    if(!state_["Account"].is_object())state_["Account"]=default_account();
    for(auto k:{"inventory","characters","vignettes","artifacts","runes","rune_stock","tickets","costumes","mails","ReceiptInfos","GachaAdjustPointInfos","RuneGrantInfos","ClaimedMailIds","ClaimedGuideMissionTaskIds","ClaimedFavorRewardIds","formations","Installations","DateFieldNodeInfos","FavorInfos","DateFieldNodeEventInfos","RecruitInfos","ScenarioPackMiniInfos","ScenarioPackDetailInfos","ShopItemInfos"})if(!state_[k].is_array())state_[k]=j_arr();
    for(auto k:{"quests","gacha_state","TutorialInfo","Progress","DateFieldState","DailyState","GuideState","PackState"})if(!state_[k].is_object())state_[k]=j_obj();
    if(!state_["TutorialInfo"].contains("InitialIndex"))state_["TutorialInfo"]["InitialIndex"]=int64_t(0);
    if(!state_["TutorialInfo"].contains("ContentBits"))state_["TutorialInfo"]["ContentBits"]=uint64_t(0);
    if(!state_["DateFieldInfo"].is_object())state_["DateFieldInfo"]=schema_default("DateFieldInfo");
    if(!state_["CollectionArtifactInfo"].is_object())state_["CollectionArtifactInfo"]=schema_default("CollectionArtifactInfo");
    if(!state_["LobbyCharacterInfo"].is_object())state_["LobbyCharacterInfo"]=schema_default("LobbyCharacterInfo");
    if(!state_["WishlistInfo"].is_object())state_["WishlistInfo"]=schema_default("WishlistInfo");
    if(!state_["ServerOptionInfo"].is_object())state_["ServerOptionInfo"]=schema_default("ServerOptionInfo");
    if(!state_.contains("LobbyBackgroundId"))state_["LobbyBackgroundId"]=uint64_t(30001);
    if(!state_.contains("NextServerId"))state_["NextServerId"]=uint64_t(100000);
    if(!state_.contains("StoreRevision"))state_["StoreRevision"]=uint64_t(0);
    if(!state_["Account"].contains("UserName"))state_["Account"]["UserName"]="Offline Player";
    if(!state_["Account"].contains("NickName"))state_["Account"]["NickName"]=str(state_["Account"],"UserName","Offline Player");
    if(!state_["Account"].contains("Level"))state_["Account"]["Level"]=1;
    if(!state_["Account"].contains("Exp"))state_["Account"]["Exp"]=0;
    state_["SchemaVersion"]=3;
    return valid_state(state_);
}
bool Backend::valid_state(const J &candidate,std::string *reason) const {
    auto fail=[&](const char *why){if(reason)*reason=why;return false;};
    if(!candidate.is_object())return fail("root must be object");
    if(!candidate["Account"].is_object())return fail("Account must be object");
    if(!candidate["inventory"].is_array())return fail("inventory must be array");
    for(const auto &item:candidate["inventory"].as_array()){
        if(!item.is_object())return fail("inventory entry must be object");
        const int64_t type=item["Type"].as_i64(-1);const uint64_t id=item["TableId"].as_u64();
        if(type==2){if(id>=8)return fail("currency TableId must be in [0,7]");}
        else if(!id)return fail("non-currency inventory TableId must be positive");
        if(type<0)return fail("inventory Type must be non-negative");
        int64_t count=item["Count"].as_i64(-1);if(count<0)return fail("inventory Count must be non-negative");
    }
    return true;
}
bool Backend::load_store(){
    last_store_recovery_="none";save_data_present_but_unusable_=false;
    auto exists=[](const std::string &path){return access(path.c_str(),F_OK)==0;};
    auto parse_file=[](const std::string &path,J &out)->bool{std::string text;if(!read_file(path,text,16*1024*1024))return false;try{out=json::parse(text);return out.is_object();}catch(...){return false;}};
    auto try_backend=[&](const std::string &path)->bool{
        J loaded;if(!parse_file(path,loaded)||loaded.contains("schema_version"))return false;
        J previous=state_;state_=std::move(loaded);
        if(normalize_state())return true;
        state_=std::move(previous);return false;
    };
    auto archive_primary=[&]()->bool{
        if(!exists(save_path_))return true;
        std::string archive=save_path_+".invalid";
        if(exists(archive))archive+="."+std::to_string(now_sec());
        if(rename(save_path_.c_str(),archive.c_str())!=0){ASTRAE_LOGE("cannot preserve invalid primary save %s: errno=%d (%s)",save_path_.c_str(),errno,std::strerror(errno));return false;}
        ASTRAE_LOGW("preserved unreadable primary save as %s",archive.c_str());return true;
    };
    // Detect the placeholder v3 profile created by the earlier broken bootstrap.
    // If a valid schema-v1 compat profile exists, it is the real historical save and
    // must replace that empty placeholder once. Meaningful native progress always wins.
    auto is_placeholder=[](const J &candidate)->bool {
        // Only the exact untouched seed emitted by the old broken bootstrap may be
        // replaced by a historical compat save. Any player-visible customization
        // (name, character, formation, lobby selection, tutorial, inventory, etc.)
        // makes this a real native save and therefore gives it priority.
        if(!candidate.is_object()||candidate["LegacySaveImported"].as_bool())return false;
        const J &account=candidate["Account"],&tutorial=candidate["TutorialInfo"];
        if(!account.is_object()||!tutorial.is_object())return false;
        if(account["UserName"].as_string()!="Offline Player"||account["NickName"].as_string("Offline Player")!="Offline Player")return false;
        if(!account["GameAccountId"].as_string().empty()||account["ServerId"].as_u64(1)!=1||account["Level"].as_i64(1)!=1||account["Exp"].as_i64()!=0)return false;
        if(tutorial["ContentBits"].as_u64()!=0||tutorial["InitialIndex"].as_i64()!=0)return false;
        for(const char *key:{"inventory","vignettes","artifacts","mails","tickets","costumes","rune_stock","ReceiptInfos","GachaAdjustPointInfos","RuneGrantInfos","ClaimedMailIds","ClaimedGuideMissionTaskIds","ClaimedFavorRewardIds","Installations","DateFieldNodeInfos","FavorInfos","DateFieldNodeEventInfos","ScenarioPackMiniInfos","ScenarioPackDetailInfos","ShopItemInfos"})if(!candidate[key].is_array()||candidate[key].size()!=0)return false;
        if(!candidate["quests"].is_object()||!candidate["quests"].as_object().empty())return false;
        if(!candidate["gacha_state"].is_object()||!candidate["gacha_state"].as_object().empty())return false;
        if(!candidate["DailyState"].is_object()||!candidate["DailyState"].as_object().empty())return false;
        if(!candidate["GuideState"].is_object()||!candidate["GuideState"].as_object().empty())return false;
        if(!candidate["PackState"].is_object())return false;
        if(!candidate["PackState"].as_object().empty()&&(!candidate["PackState"]["Packs"].is_object()||!candidate["PackState"]["Packs"].as_object().empty()))return false;
        const J &characters=candidate["characters"];
        if(!characters.is_array()||characters.size()!=1)return false;
        const J &c=characters[0];
        if(c["TableId"].as_u64()!=10001||c["ServerId"].as_u64()!=100000||c["Level"].as_i64(1)!=1||c["Exp"].as_i64()!=0||c["Star"].as_i64()!=0||c["Potential"].as_u64()!=0||c["Favorite"].as_bool()||c["Locked"].as_bool()||c["Signed"].as_bool())return false;
        for(const char *key:{"Buffs","Runes"})if(!c[key].is_array()||c[key].size()!=0)return false;
        if(!c["Skills"].is_array()||c["Skills"].size()!=8)return false;
        for(const auto &skill:c["Skills"].as_array())if(skill["Level"].as_i64(1)!=1)return false;
        if(candidate["LobbyCharacterId"].as_u64()!=10001||candidate["LobbyBackgroundId"].as_u64()!=30001)return false;
        const J &lobby=candidate["LobbyCharacterInfo"];
        auto one_id=[](const J &value,uint64_t id){return value.is_array()&&value.size()==1&&value[0].as_u64()==id;};
        if(!lobby.is_object()||!one_id(lobby["TableIds_Day"],10001)||!one_id(lobby["TableIds_Night"],10001))return false;
        const J &formations=candidate["formations"];
        if(!formations.is_array()||formations.size()!=1)return false;
        const J &formation=formations[0];
        if(formation["Index"].as_i64(-1)!=0||formation["Name"].as_string()!="Formation 1"||!one_id(formation["CharacterTableIds"],10001)||!formation["VignetteTableIds"].is_array()||formation["VignetteTableIds"].size()!=0)return false;
        if(candidate["Progress"].is_object()&&candidate["Progress"]["UnlockInfos"].is_array()&&candidate["Progress"]["UnlockInfos"].size()!=0)return false;
        return true;
    };
    bool placeholder_primary=false;
    if(exists(save_path_)){
        save_data_present_but_unusable_=true;J native;
        if(parse_file(save_path_,native)&&!native.contains("schema_version")){
            J previous=state_;state_=native;const bool native_valid=normalize_state();if(!native_valid)state_=std::move(previous);else placeholder_primary=is_placeholder(state_);
        }
    }
    bool compat_candidate_found=false;
    auto try_compat_migration=[&]()->bool {
        const std::string compat_dir=join_path(save_dir_,"compat-store");
        const std::string candidates[]={join_path(compat_dir,"save.json"),join_path(compat_dir,"save.json.bak"),join_path(compat_dir,"save.json.tmp")};
        for(const auto &path:candidates){
            if(!exists(path))continue;
            J legacy;if(!parse_file(path,legacy)){compat_candidate_found=true;save_data_present_but_unusable_=true;continue;}
            if(save_migration::is_pristine_new_profile(legacy)){ASTRAE_LOGI("compat Store contains only the untouched new_local_profile seed; no historical save to import: %s",path.c_str());continue;}
            compat_candidate_found=true;save_data_present_but_unusable_=true;
            J migrated;std::string migration_error;
            if(!save_migration::migrate_compat_document(legacy,migrated,&migration_error)){ASTRAE_LOGW("legacy save candidate rejected without modification: path=%s reason=%s",path.c_str(),migration_error.c_str());continue;}
            migrated["LegacySaveSource"]=path;J previous=state_;state_=std::move(migrated);
            if(!normalize_state()){state_=std::move(previous);ASTRAE_LOGE("legacy save migration produced invalid native state; original retained: %s",path.c_str());continue;}
            if(placeholder_primary){std::string archive=save_path_+".placeholder";if(exists(archive))archive+="."+std::to_string(now_sec());if(rename(save_path_.c_str(),archive.c_str())!=0){ASTRAE_LOGE("cannot archive old placeholder save %s: errno=%d (%s)",save_path_.c_str(),errno,std::strerror(errno));state_=std::move(previous);return false;}ASTRAE_LOGW("archived previous synthetic placeholder profile as %s before importing historical compat save",archive.c_str());}
            else if(!archive_primary()){state_=std::move(previous);return false;}
            if(!commit_store()){ASTRAE_LOGE("legacy save mapped but native commit failed; source retained at %s",path.c_str());state_=std::move(previous);return false;}
            last_store_recovery_=placeholder_primary?"compat-store-imported-over-placeholder":"compat-store-migrated";save_data_present_but_unusable_=false;
            ASTRAE_LOGI("legacy compatibility save migrated: source=%s characters=%zu inventory=%zu tutorial_bits=%llu initial_index=%lld; original compat file retained",path.c_str(),state_["characters"].size(),state_["inventory"].size(),static_cast<unsigned long long>(state_["TutorialInfo"]["ContentBits"].as_u64()),static_cast<long long>(state_["TutorialInfo"]["InitialIndex"].as_i64()));return true;
        }
        return false;
    };
    // Native v3 always wins unless it is the exact empty seed emitted by an older
    // bootstrap. In that one case import the real v1 compat save before accepting it.
    if(placeholder_primary&&try_compat_migration())return true;
    if(placeholder_primary&&compat_candidate_found){last_store_recovery_="compat-save-invalid-retained";save_data_present_but_unusable_=true;ASTRAE_LOGE("a historical compatibility save exists but cannot be imported; refusing to fall back to the previous placeholder profile");return false;}
    if(exists(save_path_)){save_data_present_but_unusable_=true;if(try_backend(save_path_)){last_store_recovery_="primary";save_data_present_but_unusable_=false;ASTRAE_LOGI("loaded existing native save: %s",save_path_.c_str());return true;}}
    const std::string backup=save_path_+".bak";
    if(exists(backup)){save_data_present_but_unusable_=true;if(try_backend(backup)){
        if(!archive_primary())return false;
        last_store_recovery_="backup-recovered";save_data_present_but_unusable_=false;
        if(!commit_store())ASTRAE_LOGW("loaded backup save but could not rewrite primary; backup remains intact");
        ASTRAE_LOGI("recovered existing native save from %s",backup.c_str());return true;
    }}
    const std::string temp=save_path_+".tmp";
    if(exists(temp)){save_data_present_but_unusable_=true;if(try_backend(temp)){
        if(!archive_primary())return false;
        last_store_recovery_="temporary-recovered";save_data_present_but_unusable_=false;
        if(!commit_store())ASTRAE_LOGW("loaded interrupted temporary save but could not rewrite primary");
        ASTRAE_LOGI("recovered native save from interrupted transaction %s",temp.c_str());return true;
    }}
    // Phase-1/compatibility Store schema v1 lives in SinglePlayer/compat-store/save.json.
    // It contains the real old account and tutorial state; the previous Backend loader
    // never looked there and consequently seeded a fresh account on each install/run.
    const std::string compat_dir=join_path(save_dir_,"compat-store");
    const std::string candidates[]={save_path_,save_path_+".bak",save_path_+".tmp",save_path_+".invalid",join_path(compat_dir,"save.json"),join_path(compat_dir,"save.json.bak"),join_path(compat_dir,"save.json.tmp")};
    for(const auto &path:candidates){
        if(!exists(path))continue;
        J legacy;if(!parse_file(path,legacy)){save_data_present_but_unusable_=true;continue;}
        if(save_migration::is_pristine_new_profile(legacy)){ASTRAE_LOGI("ignored blank compat Store seed during migration scan: %s",path.c_str());continue;}
        save_data_present_but_unusable_=true;
        J migrated;std::string migration_error;
        if(!save_migration::migrate_compat_document(legacy,migrated,&migration_error)){
            ASTRAE_LOGW("legacy save candidate rejected without modification: path=%s reason=%s",path.c_str(),migration_error.c_str());continue;
        }
        migrated["LegacySaveSource"]=path;
        J previous=state_;state_=std::move(migrated);
        if(!normalize_state()){state_=std::move(previous);ASTRAE_LOGE("legacy save migration produced invalid native state; original retained: %s",path.c_str());continue;}
        if(path==save_path_&&!archive_primary()){state_=std::move(previous);return false;}
        // If the native primary exists but was invalid, preserve it before writing the
        // migrated save. Never rename it over an existing valid backup.
        if(path!=save_path_&&!archive_primary()){state_=std::move(previous);return false;}
        if(!commit_store()){ASTRAE_LOGE("legacy save was mapped but native commit failed; source retained at %s",path.c_str());state_=std::move(previous);return false;}
        last_store_recovery_="compat-store-migrated";save_data_present_but_unusable_=false;
        ASTRAE_LOGI("legacy compatibility save migrated successfully: source=%s characters=%zu inventory=%zu tutorial_bits=%llu initial_index=%lld; source snapshot retained",path.c_str(),state_["characters"].size(),state_["inventory"].size(),static_cast<unsigned long long>(state_["TutorialInfo"]["ContentBits"].as_u64()),static_cast<long long>(state_["TutorialInfo"]["InitialIndex"].as_i64()));
        return true;
    }
    if(save_data_present_but_unusable_){last_store_recovery_="existing-save-invalid-retained";ASTRAE_LOGE("existing save files were found but none could be loaded or migrated; refusing to overwrite them with a new account");}
    return false;
}
bool Backend::commit_store(){
    if(!state_.is_object())return false;
    const uint64_t old_revision=state_["StoreRevision"].as_u64(0);
    state_["StoreRevision"]=old_revision+1;
    state_["StoreUpdatedAt"]=now_sec();
    std::string text=json::dump(state_),tmp=save_path_+".tmp",bak=save_path_+".bak";
    int fd=open(tmp.c_str(),O_WRONLY|O_CREAT|O_TRUNC,0600);
    if(fd<0){state_["StoreRevision"]=old_revision;return false;}
    size_t pos=0;bool ok=true;
    while(pos<text.size()){
        ssize_t n=write(fd,text.data()+pos,text.size()-pos);
        if(n<0){if(errno==EINTR)continue;ok=false;break;}
        if(n==0){ok=false;break;}
        pos+=size_t(n);
    }
    if(ok&&fsync(fd)!=0)ok=false;
    if(close(fd)!=0)ok=false;
    if(!ok){unlink(tmp.c_str());state_["StoreRevision"]=old_revision;return false;}
    std::string verify;
    if(!read_file(tmp,verify,16*1024*1024)){unlink(tmp.c_str());state_["StoreRevision"]=old_revision;return false;}
    try{J check=json::parse(verify);std::string why;if(!valid_state(check,&why)){unlink(tmp.c_str());state_["StoreRevision"]=old_revision;return false;}}
    catch(...){unlink(tmp.c_str());state_["StoreRevision"]=old_revision;return false;}
    bool had_primary=access(save_path_.c_str(),F_OK)==0;
    if(had_primary){unlink(bak.c_str());if(rename(save_path_.c_str(),bak.c_str())!=0){unlink(tmp.c_str());state_["StoreRevision"]=old_revision;return false;}}
    if(rename(tmp.c_str(),save_path_.c_str())!=0){if(had_primary&&access(bak.c_str(),F_OK)==0)rename(bak.c_str(),save_path_.c_str());unlink(tmp.c_str());state_["StoreRevision"]=old_revision;return false;}
    int dfd=open(save_dir_.c_str(),O_RDONLY|O_DIRECTORY);
    if(dfd>=0){fsync(dfd);close(dfd);}
    return true;
}
J Backend::schema_default(const std::string &name,int depth) const {if(depth>7)return j_obj();auto it=schemas_.find(name);if(it==schemas_.end())return j_obj();J out=j_obj();const J &fields=it->second["fields"];for(const auto &f:fields.as_array()){std::string fn=str(f,"name"),type=str(f,"type"),kind=str(f,"kind");if(fn.empty())continue;if(kind=="s")out[fn]="";else if(kind=="b")out[fn]=false;else if(kind=="v"||kind=="a")out[fn]=j_arr();else if(kind=="o")out[fn]=schema_default(type,depth+1);else if(kind=="f"||kind=="d")out[fn]=0.0;else if(type.size()>2&&type.substr(type.size()-2)=="[]")out[fn]=j_arr();else out[fn]=0;}return out;}
J Backend::response_base(const std::string &name) const {J r=schema_default(name+".Response");if(!r.is_object())r=j_obj();if(r.contains("ServerTime"))r["ServerTime"]=int64_t(now_sec());if(r.contains("NotiFlag"))r["NotiFlag"]=0;if(r.contains("RefreshProtocols"))r["RefreshProtocols"]=j_arr();return r;}
J &Backend::arr(const std::string &key){return ensure_arr(state_,key);}J &Backend::obj(const std::string &key){return ensure_obj(state_,key);}
J *Backend::find_id(J &list,const std::string &field,uint64_t id){for(auto &x:list.as_array())if(id_equal(x[field],id))return &x;return nullptr;}
const J *Backend::table_row(const std::string &table,const std::string &id_field,uint64_t id) const {auto it=tables_.find(table);if(it==tables_.end())return nullptr;for(const auto &r:it->second.as_array())if(id_equal(r[id_field],id))return &r;return nullptr;}
std::vector<J> Backend::rewards_from(const J &req,const std::string &operation) const {std::vector<J> out;const J *src=nullptr;if(req["Rewards"].is_array())src=&req["Rewards"];else if(req["RewardInfos"].is_array())src=&req["RewardInfos"];else if(req["RewardParcelInfos"].is_array())src=&req["RewardParcelInfos"];if(src)for(const auto &v:src->as_array()){J r=j_obj();r["Type"]=i64(v,"Type",i64(v,"ParcelType",0));r["TableId"]=u64(v,"TableId",u64(v,"Id",0));r["Count"]=i64(v,"Count",i64(v,"Amount",1));if((r["TableId"].as_u64()!=0||r["Type"].as_i64()==2)&&r["Count"].as_i64()>0)out.push_back(r);}const J &configured=rules_["operations"][operation]["rewards"];if(out.empty()&&configured.is_array())for(const auto &v:configured.as_array()){J r=v;if(!r.contains("Count"))r["Count"]=1;out.push_back(r);}return out;}
bool Backend::grant(const J &reward,J *changed){
    const int type=int(i64(reward,"Type",i64(reward,"ParcelType",0)));
    const uint64_t id=u64(reward,"TableId",u64(reward,"Id",0));
    const int64_t n=i64(reward,"Count",i64(reward,"Amount",0));
    if((!id&&type!=2)||n<=0||n>INT32_MAX)return false;
    auto record=[&](int64_t amount){if(changed)changed->push_back(make_reward(type,id,amount));};
    if(type==3||(type==10&&id==1)){
        J &account=state_["Account"];int64_t oldLevel=i64(account,"Level",1),oldExp=i64(account,"Exp",0);
        if(oldLevel<1||oldLevel>99||oldExp<0||oldExp>INT32_MAX-n)return false;
        const J &unlocks=state_["Progress"]["UnlockInfos"];
        for(const auto &u:unlocks.as_array())if(u64(u,"TableId")==200){std::string value=str(u,"Value");if(!value.empty()){char *end=nullptr;long long parsed=std::strtoll(value.c_str(),&end,10);if(!end||*end||parsed<10)return false;}}
        int64_t exp=oldExp+n;int64_t level=oldLevel;while(level<99&&exp>=kAccountLevelExp[level-1]){exp-=kAccountLevelExp[level-1];++level;}
        account["Level"]=level;account["Exp"]=exp;
        if(changed){J x=make_reward(type,id,n);x["FromLevel"]=oldLevel;x["FromExp"]=oldExp;x["ToLevel"]=level;x["ToExp"]=exp;changed->push_back(std::move(x));}
        return true;
    }
    if(type==1||type==4){if(n!=1)return false;const std::string key=type==1?"characters":"vignettes";J &items=arr(key);for(const auto &x:items.as_array())if(u64(x,"TableId")==id)return false;if(items.size()>=9999)return false;items.push_back(type==1?make_default_character(id,items.size()+1):make_default_vignette(id,items.size()+1));record(1);return true;}
    if(type==2||type==5){if(type==2&&id>=8)return false;J *entry=nullptr;for(auto &x:arr("inventory").as_array())if(i64(x,"Type")==type&&u64(x,"TableId")==id){entry=&x;break;}if(!entry){J x=j_obj();x["Type"]=type;x["TableId"]=id;x["Count"]=int64_t(0);arr("inventory").push_back(x);entry=&arr("inventory").as_array().back();}int64_t have=i64(*entry,"Count");int64_t max=type==5?INT32_MAX:INT64_MAX;if(have<0||have>max-n)return false;(*entry)["Count"]=have+n;record(n);return true;}
    if(type==7||type==8||type==11){const std::string key=type==7?"tickets":(type==8?"costumes":"rune_stock");if(type==11){int32_t limit=rune_stock_limit(id);if(limit<2)return false;}J &items=arr(key);J *entry=nullptr;for(auto &x:items.as_array())if(u64(x,"TableId")==id){entry=&x;break;}if(type==8){if(!entry){J x=j_obj();x["TableId"]=id;x["IsEquipped"]=false;items.push_back(x);}record(1);return true;}int64_t have=entry?i64(*entry,"Count"):0;int64_t max=type==11?rune_stock_limit(id):INT32_MAX;if(have<0||have>max-n)return false;if(type==11&&have+n>rune_stock_limit(id))return false;if(!entry){J x=j_obj();x["TableId"]=id;x["Count"]=int64_t(0);if(type==7)x["LastRechargeTime"]=int64_t(0);items.push_back(x);entry=&items.as_array().back();}(*entry)["Count"]=have+n;record(n);return true;}
    return false;
}
bool Backend::spend(const J &cost,J *changed){
    const int type=int(i64(cost,"Type",i64(cost,"ParcelType",0)));const uint64_t id=u64(cost,"TableId",u64(cost,"Id",0));const int64_t n=i64(cost,"Count",i64(cost,"Amount",0));if((!id&&type!=2)||n<=0||n>INT32_MAX)return false;
    if(type==2||type==5){if(type==2&&id>=8)return false;for(auto &x:arr("inventory").as_array())if(i64(x,"Type")==type&&u64(x,"TableId")==id){int64_t have=i64(x,"Count");if(have<n)return false;x["Count"]=have-n;if(changed)changed->push_back(make_reward(type,id,-n));return true;}return false;}
    if(type==7||type==11){const std::string key=type==7?"tickets":"rune_stock";if(type==11&&rune_stock_limit(id)<2)return false;for(auto &x:arr(key).as_array())if(u64(x,"TableId")==id){int64_t have=i64(x,"Count");if(have<n)return false;x["Count"]=have-n;if(changed)changed->push_back(make_reward(type,id,-n));return true;}return false;}
    return false;
}
bool Backend::spend_by_table_id(uint64_t id,int64_t n,J *changed){if(n<=0)return false;J *entry=nullptr;for(auto &x:arr("inventory").as_array())if(u64(x,"TableId")==id){if(entry)return false;entry=&x;}if(!entry||i64(*entry,"Count")<n)return false;int type=int(i64(*entry,"Type"));(*entry)["Count"]=i64(*entry,"Count")-n;if(changed)changed->push_back(make_reward(type,id,-n));return true;}
int64_t Backend::common_i64(const std::string &key,int64_t fallback) const {auto it=tables_.find("ConstCommon");if(it==tables_.end()||!it->second.is_array()||it->second.size()==0)return fallback;return i64(it->second[0],key.c_str(),fallback);}
bool Backend::grant_batch(const std::vector<J> &rewards,J *changed){J before=state_,previous=changed?*changed:j_arr();for(const auto &r:rewards)if(!grant(r,changed)){state_=std::move(before);if(changed)*changed=std::move(previous);return false;}return true;}
bool Backend::spend_batch(const std::vector<J> &costs,J *changed){J before=state_,previous=changed?*changed:j_arr();for(const auto &c:costs)if(!spend(c,changed)){state_=std::move(before);if(changed)*changed=std::move(previous);return false;}return true;}
J Backend::reward_total(const std::vector<J> &rewards) const {J total=schema_default("RewardTotalInfo");if(!total.is_object())total=j_obj();J visual=j_arr(),plain=j_arr();for(const auto &r:rewards){J x=schema_default("RewardParcelInfo");x["Type"]=i64(r,"Type");x["TableId"]=u64(r,"TableId");x["Count"]=i64(r,"Count");x["AddedCount"]=i64(r,"Count");plain.push_back(x);J y=schema_default("VisualRewardParcelInfo");y["Type"]=i64(r,"Type");y["TableId"]=u64(r,"TableId");y["Count"]=i64(r,"Count");y["AddedCount"]=i64(r,"Count");y["ReplaceInfos"]=j_arr();visual.push_back(y);}total["RewardInfos"]=plain;total["VisualRewardInfos"]=visual;return total;}
static J &ensure_pack_record(J &pack_state,uint64_t group,bool &created);
void Backend::fill_account_response(J &r) const {
    const J &account=state_["Account"];J user=schema_default("AccountUserInfo"),base=schema_default("AccountBaseInfo"),tut=schema_default("TutorialInfo");
    user["UserName"]=str(account,"UserName","Offline Player");user["GameLocale"]=i64(account,"GameLocale",0);user["Level"]=i64(account,"Level",1);user["Exp"]=i64(account,"Exp",0);
    base["ServerId"]=u64(account,"ServerId",1);base["NickName"]=str(account,"NickName",str(account,"UserName","Offline Player"));
    tut["InitialIndex"]=i64(state_["TutorialInfo"],"InitialIndex",0);tut["ContentBits"]=u64(state_["TutorialInfo"],"ContentBits",0);
    if(r.contains("AccountUserInfo"))r["AccountUserInfo"]=user;if(r.contains("AccountBaseInfo"))r["AccountBaseInfo"]=base;if(r.contains("TutorialInfo"))r["TutorialInfo"]=tut;
    if(r.contains("GameAccountId"))r["GameAccountId"]=str(account,"GameAccountId","");
    if(r.contains("CharacterInfos"))r["CharacterInfos"]=state_["characters"].is_array()?state_["characters"]:j_arr();
    if(r.contains("VignetteInfos"))r["VignetteInfos"]=state_["vignettes"].is_array()?state_["vignettes"]:j_arr();
    if(r.contains("ArtifactInfos"))r["ArtifactInfos"]=state_["artifacts"].is_array()?state_["artifacts"]:j_arr();
    if(r.contains("CurrencyInfos")){J a=j_arr();for(const auto &x:state_["inventory"].as_array())if(i64(x,"Type")==2){J c=schema_default("CurrencyInfo");c["Type"]=u64(x,"TableId");c["Value"]=i64(x,"Count");a.push_back(c);}r["CurrencyInfos"]=a;}
    if(r.contains("ItemInfos")){J a=j_arr();for(const auto &x:state_["inventory"].as_array())if(i64(x,"Type")==5){J c=schema_default("ItemInfo");c["TableId"]=u64(x,"TableId");c["Count"]=i64(x,"Count");a.push_back(c);}r["ItemInfos"]=a;}
    if(r.contains("Runes")){J a=j_arr();for(const auto &x:state_["rune_stock"].as_array()){J v=schema_default("RuneInfo");v["TableId"]=u64(x,"TableId");v["Count"]=i64(x,"Count");a.push_back(v);}r["Runes"]=a;}
    if(r.contains("TicketInfos")){J a=j_arr();for(const auto &x:state_["tickets"].as_array()){J v=schema_default("TicketInfo");v["TableId"]=u64(x,"TableId");v["Count"]=i64(x,"Count");v["LastRechargeTime"]=i64(x,"LastRechargeTime",0);a.push_back(v);}r["TicketInfos"]=a;}
    if(r.contains("CostumeInfos"))r["CostumeInfos"]=state_["costumes"].is_array()?state_["costumes"]:j_arr();
    if(r.contains("UnlockInfos"))r["UnlockInfos"]=state_["Progress"]["UnlockInfos"].is_array()?state_["Progress"]["UnlockInfos"]:j_arr();
    if(r.contains("DateFieldInfo"))r["DateFieldInfo"]=state_["DateFieldInfo"].is_object()?state_["DateFieldInfo"]:schema_default("DateFieldInfo");
    if(r.contains("NodeInfos"))r["NodeInfos"]=state_["DateFieldNodeInfos"].is_array()?state_["DateFieldNodeInfos"]:j_arr();
    if(r.contains("FavorInfos"))r["FavorInfos"]=state_["FavorInfos"].is_array()?state_["FavorInfos"]:j_arr();
    if(r.contains("NodeEventInfos"))r["NodeEventInfos"]=state_["DateFieldNodeEventInfos"].is_array()?state_["DateFieldNodeEventInfos"]:j_arr();
    if(r.contains("CollectionArtifactInfo"))r["CollectionArtifactInfo"]=state_["CollectionArtifactInfo"].is_object()?state_["CollectionArtifactInfo"]:schema_default("CollectionArtifactInfo");
    if(r.contains("LobbyCharacterInfo"))r["LobbyCharacterInfo"]=state_["LobbyCharacterInfo"].is_object()?state_["LobbyCharacterInfo"]:schema_default("LobbyCharacterInfo");
    if(r.contains("LobbyBackgroundInfo")){J bg=schema_default("LobbyBackgroundInfo");bg["Random"]=false;bg["TableIds"]=j_arr();bg["TableIds"].push_back(u64(state_,"LobbyBackgroundId",30001));r["LobbyBackgroundInfo"]=bg;}
    if(r.contains("WishlistInfo"))r["WishlistInfo"]=state_["gacha_state"]["WishlistInfo"].is_object()?state_["gacha_state"]["WishlistInfo"]:(state_["WishlistInfo"].is_object()?state_["WishlistInfo"]:schema_default("WishlistInfo"));
    if(r.contains("GachaAdjustPointInfos"))r["GachaAdjustPointInfos"]=state_["GachaAdjustPointInfos"].is_array()?state_["GachaAdjustPointInfos"]:j_arr();
    if(r.contains("RecruitInfos"))r["RecruitInfos"]=state_["RecruitInfos"].is_array()?state_["RecruitInfos"]:j_arr();
    if(r.contains("FormationInfos"))r["FormationInfos"]=state_["formations"].is_array()?state_["formations"]:j_arr();
    if(r.contains("ScenarioPackMiniInfos")||r.contains("ScenarioPackDetailInfos")){
        J minis=j_arr(),details=j_arr();const J &packs=state_["PackState"]["Packs"];
        if(packs.is_object())for(const auto &entry:packs.as_object()){
            const J &pack=entry.second;if(!pack.is_object())continue;char *end=nullptr;uint64_t group=u64(pack,"GroupId",std::strtoull(entry.first.c_str(),&end,10));if(!group)continue;
            J mini=schema_default("ScenarioPackMiniInfo");mini["GroupId"]=group;mini["OpenType"]=i64(pack,"OpenType",1);minis.push_back(mini);
            J detail=schema_default("ScenarioPackDetailInfo");detail["GroupId"]=group;detail["StoryQuestIndex"]=i64(pack,"StoryQuestIndex");detail["NormalQuestBit"]=u64(pack,"NormalQuestBit");detail["HardQuestBit"]=u64(pack,"HardQuestBit");detail["VeryHardQuestBit"]=u64(pack,"VeryHardQuestBit");detail["RepeatQuestIndex"]=i64(pack,"RepeatQuestIndex");detail["SubStoryQuestBit"]=u64(pack,"SubStoryQuestBit");detail["ChallengeQuestBit"]=u64(pack,"ChallengeQuestBit");detail["MaxQuestDifficulty"]=i64(pack,"MaxQuestDifficulty");details.push_back(detail);
        }
        if(r.contains("ScenarioPackMiniInfos"))r["ScenarioPackMiniInfos"]=minis;
        if(r.contains("ScenarioPackDetailInfos"))r["ScenarioPackDetailInfos"]=details;
    }
    if(r.contains("ShopItemInfos"))r["ShopItemInfos"]=state_["ShopItemInfos"].is_array()?state_["ShopItemInfos"]:j_arr();
    if(r.contains("ServerOptionInfo"))r["ServerOptionInfo"]=state_["ServerOptionInfo"].is_object()?state_["ServerOptionInfo"]:schema_default("ServerOptionInfo");
}
void Backend::fill_updated_parcels(J &r,const J &changed) const {
    if(!r.contains("UpdatedParcelInfo"))return;J u=schema_default("UpdatedParcelInfo"),account=state_["Account"],user=schema_default("AccountUserInfo");
    user["UserName"]=str(account,"UserName","Offline Player");user["GameLocale"]=i64(account,"GameLocale",0);user["Level"]=i64(account,"Level",1);user["Exp"]=i64(account,"Exp",0);u["AccountUserInfo"]=user;
    bool currency=false,character=false,vignette=false,artifact=false,item=false,rune=false,ticket=false,costume=false,exp=false;
    for(const auto &x:changed.as_array()){int t=int(i64(x,"Type"));if(t==2)currency=true;else if(t==1)character=true;else if(t==4)vignette=true;else if(t==5)item=true;else if(t==7)ticket=true;else if(t==8)costume=true;else if(t==11)rune=true;else if(t==3||(t==10&&u64(x,"TableId")==1))exp=true;}
    if(currency){J a=j_arr();for(const auto &x:state_["inventory"].as_array())if(i64(x,"Type")==2){J c=schema_default("CurrencyInfo");c["Type"]=u64(x,"TableId");c["Value"]=i64(x,"Count");a.push_back(c);}u["CurrencyInfos"]=a;}
    if(character)u["CharacterInfos"]=state_["characters"];
    if(vignette)u["VignetteInfos"]=state_["vignettes"];
    if(artifact)u["ArtifactInfos"]=state_["artifacts"];
    if(item){J a=j_arr();for(const auto &x:state_["inventory"].as_array())if(i64(x,"Type")==5){J v=schema_default("ItemInfo");v["TableId"]=u64(x,"TableId");v["Count"]=i64(x,"Count");a.push_back(v);}u["ItemInfos"]=a;}
    if(rune){J a=j_arr();for(const auto &x:state_["rune_stock"].as_array()){J v=schema_default("RuneInfo");v["TableId"]=u64(x,"TableId");v["Count"]=i64(x,"Count");a.push_back(v);}u["RuneInfos"]=a;}
    if(ticket){J a=j_arr();for(const auto &x:state_["tickets"].as_array()){J v=schema_default("TicketInfo");v["TableId"]=u64(x,"TableId");v["Count"]=i64(x,"Count");v["LastRechargeTime"]=i64(x,"LastRechargeTime",0);a.push_back(v);}u["TicketInfos"]=a;}
    if(costume)u["CostumeInfos"]=state_["costumes"];
    if(exp){J level=schema_default("AccountLevelUpResultInfo"),info=schema_default("AccountExpResultInfo");for(const auto &x:changed.as_array())if(i64(x,"FromLevel",-1)>=1){info["FromLevel"]=i64(x,"FromLevel");info["FromExp"]=i64(x,"FromExp");info["ToLevel"]=i64(x,"ToLevel");info["ToExp"]=i64(x,"ToExp");info["AddedExp"]=i64(x,"Count");}level["ExpInfo"]=info;u["AccountLevelUpResultInfo"]=level;}
    r["UpdatedParcelInfo"]=u;
}
std::string Backend::make_key(uint64_t a,uint64_t b) const {return std::to_string(a)+":"+std::to_string(b);}

J Backend::handle_account(const std::string &op,const J &req,J r,bool &changed){J &account=state_["Account"];if(op=="AccountLogin"||op=="DevLogin"){bool packCreated=false;ensure_pack_record(state_["PackState"],1000,packCreated);changed=packCreated;fill_account_response(r);if(r.contains("IsNew"))r["IsNew"]=false;return r;}if(op=="RegisterAccountName"){std::string name=str(req,"NickName",str(req,"UserName","Player"));if(name.size()>32)name.resize(32);account["UserName"]=name;account["NickName"]=name;changed=true;if(r.contains("AccountBaseInfo")){r["AccountBaseInfo"]["NickName"]=name;r["AccountBaseInfo"]["ServerId"]=u64(account,"ServerId",1);}if(r.contains("ResultInfo")){r["ResultInfo"]["UserName"]=name;r["ResultInfo"]["NickName"]=name;}return r;}if(op=="AccountUpdateOption"){account["ServerOptionInfo"]=req["ServerOptionInfo"];if(r.contains("ServerOptionInfo"))r["ServerOptionInfo"]=req["ServerOptionInfo"];changed=true;return r;}if(op=="DevAccountReset"){seed_defaults();fill_account_response(r);changed=true;return r;}if(op=="DevCheat"){auto rewards=rewards_from(req,op);J changedItems=j_arr();if(grant_batch(rewards,&changedItems)){fill_updated_parcels(r,changedItems);changed=!rewards.empty();}return r;}if(op=="TutorialProgress"||op=="TutorialContentView"){J &t=state_["TutorialInfo"];int idx=int(i64(req,"Index",i64(req,"InitialIndex",i64(req,"Group",0))));if(idx>i64(t,"InitialIndex"))t["InitialIndex"]=idx;uint64_t bits=u64(req,"ContentBits");if(bits)t["ContentBits"]=u64(t,"ContentBits")|bits;changed=true;if(r.contains("TutorialInfo"))r["TutorialInfo"]=t;return r;}if(op=="AccountLoginQueueHeartbeat"){return r;}if(op=="Encryption"||op=="AccountAck"||op=="AccountUpdateOption"){return r;}return r;}

static std::vector<J> configured_rewards(const J &rules,const std::string &bucket,uint64_t id);
static bool parse_cost_array(const J &array,std::vector<J> &out);

J Backend::handle_growth(const std::string &op,const J &req,J r,bool &changed){
    J key=req["CharacterKey"];uint64_t tableId=u64(key,"TableId",u64(req,"CharacterTableId")),serverId=u64(key,"ServerId",u64(req,"CharacterServerId"));if(!tableId)return r;J *ch=find_id(arr("characters"),"TableId",tableId);if(!ch|| (serverId&&u64(*ch,"ServerId")!=serverId))return r;
    std::string action;int target=0,group=0;bool hasCosts=true;std::vector<J> costs;
    if(op=="CharacterLevelUp"){action="level";target=int(i64(req,"TargetLevel"));int current=int(i64(*ch,"Level",1));if(target!=current+1||target>common_i64("Character_Level_Max",50))return r;const J &entry=rules_["characterGrowthCosts"][action][std::to_string(tableId)][std::to_string(target)];hasCosts=parse_cost_array(entry,costs)&&!costs.empty();}
    else if(op=="CharacterStarRankUp"){action="star";target=int(i64(req,"TargetRank"));int current=int(i64(*ch,"Star",1));if(target!=current+1||target>common_i64("Character_StarRankUp_Max",5))return r;const J &entry=rules_["characterGrowthCosts"][action][std::to_string(tableId)][std::to_string(target)];hasCosts=parse_cost_array(entry,costs)&&!costs.empty();uint64_t materialId=u64(req,"MaterialItemId");if(materialId){J *material=nullptr;for(auto &x:arr("inventory").as_array())if(u64(x,"TableId")==materialId){if(material)return r;material=&x;}if(!material)return r;J c=j_obj();c["Type"]=i64(*material,"Type");c["TableId"]=materialId;c["Count"]=1;costs.push_back(c);hasCosts=true;} }
    else if(op=="CharacterAddPotential"){action="potential";target=int(i64(req,"TargetPotentialNode",-1));if(target<0||target>63)return r;uint64_t mask=u64(*ch,"Potential");if(mask&(uint64_t(1)<<target))return r;const J &entry=rules_["characterGrowthCosts"][action][std::to_string(tableId)][std::to_string(target)];hasCosts=parse_cost_array(entry,costs)&&!costs.empty();}
    else if(op=="CharacterResetPotential"){action="resetPotential";if(!u64(*ch,"Potential"))return r;const J &entry=rules_["characterGrowthCosts"][action][std::to_string(tableId)]["reset"];if(entry.is_array())hasCosts=parse_cost_array(entry,costs)&&!costs.empty();else{uint64_t itemId=uint64_t(common_i64("CharacterPotentialResetItemId",40001));bool found=false;for(const auto &x:state_["inventory"].as_array())if(u64(x,"TableId")==itemId){if(found)return r;J c=j_obj();c["Type"]=i64(x,"Type");c["TableId"]=itemId;c["Count"]=1;costs.push_back(c);found=true;}hasCosts=found;}}
    else if(op=="CharacterSkillLevelUp"){action="skill";group=int(i64(req,"TargetGroup"));J &skills=ensure_arr(*ch,"Skills");J *skill=nullptr;for(auto &x:skills.as_array())if(i64(x,"Group")==group){skill=&x;break;}int current=skill?int(i64(*skill,"Level",0)):0;target=int(i64(req,"TargetLevel",current+1));if(target!=current+1)return r;const J &entry=rules_["characterGrowthCosts"][action][std::to_string(tableId)][std::to_string(group)][std::to_string(target)];hasCosts=parse_cost_array(entry,costs)&&!costs.empty();}
    else if(op=="CharacterSetFavorite"){(*ch)["Favorite"]=boolean(req,"Favorite",boolean(req,"IsFavorite",true));changed=true;}
    else if(op=="CostumeEquip"){(*ch)["CostumeTableId"]=u64(req,"CostumeTableId",u64(req,"TableId"));changed=true;}
    else if(op=="VignetteStarRankUp"){action="vignette";int current=int(i64(*ch,"VignetteStar",0));target=int(i64(req,"TargetRank",current+1));if(target!=current+1||target>common_i64("Vignette_StarRankUp_Max",5))return r;const J &entry=rules_["characterGrowthCosts"][action][std::to_string(tableId)][std::to_string(target)];hasCosts=parse_cost_array(entry,costs)&&!costs.empty();}
    else return r;
    if(op!="CharacterSetFavorite"&&op!="CostumeEquip"){
        if(!hasCosts)return r;J before=state_,changes=j_arr();if(!costs.empty()&&!spend_batch(costs,&changes)){state_=std::move(before);return r;}
        if(op=="CharacterLevelUp")(*ch)["Level"]=target;
        else if(op=="CharacterStarRankUp")(*ch)["Star"]=target;
        else if(op=="CharacterAddPotential"){J x=j_obj();x["NodeId"]=target;x["Level"]=1;ensure_arr(*ch,"PotentialNodes").push_back(x);(*ch)["Potential"]=u64(*ch,"Potential")|(uint64_t(1)<<target);}
        else if(op=="CharacterResetPotential"){(*ch)["Potential"]=uint64_t(0);(*ch)["PotentialNodes"]=j_arr();}
        else if(op=="CharacterSkillLevelUp"){J &skills=ensure_arr(*ch,"Skills");J *skill=nullptr;for(auto &x:skills.as_array())if(i64(x,"Group")==group){skill=&x;break;}if(!skill){J x=j_obj();x["Group"]=group;x["Level"]=target;skills.push_back(x);}else (*skill)["Level"]=target;}
        else if(op=="VignetteStarRankUp")(*ch)["VignetteStar"]=target;
        changed=true;fill_updated_parcels(r,changes);
    }
    J info=schema_default("CharacterInfo");for(auto &kv:ch->as_object())info[kv.first]=kv.second;if(r.contains("ResultInfo")){if(r["ResultInfo"].contains("ResultInfo"))r["ResultInfo"]["ResultInfo"]=info;else r["ResultInfo"]=info;}return r;
}

J Backend::handle_artifact(const std::string &op,const J &req,J r,bool &changed){
    uint64_t sid=u64(req,"ArtifactServerId");J *item=find_id(arr("artifacts"),"ServerId",sid);
    if(op=="ArtifactEquip"){J k=req["CharacterKey"];uint64_t charTable=u64(k,"TableId"),charServer=u64(k,"ServerId");item=find_id(arr("artifacts"),"ServerId",sid);J *character=find_id(arr("characters"),"TableId",charTable);if(!item||!character||(charServer&&u64(*character,"ServerId")!=charServer))return r;for(auto &x:arr("artifacts").as_array())if(u64(x,"CharacterTableId")==charTable&&u64(x,"ServerId")!=sid){x["CharacterTableId"]=uint64_t(0);x["CharacterServerId"]=uint64_t(0);}(*item)["CharacterTableId"]=charTable;(*item)["CharacterServerId"]=charServer;changed=true;if(r.contains("ArtifactInfos")){r["ArtifactInfos"]=j_arr();r["ArtifactInfos"].push_back(*item);}return r;}
    if(op=="ArtifactSetLock"){if(!item)return r;(*item)["Locked"]=boolean(req,"Locked");changed=true;if(r.contains("ArtifactInfo"))r["ArtifactInfo"]=*item;return r;}
    if(op=="ArtifactLevelUp"){if(!item)return r;int current=int(i64(*item,"Level",1)),target=int(i64(req,"TargetLevel"));if(target!=current+1||target>common_i64("Artifact_Level_Max",50))return r;uint64_t tableId=u64(*item,"TableId");const J &entry=rules_["artifactGrowthCosts"]["level"][std::to_string(tableId)][std::to_string(target)];std::vector<J> costs;if(!parse_cost_array(entry,costs)||costs.empty())return r;J before=state_,changes=j_arr();if(!costs.empty()&&!spend_batch(costs,&changes)){state_=std::move(before);return r;}(*item)["Level"]=target;changed=true;if(r.contains("ResultInfo")&&r["ResultInfo"].contains("ResultInfo"))r["ResultInfo"]["ResultInfo"]=*item;fill_updated_parcels(r,changes);return r;}
    if(op=="ArtifactStarRankUp"){
        if(!item||boolean(*item,"Locked"))return r;int current=int(i64(*item,"Star",0)),target=current+1;if(target>common_i64("Artifact_StarRankUp_Max",5))return r;const J &entry=rules_["artifactGrowthCosts"]["star"][std::to_string(u64(*item,"TableId"))][std::to_string(target)];std::vector<J> costs;if(!parse_cost_array(entry,costs)||costs.empty())return r;std::vector<uint64_t> materials;std::set<uint64_t> unique;for(const auto &v:req["MaterialArtifactServerIds"].as_array()){uint64_t id=v.as_u64();J *material=find_id(arr("artifacts"),"ServerId",id);if(!id||id==sid||!unique.insert(id).second||!material||boolean(*material,"Locked"))return r;materials.push_back(id);}if(materials.empty())return r;J before=state_,changes=j_arr();if(!costs.empty()&&!spend_batch(costs,&changes)){state_=std::move(before);return r;}auto &items=arr("artifacts").as_array();items.erase(std::remove_if(items.begin(),items.end(),[&](const J &x){return unique.count(u64(x,"ServerId"))!=0;}),items.end());item=find_id(arr("artifacts"),"ServerId",sid);if(!item){state_=std::move(before);return r;}(*item)["Star"]=target;changed=true;if(r.contains("ResultInfo")&&r["ResultInfo"].contains("ResultInfo"))r["ResultInfo"]["ResultInfo"]=*item;fill_updated_parcels(r,changes);return r;
    }
    if(op=="ArtifactDecompose"){
        const J &ids=req["ArtifactServerIds"];if(!ids.is_array()||ids.size()==0||ids.size()>size_t(common_i64("Decompose_Artifact_Max_Count",100)))return r;std::set<uint64_t> unique;std::vector<J> rewards;for(const auto &v:ids.as_array()){uint64_t id=v.as_u64();J *artifact=find_id(arr("artifacts"),"ServerId",id);if(!id||!unique.insert(id).second||!artifact||boolean(*artifact,"Locked"))return r;auto configured=configured_rewards(rules_,"artifactDecomposeRewards",u64(*artifact,"TableId"));if(configured.empty())return r;rewards.insert(rewards.end(),configured.begin(),configured.end());}J before=state_,changes=j_arr();auto &items=arr("artifacts").as_array();items.erase(std::remove_if(items.begin(),items.end(),[&](const J &x){return unique.count(u64(x,"ServerId"))!=0;}),items.end());if(!grant_batch(rewards,&changes)){state_=std::move(before);return r;}changed=true;if(r.contains("ResultInfo"))r["ResultInfo"]["RewardInfos"]=reward_total(rewards)["RewardInfos"];fill_updated_parcels(r,changes);return r;
    }
    return r;
}

static uint64_t rng_next(uint64_t &x){x^=x>>12;x^=x<<25;x^=x>>27;return x*2685821657736338717ULL;}
J Backend::handle_gacha(const std::string &op,const J &req,J r,bool &changed){J &gs=obj("gacha_state");if(op=="GachaSetWishlist"){gs["WishlistInfo"]=req["WishlistInfo"];changed=true;return r;}if(op=="GachaGetRateSheet"||op=="GachaStatsView"){return r;}if(op=="GachaGetAdjustPointReward"){auto rewards=rewards_from(req,op);J changes=j_arr();if(grant_batch(rewards,&changes)){fill_updated_parcels(r,changes);changed=!rewards.empty();}return r;}if(op!="GachaStart")return r;uint64_t recruit=u64(req,"ShopRecruitId");const J *pool=nullptr;for(const auto &p:rules_["gachaPools"].as_array())if(id_equal(p["ShopRecruitId"],recruit)){pool=&p;break;}int drawCount=1;int requestType=int(i64(req,"RequesstType",0));if(pool)drawCount=int(i64(*pool,"DrawCount",requestType==10?10:1));drawCount=std::clamp(drawCount,1,100);std::vector<J> rewards;uint64_t seed=u64(gs,"Seed",recruit^uint64_t(now_sec()));if(!seed)seed=0x9e3779b97f4a7c15ULL;for(int i=0;i<drawCount;i++){if(pool&&(*pool)["Items"].is_array()){uint64_t total=0;for(const auto &x:(*pool)["Items"].as_array())total+=u64(x,"Weight",1);if(total){uint64_t pick=rng_next(seed)%total;for(const auto &x:(*pool)["Items"].as_array()){uint64_t w=u64(x,"Weight",1);if(pick<w){J z=j_obj();z["Type"]=i64(x,"Type",0);z["TableId"]=u64(x,"TableId");z["Count"]=i64(x,"Count",1);rewards.push_back(z);break;}pick-=w;}}}else if(boolean(rules_,"allowMockGachaResults",false)&&req["MockResults"].is_array()&&size_t(i)<req["MockResults"].size()){const J &x=req["MockResults"][size_t(i)];J z=j_obj();z["Type"]=i64(x,"Type");z["TableId"]=u64(x,"TableId");z["Count"]=i64(x,"Count",1);if(z["TableId"].as_u64())rewards.push_back(z);}}gs["Seed"]=seed;gs["LastShopRecruitId"]=recruit;gs["LastResults"]=j_arr();J visual=j_arr();for(auto &x:rewards){gs["LastResults"].push_back(x);J v=schema_default("VisualRewardParcelInfo");v["Type"]=i64(x,"Type");v["TableId"]=u64(x,"TableId");v["Count"]=i64(x,"Count");v["AddedCount"]=i64(x,"Count");visual.push_back(v);}J changes=j_arr();if(!grant_batch(rewards,&changes))return r;if(r.contains("ResultInfo")){r["ResultInfo"]["ShopRecruitId"]=recruit;r["ResultInfo"]["AdjustCount"]=drawCount;r["ResultInfo"]["ParcelInfos"]=visual;r["ResultInfo"]["BonusInfos"]=j_arr();}if(r.contains("GachaAdjustPointInfo")){J *p=find_id(arr("GachaAdjustPointInfos"),"TableId",recruit);if(p)r["GachaAdjustPointInfo"]=*p;else{r["GachaAdjustPointInfo"]["TableId"]=recruit;r["GachaAdjustPointInfo"]["Value"]=0;}}if(r.contains("RecruitInfo")){r["RecruitInfo"]["ShopRecruitId"]=recruit;}fill_updated_parcels(r,changes);changed=true;return r;}

J Backend::handle_runes(const std::string &op,const J &req,J r,bool &changed){if(op=="RuneChange"){uint64_t charId=u64(req,"CharacterTableId"),runeId=u64(req,"TableId");int set=int(i64(req,"Set")),slot=int(i64(req,"Slot"));if(set<0||set>9||slot<0||slot>9)return r;J *rune=find_id(arr("runes"),"TableId",runeId);if(!rune)return r;for(const auto &x:arr("runes").as_array())if(u64(x,"CharacterTableId")==charId&&i64(x,"Set")==set&&i64(x,"Slot")==slot&&u64(x,"TableId")!=runeId)return r;(*rune)["CharacterTableId"]=charId;(*rune)["Set"]=set;(*rune)["Slot"]=slot;changed=true;if(r.contains("Change"))r["Change"]=true;if(r.contains("RuneChangeInfo"))r["RuneChangeInfo"]=*rune;fill_updated_parcels(r,j_arr());return r;}if(op=="RuneExtract"||op=="RuneExtractAll"){uint64_t charId=u64(req,"CharacterTableId");uint64_t sid=u64(req,"ServerId");J extracted=j_arr();for(auto &x:arr("runes").as_array())if(u64(x,"CharacterTableId")==charId&&(!sid||u64(x,"ServerId")==sid)){extracted.push_back(x);x["CharacterTableId"]=uint64_t(0);x["Set"]=-1;x["Slot"]=-1;}changed=!extracted.as_array().empty();if(r.contains("ExtractedCharacterRuneInfos"))r["ExtractedCharacterRuneInfos"]=extracted;return r;}if(op=="RuneGrant"||op=="RunesGrant"){J additions=req["RuneGrantInfos"].is_array()?req["RuneGrantInfos"]:req["Runes"].is_array()?req["Runes"]:j_arr();for(const auto &x:additions.as_array()){J rune=x;if(!rune.contains("ServerId")){rune["ServerId"]=make_uid(state_);state_["NextServerId"]=u64(state_,"NextServerId",100000)+1;}if(!rune.contains("CharacterTableId"))rune["CharacterTableId"]=uint64_t(0);arr("runes").push_back(rune);}changed=!additions.as_array().empty();if(r.contains("RuneGrantInfos"))r["RuneGrantInfos"]=additions;return r;}if(op=="RuneTransfer"){J src=req["SourceCharacter"],dst=req["TargetCharacter"];uint64_t from=u64(src,"CharacterTableId"),to=u64(dst,"CharacterTableId"),runeId=u64(src,"TableId");J *rune=find_id(arr("runes"),"TableId",runeId);if(rune&&u64(*rune,"CharacterTableId")==from){(*rune)["CharacterTableId"]=to;(*rune)["Set"]=i64(dst,"Set",0);(*rune)["Slot"]=i64(dst,"Slot",0);changed=true;}return r;}return r;}

J Backend::handle_mail(const std::string &op,const J &req,J r,bool &changed){J &mails=arr("mails");if(op=="ReadMail"||op=="MailDetail"){uint64_t sid=u64(req,"ServerId",u64(req,"Uid"));for(auto &m:mails.as_array())if(id_equal(m["ServerId"],sid)){m["Receipt"]=1;changed=true;if(r.contains("MailInfo"))r["MailInfo"]=m;return r;}return r;}if(op=="DeleteMail"){uint64_t sid=u64(req,"ServerId");auto &a=mails.as_array();size_t n=a.size();a.erase(std::remove_if(a.begin(),a.end(),[&](const J &m){return id_equal(m["ServerId"],sid)&&i64(m,"Receipt")==1;}),a.end());changed=a.size()!=n;return r;}if(op!="ReceiveAllMail"&&op!="ReceiveMailByUids"&&op!="ReceivedMail")return r;J before=state_,changes=j_arr(),receipts=j_arr();std::vector<uint64_t> ids;if(op=="ReceiveAllMail"){for(auto &m:mails.as_array())ids.push_back(u64(m,"ServerId"));}else if(req["Uids"].is_array()){for(auto &x:req["Uids"].as_array())ids.push_back(x.as_u64());}else if(req["ServerIds"].is_array()){for(auto &x:req["ServerIds"].as_array())ids.push_back(x.as_u64());}else if(u64(req,"ServerId"))ids.push_back(u64(req,"ServerId"));int claimed=0;for(uint64_t sid:ids){J *m=find_id(mails,"ServerId",sid);if(!m||i64(*m,"Receipt")!=0||list_has_id(state_["ClaimedMailIds"],"ServerId",sid))continue;bool ok=true;for(const auto &att:(*m)["AttachInfos"].as_array()){J reward=j_obj();reward["Type"]=i64(att,"Type",i64(att,"ParcelType"));reward["TableId"]=u64(att,"TableId",u64(att,"Id"));reward["Count"]=i64(att,"Count",i64(att,"Amount",0));if(!grant(reward,&changes)){ok=false;break;}}if(!ok){state_=std::move(before);return r;}(*m)["Receipt"]=1;J marker=j_obj();marker["ServerId"]=sid;arr("ClaimedMailIds").push_back(marker);J rec=schema_default("MailReceiptInfo");rec["ServerId"]=sid;rec["Receipt"]=1;rec["ExpireTime"]=u64(*m,"ExpireTime");receipts.push_back(rec);claimed++;}if(claimed){changed=true;if(r.contains("ReceiptInfos"))r["ReceiptInfos"]=receipts;if(r.contains("ResultInfo"))r["ResultInfo"]["RewardInfo"]=reward_total(changes.as_array());fill_updated_parcels(r,changes);}return r;}

static std::vector<J> configured_rewards(const J &rules,const std::string &bucket,uint64_t id){std::vector<J> out;const J &list=rules[bucket][std::to_string(id)];for(const auto &x:list.as_array())out.push_back(x);return out;}
static std::vector<J> rewards_from_table_row(const J &row,const char *type_key="RewardParcelType",const char *id_key="RewardParcelId",const char *count_key="RewardParcelAmount"){std::vector<J> out;const J &types=row[type_key],&ids=row[id_key],&counts=row[count_key];size_t n=std::min(types.size(),std::min(ids.size(),counts.size()));for(size_t i=0;i<n;i++){int type=int(types[i].as_i64(-1));uint64_t id=ids[i].as_u64(0);int64_t count=counts[i].as_i64(0);if(type<0||!id||count<=0)continue;J r=j_obj();r["Type"]=type;r["TableId"]=id;r["Count"]=count;out.push_back(r);}return out;}
static bool list_has_scalar(const J &list,uint64_t id){for(const auto &x:list.as_array())if(id_equal(x,id))return true;return false;}
static bool parse_cost_array(const J &array,std::vector<J> &out){if(!array.is_array())return false;for(const auto &x:array.as_array()){J c=j_obj();c["Type"]=i64(x,"Type",i64(x,"ParcelType",-1));c["TableId"]=u64(x,"TableId",u64(x,"Id"));c["Count"]=i64(x,"Count",i64(x,"Amount"));if(i64(c,"Type")<0||!u64(c,"TableId")||i64(c,"Count")<=0)return false;out.push_back(c);}return true;}
static std::vector<J> request_costs(const J &req){std::vector<J> out;const J *src=nullptr;if(req["Costs"].is_array())src=&req["Costs"];else if(req["CostInfos"].is_array())src=&req["CostInfos"];else if(req["CostParcelInfos"].is_array())src=&req["CostParcelInfos"];if(src)for(const auto &x:src->as_array()){J c=j_obj();c["Type"]=i64(x,"Type",i64(x,"ParcelType",0));c["TableId"]=u64(x,"TableId",u64(x,"Id",0));c["Count"]=i64(x,"Count",i64(x,"Amount",0));if(u64(c,"TableId")&&i64(c,"Count")>0)out.push_back(c);}return out;}
static J &ensure_pack_record(J &pack_state,uint64_t group,bool &created){
    J &packs=ensure_obj(pack_state,"Packs");
    const std::string key=std::to_string(group);
    if(!packs[key].is_object()){packs[key]=j_obj();created=true;}
    J &pack=packs[key];
    auto set_default=[&](const char *name,const J &value){if(!pack.contains(name)){pack[name]=value;created=true;}};
    set_default("GroupId",J(group));set_default("OpenType",J(int64_t(1)));set_default("StoryQuestIndex",J(int64_t(0)));set_default("RepeatQuestIndex",J(int64_t(0)));set_default("MaxQuestDifficulty",J(int64_t(0)));set_default("NormalQuestBit",J(uint64_t(0)));set_default("HardQuestBit",J(uint64_t(0)));set_default("VeryHardQuestBit",J(uint64_t(0)));set_default("SubStoryQuestBit",J(uint64_t(0)));set_default("ChallengeQuestBit",J(uint64_t(0)));
    if(group==1000&&i64(pack,"StoryQuestIndex")>=9&&i64(pack,"OpenType")!=3){pack["OpenType"]=3;created=true;}
    return pack;
}
static void fill_pack_detail(J &response,uint64_t group,const J &pack){
    if(response.contains("MiniInfo")){response["MiniInfo"]["GroupId"]=group;response["MiniInfo"]["OpenType"]=i64(pack,"OpenType",1);}
    if(response.contains("DetailInfo")){J &d=response["DetailInfo"];d["GroupId"]=group;for(const char *k:{"StoryQuestIndex","RepeatQuestIndex","NormalQuestBit","HardQuestBit","VeryHardQuestBit","SubStoryQuestBit","ChallengeQuestBit","MaxQuestDifficulty"})if(pack.contains(k))d[k]=pack[k];}
}
static uint64_t packed_rank_set(uint64_t old,int bit,int rank){if(bit<1||bit>21||rank<1||rank>3)return old;unsigned shift=unsigned((bit-1)*3);uint64_t mask=uint64_t(7)<<shift;uint64_t previous=(old>>shift)&7u;if(uint64_t(rank)<=previous)return old;return (old&~mask)|(uint64_t(rank)<<shift);}
static bool is_success_result(const J &rules,int result){const J &values=rules["successResultValues"];if(!values.is_array())return result==0;for(const auto &v:values.as_array())if(v.as_i64()==result)return true;return false;}
J Backend::handle_quest(const std::string &op,const J &req,J r,bool &changed){
    const bool start=op=="ScenarioPackQuestStageStart",end=op=="ScenarioPackQuestStageEnd";
    if(op=="ScenarioPackLogin"){
        uint64_t group=u64(req,"GroupId");if(!group)return r;bool created=false;J &pack=ensure_pack_record(state_["PackState"],group,created);fill_pack_detail(r,group,pack);changed=created;return r;
    }
    if(op=="ScenarioPackMissionInfo"||op=="ScenarioPackMissionClear"||op=="ScenarioPackMissionClearAll"||op=="ScenarioPackMissionPointReward"){
        uint64_t group=u64(req,"GroupId"),task=u64(req,"TaskId");std::string k=make_key(group,uint64_t(i64(req,"ScenarioPackType")));J &p=obj("PackState")["MissionState"][k];
        if(op=="ScenarioPackMissionClear"||op=="ScenarioPackMissionClearAll"){
            J &claimed=ensure_arr(p,"ClaimedTasks");if(!task&&op=="ScenarioPackMissionClearAll"){for(const auto &x:tables_["Mission_Task"].as_array())if(u64(x,"GroupId")==group){uint64_t tid=u64(x,"Id");if(!list_has_id(claimed,"TaskId",tid)){J m=j_obj();m["TaskId"]=tid;claimed.push_back(m);}}}else if(task&&!list_has_id(claimed,"TaskId",task)){J m=j_obj();m["TaskId"]=task;claimed.push_back(m);}changed=true;
        }
        if(op=="ScenarioPackMissionPointReward"){auto rw=rewards_from(req,op);J changes=j_arr();if(grant_batch(rw,&changes)){changed=!rw.empty();fill_updated_parcels(r,changes);if(r.contains("ResultInfo"))r["ResultInfo"]["RewardInfo"]=reward_total(rw);}}
        return r;
    }
    if(op=="ScenarioPackShopBuy"||op=="ScenarioPackUnlockByItem"){
        auto costs=request_costs(req);auto rewards=rewards_from(req,op);J changes=j_arr();if(spend_batch(costs,&changes)&&grant_batch(rewards,&changes)){changed=!costs.empty()||!rewards.empty();fill_updated_parcels(r,changes);}return r;
    }
    if(op=="ScenarioPackSPCPuzzleStart"||op=="ScenarioPackSPCPuzzleEnd"){
        uint64_t id=u64(req,"PuzzleId",u64(req,"Id"));if(!id)return r;std::string key=std::to_string(id);J &p=obj("PackState")["Puzzle"][key];
        if(op=="ScenarioPackSPCPuzzleStart"){p["StartedAt"]=now_sec();p["Active"]=true;changed=true;}
        else if(p["Active"].as_bool()){auto rw=configured_rewards(rules_,"puzzleRewards",id);J changes=j_arr();if(!grant_batch(rw,&changes))return r;p["Active"]=false;p["Completed"]=true;changed=true;if(r.contains("ResultInfo"))r["ResultInfo"]["RewardInfo"]=reward_total(rw);fill_updated_parcels(r,changes);}
        return r;
    }
    if(!start&&!end)return r;
    const uint64_t group=u64(req,"GroupId"),quest=u64(req,"QuestId");const int difficulty=int(i64(req,"Difficulty")),count=int(i64(req,"Count",1)),formation=int(i64(req,"FormationIndex"));
    if(!group||!quest||difficulty<0||difficulty>3)return r;
    const J *row=table_row("ScenarioPack_Quest","Id",quest);if(!row||u64(*row,"ScenarioPackId")!=group)return r;
    const int kind=int(i64(*row,"ScenarioPackType",-1)),bit=int(i64(*row,"BitIndex"));if(bit<1)return r;
    bool packCreated=false;J &pack=ensure_pack_record(state_["PackState"],group,packCreated);if(packCreated)changed=true;
    if(i64(pack,"OpenType",1)==0)return r;
    const bool storyKind=kind==0||kind==8;
    if(storyKind){if(difficulty!=0||count<0||count>1)return r;const int storyIndex=int(i64(pack,"StoryQuestIndex"));if(start){if(bit>storyIndex+1)return r;J &active=obj("PackState")["ActiveStory"];if(active.is_object()){if(u64(active,"GroupId")==group&&u64(active,"QuestId")==quest&&i64(active,"Bit")==bit)return r;return r;}J receipt=j_obj();receipt["GroupId"]=group;receipt["QuestId"]=quest;receipt["Bit"]=bit;receipt["Difficulty"]=0;receipt["Count"]=1;receipt["StartedAt"]=now_sec();obj("PackState")["ActiveStory"]=receipt;changed=true;return r;}
        J &ps=obj("PackState");J &active=ps["ActiveStory"];J &last=ps["LastStoryResult"];if(!active.is_object()){if(last.is_object()&&u64(last,"GroupId")==group&&u64(last,"QuestId")==quest&&i64(last,"ResultType")==i64(req,"ResultType")&&i64(last,"Count")==count)return r;return r;}if(u64(active,"GroupId")!=group||u64(active,"QuestId")!=quest||i64(active,"Bit")!=bit||i64(active,"Count")!=1||count<0||count>1)return r;
        int result=int(i64(req,"ResultType",0));if(is_success_result(rules_,result)&&bit>i64(pack,"StoryQuestIndex"))pack["StoryQuestIndex"]=bit;J record=j_obj();record["GroupId"]=group;record["QuestId"]=quest;record["Bit"]=bit;record["Difficulty"]=0;record["Count"]=count;record["ResultType"]=result;record["CompletedAt"]=now_sec();ps["LastStoryResult"]=record;ps.as_object().erase("ActiveStory");if(group==1000&&i64(pack,"StoryQuestIndex")>=9)pack["OpenType"]=3;fill_pack_detail(r,group,pack);if(r.contains("ResultInfo")){r["ResultInfo"]["ResultType"]=result;r["ResultInfo"]["RewardInfo"]=reward_total({});}changed=true;return r;
    }
    const bool allowedKind=kind==1||kind==2||kind==3||kind==5||kind==12;const int maxBit=kind==12?63:21;if(!allowedKind||bit>maxBit||count<1||count>100||formation<0)return r;
    J &ps=obj("PackState");J &active=ps["ActiveQuest"];
    if(start){
        if(active.is_object()){if(u64(active,"GroupId")==group&&u64(active,"QuestId")==quest&&i64(active,"Kind")==kind&&i64(active,"Bit")==bit&&i64(active,"Difficulty")==difficulty&&i64(active,"Formation")==formation&&i64(active,"Count")==count)return r;return r;}
        std::vector<J> costs;uint64_t ticketId=u64(*row,"CostTicketId");int64_t amount=i64(*row,"CostTicketAmount");if(ticketId&&amount>0){J c=j_obj();c["Type"]=7;c["TableId"]=ticketId;c["Count"]=amount*count;costs.push_back(c);}if(costs.empty())costs=request_costs(req);
        J paid=j_arr();if(!spend_batch(costs,&paid))return r;J receipt=j_obj();receipt["GroupId"]=group;receipt["QuestId"]=quest;receipt["Kind"]=kind;receipt["Bit"]=bit;receipt["Difficulty"]=difficulty;receipt["Formation"]=formation;receipt["Count"]=count;receipt["Costs"]=paid;for(const auto &f:state_["formations"].as_array())if(i64(f,"Index")==formation&&f["CharacterTableIds"].is_array()){receipt["BattleRosterSnapshot"]=f["CharacterTableIds"];break;}receipt["StartedAt"]=now_sec();ps["ActiveQuest"]=receipt;changed=true;fill_updated_parcels(r,paid);return r;
    }
    int result=int(i64(req,"ResultType",0)),rank=int(i64(req,"RankType",0));
    if(!active.is_object()){J &last=ps["LastQuestResult"];if(last.is_object()&&u64(last,"GroupId")==group&&u64(last,"QuestId")==quest&&i64(last,"Difficulty")==difficulty&&i64(last,"ResultType")==result&&i64(last,"RankType")==rank&&i64(last,"Count")==count)return r;return r;}
    if(u64(active,"GroupId")!=group||u64(active,"QuestId")!=quest||i64(active,"Kind")!=kind||i64(active,"Bit")!=bit||i64(active,"Difficulty")!=difficulty||i64(active,"Count")!=count)return r;
    const bool success=is_success_result(rules_,result);if(success&&(rank<1||rank>3))return r;
    auto rewards=success?rewards_from(req,op):std::vector<J>{};if(success&&rewards.empty())rewards=configured_rewards(rules_,"questRewards",quest);J changes=j_arr();if(!grant_batch(rewards,&changes))return r;
    if(success){const char *field=kind==1?"NormalQuestBit":(kind==2?"HardQuestBit":(kind==3?"VeryHardQuestBit":(kind==5?"ChallengeQuestBit":"SubStoryQuestBit")));uint64_t old=u64(pack,field);if(kind==12)pack[field]=old|(uint64_t(1)<<bit);else pack[field]=packed_rank_set(old,bit,rank);pack["MaxQuestDifficulty"]=std::max<int64_t>(i64(pack,"MaxQuestDifficulty"),difficulty);}J record=j_obj();record["GroupId"]=group;record["QuestId"]=quest;record["Kind"]=kind;record["Bit"]=bit;record["Difficulty"]=difficulty;record["Count"]=count;record["ResultType"]=result;record["RankType"]=rank;record["CompletedAt"]=now_sec();ps["LastQuestResult"]=record;ps.as_object().erase("ActiveQuest");if(r.contains("ResultInfo")){r["ResultInfo"]["ResultType"]=result;r["ResultInfo"]["RewardInfo"]=reward_total(rewards);}fill_pack_detail(r,group,pack);fill_updated_parcels(r,changes);changed=true;return r;
}

J Backend::handle_farming(const std::string &op,const J &req,J r,bool &changed){
    if(op=="FarmingDungeonInfo"){uint64_t group=u64(req,"GroupId");J &f=obj("FarmingState");J &groups=ensure_obj(f,"Groups");J &g=ensure_obj(groups,std::to_string(group));if(!g.contains("GroupId"))g["GroupId"]=group;if(!g.contains("GoldBit"))g["GoldBit"]=uint64_t(0);if(!g.contains("CharacterExpBit"))g["CharacterExpBit"]=uint64_t(0);if(!g.contains("ArtifactExpBit"))g["ArtifactExpBit"]=uint64_t(0);if(r.contains("DetailInfo")){r["DetailInfo"]["GroupId"]=group;r["DetailInfo"]["GoldBit"]=g["GoldBit"];r["DetailInfo"]["CharacterExpBit"]=g["CharacterExpBit"];r["DetailInfo"]["ArtifactExpBit"]=g["ArtifactExpBit"];}return r;}
    const bool start=op=="FarmingDungeonQuestStart",end=op=="FarmingDungeonQuestEnd";if(!start&&!end)return r;
    uint64_t group=u64(req,"GroupId"),quest=u64(req,"QuestId");int count=int(i64(req,"Count",1)),formation=int(i64(req,"FormationIndex"));if(!group||!quest||count<1||count>99)return r;
    const J *row=table_row("FarmingDungeon_Quest","Id",quest);if(!row||u64(*row,"GroupId")!=group)return r;int type=int(i64(*row,"FarmingDungeonType")),bit=int(i64(*row,"BitIndex"));if(type<1||type>3||bit<1||bit>21)return r;
    J &fs=obj("FarmingState");J &groups=ensure_obj(fs,"Groups");J &groupState=ensure_obj(groups,std::to_string(group));if(!groupState.contains("GroupId"))groupState["GroupId"]=group;if(!groupState.contains("GoldBit"))groupState["GoldBit"]=uint64_t(0);if(!groupState.contains("CharacterExpBit"))groupState["CharacterExpBit"]=uint64_t(0);if(!groupState.contains("ArtifactExpBit"))groupState["ArtifactExpBit"]=uint64_t(0);
    J &active=fs["Active"];
    if(start){if(formation<1||formation>10)return r;if(active.is_object()){if(u64(active,"GroupId")==group&&u64(active,"QuestId")==quest&&i64(active,"Type")==type&&i64(active,"Bit")==bit&&i64(active,"Formation")==formation&&i64(active,"Count")==count)return r;return r;}
        std::vector<J> costs;uint64_t ticketId=u64(*row,"CostTicketId");int64_t amount=i64(*row,"CostTicketAmount");if(ticketId&&amount>0){J c=j_obj();c["Type"]=7;c["TableId"]=ticketId;c["Count"]=amount*count;costs.push_back(c);}if(costs.empty())costs=request_costs(req);
        J paid=j_arr();if(!spend_batch(costs,&paid))return r;J receipt=j_obj();receipt["GroupId"]=group;receipt["QuestId"]=quest;receipt["Type"]=type;receipt["Bit"]=bit;receipt["Formation"]=formation;receipt["Count"]=count;receipt["Costs"]=paid;receipt["StartedAt"]=now_sec();fs["Active"]=receipt;changed=true;fill_updated_parcels(r,paid);return r;
    }
    if(!active.is_object()||u64(active,"GroupId")!=group||u64(active,"QuestId")!=quest||i64(active,"Type")!=type||i64(active,"Bit")!=bit||i64(active,"Count")!=count)return r;
    int result=int(i64(req,"ResultType",0)),rank=int(i64(req,"RankType",0));const bool success=is_success_result(rules_,result);if(success&&(rank<1||rank>3))return r;
    std::vector<J> rewards=success?rewards_from(req,op):std::vector<J>{};if(success&&rewards.empty())rewards=configured_rewards(rules_,"farmingRewards",quest);J changes=j_arr();if(!grant_batch(rewards,&changes))return r;
    if(success){const char *field=type==1?"GoldBit":(type==2?"CharacterExpBit":"ArtifactExpBit");groupState[field]=packed_rank_set(u64(groupState,field),bit,rank);}
    J last=active;last["ResultType"]=result;last["RankType"]=rank;last["CompletedAt"]=now_sec();fs["LastResult"]=last;fs.as_object().erase("Active");if(r.contains("ResultInfo"))r["ResultInfo"]["RewardInfo"]=reward_total(rewards);if(r.contains("DetailInfo")){r["DetailInfo"]["GroupId"]=group;r["DetailInfo"]["GoldBit"]=groupState["GoldBit"];r["DetailInfo"]["CharacterExpBit"]=groupState["CharacterExpBit"];r["DetailInfo"]["ArtifactExpBit"]=groupState["ArtifactExpBit"];}fill_updated_parcels(r,changes);changed=true;return r;
}

J Backend::handle_raid(const std::string &op,const J &req,J r,bool &changed){if(op=="RaidLogin"){if(r.contains("RaidOpenInfos")){J a=j_arr();for(const auto &row:tables_["Raid"].as_array()){J x=schema_default("RaidOpenInfo");x["TableId"]=u64(row,"Id");x["RaidLevel"]=i64(row,"DifficultyLevel");a.push_back(x);}r["RaidOpenInfos"]=a;}return r;}if(op=="RaidClaimReward"){auto rw=rewards_from(req,op);J c=j_arr();if(grant_batch(rw,&c)){fill_updated_parcels(r,c);changed=!rw.empty();}return r;}bool start=op=="RaidStageStart",end=op=="RaidStageEnd";if(!start&&!end)return r;uint64_t tableId=u64(req,"TableId");int level=int(i64(req,"RaidLevel",1));std::string key="raid:"+make_key(tableId,uint64_t(level));if(start){J receipt=j_obj();receipt["Key"]=key;receipt["Category"]="raid";receipt["TableId"]=tableId;receipt["RaidLevel"]=level;receipt["StartedAt"]=now_sec();receipt["FormationIndex"]=i64(req,"FormationIndex");arr("ReceiptInfos").push_back(receipt);changed=true;return r;}auto &receipts=arr("ReceiptInfos").as_array();auto it=std::find_if(receipts.begin(),receipts.end(),[&](const J &x){return str(x,"Key")==key;});if(it==receipts.end())return r;int result=int(i64(req,"ResultType",0));uint64_t score=u64(req["Report"],"Score",u64(req["Report"],"TotalScore",u64(req["SummaryReport"],"Score")));std::vector<J> rewards=rewards_from(req,op);if(result==0&&rewards.empty()){uint64_t rewardGroup=10001;for(const auto &s:tables_["RaidSchedule"].as_array()){rewardGroup=u64(s,"RaidRewardId",rewardGroup);break;}const J *best=nullptr;for(const auto &row:tables_["Raid_Reward"].as_array())if(u64(row,"GroupId")==rewardGroup&&u64(row,"ReqPoint")<=score){if(!best||u64(row,"ReqPoint")>u64(*best,"ReqPoint"))best=&row;}if(best){const J &types=(*best)["RewardParcelType"],&ids=(*best)["RewardParcelId"],&amounts=(*best)["RewardParcelAmount"];for(size_t i=0;i<ids.size();i++){J rw=j_obj();rw["Type"]=types[i].as_i64();rw["TableId"]=u64(ids[i]);rw["Count"]=amounts[i].as_i64();rewards.push_back(rw);}}}J changes=j_arr();if(!grant_batch(rewards,&changes))return r;J record=j_obj();record["TableId"]=tableId;record["RaidLevel"]=level;record["Score"]=score;record["ResultType"]=result;record["CompletedAt"]=now_sec();obj("quests")[key]=record;receipts.erase(it);if(r.contains("ResultInfo")){r["ResultInfo"]["Score"]=score;r["ResultInfo"]["RewardInfo"]=reward_total(rewards);}fill_updated_parcels(r,changes);changed=true;return r;}

J Backend::handle_missions(const std::string &op,const J &req,J r,bool &changed){
    if(op=="DailyMissionInfo"||op=="WeeklyMissionInfo"||op=="ChallengeMissionInfo"||op=="GuideBookMissionInfo"){
        if(op=="DailyMissionInfo"&&r.contains("MissionList")){J list=j_arr();for(const auto &row:tables_["DailyMission_Task"].as_array()){J m=schema_default("DailyMission");for(const auto &kv:row.as_object())if(m.contains(kv.first))m[kv.first]=kv.second;uint64_t id=u64(row,"Id");m["MissionNo"]=u64(row,"MissionNumber");m["MissionValue"]=state_["DailyState"]["TaskValues"][std::to_string(id)].as_u64();m["MissionClear"]=list_has_scalar(state_["DailyState"]["ClaimedTaskIds"],id)?1:0;list.push_back(m);}r["MissionList"]=list;r["MissionPoint"]=i64(state_["DailyState"],"MissionPoint");r["LastRewardedMissionPoint"]=i64(state_["DailyState"],"LastRewardedMissionPoint");}
        if(op=="GuideBookMissionInfo"&&r.contains("GuideBookMissions")){uint64_t systemType=u64(req,"MissionSystemType",8);J chapters=j_arr();for(const auto &chapter:tables_["GuideBook_Mission"].as_array()){if(u64(chapter,"SystemType")!=systemType)continue;uint64_t group=u64(chapter,"TaskGroupId");int chapterPoint=0;bool all=true;J tasks=j_arr();for(const auto &task:tables_["Mission_Task"].as_array()){if(u64(task,"GroupId")!=group)continue;uint64_t id=u64(task,"Id");bool claimed=list_has_scalar(state_["ClaimedGuideMissionTaskIds"],id);J t=schema_default("GuideBookMissionTask");t["MissionTaskId"]=id;t["MissionValue"]=state_["GuideState"]["TaskValues"][std::to_string(id)].as_u64();t["MissionClear"]=claimed?1:0;tasks.push_back(t);if(claimed)chapterPoint+=int(i64(task,"RewardPoint"));else all=false;}J c=schema_default("GuideBookMission");c["Chapter"]=u64(chapter,"SystemIndex");c["ChapterPoint"]=chapterPoint;c["ChapterClear"]=all&&!tasks.as_array().empty()?1:0;c["IsLocked"]=false;c["GuideBookMissionTasks"]=tasks;chapters.push_back(c);}r["MissionSystemType"]=systemType;r["GuideBookMissions"]=chapters;}
        return r;
    }
    if(op=="GuideBookOfferingLevelUp"){
        uint64_t offeringId=u64(req,"OfferingId");const J *offering=table_row("GuideBookOffering","Id",offeringId);if(!offering)return r;uint64_t group=u64(*offering,"LevelGroupId",1);int current=int(i64(state_["GuideState"],"OfferingLevel",0)),target=current+1;const J *levelRow=nullptr;for(const auto &row:tables_["GuideBookOffering_Level"].as_array())if(u64(row,"GroupId")==group&&i64(row,"Level")==target){levelRow=&row;break;}if(!levelRow)return r;uint64_t costId=u64(*levelRow,"ReqLevelUpItemId");int64_t costCount=i64(*levelRow,"ReqLevelUpItemAmount");if(!costId||costCount<=0)return r;
        J before=state_,changes=j_arr();J *inventoryItem=nullptr;for(auto &item:arr("inventory").as_array())if(u64(item,"TableId")==costId){if(inventoryItem)return r;inventoryItem=&item;}if(!inventoryItem||i64(*inventoryItem,"Count")<costCount)return r;int costType=int(i64(*inventoryItem,"Type"));(*inventoryItem)["Count"]=i64(*inventoryItem,"Count")-costCount;changes.push_back(make_reward(costType,costId,-costCount));auto rewards=rewards_from_table_row(*levelRow);if(!grant_batch(rewards,&changes)){state_=std::move(before);return r;}state_["GuideState"]["OfferingId"]=offeringId;state_["GuideState"]["OfferingLevel"]=target;changed=true;if(r.contains("ResultInfo"))r["ResultInfo"]["RewardInfo"]=reward_total(rewards);fill_updated_parcels(r,changes);return r;
    }
    if(op=="GuideBookMissionClear"||op=="GuideBookMissionClearAll"){
        uint64_t systemType=u64(req,"MissionSystemType");std::set<uint64_t> groups;if(op=="GuideBookMissionClear"){uint64_t chapterIndex=u64(req,"Chapter"),taskId=u64(req,"TaskId");if(!chapterIndex||!taskId)return r;for(const auto &chapter:tables_["GuideBook_Mission"].as_array())if(u64(chapter,"SystemType")==systemType&&u64(chapter,"SystemIndex")==chapterIndex)groups.insert(u64(chapter,"TaskGroupId"));}else{for(const auto &chapter:tables_["GuideBook_Mission"].as_array())if(u64(chapter,"SystemType")==systemType)groups.insert(u64(chapter,"TaskGroupId"));}
        std::vector<uint64_t> claimIds;std::vector<J> rewards;for(const auto &task:tables_["Mission_Task"].as_array()){uint64_t id=u64(task,"Id");if(!groups.count(u64(task,"GroupId"))||(op=="GuideBookMissionClear"&&id!=u64(req,"TaskId"))||list_has_scalar(state_["ClaimedGuideMissionTaskIds"],id))continue;claimIds.push_back(id);auto rowRewards=rewards_from_table_row(task);rewards.insert(rewards.end(),rowRewards.begin(),rowRewards.end());}
        if(claimIds.empty())return r;J before=state_,changes=j_arr();if(!grant_batch(rewards,&changes)){state_=std::move(before);return r;}for(uint64_t id:claimIds)arr("ClaimedGuideMissionTaskIds").push_back(id);changed=true;if(r.contains("ResultInfo"))r["ResultInfo"]["RewardInfo"]=reward_total(rewards);fill_updated_parcels(r,changes);return r;
    }
    if(op=="GuideBookMissionChapterClear")return r;
    if(op=="DailyMissionPointReward"||op=="WeeklyMissionClear"||op=="LimitedMissionClear"||op=="ChallengeMissionTrophyReward"||op=="ChallengeMissionClearAll"){
        uint64_t task=op=="LimitedMissionClear"?u64(req,"MissionTaskId"):u64(req,"TaskId",u64(req,"MissionId"));std::string bucket=op.find("Daily")==0?"DailyState":op.find("Weekly")==0?"WeeklyState":op.find("Challenge")==0?"ChallengeState":"LimitedState";J &s=obj(bucket);J &claimed=ensure_arr(s,"Claimed");std::string mark=std::to_string(task?task:u64(req,"TrophyType",u64(req,"GroupId")));if(mark=="0")return r;if(list_has_id(claimed,"Id",task?task:u64(req,"TrophyType",u64(req,"GroupId"))))return r;std::vector<J> rewards;if(op=="LimitedMissionClear"||op=="WeeklyMissionClear")for(const auto &row:tables_["Mission_Task"].as_array())if(u64(row,"Id")==task){rewards=rewards_from_table_row(row);break;}if(rewards.empty())rewards=rewards_from(req,op);J changes=j_arr();if(!grant_batch(rewards,&changes))return r;J marker=j_obj();marker["Id"]=task?task:u64(req,"TrophyType",u64(req,"GroupId"));claimed.push_back(marker);changed=true;if(r.contains("ResultInfo"))r["ResultInfo"]["RewardInfo"]=reward_total(rewards);fill_updated_parcels(r,changes);return r;
    }
    return r;
}

J Backend::handle_date(const std::string &op,const J &req,J r,bool &changed){
    J &ds=obj("DateFieldState");
    auto favor_level=[&](uint64_t exp)->int{int level=1;for(const auto &row:tables_["Character_FavorLevel"].as_array())if(exp>=u64(row,"AccumulationExp")&&i64(row,"CurrentLevel")>level)level=int(i64(row,"CurrentLevel"));return level;};
    auto set_favor_response=[&](uint64_t character,const J &favor){if(r.contains("FavorInfo")){r["FavorInfo"]["TableId"]=character;r["FavorInfo"]["FavorExp"]=u64(favor,"FavorExp",u64(favor,"Exp"));r["FavorInfo"]["DateExp"]=u64(favor,"DateExp");r["FavorInfo"]["RewardFlag"]=u64(favor,"RewardFlag");}};
    if(op=="DateFieldEventStart"){uint64_t event=u64(req,"NodeEventId");const J *row=table_row("DateField_NodeEvent","Id",event);if(!row)return r;J info=schema_default("DateFieldNodeEventInfo");info["GroupId"]=u64(*row,"GroupId");ds["ActiveNodeEventId"]=event;ds["ActiveStartedAt"]=now_sec();changed=true;if(r.contains("NodeEventInfo"))r["NodeEventInfo"]=info;if(r.contains("DateFieldInfo"))r["DateFieldInfo"]["IsDay"]=true;return r;}
    if(op=="DateFieldEventPresent"){
        uint64_t event=u64(req,"NodeEventId");const J *eventRow=table_row("DateField_NodeEvent","Id",event);if(!eventRow||!u64(*eventRow,"CharacterId")||!req["PresentItems"].is_array())return r;uint64_t character=u64(*eventRow,"CharacterId");uint64_t totalCount=0;int64_t addedExp=0;std::vector<J> costs;for(const auto &pair:req["PresentItems"].as_array()){uint64_t itemId=u64(pair,"Key");int64_t count=i64(pair,"Value");if(!itemId||count<=0)return r;const J *item=table_row("Item_Favor","Id",itemId);if(!item)return r;totalCount+=uint64_t(count);if(totalCount>uint64_t(std::max<int64_t>(1,common_i64("DateFieldMaxGiftCount",10))))return r;int64_t oneExp=i64(*item,"DefaultFavorExp");if(oneExp<=0||count>INT32_MAX||oneExp>(INT32_MAX-addedExp)/count)return r;addedExp+=oneExp*count;J *owned=nullptr;for(auto &entry:arr("inventory").as_array())if(u64(entry,"TableId")==itemId){if(owned)return r;owned=&entry;}if(!owned||i64(*owned,"Count")<count)return r;J c=j_obj();c["Type"]=i64(*owned,"Type");c["TableId"]=itemId;c["Count"]=count;costs.push_back(c);}
        if(totalCount==0||addedExp<=0)return r;J before=state_,changes=j_arr();if(!spend_batch(costs,&changes)){state_=std::move(before);return r;}J &favor=ensure_obj(ensure_obj(ds,"Favor"),std::to_string(character));uint64_t from=u64(favor,"FavorExp",u64(favor,"Exp"));if(from>uint64_t(INT32_MAX)-uint64_t(addedExp)){state_=std::move(before);return r;}uint64_t to=from+uint64_t(addedExp);favor["FavorExp"]=to;favor["DateExp"]=u64(favor,"DateExp");favor["RewardFlag"]=u64(favor,"RewardFlag");favor["Level"]=favor_level(to);if(r.contains("ResultInfo")){r["ResultInfo"]["FavorExpInfo"]["FromExp"]=from;r["ResultInfo"]["FavorExpInfo"]["ToExp"]=to;r["ResultInfo"]["FavorExpInfo"]["AddedExp"]=addedExp;r["ResultInfo"]["DateExpInfo"]["FromExp"]=u64(favor,"DateExp");r["ResultInfo"]["DateExpInfo"]["ToExp"]=u64(favor,"DateExp");r["ResultInfo"]["DateExpInfo"]["AddedExp"]=0;}set_favor_response(character,favor);fill_updated_parcels(r,changes);changed=true;return r;
    }
    if(op=="DateFieldClaimReward"){
        uint64_t character=u64(req,"CharacterId");if(!character)return r;J &favor=ensure_obj(ensure_obj(ds,"Favor"),std::to_string(character));uint64_t exp=u64(favor,"FavorExp",u64(favor,"Exp"));int level=favor_level(exp);std::vector<J> rewards;std::vector<uint64_t> rowIds;uint64_t rewardFlag=u64(favor,"RewardFlag");for(const auto &row:tables_["Character_FavorReward"].as_array()){if(u64(row,"CharacterId")!=character||i64(row,"FavorLevel")>level||list_has_scalar(state_["ClaimedFavorRewardIds"],u64(row,"Id")))continue;auto configured=configured_rewards(rules_,"favorRewards",u64(row,"CommonRewardId"));if(configured.empty())continue;rewards.insert(rewards.end(),configured.begin(),configured.end());rowIds.push_back(u64(row,"Id"));int bit=int(i64(row,"BitIndex"));if(bit>=1&&bit<=64)rewardFlag|=uint64_t(1)<<(bit-1);}
        if(rowIds.empty())return r;J before=state_,changes=j_arr();if(!grant_batch(rewards,&changes)){state_=std::move(before);return r;}for(uint64_t id:rowIds)arr("ClaimedFavorRewardIds").push_back(id);favor["RewardFlag"]=rewardFlag;favor["Level"]=level;if(r.contains("ResultInfo"))r["ResultInfo"]["RewardInfo"]=reward_total(rewards);set_favor_response(character,favor);fill_updated_parcels(r,changes);changed=true;return r;
    }
    if(op=="DateFieldNodeInfoUpdate"){uint64_t node=u64(req,"NodeId");if(!node)return r;ensure_obj(ds,"Nodes")[std::to_string(node)]=req;changed=true;return r;}
    if(op=="DateFieldTimePass"){ds["LastTimePass"]=now_sec();changed=true;return r;}
    return r;
}

J Backend::handle_installation(const std::string &op,const J &req,J r,bool &changed){if(op=="InstallationList"||op=="BarracksInstallationInfo"||op=="BuffInstallationLoad"){if(r.contains("BarracksInfo")){r["BarracksInfo"]=schema_default("BarracksInfo");r["BarracksInfo"]["InstallationInfos"]=state_["Installations"];}if(r.contains("AvailableBuffTableIds"))r["AvailableBuffTableIds"]=j_arr();if(r.contains("AvailableMissionTableIds"))r["AvailableMissionTableIds"]=j_arr();if(r.contains("Info"))r["Info"]=schema_default("BuffInstallationInfo");return r;}uint64_t sid=u64(req,"InstallationServerId");J *inst=find_id(arr("Installations"),"ServerId",sid);if(!inst&&sid){J n=j_obj();n["ServerId"]=sid;n["TableId"]=u64(req,"TableId");n["Level"]=1;n["Exp"]=0;n["Agents"]=j_arr();n["Tasks"]=j_arr();arr("Installations").push_back(n);inst=&arr("Installations").as_array().back();}if(op=="BarracksInstallationLevelUp"||op=="BuffInstallationBuffLevelUp"||op=="BuffInstallationInteractLevelUp"){if(!inst)return r;(*inst)["Level"]=i64(*inst,"Level",1)+1;changed=true;if(r.contains("BarracksInfo"))r["BarracksInfo"]=*inst;fill_updated_parcels(r,j_arr());return r;}if(op=="BarracksInstallationRegisterAgent"){if(!inst)return r;J &a=ensure_arr(*inst,"Agents");for(const auto &x:req["CharacterIds"].as_array())if(!list_has_id(a,"CharacterId",x.as_u64())){J y=j_obj();y["CharacterId"]=x.as_u64();a.push_back(y);}changed=true;return r;}if(op=="BarracksInstallationRunTask"){if(!inst)return r;J rewards=j_arr();for(const auto &task:req["BarracksTaskInfos"].as_array()){uint64_t tid=u64(task,"TableId",u64(task,"Id"));for(const auto &row:tables_["Ins_Barracks_Reward"].as_array())if(u64(row,"Id")==u64(task,"RewardId",tid)){J x=j_obj();x["Type"]=i64(row,"RewardParcelType");x["TableId"]=u64(row,"RewardParcelId");x["Count"]=i64(row,"RewardParcelAmount");rewards.push_back(x);}}std::vector<J> rw(rewards.as_array().begin(),rewards.as_array().end());J changes=j_arr();if(grant_batch(rw,&changes)){changed=!rw.empty();fill_updated_parcels(r,changes);}if(r.contains("BarracksInfo"))r["BarracksInfo"]=*inst;return r;}if(op=="InstallationUnlock"){J &u=ensure_arr(state_["Progress"],"UnlockInfos");uint64_t id=u64(req,"InstallationTableId",u64(req,"TableId"));if(!list_has_id(u,"TableId",id)){J x=j_obj();x["TableId"]=id;u.push_back(x);changed=true;}return r;}if(op=="BuffInstallationCharacterBuff"||op=="BuffInstallationUnlockBuffCharacterSlot"||op=="BuffInstallationMissionUpdate"){J &s=obj("BuffInstallationState");s[std::to_string(u64(req,"TableId"))]=req;changed=true;return r;}return r;}

J Backend::handle_shop(const std::string &op,const J &req,J r,bool &changed){if(op!="ShopBuyItem"&&op!="ShopProductList")return r;if(op=="ShopProductList")return r;uint64_t id=u64(req,"ShopItemTableId");int count=int(i64(req,"Count",1));if(count<1||count>999)return r;J configured=rules_["shopItems"][std::to_string(id)];std::vector<J> costs,rewards;if(configured["costs"].is_array())for(auto &x:configured["costs"].as_array()){J c=x;c["Count"]=i64(c,"Count",1)*count;costs.push_back(c);}if(configured["rewards"].is_array())for(auto &x:configured["rewards"].as_array()){J z=x;z["Count"]=i64(z,"Count",1)*count;rewards.push_back(z);}if(costs.empty()||rewards.empty())return r;J changes=j_arr();if(!spend_batch(costs,&changes)||!grant_batch(rewards,&changes))return r;changed=true;if(r.contains("ResultInfo"))r["ResultInfo"]["RewardInfo"]=reward_total(rewards);fill_updated_parcels(r,changes);return r;}

J Backend::handle_misc(const std::string &op,const J &req,J r,bool &changed){if(op=="FormationChange"||op=="FormationChangeName"){if(req["FormationInfos"].is_array()){for(const auto &x:req["FormationInfos"].as_array()){int index=int(i64(x,"Index"));J *f=nullptr;for(auto &y:arr("formations").as_array())if(i64(y,"Index")==index){f=&y;break;}if(!f){arr("formations").push_back(x);f=&arr("formations").as_array().back();}else *f=x;}changed=true;}return r;}if(op=="LobbySetBackground"){uint64_t id=u64(req,"BackgroundId",u64(req,"TableId"));if(id){state_["LobbyBackgroundId"]=id;changed=true;}return r;}if(op=="LobbySetCharacter"){state_["LobbyCharacterId"]=u64(req,"CharacterId",u64(req,"TableId"));changed=true;return r;}if(op=="ItemUseConsumable"||op=="ItemUseSelectable"){std::vector<J> costs;uint64_t itemId=u64(req,"ItemId");int count=int(i64(req,"Count",1));if(op=="ItemUseConsumable"&&req["Items"].is_array()){for(const auto &x:req["Items"].as_array()){J c=j_obj();c["Type"]=int64_t(0);c["TableId"]=x[0].as_u64();c["Count"]=x[1].as_i64();costs.push_back(c);}}else if(itemId&&count>0){J c=j_obj();c["Type"]=i64(req,"Type",0);c["TableId"]=itemId;c["Count"]=count;costs.push_back(c);}std::vector<J> rewards=rewards_from(req,op);J changes=j_arr();if(spend_batch(costs,&changes)&&grant_batch(rewards,&changes)){changed=!costs.empty()||!rewards.empty();fill_updated_parcels(r,changes);}return r;}if(op=="AttendanceReward"||op=="CardBookReward"||op=="ChallengeMissionTrophyReward"||op=="RaidClaimReward"){auto rw=rewards_from(req,op);if(rw.empty())rw=configured_rewards(rules_,"fixedRewards",u64(req,"RewardId",u64(req,"Id")));J changes=j_arr();if(grant_batch(rw,&changes)){changed=!rw.empty();if(r.contains("ResultInfo"))r["ResultInfo"]["RewardInfo"]=reward_total(rw);fill_updated_parcels(r,changes);}return r;}if(op=="AttendanceReward"||op=="ChallengeMissionTrophyReward")return r;if(op=="ViewHelpBook"){state_["HelpBookRead"]=true;changed=true;return r;}if(op=="TutorialContentView"){uint64_t bit=u64(req,"ContentBit");state_["TutorialInfo"]["ContentBits"]=u64(state_["TutorialInfo"],"ContentBits")|bit;changed=bit!=0;return r;}if(op=="InstallationUnlock")return handle_installation(op,req,r,changed);if(op=="ScenarioPackLogin"||op=="ScenarioPackMissionInfo"||op=="ScenarioPackMissionClear"||op=="ScenarioPackMissionClearAll"||op=="ScenarioPackMissionPointReward"||op=="ScenarioPackShopBuy"||op=="ScenarioPackUnlockByItem"||op=="ScenarioPackSPCPuzzleStart"||op=="ScenarioPackSPCPuzzleEnd")return handle_quest(op,req,r,changed);if(op=="DateFieldEventStart"||op=="DateFieldEventPresent"||op=="DateFieldClaimReward"||op=="DateFieldNodeInfoUpdate"||op=="DateFieldTimePass")return handle_date(op,req,r,changed);if(op=="BarracksInstallationInfo"||op=="BarracksInstallationLevelUp"||op=="BarracksInstallationRegisterAgent"||op=="BarracksInstallationRunTask"||op=="BuffInstallationLoad"||op.find("BuffInstallation")==0||op=="InstallationList"||op=="InstallationUnlock")return handle_installation(op,req,r,changed);return r;}
}

namespace astrae {
std::string Backend::handle(const std::string &protocol,const std::string &request_json){
    bool ready=false;{std::lock_guard<std::mutex> guard(mutex_);ready=initialized_;}
    if(!ready&&!init("",""))return "{}";
    std::lock_guard<std::mutex> lk(mutex_);
    const std::string op=trim_protocol(protocol);
    auto route=protocol_routes_.find(op);
    if(route==protocol_routes_.end()){J e=j_obj();e["Error"]="UnknownProtocol";e["Protocol"]=op;return json::dump(e);}
    J req;
    try{req=request_json.empty()?j_obj():json::parse(request_json);if(!req.is_object()){J e=j_obj();e["Error"]="RequestMustBeObject";e["Protocol"]=op;return json::dump(e);}}
    catch(...){J e=j_obj();e["Error"]="InvalidRequestJson";e["Protocol"]=op;return json::dump(e);}
    J response=response_base(op),before=state_;
    bool changed=false;
    try{
        const std::string &group=route->second;
        if(group=="account")response=handle_account(op,req,std::move(response),changed);
        else if(group=="growth")response=handle_growth(op,req,std::move(response),changed);
        else if(group=="artifact")response=handle_artifact(op,req,std::move(response),changed);
        else if(group=="gacha")response=handle_gacha(op,req,std::move(response),changed);
        else if(group=="rune")response=handle_runes(op,req,std::move(response),changed);
        else if(group=="mail")response=handle_mail(op,req,std::move(response),changed);
        else if(group=="quest")response=handle_quest(op,req,std::move(response),changed);
        else if(group=="farming")response=handle_farming(op,req,std::move(response),changed);
        else if(group=="raid")response=handle_raid(op,req,std::move(response),changed);
        else if(group=="mission")response=handle_missions(op,req,std::move(response),changed);
        else if(group=="date")response=handle_date(op,req,std::move(response),changed);
        else if(group=="installation")response=handle_installation(op,req,std::move(response),changed);
        else if(group=="shop")response=handle_shop(op,req,std::move(response),changed);
        else if(group=="misc")response=handle_misc(op,req,std::move(response),changed);
        if(changed){if(!commit_store())state_=std::move(before);}
        else state_=std::move(before);
    }catch(...){state_=std::move(before);J e=response_base(op);if(e.contains("NotiFlag"))e["NotiFlag"]=0;response=std::move(e);}
    return json::dump(response);
}
std::string Backend::state_json(){bool ready=false;{std::lock_guard<std::mutex> guard(mutex_);ready=initialized_;}if(!ready&&!init("",""))return "{}";std::lock_guard<std::mutex> lk(mutex_);return json::dump(state_,true);}
int Backend::replace_state(const std::string &text){
    bool ready=false;{std::lock_guard<std::mutex> guard(mutex_);ready=initialized_;}
    if(!ready&&!init("",""))return 0;
    std::lock_guard<std::mutex> lk(mutex_);
    try{J next=json::parse(text);if(!next.is_object())return 0;J old=state_;state_=std::move(next);if(!normalize_state()){state_=std::move(old);return 0;}if(commit_store())return 1;state_=std::move(old);return 0;}catch(...){return 0;}
}
bool Backend::is_initialized(){std::lock_guard<std::mutex> lk(mutex_);return initialized_;}
bool Backend::apply_account_unlocks(const J &unlocks){
    std::lock_guard<std::mutex> lk(mutex_);if(!initialized_||!unlocks.is_array())return false;
    J previous=state_;J &progress=state_["Progress"];if(!progress.is_object())progress=j_obj();progress["UnlockInfos"]=unlocks;
    J &account=state_["Account"];if(!account.is_object()||!account["Level"].is_number()||!account["Exp"].is_number()){state_=std::move(previous);return false;}
    int64_t level=account["Level"].as_i64(-1),exp=account["Exp"].as_i64(-1);if(level<1||level>99||exp<0||exp>INT32_MAX||account["Exp"].as_double(-1.0)!=static_cast<double>(exp)){state_=std::move(previous);return false;}
    for(const auto &entry:unlocks.as_array()){
        if(!entry.is_object()||!entry["TableId"].is_string()||entry["TableId"].as_string()!="200"||!entry["Value"].is_string())continue;
        const std::string text=entry["Value"].as_string();char *end=nullptr;errno=0;long long value=std::strtoll(text.c_str(),&end,10);if(errno||end==text.c_str()||!end||*end!='\0'||value<10){state_=std::move(previous);return false;}
    }
    while(level<99&&exp>=kAccountLevelExp[static_cast<size_t>(level-1)]){exp-=kAccountLevelExp[static_cast<size_t>(level-1)];++level;}
    account["Level"]=level;account["Exp"]=exp;
    if(!commit_store()){state_=std::move(previous);return false;}
    return true;
}
std::string Backend::diagnostics_json(){
    bool ready=false;{std::lock_guard<std::mutex> guard(mutex_);ready=initialized_;}
    if(!ready&&!init("",""))return "{\"ready\":false,\"error\":\"init_failed\"}";
    std::lock_guard<std::mutex> lk(mutex_);
    J out=j_obj();out["ready"]=initialized_;out["version"]="AstraeOfflineBackend-Reconstruction/1.0.0-phase2";out["saveDirectory"]=save_dir_;out["tableDirectory"]=table_dir_;out["schemaCount"]=uint64_t(schemas_.size());out["requestCount"]=uint64_t(request_names_.size());out["responseCount"]=uint64_t(response_names_.size());out["tableCount"]=uint64_t(tables_.size());out["tableRowCount"]=uint64_t(table_row_count_);J tableRows=j_obj();for(const auto &kv:tables_)tableRows[kv.first]=uint64_t(kv.second.size());out["tableRows"]=std::move(tableRows);out["storeRevision"]=u64(state_,"StoreRevision");out["schemaVersion"]=u64(state_,"SchemaVersion");out["storeRecovery"]=last_store_recovery_;out["saveDataPresentButUnusable"]=save_data_present_but_unusable_;out["legacySaveImported"]=state_["LegacySaveImported"].as_bool(false);J counts=j_obj();for(const auto &kv:protocol_routes_){uint64_t n=counts[kv.second].as_u64(0);counts[kv.second]=n+1;}out["routes"]=counts;out["mappedRequestCount"]=uint64_t(std::count_if(protocol_routes_.begin(),protocol_routes_.end(),[](const auto &kv){return kv.second!="unmapped";}));return json::dump(out,true);
}
std::string Backend::protocol_catalog_json(){
    bool ready=false;{std::lock_guard<std::mutex> guard(mutex_);ready=initialized_;}
    if(!ready&&!init("",""))return "{}";
    std::lock_guard<std::mutex> lk(mutex_);
    J out=j_obj(),requests=j_arr(),responses=j_arr();
    for(const auto &kv:protocol_routes_){J x=j_obj();x["name"]=kv.first;x["handlerGroup"]=kv.second;x["schemaAvailable"]=schemas_.find(kv.first+".Request")!=schemas_.end();requests.push_back(x);}
    for(const auto &name:response_names_)responses.push_back(name);
    out["requests"]=requests;out["responses"]=responses;out["requestCount"]=uint64_t(request_names_.size());out["responseCount"]=uint64_t(response_names_.size());return json::dump(out,true);
}
std::string Backend::validate_request_json(const std::string &protocol,const std::string &request_json){
    bool ready=false;{std::lock_guard<std::mutex> guard(mutex_);ready=initialized_;}
    if(!ready&&!init("",""))return "{\"valid\":false,\"error\":\"init_failed\"}";
    std::lock_guard<std::mutex> lk(mutex_);
    std::string op=trim_protocol(protocol);J out=j_obj(),errors=j_arr(),unknown=j_arr();out["protocol"]=op;
    auto route=protocol_routes_.find(op);
    if(route==protocol_routes_.end()){out["valid"]=false;out["error"]="UnknownProtocol";return json::dump(out,true);}
    out["handlerGroup"]=route->second;
    J req;
    try{req=request_json.empty()?j_obj():json::parse(request_json);}catch(...){out["valid"]=false;out["error"]="InvalidRequestJson";return json::dump(out,true);}
    if(!req.is_object()){out["valid"]=false;out["error"]="RequestMustBeObject";return json::dump(out,true);}
    auto shape=schemas_.find(op+".Request");
    if(shape==schemas_.end()){out["valid"]=false;out["error"]="RequestSchemaMissing";return json::dump(out,true);}
    const J &fields=shape->second["fields"];
    auto type_error=[&](const J &f,const J &v)->bool{
        std::string kind=str(f,"kind"),type=str(f,"type");
        if(v.is_null())return false;
        if(kind=="s")return !v.is_string();
        if(kind=="b")return !v.is_bool();
        if(kind=="f"||kind=="d")return !v.is_number();
        if(kind=="v"||kind=="a"||type.find("[]")!=std::string::npos||type.rfind("List<",0)==0)return !v.is_array();
        if(kind=="o")return !v.is_object();
        if(kind=="i"||kind=="l"||kind=="L"||kind=="u"||kind=="U"||kind=="c"||kind=="C"||kind=="H"||kind=="I")return !v.is_number();
        return false;
    };
    std::set<std::string> known;
    for(const auto &f:fields.as_array()){
        std::string name=str(f,"name");if(name.empty())continue;known.insert(name);
        if(req.contains(name)&&type_error(f,req[name])){J e=j_obj();e["field"]=name;e["expectedKind"]=str(f,"kind");e["expectedType"]=str(f,"type");errors.push_back(e);}
    }
    for(const auto &kv:req.as_object())if(known.find(kv.first)==known.end())unknown.push_back(kv.first);
    out["valid"]=errors.size()==0;out["errors"]=errors;out["unknownFields"]=unknown;out["fieldCount"]=uint64_t(fields.size());out["schemaAvailable"]=true;
    return json::dump(out,true);
}
}
