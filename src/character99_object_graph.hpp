#pragma once
#include <cstddef>
#include <cstdint>
#include <cstring>

namespace astrae::lobby {
struct Character99ObjectGraph {
    void *data_loader=nullptr;
    void *db_loader=nullptr;
    void *level_factor_loader=nullptr;
    void *level_factor_root=nullptr;
    void *level_factor_data=nullptr;
    void *level_factor_dictionary=nullptr;
    void *growth_recipe_loader=nullptr;
    void *growth_recipe_error_message=nullptr;
    void *growth_recipe_root=nullptr;
    void *growth_recipe_data=nullptr;
    void *growth_recipe_dictionary=nullptr;
};
inline void *character99_read_pointer(const void *base,size_t offset){void *value=nullptr;if(base)std::memcpy(&value,static_cast<const uint8_t*>(base)+offset,sizeof(value));return value;}
inline Character99ObjectGraph resolve_character99_object_graph(void *data_manager){
    Character99ObjectGraph graph;
    graph.data_loader=character99_read_pointer(data_manager,0x10);
    graph.db_loader=character99_read_pointer(data_manager,0x18);
    graph.level_factor_loader=character99_read_pointer(graph.data_loader,0xb8);
    graph.level_factor_root=character99_read_pointer(graph.level_factor_loader,0x18);
    graph.level_factor_data=character99_read_pointer(graph.level_factor_root,0x10);
    graph.level_factor_dictionary=character99_read_pointer(graph.level_factor_root,0x18);
    graph.growth_recipe_loader=character99_read_pointer(graph.data_loader,0x1e0);
    graph.growth_recipe_error_message=character99_read_pointer(graph.growth_recipe_loader,0x10);
    graph.growth_recipe_root=character99_read_pointer(graph.growth_recipe_loader,0x18);
    graph.growth_recipe_data=character99_read_pointer(graph.growth_recipe_root,0x10);
    graph.growth_recipe_dictionary=character99_read_pointer(graph.growth_recipe_root,0x18);
    return graph;
}
}
