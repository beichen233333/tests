#pragma once
#include <cstdint>
#include <string>

namespace astrae::lobby {
struct SignedExchangeXmlSpec {
    uint64_t id=100;
    uint32_t type=2;
    uint64_t parcel_id=900010006ULL;
    uint64_t packed_cost={0x000003e800000001ULL};
    uint32_t trailing_flag=0;
    const char *icon="UI/UIResources/03_Icon/Item/Icon_AdjustStore_RandomArtifact";
    const char *name_localize="12969540579487863062";
    const char *description_localize="1983076511861317074";
};
const SignedExchangeXmlSpec &signed_exchange_xml_spec();
bool signed_exchange_should_insert(uint64_t current_count,bool parcel_id_present,std::string *error=nullptr);
bool restore_signed_exchange_tables_runtime(void *data_manager,std::string *error=nullptr);
}
