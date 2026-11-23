#include "touch_fallback_policy.hpp"
#include <cassert>
#include <iostream>
int main(){using astrae::hooks::should_forward_original_touch;assert(!should_forward_original_touch(false,false));assert(!should_forward_original_touch(true,false));assert(!should_forward_original_touch(true,true));assert(should_forward_original_touch(false,true));std::cout<<"touch fallback policy passed\n";return 0;}
