#include "custom_resource_repair.hpp"
#include "il2cpp_runtime.hpp"
#include <array>
#include <cassert>
#include <cstdint>
#include <cstring>
#include <iostream>
#include <memory>
#include <string>
#include <unordered_map>
#include <vector>

namespace {
using namespace astrae;
struct FakeClass {};
struct alignas(16) FakeObject { void *klass=nullptr;std::array<uint8_t,0x300> bytes{}; };
struct FakeMethod { std::string name; };
struct FakeBox { void *klass=nullptr;uint64_t value=0; };
struct Fixture {
    FakeClass klass{};
    std::vector<std::unique_ptr<FakeObject>> objects;
    std::vector<std::unique_ptr<FakeBox>> boxes;
    std::unordered_map<std::string,std::unique_ptr<FakeMethod>> methods;
    std::unordered_map<void*,std::unordered_map<std::string,uint64_t>> properties;
    std::unordered_map<void*,std::unordered_map<uint64_t,void*>> dictionaries;
    std::unordered_map<void*,std::string> managed_strings;
    size_t write_barriers=0;
    FakeObject *object(){auto p=std::make_unique<FakeObject>();p->klass=&klass;auto *raw=p.get();objects.push_back(std::move(p));return raw;}
    template<typename T> static void put(void *base,size_t offset,const T &value){std::memcpy(static_cast<uint8_t*>(base)+offset,&value,sizeof(value));}
    template<typename T> static T get(void *base,size_t offset){T value{};std::memcpy(&value,static_cast<uint8_t*>(base)+offset,sizeof(value));return value;}
    void set_prop(void *row,const std::string &name,uint64_t value){properties[row][name]=value;}
    void *make_rows(void *root,uint32_t offset,const std::vector<void*> &rows){auto *manager=object();auto *wrapper=object();auto *list=object();put(root,offset,static_cast<void*>(manager));put(manager,0x18,static_cast<void*>(wrapper));put(wrapper,0x10,static_cast<void*>(list));put(list,0x18,static_cast<uint64_t>(rows.size()));for(size_t i=0;i<rows.size();++i)put(list,0x20+i*sizeof(void*),rows[i]);return list;}
    void *make_array(uint32_t first){auto *array=object();put(array,0x18,uint64_t{1});put(array,0x20,first);return array;}
    void make_vignette_dictionary(void *root,void *row){auto *manager=object();auto *dict=object();put(root,0x4b8,static_cast<void*>(manager));put(manager,0x18,static_cast<void*>(dict));dictionaries[dict][0x2714]=row;}
    void build_fixture(void **data_manager,void **vignette,void **challenge_a,void **challenge_b,void **story_a,void **story_b,void **sigrid_a,void **sigrid_b,bool invalid_challenge=false){
        auto *dm=object();auto *root=object();put(dm,0x10,static_cast<void*>(root));*data_manager=dm;
        *vignette=object();make_vignette_dictionary(root,*vignette);
        *challenge_a=object();*challenge_b=object();
        auto *groups_a=make_array(1000);auto *values_a=make_array(invalid_challenge?4:3);auto *groups_b=make_array(1000);auto *values_b=make_array(6);
        put(*challenge_a,0x68,groups_a);put(*challenge_a,0x70,values_a);put(*challenge_b,0x68,groups_b);put(*challenge_b,0x70,values_b);
        set_prop(*challenge_a,"get_Id",0x000f47b9ULL);set_prop(*challenge_a,"get_ReqAccountLevel",20);
        set_prop(*challenge_b,"get_Id",0x000f4ba1ULL);set_prop(*challenge_b,"get_ReqAccountLevel",20);
        make_rows(root,0x2f0,{*challenge_b,*challenge_a});
        *story_a=object();*story_b=object();set_prop(*story_a,"get_GroupId",1001);set_prop(*story_b,"get_GroupId",1002);make_rows(root,0x3a8,{*story_a,*story_b});
        *sigrid_a=object();*sigrid_b=object();
        const uint64_t group_id=0x000f4845ULL,localize=0x35e85afbe859839bULL;
        for(size_t i=0;i<2;++i){void *row=i==0?*sigrid_a:*sigrid_b;const uint64_t skill_group=100154104ULL+i;put(row,0x30,uint32_t{9});put(row,0x5c,uint32_t{2});put(row,0x80,uint32_t{1});set_prop(row,"get_GroupId",group_id);set_prop(row,"get_SkillGroupId",skill_group);set_prop(row,"get_SkillLocalizeId",localize);set_prop(row,"get_EnableParameter",i+4);set_prop(row,"get_DisableParameter",i==0?5:99999);}
        make_rows(root,0x400,{*sigrid_a,*sigrid_b});
    }
    static void *object_get_class(void *object){return object?static_cast<FakeObject*>(object)->klass:nullptr;}
    static void *object_unbox(void *object){return object?&static_cast<FakeBox*>(object)->value:nullptr;}
    static void *class_get_method(void *,const char *name,int){auto &f=self();auto it=f.methods.find(name?name:"");if(it==f.methods.end()){auto method=std::make_unique<FakeMethod>();method->name=name?name:"";auto *raw=method.get();f.methods.emplace(method->name,std::move(method));return raw;}return it->second.get();}
    static void *runtime_invoke(void *method,void *instance,void **args,void **exception){if(exception)*exception=nullptr;if(!method)return nullptr;auto &f=self();const auto &name=static_cast<FakeMethod*>(method)->name;if(name=="get_Item"){if(!args||!args[0])return nullptr;const uint64_t key=*static_cast<uint64_t*>(args[0]);auto dit=f.dictionaries.find(instance);if(dit==f.dictionaries.end())return nullptr;auto it=dit->second.find(key);return it==dit->second.end()?nullptr:it->second;}auto row=f.properties.find(instance);if(row==f.properties.end())return nullptr;auto prop=row->second.find(name);if(prop==row->second.end())return nullptr;auto box=std::make_unique<FakeBox>();box->klass=&f.klass;box->value=prop->second;auto *raw=box.get();f.boxes.push_back(std::move(box));return raw;}
    static void *thread_current(){return reinterpret_cast<void*>(uintptr_t{1});}
    static void *string_new(const char *value){auto &f=self();auto *obj=f.object();f.managed_strings[obj]=value?value:"";return obj;}
    static void gc_wbarrier_set_field(void *owner,void **target,void *value){auto &f=self();if(owner&&target){std::memcpy(target,&value,sizeof(value));++f.write_barriers;}}
    static Fixture &self(){return *active;}
    static inline Fixture *active=nullptr;
    il2cpp::Api api(){il2cpp::Api a{};a.object_get_class=&object_get_class;a.object_unbox=&object_unbox;a.class_get_method_from_name=&class_get_method;a.runtime_invoke=&runtime_invoke;a.thread_current=&thread_current;a.string_new=&string_new;a.gc_wbarrier_set_field=&gc_wbarrier_set_field;return a;}
};
}

