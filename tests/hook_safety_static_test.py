#!/usr/bin/env python3
from pathlib import Path
import sys
root=Path(sys.argv[1])
hook=(root/'src/hook_runtime.cpp').read_text(encoding='utf-8')
boot=(root/'src/runtime_bootstrap.cpp').read_text(encoding='utf-8')
cmake=(root/'CMakeLists.txt').read_text(encoding='utf-8')
build=(root/'scripts/build_android_arm64.sh').read_text(encoding='utf-8')
manifest=(root/'src/original_hook_manifest.hpp').read_text(encoding='utf-8')
assert 'ASTRAE_ENABLE_RUNTIME_AUTOSTART' in boot
assert 'ASTRAE_ENABLE_RUNTIME_AUTOSTART' in cmake and ' OFF)' in cmake
assert 'ASTRAE_ENABLE_RUNTIME_AUTOSTART="${ASTRAE_ENABLE_RUNTIME_AUTOSTART:-OFF}"' in build
assert 'if(!spec.has_trampoline)' in hook
assert 'if(!spec->has_trampoline)' in hook
line=next(x for x in manifest.splitlines() if '"local_request"' in x)
assert ',0x0,false,"local_request"' in line
assert 'no original trampoline' in hook
assert 'automatic installation is disabled until its original control flow is recovered' in hook
assert 'make_trampoline' in hook and 'target+kPatchSize' in hook
assert 'AstraeOffline_RespondManaged(network,"ErrorPacket"' not in hook
# UI passthrough remains conservative: no hook without a trampoline can be registered.
ui=hook[hook.index('static const UiBinding kUiBindings[]'):hook.index('};',hook.index('static const UiBinding kUiBindings[]'))]
for forbidden in ['{10,','{11,','{20,','{37,']:
    assert forbidden not in ui
assert 'ASTRAE_ENABLE_RUNTIME_AUTOSTART' in boot
assert 'pthread_create' in boot
print('safe-load default and no-fabricated-trampoline checks passed')
