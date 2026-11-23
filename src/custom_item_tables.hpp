#pragma once
#include <string>
namespace astrae::lobby {
// Runtime-only schema audit for the original custom_item_tables gate.
bool inspect_custom_item_tables_runtime(void *data_manager,std::string *error=nullptr);
// Rebuilds the original Item and Item_Reward Managed rows from the recovered
// Custom Mail descriptors and merges them into the live DataSheet roots.
bool restore_custom_item_tables_runtime(void *data_manager,std::string *error=nullptr);
}
