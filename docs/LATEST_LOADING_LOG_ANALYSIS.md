# Latest loading-screen log analysis

Observed device build identity: `4b8fe8d415dd-216374b60bb84369` (2026-11-24 01:03:14–01:03:40). This log predates the latest `custom_resource_tables` source integration; do not use it as device evidence for that rewritten helper.

## Confirmed runtime facts

- Runtime autostart, Hook #3 and Hook #5 were enabled in this device build.
- IL2CPP initialized: 174 assemblies and 52 required exports resolved.
- ManagedWire loaded successfully. Its schema report contained 359 types and 1471 fields.
- Hook #5 `local_request` target fingerprint validated and direct replacement installed at RVA `0x2a5be04`.
- Hook #3 `touch` target fingerprint validated and direct replacement installed after Hook #5 at RVA `0x5cdf7a4`.
- A watchdog warning at 01:03:39.928 reported no Hook #3 callback during its first 15-second observation window; the first callback was logged at 01:03:39.993. This is a delayed observation, not a Hook install failure.
- No `Hook #5 local_request entered` line appears anywhere in the supplied log. Hook #5 installation is confirmed, but request interception is not.
- The Hook #3 callback recovered and validated the Controller, DataManager and Account. It used borrowed Managed roots from the active invocation rather than creating/releasing temporary GCHandles.
- Artifact capacity step 1 completed: `Common+0xc8` SecureInt was set to 9999 and the getter returned 9999.
- Character99 step 2 completed on device. The log records the LevelFactor FromXml invocation, merge from 720 to 880 rows using the 160-row extension, and GrowthRecipe FromXml invocation with 736 rows. The helper logged `reference character99_tables completed`.
- Permanent Raid step 3 completed.
- Seed step 4 was explicitly not reconstructed; the source log states the reference false-return branch is non-fatal and continues. This build skipped the helper and therefore did not reproduce any seed-helper side effects.
- Battle Multiple step 5 completed.
- The device binary then stopped before step 6 `custom_resource_tables`. The log lists it and later helpers as missing. That is consistent with an older build, not evidence that the current source step 6 failed.
- Hook #3 fallback forwarded the tap to original game `touch()`. The log explicitly says Hook #5 has not observed a `local_request` callback.

## Current source changes after this log

- `custom_resource_tables@0x24ebf4` has since been rewritten from the reference disassembly and wired into touch step 6. It contains four recovered branches: Vignette fallback strings, Challenge Gate normalization, Story Pack expiry and Sigrid Mastery repair.
- Added Host mock coverage for 32 managed string writes/GC write barriers, Challenge row invariants, DateTime fields, Sigrid matching, and the reference behavior that Story/Sigrid branches continue even if Challenge fails.
- Added static Custom Mail data from the reference ELF: 22 descriptors, 715 choice rows and a 136-slot parcel resolver. The C++ read-only resolver now implements exact mapping behavior and has a Host test.
- Current source now attempts step 7 `custom_item_tables` Managed Item/Item_Reward reconstruction after step 6. It can build 22 Item rows and 715 Item_Reward rows after runtime ABI preflight, but Gacha/Reward conversion parity and original binary Wire behavior remain incomplete; it still fails closed before `EnterLobby()` / `PlayFirstDay()`.

## Verification boundary

The current Host Release suite has 12 CTest cases and all pass. These tests validate only the implemented helper slices and mock object graphs. A new Android ARM64 build and device log are still required to verify `custom_resource_tables` in the actual game process. The next device run should include:

1. `reference touch step 6/15 custom_resource_tables substep completed` for Vignette, Challenge, Story and Sigrid.
2. `reference touch step 6/15 completed: custom_resource_tables`.
3. The expected fail-closed message after step 7 Managed Item/Item_Reward reconstruction while Gacha/Reward conversion parity remains incomplete.
4. Separate evidence of a real `Hook #5 local_request entered` callback and a mapped local Backend response.

Do not treat Hook #5 installation as proof of request interception, Host tests as proof of Android memory-layout correctness, or this partial progress as a complete offline lobby.
