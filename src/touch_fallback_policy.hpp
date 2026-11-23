#pragma once
namespace astrae::hooks {
// Preserve the original game click path only when the custom dispatcher is not
// ready AND Hook #5 has already been installed to keep requests local.
constexpr bool should_forward_original_touch(bool custom_prelude_ready,bool local_request_hook_ready){return !custom_prelude_ready&&local_request_hook_ready;}
}
