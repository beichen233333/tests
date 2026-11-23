#include "backend.hpp"
#include "json.hpp"
#include <cassert>
#include <cstdio>
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <iterator>
#include <string>
#include <sys/stat.h>
#include <unistd.h>
using astrae::Backend;
using astrae::json::Value;
static void write_file(const std::string &path,const std::string &text){std::ofstream out(path,std::ios::binary|std::ios::trunc);assert(out.good());out<<text;out.close();assert(out.good());}
static std::string read_file(const std::string &path){std::ifstream in(path,std::ios::binary);assert(in.good());return std::string(std::istreambuf_iterator<char>(in),{});}
int main(){
    char temp[]="/tmp/astrae-save-migrate-XXXXXX";char *base=mkdtemp(temp);assert(base);
    const std::string root=base,compat=root+"/compat-store",compat_path=compat+"/save.json",native_path=root+"/save.json";
    std::filesystem::create_directories(compat);
    const std::string old=R"({"schema_version":1,"substory_direct_index":true,"profile_origin":"old_local_profile","account":{"id":"account-42","name":"Old Player","level":31,"exp":2468},"tutorial":{"content_bits":"725367","initial_index":5},"characters":[{"TableId":"10001","ServerId":"100000","Level":77,"Exp":345,"Star":3,"Locked":false,"Signed":true,"Buffs":[],"Runes":[],"Skills":[]}],"inventory":[{"type":5,"table_id":"20001","count":41}],"currencies":[{"type":0,"value":"7"},{"type":1,"value":"54321"}],"vignettes":[],"artifacts":[],"runes":[{"TableId":"40001","Count":9}],"tickets":[],"costumes":[],"mails":[],"formations":[{"Index":0,"CharacterTableIds":["10001"],"VignetteTableIds":[],"Name":"Old formation"}],"quests":[],"rewards":[],"unlocks":[{"TableId":200,"Value":"12"}],"packs":{},"gacha_state":{},"lobby_background_id":"30002"})";
    write_file(compat_path,old);
    Backend &backend=Backend::instance();
    assert(backend.init(root,"assets")==1);
    Value state=astrae::json::parse(backend.state_json());
    assert(state["LegacySaveImported"].as_bool());
    assert(state["Account"]["GameAccountId"].as_string()=="account-42");
    assert(state["Account"]["UserName"].as_string()=="Old Player");
    assert(state["Account"]["Level"].as_i64()==31 && state["Account"]["Exp"].as_i64()==2468);
    assert(state["TutorialInfo"]["ContentBits"].as_u64()==725367 && state["TutorialInfo"]["InitialIndex"].as_i64()==5);
    assert(state["characters"].size()==1 && state["characters"][0]["TableId"].as_u64()==10001);
    assert(state["inventory"].size()==3 && state["inventory"][0]["Count"].as_i64()==41 && state["inventory"][1]["TableId"].as_u64()==0 && state["inventory"][1]["Count"].as_i64()==7 && state["inventory"][2]["Count"].as_i64()==54321);
    // Currency type 2 legitimately uses TableId 0; it must survive validation and grants.
    (void)backend.handle("DevCheat",R"({"Rewards":[{"Type":2,"TableId":0,"Count":5}]})");
    state=astrae::json::parse(backend.state_json());
    assert(state["inventory"][1]["Type"].as_i64()==2 && state["inventory"][1]["TableId"].as_u64()==0 && state["inventory"][1]["Count"].as_i64()==12);
    assert(state["rune_stock"].size()==1 && state["rune_stock"][0]["Count"].as_i64()==9);
    assert(state["formations"].size()==1 && state["formations"][0]["Name"].as_string()=="Old formation");
    assert(std::filesystem::exists(native_path));
    assert(read_file(compat_path)==old);
    Value diagnostics=astrae::json::parse(backend.diagnostics_json());
    assert(diagnostics["storeRecovery"].as_string()=="compat-store-migrated");
    backend.shutdown();
    assert(backend.init(root,"assets")==1);
    Value reloaded=astrae::json::parse(backend.state_json());
    assert(reloaded["Account"]["GameAccountId"].as_string()=="account-42");
    assert(reloaded["TutorialInfo"]["ContentBits"].as_u64()==725367);
    assert(reloaded["characters"].size()==1 && reloaded["inventory"].size()==3);
    Value reloaded_diagnostics=astrae::json::parse(backend.diagnostics_json());
    assert(reloaded_diagnostics["storeRecovery"].as_string()=="primary");
    backend.shutdown();

    // A previous buggy build may already have written a native placeholder profile.
    // A real historical compat profile must replace only that exact pristine placeholder.
    char placeholder_temp[]="/tmp/astrae-save-placeholder-XXXXXX";char *placeholder_base=mkdtemp(placeholder_temp);assert(placeholder_base);
    const std::string placeholder_root=placeholder_base,placeholder_native=placeholder_root+"/save.json",placeholder_compat=placeholder_root+"/compat-store";
    assert(backend.init(placeholder_root,"assets")==1);
    Value placeholder_state=astrae::json::parse(backend.state_json());
    assert(placeholder_state["Account"]["UserName"].as_string()=="Offline Player" && placeholder_state["characters"].size()==1);
    backend.shutdown();
    std::filesystem::create_directories(placeholder_compat);
    const std::string historical=R"({"schema_version":1,"substory_direct_index":true,"profile_origin":"old_local_profile","account":{"id":"historical-88","name":"Historical Player","level":22,"exp":777},"tutorial":{"content_bits":"0","initial_index":0},"characters":[],"inventory":[],"currencies":[],"formations":[],"vignettes":[],"artifacts":[],"runes":[],"tickets":[],"costumes":[],"mails":[],"unlocks":[],"quests":[],"rewards":[],"packs":{}})";
    const std::string historical_path=placeholder_compat+"/save.json";write_file(historical_path,historical);
    assert(backend.init(placeholder_root,"assets")==1);
    Value historical_state=astrae::json::parse(backend.state_json());
    assert(historical_state["LegacySaveImported"].as_bool());
    assert(historical_state["Account"]["GameAccountId"].as_string()=="historical-88");
    assert(historical_state["characters"].size()==0 && historical_state["formations"].size()==0);
    assert(historical_state["TutorialInfo"]["ContentBits"].as_u64()==0 && historical_state["TutorialInfo"]["InitialIndex"].as_i64()==0);
    assert(std::filesystem::exists(placeholder_native+".placeholder"));
    assert(read_file(historical_path)==historical);
    assert(astrae::json::parse(backend.diagnostics_json())["storeRecovery"].as_string()=="compat-store-imported-over-placeholder");
    // No first-day/tutorial flow exists yet, so importing an empty historical save
    // must not synthesize a character or advance tutorial state.
    backend.shutdown();

    // A meaningful native V3 save must always win over a stale compatibility file.
    char native_progress_temp[]="/tmp/astrae-save-native-progress-XXXXXX";char *native_progress_base=mkdtemp(native_progress_temp);assert(native_progress_base);
    const std::string native_progress_root=native_progress_base,native_progress_path=native_progress_root+"/save.json",native_progress_compat=native_progress_root+"/compat-store";
    assert(backend.init(native_progress_root,"assets")==1);
    Value native_progress=astrae::json::parse(backend.state_json());
    native_progress["Account"]["UserName"]="Veteran V3";native_progress["Account"]["NickName"]="Veteran V3";native_progress["Account"]["Level"]=int64_t(9);native_progress["characters"][0]["Level"]=int64_t(55);
    backend.shutdown();write_file(native_progress_path,astrae::json::dump(native_progress));
    std::filesystem::create_directories(native_progress_compat);
    const std::string stale_compat=R"({"schema_version":1,"profile_origin":"old_local_profile","account":{"id":"stale-compat","name":"Stale Compat","level":2,"exp":0},"tutorial":{"content_bits":"0","initial_index":0},"characters":[],"inventory":[],"currencies":[],"formations":[],"vignettes":[],"artifacts":[],"runes":[],"tickets":[],"costumes":[],"mails":[],"unlocks":[],"quests":[],"rewards":[],"packs":{}})";
    write_file(native_progress_compat+"/save.json",stale_compat);
    assert(backend.init(native_progress_root,"assets")==1);
    Value native_reloaded=astrae::json::parse(backend.state_json());
    assert(native_reloaded["Account"]["UserName"].as_string()=="Veteran V3"&&native_reloaded["Account"]["Level"].as_i64()==9);
    assert(native_reloaded["characters"].size()==1&&native_reloaded["characters"][0]["Level"].as_i64()==55);
    assert(!native_reloaded["LegacySaveImported"].as_bool(false));
    assert(astrae::json::parse(backend.diagnostics_json())["storeRecovery"].as_string()=="primary");
    backend.shutdown();

    // The untouched compat seed on a genuinely fresh install is not a historical save.
    char fresh_temp[]="/tmp/astrae-save-fresh-XXXXXX";char *fresh_base=mkdtemp(fresh_temp);assert(fresh_base);
    const std::string fresh_root=fresh_base,fresh_compat=fresh_root+"/compat-store";std::filesystem::create_directories(fresh_compat);
    const std::string blank_compat=R"({"schema_version":1,"substory_direct_index":true,"profile_origin":"new_local_profile","account":{"id":"1","name":"Offline Player","level":1,"exp":0},"tutorial":{"content_bits":"0","initial_index":0},"characters":[],"inventory":[],"currencies":[],"formations":[],"vignettes":[],"artifacts":[],"runes":[],"tickets":[],"costumes":[],"mails":[],"unlocks":[],"quests":[],"rewards":[],"packs":{}})";
    write_file(fresh_compat+"/save.json",blank_compat);
    assert(backend.init(fresh_root,"assets")==1);
    Value fresh_state=astrae::json::parse(backend.state_json());
    assert(!fresh_state["LegacySaveImported"].as_bool(false));
    assert(fresh_state["characters"].size()==1 && fresh_state["formations"].size()==1);
    backend.shutdown();

    char bad_temp[]="/tmp/astrae-save-invalid-XXXXXX";char *bad_base=mkdtemp(bad_temp);assert(bad_base);
    const std::string bad_root=bad_base,bad_compat=bad_root+"/compat-store",bad_path=bad_compat+"/save.json";
    std::filesystem::create_directories(bad_compat);
    const std::string bad_old=R"({"schema_version":99,"account":{"id":"must-not-overwrite"}})";
    write_file(bad_path,bad_old);
    assert(backend.init(bad_root,"assets")==0);
    assert(!std::filesystem::exists(bad_root+"/save.json"));
    assert(read_file(bad_path)==bad_old);
    std::filesystem::remove_all(root);std::filesystem::remove_all(placeholder_root);std::filesystem::remove_all(native_progress_root);std::filesystem::remove_all(fresh_root);std::filesystem::remove_all(bad_root);
    return 0;
}
