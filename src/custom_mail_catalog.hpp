#pragma once
#include <cstddef>
#include <cstdint>
#include <string>
#include <vector>
namespace astrae::lobby {
struct CustomMailItem { uint64_t parcel_id=0;uint32_t type=0;std::string name;std::string description; };
struct CustomMailChoice { uint64_t item=0;uint64_t id=0;uint32_t type=0; };
bool ensure_custom_mail_catalog(std::string *error=nullptr);
const CustomMailItem *resolve_custom_mail_item(uint64_t parcel_id);
const CustomMailItem *custom_mail_item_at(size_t index);
const std::vector<CustomMailChoice> *custom_mail_choices(uint64_t parcel_id);
size_t custom_mail_item_count();
size_t custom_mail_choice_count();
}
