#include "custom_resource_repair.hpp"
#include "lobby_restore.hpp"
#include "il2cpp_runtime.hpp"
#include "debug_log.hpp"
#include <array>
#include <cstring>
#include <limits>

namespace astrae::lobby {
namespace {
constexpr VignetteIconFallback kVignetteIconFallbacks[]={
    {0x2714,"UI/UIResources/03_Icon/Vignette/Vignette_Common/Img_Vignette_VT10004"},
    {0x2717,"UI/UIResources/03_Icon/Vignette/Vignette_Common/Img_Vignette_VT10007"},
    {0x271f,"UI/UIResources/03_Icon/Vignette/Vignette_Common/Img_Vignette_VT10015"},
    {0x7531,"UI/UIResources/03_Icon/Vignette/Vignette_Common/Img_Vignette_VT30001"},
    {0x7532,"UI/UIResources/03_Icon/Vignette/Vignette_Common/Img_Vignette_VT30002"},
    {0x7533,"UI/UIResources/03_Icon/Vignette/Vignette_Common/Img_Vignette_VT30003"},
    {0x7534,"UI/UIResources/03_Icon/Vignette/Vignette_Common/Img_Vignette_VT30004"},
    {0x7535,"UI/UIResources/03_Icon/Vignette/Vignette_Common/Img_Vignette_VT30005"},
    {0x7536,"UI/UIResources/03_Icon/Vignette/Vignette_Common/Img_Vignette_VT30006"},
    {0x7537,"UI/UIResources/03_Icon/Vignette/Vignette_Common/Img_Vignette_VT30007"},
    {0x7538,"UI/UIResources/03_Icon/Vignette/Vignette_Common/Img_Vignette_VT30008"},
    {0x7539,"UI/UIResources/03_Icon/Vignette/Vignette_Common/Img_Vignette_VT30009"},
    {0x753a,"UI/UIResources/03_Icon/Vignette/Vignette_Common/Img_Vignette_VT30010"},
    {0x753b,"UI/UIResources/03_Icon/Vignette/Vignette_Common/Img_Vignette_VT30011"},
    {0x753c,"UI/UIResources/03_Icon/Vignette/Vignette_Common/Img_Vignette_VT30012"},
    {0x753d,"UI/UIResources/03_Icon/Vignette/Vignette_Common/Img_Vignette_VT30013"},
    {0x753e,"UI/UIResources/03_Icon/Vignette/Vignette_Common/Img_Vignette_VT30014"},
    {0x753f,"UI/UIResources/03_Icon/Vignette/Vignette_Common/Img_Vignette_VT30015"},
    {0x7540,"UI/UIResources/03_Icon/Vignette/Vignette_Common/Img_Vignette_VT30016"},
    {0x7541,"UI/UIResources/03_Icon/Vignette/Vignette_Common/Img_Vignette_VT30017"},
    {0x7542,"UI/UIResources/03_Icon/Vignette/Vignette_Common/Img_Vignette_VT30018"},
    {0x7543,"UI/UIResources/03_Icon/Vignette/Vignette_Common/Img_Vignette_VT30019"},
    {0x7544,"UI/UIResources/03_Icon/Vignette/Vignette_Common/Img_Vignette_VT30020"},
    {0x7545,"UI/UIResources/03_Icon/Vignette/Vignette_Common/Img_Vignette_VT30021"},
    {0x7546,"UI/UIResources/03_Icon/Vignette/Vignette_Common/Img_Vignette_VT30022"},
    {0x7547,"UI/UIResources/03_Icon/Vignette/Vignette_Common/Img_Vignette_VT30023"},
    {0x7548,"UI/UIResources/03_Icon/Vignette/Vignette_Common/Img_Vignette_VT30024"},
    {0x7549,"UI/UIResources/03_Icon/Vignette/Vignette_Common/Img_Vignette_VT30025"},
    {0x754a,"UI/UIResources/03_Icon/Vignette/Vignette_Common/Img_Vignette_VT30026"},
    {0x754b,"UI/UIResources/03_Icon/Vignette/Vignette_Common/Img_Vignette_VT30027"},
    {0x754c,"UI/UIResources/03_Icon/Vignette/Vignette_Common/Img_Vignette_VT30028"},
    {0x754d,"UI/UIResources/03_Icon/Vignette/Vignette_Common/Img_Vignette_VT30029"}
};
void set_error(std::string *error,const std::string &value){if(error)*error=value;}
}
const VignetteIconFallback *vignette_icon_fallback_data(size_t *count){if(count)*count=sizeof(kVignetteIconFallbacks)/sizeof(kVignetteIconFallbacks[0]);return kVignetteIconFallbacks;}
bool repair_vignette_icon_fallbacks(void *data_manager,std::string *error){
    if(error)error->clear();
    if(!data_manager){set_error(error,"vignette icon fallback received a null DataManager");return false;}
    // Recovered from custom_resource_tables@0x24ebf4: table_get(dm,0x4b8,0x2714),
    // then create all 32 strings and write the same row's +0xd0 reference with the GC barrier.
    std::string lookup_error;void *row=table_get_borrowed(data_manager,0x4b8,kVignetteIconFallbacks[0].id,&lookup_error);
    if(!row){set_error(error,"vignette icon fallback table lookup failed: "+lookup_error);return false;}
    auto &runtime=il2cpp::Runtime::instance();const auto &api=runtime.api();
    if(!api.string_new||!api.gc_wbarrier_set_field){set_error(error,"IL2CPP string allocation or GC write barrier is unavailable for vignette icon fallback");return false;}
    il2cpp::Runtime::ThreadScope thread(runtime);if(!thread.ok()){set_error(error,thread.error());return false;}
    constexpr size_t kFieldOffset=0xd0;size_t repaired=0;
    for(const auto &entry:kVignetteIconFallbacks){void *managed_path=api.string_new(entry.path);if(!managed_path){set_error(error,"il2cpp_string_new failed for vignette fallback id="+std::to_string(entry.id));return false;}api.gc_wbarrier_set_field(row,reinterpret_cast<void**>(static_cast<uint8_t*>(row)+kFieldOffset),managed_path);++repaired;}
    ASTRAE_LOGI("original vignette icon paths repaired=%llu row=%p table_offset=0x4b8 field_offset=0xd0",static_cast<unsigned long long>(repaired),row);
    return repaired==sizeof(kVignetteIconFallbacks)/sizeof(kVignetteIconFallbacks[0]);
}

namespace {
struct RowListView { void *list=nullptr;uint64_t count=0; };
void *load_pointer(const void *base,size_t offset){void *value=nullptr;if(base)std::memcpy(&value,static_cast<const uint8_t*>(base)+offset,sizeof(value));return value;}
uint64_t load_u64(const void *base,size_t offset){uint64_t value=0;if(base)std::memcpy(&value,static_cast<const uint8_t*>(base)+offset,sizeof(value));return value;}
uint32_t load_u32(const void *base,size_t offset){uint32_t value=0;if(base)std::memcpy(&value,static_cast<const uint8_t*>(base)+offset,sizeof(value));return value;}
void store_u32(void *base,size_t offset,uint32_t value){std::memcpy(static_cast<uint8_t*>(base)+offset,&value,sizeof(value));}
bool get_rows(void *data_manager,uint32_t table_offset,uint64_t min_count,uint64_t max_count,RowListView *view,std::string *error){
    if(view)*view={};if(!data_manager||!view){set_error(error,"table row view received a null argument");return false;}
    void *root=load_pointer(data_manager,0x10);if(!root){set_error(error,"DataManager root is null at table offset "+std::to_string(table_offset));return false;}
    void *manager=load_pointer(root,table_offset);if(!manager){set_error(error,"DataManager table manager is null at offset "+std::to_string(table_offset));return false;}
    void *wrapper=load_pointer(manager,0x18);if(!wrapper){set_error(error,"table wrapper is null at offset "+std::to_string(table_offset));return false;}
    void *list=load_pointer(wrapper,0x10);if(!list){set_error(error,"table row list is null at offset "+std::to_string(table_offset));return false;}
    const uint64_t count=load_u64(list,0x18);if(count<min_count||count>max_count){set_error(error,"table row count is outside the recovered "+std::to_string(min_count)+".."+std::to_string(max_count)+" range at offset "+std::to_string(table_offset)+": "+std::to_string(count));return false;}
    view->list=list;view->count=count;return true;
}
void *list_row(const RowListView &view,uint64_t index){if(!view.list||index>=view.count)return nullptr;return load_pointer(view.list,0x20+static_cast<size_t>(index)*sizeof(void*));}
}
bool challenge_gate_tables(void *data_manager,std::string *error){
    if(error)error->clear();RowListView rows; if(!get_rows(data_manager,0x2f0,1,4096,&rows,error))return false;
    static constexpr uint64_t ids[]={0x000f47b9ULL,0x000f4ba1ULL};static constexpr uint32_t old_bits[]={2,5};static constexpr uint32_t accepted_bits[]={3,6};
    for(size_t target=0;target<2;++target){
        bool found=false;
        // The reference restarts its list scan for each target ID; do not rely on table row ordering.
        for(uint64_t i=0;i<rows.count;++i){
            void *row=list_row(rows,i);if(!row){set_error(error,"challenge gate row pointer is null at index "+std::to_string(i));return false;}
            uint64_t id=0;std::string getter_error;if(!row_u64(row,"get_Id",&id,&getter_error)){set_error(error,"challenge gate Id getter failed at row "+std::to_string(i)+": "+getter_error);return false;}if(id!=ids[target])continue;
            void *groups=load_pointer(row,0x68);void *values=load_pointer(row,0x70);const uint64_t group_count=groups?load_u64(groups,0x18):0;const uint64_t value_count=values?load_u64(values,0x18):0;const uint32_t group0=group_count?load_u32(groups,0x20):0;const uint32_t value0=value_count?load_u32(values,0x20):0;
            ASTRAE_LOGI("challenge table probe id=%llu row=%p ok=%d groups=%llu values=%llu group0=%llu value0=%u",static_cast<unsigned long long>(id),row,1,static_cast<unsigned long long>(group_count),static_cast<unsigned long long>(value_count),static_cast<unsigned long long>(group0),value0);
            if(!groups||!values||group_count==0||value_count==0||group0!=1000||(value0!=old_bits[target]&&value0!=accepted_bits[target])){set_error(error,"challenge gate row failed its recovered Groups/Values invariant: id="+std::to_string(id));return false;}
            // Original code accepts either the old or already-expanded bit and always normalizes to old_bits[i].
            store_u32(values,0x20,old_bits[target]);int32_t account_level=0;if(!row_i32(row,"get_ReqAccountLevel",&account_level,&getter_error)){set_error(error,"challenge gate ReqAccountLevel getter failed: "+getter_error);return false;}
            if(account_level!=20){set_error(error,"challenge gate account level mismatch for id="+std::to_string(id)+": "+std::to_string(account_level));return false;}
            ASTRAE_LOGI("challenge gate id=%llu Normal chapter clear bit=%u account=%d",static_cast<unsigned long long>(id),old_bits[target],account_level);found=true;break;
        }
        if(!found){set_error(error,"challenge gate target row missing or invalid: id="+std::to_string(ids[target]));return false;}
    }
    ASTRAE_LOGI("challenge gate table repair completed: targets=2 groups=2/5 values_normalized=2/5");return true;
}
bool permanent_story_pack_tables(void *data_manager,std::string *error){
    if(error)error->clear();RowListView rows;if(!get_rows(data_manager,0x3a8,1,256,&rows,error))return false;
    constexpr uint64_t kDateTimeMax=0x2bca2875f4373fffULL;uint32_t pack1001=0,pack1002=0;bool valid=true;
    for(uint64_t i=0;i<rows.count;++i){void *row=list_row(rows,i);if(!row){valid=false;set_error(error,"story pack row pointer is null at index "+std::to_string(i));return false;}uint64_t group=0;std::string getter_error;
        if(!row_u64(row,"get_GroupId",&group,&getter_error)){valid=false;set_error(error,"story pack GroupId getter failed at row "+std::to_string(i)+": "+getter_error);return false;}
        if(group!=1001&&group!=1002)continue;std::memcpy(static_cast<uint8_t*>(row)+0xc8,&kDateTimeMax,sizeof(kDateTimeMax));std::array<uint64_t,2> remaining{kDateTimeMax,kDateTimeMax};std::memcpy(static_cast<uint8_t*>(row)+0xd8,remaining.data(),sizeof(remaining));if(group==1001)++pack1001;else ++pack1002;
    }
    ASTRAE_LOGI("story pack expiry set to DateTime maximum packs=%u/%u valid=%d",pack1001,pack1002,valid?1:0);
    if(pack1001==0||pack1002==0||!valid){set_error(error,"story pack expiry groups 1001 and 1002 were not both found with valid row metadata");return false;}return true;
}
bool sigrid_mastery_tables(void *data_manager,std::string *error){
    if(error)error->clear();RowListView group_rows,rows;if(!get_rows(data_manager,0x3a8,1,256,&group_rows,error))return false;(void)group_rows;
    if(!get_rows(data_manager,0x400,1,65536,&rows,error))return false;
    constexpr uint64_t kSigridGroupId=0x000f4845ULL;constexpr uint64_t kSigridSkillLocalizeId=0x35e85afbe859839bULL;constexpr uint64_t kSkillGroupBase=100154104ULL;
    void *targets[2]={nullptr,nullptr};
    for(uint64_t i=0;i<rows.count&&(!targets[0]||!targets[1]);++i){void *row=list_row(rows,i);if(!row){set_error(error,"Sigrid mastery row pointer is null at index "+std::to_string(i));return false;}
        uint64_t group_id=0,skill_group=0;std::string getter_error;if(!row_u64(row,"get_GroupId",&group_id,&getter_error)){set_error(error,"Sigrid GroupId getter failed: "+getter_error);return false;}if(group_id!=kSigridGroupId)continue;
        if(!row_u64(row,"get_SkillGroupId",&skill_group,&getter_error)){set_error(error,"Sigrid SkillGroupId getter failed: "+getter_error);return false;}if(skill_group<kSkillGroupBase||skill_group>kSkillGroupBase+1)continue;
        const size_t index=static_cast<size_t>(skill_group-kSkillGroupBase);if(targets[index])continue;
        const uint32_t node_type=load_u32(row,0x30),node_group=load_u32(row,0x5c),node_state=load_u32(row,0x80);if((node_type!=9&&node_type!=4)||node_group!=2||node_state!=1)continue;
        uint64_t localize_id=0;if(!row_u64(row,"get_SkillLocalizeId",&localize_id,&getter_error)){set_error(error,"Sigrid SkillLocalizeId getter failed: "+getter_error);return false;}if(localize_id!=kSigridSkillLocalizeId)continue;
        int32_t enable=0,disable=0;if(!row_i32(row,"get_EnableParameter",&enable,&getter_error)||!row_i32(row,"get_DisableParameter",&disable,&getter_error)){set_error(error,"Sigrid parameter getter failed: "+getter_error);return false;}
        const int32_t expected_enable=static_cast<int32_t>(index+4);const int32_t expected_disable=index==0?5:99999;if(enable!=expected_enable||disable!=expected_disable)continue;
        targets[index]=row;
    }
    if(!targets[0]||!targets[1]){set_error(error,std::string("Sigrid mastery target rows incomplete: variant100154104=")+(targets[0]?"found":"missing")+" variant100154105="+(targets[1]?"found":"missing"));return false;}
    store_u32(targets[0],0x30,4);store_u32(targets[1],0x30,4);
    ASTRAE_LOGI("Sigrid mastery skill level groups repaired nodes=4/5 variants=100154104/100154105 index=4");return true;
}
bool custom_resource_tables(void *data_manager,std::string *error){
    if(error)error->clear();std::string vignette_error,challenge_error,story_error,sigrid_error;
    if(!repair_vignette_icon_fallbacks(data_manager,&vignette_error)){set_error(error,"vignette_icon_fallbacks: "+vignette_error);return false;}
    // The reference continues through all three table repairs after a challenge-gate
    // failure and combines their status flags only at the epilogue.
    const bool challenge_ok=challenge_gate_tables(data_manager,&challenge_error);
    const bool story_ok=permanent_story_pack_tables(data_manager,&story_error);
    const bool sigrid_ok=sigrid_mastery_tables(data_manager,&sigrid_error);
    ASTRAE_LOGI("challenge gate table repair=%d pack expiry=%d Sigrid mastery=%d",challenge_ok?1:0,story_ok?1:0,sigrid_ok?1:0);
    if(!challenge_ok||!story_ok||!sigrid_ok){std::string combined;if(!challenge_ok)combined+="challenge_gate_tables: "+challenge_error;if(!story_ok){if(!combined.empty())combined+="; ";combined+="permanent_story_pack_tables: "+story_error;}if(!sigrid_ok){if(!combined.empty())combined+="; ";combined+="sigrid_mastery_tables: "+sigrid_error;}set_error(error,combined);return false;}
    ASTRAE_LOGI("reference custom_resource_tables completed: vignette=32 challenge_gate=true story_pack_expiry=true sigrid_mastery=true");return true;
}

}
