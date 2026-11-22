#pragma once
#include <cstdint>
#include <string>
#include "json.hpp"

struct cJSON;

namespace astrae::lobby {
// Reconstructed from permanent_raid_tables@0x24e494 in the original ELF.
// Applies the original 128-bit row field constant to every row in the
// DataManager's permanent-raid table (original accepted count: 0..128).
bool apply_permanent_raid_tables(void *data_manager,std::string *error=nullptr);
// Reconstructed from local_activity_period@0x24b070. Returns the managed
// DateTimeExtensions.GetRecentResetDay(DateTime.UtcNow) ticks, or -1 on failure.
int64_t local_activity_period(std::string *error=nullptr);
// Registers the reconstructed DateTime reset provider with the local Store ABI, matching touch@0x23972c.
void register_local_activity_period_provider();
// Reconstructed IL2CPP access primitives shared by the original lobby/table helpers.
// Returns a pinned GC handle to the row; the caller must release it with Runtime::release().
uint32_t table_get_key_pinned(void *data_manager,uint32_t table_offset,const void *key,std::string *error=nullptr);
uint32_t table_get_pinned(void *data_manager,uint32_t table_offset,uint64_t key,std::string *error=nullptr);
uint32_t table_get_i32_pinned(void *data_manager,uint32_t table_offset,int32_t key,std::string *error=nullptr);
bool row_u64(void *row,const std::string &getter,uint64_t *value,std::string *error=nullptr);
bool row_i32(void *row,const std::string &getter,int32_t *value,std::string *error=nullptr);
// One-time five-protocol ManagedWire round-trip probe recovered from office_wire_probe.
bool office_wire_probe(std::string *error=nullptr);
// Reconstructs guide_state@0x249598. Returns an owned deep copy of store key "guidebook"
// with the original offerings/claims/progress object containers ensured.
cJSON *guide_state_from_document(const cJSON *document);
cJSON *guide_state();
// Reconstructs installation_native_tables@0x248410 and its cached static-data overlay.
bool installation_native_tables(void *data_manager,std::string *error=nullptr);
// Reconstructed from battle_multiple_tables@0x24e914: sets the two inline SecureInt
// values in the common DataManager object to 99 and verifies the corresponding getters.
bool battle_multiple_tables(void *data_manager,std::string *error=nullptr);
bool installation_native_get_row(const std::string &table,uint64_t id,json::Value *row,std::string *error=nullptr);
enum class InitialLobbyAction : uint8_t { None=0, EnterLobby=1, PlayFirstDay=2 };
// Reconstructs the final login-page branch in touch@0x2392f4. The caller supplies
// the controller instance retained by the original hook and the current content bitset.
bool run_initial_lobby_route(void *login_controller,void *data_manager,uint64_t content_bits,InitialLobbyAction *action,std::string *error=nullptr);
// Same route using the reconstructed local Store API's content_bits value.
bool run_initial_lobby_route_from_store(void *login_controller,void *data_manager,InitialLobbyAction *action,std::string *error=nullptr);
// Minimal Phase-1 replacement for touch: deliberately skips the first-day gate and enters the lobby.
bool touch_enter_lobby(void *login_controller,std::string *error=nullptr);
}
