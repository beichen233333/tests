#include "embedded_assets.hpp"
#include "json.hpp"
#include <cassert>
#include <cstdint>
#include <iostream>
#include <map>
#include <set>
#include <string>

int main(){
    std::string raw;assert(astrae::assets::read_embedded_asset("assets/tables/embedded_10_character99_progression.json",raw));
    const auto root=astrae::json::parse(raw);assert(root.is_object());assert(root["formatVersion"].as_u64()==1);
    const auto &factors=root["factors"].as_array();const auto &original=root["originalRecipes"].as_array();const auto &extended=root["extendedRecipes"].as_array();
    assert(factors.size()==160);assert(original.size()==619);assert(extended.size()==117);
    std::map<std::string,size_t> stat_counts;std::set<uint64_t> factor_levels;
    for(const auto &row:factors){assert(row["group"].as_u64()==1001);uint64_t level=row["level"].as_u64();assert(level>=60&&level<=99);assert(row["next"].as_u64()==(level==99?99:level+1));assert(row["rate"].as_u64()>0);const auto stat=row["stat"].as_string();assert(!stat.empty());++stat_counts[stat];factor_levels.insert(level);}
    assert(factor_levels.size()==40);assert(stat_counts.size()==4);for(const auto &entry:stat_counts)assert(entry.second==40);
    std::map<uint64_t,size_t> original_groups;for(const auto &row:original){assert(row["group"].as_u64()>0);assert(row["type"].is_string());for(const char *key:{"pt","pid","amount","personal","personal_amount"}){assert(row[key].is_array());assert(row[key].size()==3);}++original_groups[row["group"].as_u64()];}
    assert(original_groups[1001]==59&&original_groups[1002]==59&&original_groups[1003]==59&&original_groups[1004]==59);
    std::map<uint64_t,size_t> custom_groups;for(const auto &row:extended){auto group=row["group"].as_u64(),level=row["current"].as_u64();assert(group>=1001&&group<=1003);assert(level>=60&&level<=98);assert(row["next"].as_u64()==level+1);assert(row["type"].as_string()=="8941476925601368782");for(const char *key:{"pt","pid","amount","personal","personal_amount"})assert(row[key].size()==3);++custom_groups[group];}
    assert(custom_groups[1001]==39&&custom_groups[1002]==39&&custom_groups[1003]==39);
    std::cout<<"character99 progression asset: 160 factors, 619 original recipes and 117 extended recipes validated\n";return 0;
}
