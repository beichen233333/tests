#pragma once
#include <cstddef>
#include <cstdint>
#include <string>

namespace astrae::lobby {
struct VignetteIconFallback { uint64_t id; const char *path; };
const VignetteIconFallback *vignette_icon_fallback_data(size_t *count=nullptr);
bool repair_vignette_icon_fallbacks(void *data_manager,std::string *error=nullptr);
bool challenge_gate_tables(void *data_manager,std::string *error=nullptr);
bool permanent_story_pack_tables(void *data_manager,std::string *error=nullptr);
bool sigrid_mastery_tables(void *data_manager,std::string *error=nullptr);
bool custom_resource_tables(void *data_manager,std::string *error=nullptr);
}
