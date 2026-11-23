#include "character99_object_graph.hpp"
#include <array>
#include <cassert>
#include <cstdint>
#include <cstring>
#include <iostream>
namespace {
void put(void *base,size_t offset,void *value){std::memcpy(static_cast<uint8_t*>(base)+offset,&value,sizeof(value));}
}
int main(){
    alignas(16) std::array<uint8_t,0x240> manager_root{};
    alignas(16) std::array<uint8_t,0x240> data_loader{};
    alignas(16) std::array<uint8_t,0x40> level_loader{};
    alignas(16) std::array<uint8_t,0x40> level_root{};
    alignas(16) std::array<uint8_t,0x40> growth_loader{};
    alignas(16) std::array<uint8_t,0x40> growth_root{};
    alignas(16) std::array<uint8_t,0x20> level_data{};
    alignas(16) std::array<uint8_t,0x20> level_dictionary{};
    alignas(16) std::array<uint8_t,0x20> growth_data{};
    alignas(16) std::array<uint8_t,0x20> growth_dictionary{};
    alignas(16) std::array<uint8_t,0x20> error_message{};
    void *data_loader_ptr=data_loader.data(),*level_loader_ptr=level_loader.data(),*level_root_ptr=level_root.data(),*growth_loader_ptr=growth_loader.data(),*growth_root_ptr=growth_root.data();
    put(manager_root.data(),0x10,data_loader_ptr);
    put(data_loader.data(),0xb8,level_loader_ptr);
    put(data_loader.data(),0x1e0,growth_loader_ptr);
    put(level_loader.data(),0x18,level_root_ptr);
    put(level_root.data(),0x10,level_data.data());put(level_root.data(),0x18,level_dictionary.data());
    // GrowthRecipeLoader+0x10 is errorMessage; +0x18 is the actual Root.
    put(growth_loader.data(),0x10,error_message.data());put(growth_loader.data(),0x18,growth_root_ptr);
    put(growth_root.data(),0x10,growth_data.data());put(growth_root.data(),0x18,growth_dictionary.data());
    auto graph=astrae::lobby::resolve_character99_object_graph(manager_root.data());
    assert(graph.data_loader==data_loader.data());assert(graph.level_factor_loader==level_loader.data());assert(graph.level_factor_root==level_root.data());assert(graph.level_factor_data==level_data.data());assert(graph.level_factor_dictionary==level_dictionary.data());assert(graph.growth_recipe_loader==growth_loader.data());assert(graph.growth_recipe_error_message==error_message.data());assert(graph.growth_recipe_root==growth_root.data());assert(graph.growth_recipe_data==growth_data.data());assert(graph.growth_recipe_dictionary==growth_dictionary.data());
    put(growth_loader.data(),0x18,nullptr);graph=astrae::lobby::resolve_character99_object_graph(manager_root.data());assert(graph.growth_recipe_root==nullptr&&graph.growth_recipe_data==nullptr&&graph.growth_recipe_dictionary==nullptr);
    std::cout<<"character99 object graph offsets passed\n";return 0;
}
