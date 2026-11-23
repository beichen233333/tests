# Crash containment and legacy save recovery

## 1. Confirmed device crash

The merged device log contains an Android SystemUI notification NPE and a separate game-process SIGSEGV. The SystemUI exception is outside this library. The game crash is an ARM64 `SIGSEGV/SEGV_MAPERR` with fault address `0x38` in the Unity main thread. In the same run, the last AstraeOffline diagnostic before the tombstone was the `Character_LevelFactorRoot.FromXml` ABI validation line; there was no successful invocation/return log. Treat that managed factory call as unsafe on this target until it is isolated in an instrumented process.

`src/character99_safety_policy.hpp` therefore hard-disables experimental `FromXml` construction at compile time. `build_from_xml_root()` checks this policy as its first operation and returns before resolving/allocating XML root objects, data rows, arrays, or calling `il2cpp_runtime_invoke`. The former CMake option was removed, so an old CMake cache cannot turn the unsafe path back on. The Character99 Managed rewrite remains incomplete; the touch prelude reports the failure and uses the configured original-touch fallback only when Hook #5 was installed first. This is a crash containment change, not a claim that Character99 data has been restored.

## 2. Save locations and load precedence

The native Backend save is:

`files/AstraeOffline/SinglePlayer/save.json`

The previous compatibility Store save is:

`files/AstraeOffline/SinglePlayer/compat-store/save.json`

Runtime bootstrap now opens the compatibility Store before initializing Backend, so its schema-v1 document is available to the migration path. Backend precedence is:

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

## 4. Validation

Host CMake tests cover legacy schema mapping, currency IDs, byte-for-byte retention of the original compat file, historical-save import over the old empty placeholder, V3-primary precedence when it has progress, fresh-profile creation, and fail-closed behavior for an invalid old save. `character99_fromxml_safety_policy_test` asserts the dangerous managed factory is compile-time disabled.

The Android build script also uses the binary ELF header validator (ELF64, little-endian, ET_DYN, `e_machine=183`) rather than matching `readelf` wording, so the prior CI architecture-check false negative is not reintroduced. Host tests do not replace an Android NDK build or a device run. The next Android log should show a new build identity, `compat Store opened before Backend migration`, either `compat-store-migrated` / `compat-store-imported-over-placeholder` or `storeRecovery=primary`, and `character99 FromXml invocation BLOCKED before Managed allocations` if Character99 reconstruction is reached.
