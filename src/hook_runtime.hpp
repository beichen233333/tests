#pragma once
#include <string>
namespace astrae::hooks {
std::string validate_original_hooks_json();
bool install_local_request_hook(std::string *error=nullptr);
bool local_request_hook_installed();
}
