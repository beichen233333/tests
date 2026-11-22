#pragma once
#include <cstddef>
#include <string>
namespace astrae::assets {
bool read_embedded_asset(const std::string &path,std::string &out);
size_t embedded_asset_count();
}
