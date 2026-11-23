# Crash containment and legacy save recovery

## 1. Latest Character99 crash diagnosis and fix

The 2026-11-23 23:55:52 ARM64 tombstone is a game-process `SIGSEGV/SEGV_MAPERR` on the Unity main thread, fault address `0x38`. In that build, the last project log was the `Character_LevelFactorRoot.FromXml` ABI check; the stack enters `libil2cpp.so` from the Character99 reconstruction path. The next diagnostic build intentionally blocked `FromXml` before allocating XML objects, so no new crash occurred but Character99 step 2 remained incomplete.

Source review identified an argument-marshalling defect in the first reconstruction attempt: one generic setter helper treated managed references as addresses of native pointer variables. `il2cpp_runtime_invoke` expects a value-type argument as a pointer to its storage, but a reference-type argument is passed as the managed object pointer itself. This follows the IL2CPP runtime implementation: reference-type by-value arguments are forwarded as `parameters[i]`, while by-reference parameters are handled separately. See the IL2CPP runtime implementation at <https://github.com/MlgmXyysd/libil2cpp/blob/master/libil2cpp/Unity_2023.2/2023.2.0b2/vm/Runtime.cpp>.

The current code separates `set_value_property()` from `set_reference_property()`. Value setters validate the declared parameter is a value type and that its managed size matches the supplied native storage width. Reference setters validate that the declared parameter is a reference type and that the actual managed object's runtime class matches the parameter class; the argument array receives the managed object pointer directly, not `&object_pointer`. This covers `DataList`, `String`, `String[]`, XML Root and GrowthRecipeLoader.Root assignments.

Before calling either `FromXml`, the code now verifies the XML Root's `DataList` field is the exact array just built, checks its length, checks the array element class, and checks the first and last row runtime classes. Required IL2CPP reflection exports are checked before the operation, and the invocation-start log includes method, instance, XML Root and row array pointers. If any check fails, the function returns before invoking `FromXml`.

The previous hard-disable of `FromXml` has been removed because the concrete reference-argument marshalling defect is now fixed and guarded. This remains a candidate source-level fix until a new Android ARM64 build is installed and the device log confirms `XML payload validated`, `FromXml invocation starting`, and a successful return. Host tests cannot prove Unity/IL2CPP runtime behavior.

## 2. Save locations and load precedence

The native Backend save is:

`files/AstraeOffline/SinglePlayer/save.json`

The previous compatibility Store save is:

`files/AstraeOffline/SinglePlayer/compat-store/save.json`

Runtime bootstrap opens the compatibility Store before initializing Backend. The latest device log confirms the migration path: the previous synthetic placeholder was archived as `save.json.placeholder`; the old compatibility save was imported with 30 characters, 131 inventory entries, `tutorial.content_bits=725367`, and `initial_index=11`; the original compat file was retained byte-for-byte. Backend diagnostics report `legacySaveImported=true` and `storeRecovery=compat-store-imported-over-placeholder`.

Backend precedence is:

1. A valid native V3 primary save with real progress.
2. A recognized empty placeholder V3 save created by the earlier broken bootstrap may be replaced once by a valid historical compatibility save. The placeholder is renamed to `save.json.placeholder` (timestamp suffix if needed).
3. Native `.bak` / interrupted `.tmp` recovery.
4. A valid historical schema-v1 compatibility save, imported once into native V3.
5. Only when no prior save exists and no unreadable/unsupported save candidate is present, create a fresh local profile.

The placeholder predicate is intentionally narrow: default offline account name/ID/level, tutorial bits and index both zero, untouched starter character, untouched default formation/lobby selections, and empty gameplay/progression containers. Any meaningful native progress or customization makes the native V3 save win over compat data.

Migration preserves the original compatibility file byte-for-byte and embeds the full source document in `LegacyCompatSnapshot` in the V3 save. A malformed, unsupported, or semantically invalid existing save is retained and causes initialization to fail rather than silently replacing it with a new account.

## 3. Tutorial state

No new tutorial progression is synthesized. The migration copies `tutorial.content_bits` and `tutorial.initial_index` from the old save. A legacy profile with zero characters remains zero-character; Backend does not invent a starter character or formation for imported legacy profiles. A genuinely new install gets one initial local profile once, and later launches load its native V3 primary.

The compatibility Store's narrowly tagged `first_day_skipped` migration marker, written by an earlier development build, is removed together with its synthetic bit-0 change. Other tutorial bits and the initial index are retained.

## 4. Validation and remaining boundary

Host CMake tests cover legacy schema mapping, currency IDs, byte-for-byte retention of the original compat file, historical-save import over the old empty placeholder, V3-primary precedence when it has progress, fresh-profile creation, and fail-closed behavior for an invalid old save. The Character99 invoke-marshalling test verifies that value-type arguments use storage addresses while reference-type arguments use the managed object pointer. Additional runtime guards compare IL2CPP setter parameter metadata, object classes, array element classes and row counts.

The Android build script validates the binary ELF header (ELF64, little-endian, ET_DYN, `e_machine=183`) rather than matching `readelf` wording. Host tests do not replace an Android NDK build or a device run. Hook #5 installation is also not proof that the local request handler has received a callback; the latest supplied log had no `Hook #5 local_request entered` line. The next device run must confirm both Character99 FromXml completion and an actual local request callback before claiming the offline login path works.
