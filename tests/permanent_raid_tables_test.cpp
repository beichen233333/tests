#include "lobby_restore.hpp"
#include <array>
#include <cassert>
#include <cstdint>
#include <cstring>
#include <iostream>
#include <string>
#include <vector>

namespace {
constexpr uint64_t kExpected=0x2bca2875f4373fffULL;
struct Fixture {
    std::vector<uint8_t> data_manager=std::vector<uint8_t>(0x18);
    std::vector<uint8_t> root=std::vector<uint8_t>(0x328);
    std::vector<uint8_t> manager=std::vector<uint8_t>(0x20);
    std::vector<uint8_t> table=std::vector<uint8_t>(0x18);
    std::vector<uint8_t> list;
    std::vector<std::array<uint8_t,0x50>> rows;
    explicit Fixture(size_t count):list(0x20+count*sizeof(void*)),rows(count){
        put(data_manager.data(),0x10,root.data());put(root.data(),0x320,manager.data());put(manager.data(),0x18,table.data());put(table.data(),0x10,list.data());put(list.data(),0x18,static_cast<uint64_t>(count));
        for(size_t i=0;i<count;++i){rows[i].fill(0);put(list.data(),0x20+i*sizeof(void*),rows[i].data());}
    }
    template<typename T> static void put(void *base,size_t offset,T value){std::memcpy(static_cast<uint8_t*>(base)+offset,&value,sizeof(value));}
};
void verify_count(size_t count,bool expected){Fixture f(count);std::string error;const bool ok=astrae::lobby::apply_permanent_raid_tables(f.data_manager.data(),&error);assert(ok==expected);if(!expected){assert(!error.empty());return;}assert(error.empty());for(const auto &row:f.rows){std::array<uint64_t,2> actual{};std::memcpy(actual.data(),row.data()+0x38,sizeof(actual));assert(actual[0]==kExpected&&actual[1]==kExpected);}}
}
int main(){verify_count(0,false);verify_count(1,true);verify_count(128,true);verify_count(129,false);std::cout<<"permanent_raid_tables: counts 0, 1, 128, 129 validated\n";return 0;}
