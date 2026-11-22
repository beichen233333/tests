#pragma once
#include <string>
#include <cstddef>
namespace astrae::hooks {
std::string validate_original_hooks_json();
bool install_local_request_hook(std::string *error=nullptr);
size_t install_ui_passthrough_hooks(std::string *error=nullptr);
bool local_request_hook_installed();
bool managed_request_seen();
}
