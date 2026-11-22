#!/usr/bin/env python3
from pathlib import Path
import sys
root=Path(sys.argv[1])
hook=(root/'src/hook_runtime.cpp').read_text(encoding='utf-8')
boot=(root/'src/runtime_bootstrap.cpp').read_text(encoding='utf-8')
assert 'make_trampoline' in hook and 'target+kPatchSize' in hook
assert 'LocalRequestFn original=ASTRAE_ORIGINAL(LocalRequestFn,5)' in hook
assert 'original(network,server,url,request,refresh,method)' in hook
assert 'AstraeOffline_RespondManaged(network,"ErrorPacket"' not in hook
for symbol in ['text_locale_hook','banner_click_hook','ui_input_tick_hook','unlock_notifications_hook','unlock_popup_step_hook','action_pack_shown_hook','first_day_load_hook','native_facility_click_hook','item_selection_initialize_hook','signed_exchange_refresh_hook']:
    assert symbol in hook, symbol
assert 'initialize_wait(120000,100,&error)' in boot
assert boot.index('initialize_wait') < boot.index('load_schema') < boot.index('install_local_request_hook')
assert 'install_ui_passthrough_hooks' in boot
assert boot.index('managed_request_seen') < boot.index('install_ui_passthrough_hooks')
assert 'bool managed_request_seen()' in (root/'src/hook_runtime.hpp').read_text(encoding='utf-8')
assert '{37,' not in hook[hook.index('static const UiBinding kUiBindings[]'):hook.index('};',hook.index('static const UiBinding kUiBindings[]'))]
assert 'no hooks installed' in boot
print('hook fallback/trampoline/deferred UI safety checks passed')
