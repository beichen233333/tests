# Reference `touch()` initialization sequence

This file records the ordered control flow recovered from the user-supplied reference ELF's AArch64 disassembly and DWARF line information. The reference ELF is an analysis input only: it must never be linked, loaded, copied into the application, or shipped in the source archive.

## Outer sequence

The reference `touch(controller, method)` does not jump directly to `EnterLobby()`. It recovers the live account and DataManager objects, applies local activity/reset state, executes the following helpers, and checks their return values before the final first-day/lobby branch:

1. `artifact_capacity_tables(DataManager)` — mandatory; failure aborts the route.
2. `character99_tables(DataManager)` — mandatory; failure aborts the route.
3. `permanent_raid_tables(DataManager)` — mandatory; the project has a candidate reconstruction named `apply_permanent_raid_tables`, but it is not yet called from the verified touch sequence.
4. `seed_table_defaults(DataManager)` — mandatory; this itself calls `seed_ticket_defaults()` and accesses the seed table at DataManager root offset `0x328`.
5. `battle_multiple_tables(DataManager)` — mandatory; the project has a candidate reconstruction, but it is not yet called from the verified touch sequence.
6. `custom_resource_tables(DataManager)` — mandatory.
7. `custom_item_tables(DataManager)` — mandatory.
8. `signed_exchange_tables(DataManager)` — mandatory.
9. `sync_unlocks()` — mandatory; calls `earned_unlocks()` and `store_apply_account_unlocks()` when the persistent unlock snapshot differs.
10. `bootstrap(Account)` — mandatory; see its nested sequence below.
11. `local_account_event_login()` — mandatory.
12. `initialize_guide_cache(Account)` — mandatory; calls `guide_restore_cache(Account, true)`.
13. `office_wire_probe()` — present in the current project, but not yet wired into the ordered sequence.
14. `installation_native_tables(DataManager)` — the reference treats failure as a logged degradation rather than the same hard stop as the earlier mandatory helpers; a candidate reconstruction exists but is not yet wired into the ordered sequence.
15. Only after the above, evaluate the Store content bits/first-day state and invoke `EnterLobby()` or `PlayFirstDay()`.

## `bootstrap(Account)` nested sequence

The reference `bootstrap` is not a single field assignment. The disassembly shows it constructs/updates Managed state from the persistent local Store, queues a prologue gift when the account's first-day state requires it, converts up to 30 type-5 reward balances through `rune_reward_resolve()` and `store_purchase_rewards()`, then executes these mandatory restorers in order:

1. `restore_balances(Account)`
2. `restore_foundation(Account)`
3. `pack_objects_cache(...)` for stored pack identifiers
4. `local_mail_count(Account)`
5. `restore_gacha(Account)`
6. `restore_farming_cache(Account)`
7. `restore_raid_cache(Account)`
8. `date_restore(Account)`
9. `installation_restore(Account)`
10. `guide_restore_cache(Account, false)`
11. `restore_daily_cache(Account)`
12. `restore_pack_missions(Account)`
13. `restore_puzzles(Account)`

`bootstrap` returns failure if required substeps fail. After it succeeds, `touch()` separately calls `local_account_event_login()` and `initialize_guide_cache(Account)` (`guide_restore_cache(Account, true)`). These two Guide passes are distinct and must not be collapsed into one no-op.

## Reconstruction status in this source tree

| Reference helper | Current source status |
|---|---|
| `artifact_capacity_tables` | Reconstructed from `0x24c2ec`, wired as outer step 1; Host IL2CPP-mock tests cover exact DataManager path, `SecureInt.set_Value(9999)`, getter read-back, missing object and mismatch. Android game validation is pending. |
| `character99_tables` | Not reconstructed; complex recipe/stat cache mutation |
| `permanent_raid_tables` | Candidate `apply_permanent_raid_tables` exists; count range 1..128 and row writes are Host-tested; integration/device verification pending (cannot run before missing step 2 and step 4 are restored). |
| `seed_ticket_defaults` / `seed_table_defaults` | Not reconstructed |
| `battle_multiple_tables` | Candidate implementation exists; integration/device verification pending (outer step 5). |
| `custom_resource_tables` | Not reconstructed |
| `custom_item_tables` | Not reconstructed |
| `signed_exchange_tables` | Not reconstructed |
| `earned_unlocks` / `sync_unlocks` | Not reconstructed |
| `bootstrap` and all nested restorers above | Not reconstructed as a complete Managed-state pipeline |
| `local_account_event_login` | Not reconstructed |
| `guide_restore_cache` / `initialize_guide_cache` | Only JSON-side `guide_state_from_document` exists; live Managed cache restoration is not reconstructed |
| `office_wire_probe` | Candidate implementation exists; integration/device verification pending |
| `installation_native_tables` | Candidate implementation exists; integration/device verification pending |

## Current phase execution record

- Step 1 `artifact_capacity_tables` is now called by `touch_enter_lobby()` immediately after Controller/DataManager/Account validation. It uses the live Common object at `DataManager root + 0x140 -> wrapper + 0x18 -> object + 0x10`, locates `_AT.Shared.SecureInt`, invokes `set_Value(9999)` on the inline field at `Common+0xc8`, then invokes `get_Inventory_Artifact_Max_Slots()` and validates the boxed Int32 is exactly 9999. Any failed lookup, Managed exception, null result, or mismatched readback aborts before later helpers.
- Host regression test `artifact_capacity_tables` validates the field path, setter/getter behavior, missing Common and incorrect readback. `permanent_raid_tables` separately validates the source-derived 1..128 count bounds and both 64-bit row values. These are host mock tests, not Android device validation.
- The callback still stops after Step 1 because Step 2 `character99_tables` is not reconstructed. The reference's following steps have not been skipped or marked successful. The fail-closed status is expected until the complete ordered chain exists.

## Gate policy

The current Hook #3 intentionally fails closed. A helper being present in C++ does not mean it has been run in the reference order or passed on the target game. Do not enable `EnterLobby()` / `PlayFirstDay()` until every mandatory helper has a tested implementation, every nested `bootstrap` step has succeeded, and the resulting Account/DataManager/cache state has been compared against a reference run. Do not mark Hook #5 fully offline until every request/response family and all active Managed event paths have been verified.