int main(){
    size_t count=0;const auto *entries=astrae::lobby::vignette_icon_fallback_data(&count);assert(entries&&count==32);
    assert(entries[0].id==0x2714&&std::string(entries[0].path).find("VT10004")!=std::string::npos);
    assert(entries[1].id==0x2717&&std::string(entries[1].path).find("VT10007")!=std::string::npos);
    assert(entries[2].id==0x271f&&std::string(entries[2].path).find("VT10015")!=std::string::npos);
    assert(entries[3].id==0x7531&&std::string(entries[3].path).find("VT30001")!=std::string::npos);
    assert(entries[31].id==0x754d&&std::string(entries[31].path).find("VT30029")!=std::string::npos);
    std::unordered_map<uint64_t,bool> ids;std::unordered_map<std::string,bool> paths;
    for(size_t i=0;i<count;++i){assert(entries[i].path&&*entries[i].path);assert(std::string(entries[i].path).find("UI/UIResources/03_Icon/Vignette/Vignette_Common/")==0);ids[entries[i].id]=true;paths[entries[i].path]=true;}
    assert(ids.size()==32&&paths.size()==32);

    auto &runtime=astrae::il2cpp::Runtime::instance();constexpr uint64_t date_max=0x2bca2875f4373fffULL;
    {
        Fixture invalid;Fixture::active=&invalid;void *dm=nullptr,*vignette=nullptr,*challenge_a=nullptr,*challenge_b=nullptr,*story_a=nullptr,*story_b=nullptr,*sigrid_a=nullptr,*sigrid_b=nullptr;
        invalid.build_fixture(&dm,&vignette,&challenge_a,&challenge_b,&story_a,&story_b,&sigrid_a,&sigrid_b,true);
        assert(runtime.initialize_for_testing(invalid.api(),reinterpret_cast<void*>(uintptr_t{1})));
        std::string error;assert(!astrae::lobby::custom_resource_tables(dm,&error));assert(error.find("challenge_gate_tables")!=std::string::npos);
        // The original continues to pack expiry and Sigrid mutations after a failed challenge gate.
        assert(invalid.write_barriers==32);for(void *row:{story_a,story_b}){assert(Fixture::get<uint64_t>(row,0xc8)==date_max);assert(Fixture::get<uint64_t>(row,0xd8)==date_max);assert(Fixture::get<uint64_t>(row,0xe0)==date_max);}
        assert(Fixture::get<uint32_t>(sigrid_a,0x30)==4&&Fixture::get<uint32_t>(sigrid_b,0x30)==4);runtime.shutdown();
    }
    {
        Fixture fixture;Fixture::active=&fixture;void *dm=nullptr,*vignette=nullptr,*challenge_a=nullptr,*challenge_b=nullptr,*story_a=nullptr,*story_b=nullptr,*sigrid_a=nullptr,*sigrid_b=nullptr;
        fixture.build_fixture(&dm,&vignette,&challenge_a,&challenge_b,&story_a,&story_b,&sigrid_a,&sigrid_b);
        assert(runtime.initialize_for_testing(fixture.api(),reinterpret_cast<void*>(uintptr_t{1})));
        std::string error;assert(astrae::lobby::custom_resource_tables(dm,&error)&&error.empty());
        assert(fixture.write_barriers==32);assert(fixture.managed_strings.size()==32);
        void *vignette_field=Fixture::get<void*>(vignette,0xd0);assert(vignette_field&&fixture.managed_strings.count(vignette_field)==1);
        assert(Fixture::get<uint32_t>(Fixture::get<void*>(challenge_a,0x70),0x20)==2);
        assert(Fixture::get<uint32_t>(Fixture::get<void*>(challenge_b,0x70),0x20)==5);
        for(void *row:{story_a,story_b}){assert(Fixture::get<uint64_t>(row,0xc8)==date_max);assert(Fixture::get<uint64_t>(row,0xd8)==date_max);assert(Fixture::get<uint64_t>(row,0xe0)==date_max);}
        assert(Fixture::get<uint32_t>(sigrid_a,0x30)==4&&Fixture::get<uint32_t>(sigrid_b,0x30)==4);runtime.shutdown();
    }
    Fixture::active=nullptr;
    std::cout<<"custom_resource_repair: 32 relocation-backed Vignette records and Challenge/Story/Sigrid runtime mutations validated\n";
    return 0;
}
