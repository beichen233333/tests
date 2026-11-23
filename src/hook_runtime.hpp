#pragma once
#include <cstddef>
#include <cstdint>
#include <string>

namespace astrae::hooks {
std::string validate_original_hooks_json();
// A trampoline hook preserves a callable original function. It is only valid
// for manifest entries whose original ELF actually created a trampoline.
bool install_local_request_hook(std::string *error=nullptr);
// Installs Hook #3 touch replacement. If the custom prelude is incomplete, it forwards to original touch only when Hook #5 is installed.
bool install_touch_hook(std::string *error=nullptr);
bool touch_hook_installed();
uint64_t touch_hook_call_count();
size_t install_ui_passthrough_hooks(std::string *error=nullptr);
// Direct replacements normally own the selected function. Hook #3 is the exception: in
// ShadowHook shared mode it may call SHADOWHOOK_CALL_PREV only after Hook #5 is installed.
bool install_direct_replacement_hook(uint32_t hook_id,void *replacement,std::string *error=nullptr);
bool local_request_hook_installed();
bool managed_request_seen();
}
