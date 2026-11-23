# `earned_unlocks` / `sync_unlocks` reverse reconstruction

Source: original `libastraeoffline.so` AArch64 disassembly (`earned_unlocks@0x2531ac`, `sync_unlocks@0x250da4`) plus the compatibility Store ABI implementation of `store_quest_rank`, `store_apply_account_unlocks` and `apply_account_exp`. The original ELF is an analysis reference only; this project does not load or link it.

## Recovered pseudocode

```text
sync_unlocks(data_manager):
    earned = earned_unlocks(data_manager, max_bit=0)
    if earned == null: return false
    current = store_document["unlocks"]
    if print_unformatted(current) != print_unformatted(earned):
        return store_apply_account_unlocks(earned)
    return true

earned_unlocks(data_manager, max_bit):
    loader = *(data_manager + 0x10)
    unlock_rows = *( *(loader + 0x4a0) + 0x18 ).DataList
    scenario_rows = *( *(loader + 0x3c8) + 0x18 ).DataList
    require 1..128 unlock rows and <=4096 scenario rows
    output = deep_copy(store.unlocks) or []
    for each Unlock row:
        group_id = get_UnlockGroupId()
        mode = *(row + 0x20)
        accumulator = get_DefaultValue()
        if output already contains this TableId: accumulator = parsed existing Value
        for each ScenarioPack row:
            if store_quest_rank(pack_id, kind_at_0x30, get_BitIndex()) == false:
                only special-case pack 1000 / kind 0 / bit 1..max_bit
            read ReqUnlockGroupId[] at +0x178 and ReqUnlockGroupValue[] at +0x180
            null arrays => skip; unequal lengths or length >32 => finalize current group and stop later groups
            for each matching group ID:
                mode 1 => accumulator |= (1ULL << value), only value <=63
                mode 2 => accumulator = max(accumulator, uint32(value))
        if mode==2 and group_id==200: accumulator=99
        output[TableId] = decimal string(group_id)
        output[Value] = decimal string(accumulator)
    return output
```

## Runtime layout and bounds

| Object | Evidence / access |
|---|---|
| DataManager loader | `DataManager +0x10` |
| Unlock table | loader `+0x4a0` → table Root `+0x18` → `DataList +0x10` |
| ScenarioPack table | loader `+0x3c8` → table Root `+0x18` → `DataList +0x10` |
| Managed array | length `+0x18`, vector `+0x20` |
| Unlock mode | row `+0x20` (`int32`) |
| ScenarioPack mode/kind | row `+0x30` (`int32`) |
| Scenario requirement arrays | `ReqUnlockGroupId` `+0x178`; `ReqUnlockGroupValue` `+0x180` |
| Bounds | Unlock count `1..128`; ScenarioPack count `0..4096`; each paired requirement array `0..32` and equal length |

## Quest rank adapter

`store_quest_rank(group, kind, bit)` mapping recovered from the compatibility Store implementation:

- kind `0` / `8`: `story_index >= bit`
- kind `1`: `normal_bits`, kind `2`: `hard_bits`, kind `3`: `very_hard_bits`, kind `5`: `challenge_bits`; 3-bit rank at shift `(bit-1)*3`, max bit 21
- kind `12`: `substory_bits`; single bit at shift `bit`, max bit 63

The reconstructed Backend stores normalized `PackState.Packs` fields (`NormalQuestBit`, etc.) as JSON integers, whereas the compatibility Store file stores decimal strings. `pack_state_quest_rank()` accepts both representations but preserves the exact bit positions and mode mapping.

## Commit / rollback behavior

The compatibility implementation deep-copies the Store document and the new unlock array, replaces the `unlocks` key, calls `apply_account_exp(0)`, and commits. Failure restores the whole previous document. `apply_account_exp(0)` checks the account level/experience; if an unlock row has string `TableId=="200"` and a string `Value`, that value must parse as a signed integer >=10. The current Backend adapter writes `Progress.UnlockInfos`, applies the same account level threshold migration, commits atomically, and restores the pre-call state on validation/commit failure.

## Code status

- `src/unlock_sync.cpp`: pure reducer and quest-rank adapter.
- `src/unlock_sync_runtime.cpp`: IL2CPP DataManager/DataList extraction and `sync_unlocks_runtime()`.
- `src/backend.cpp`: atomic `Backend::apply_account_unlocks()` persistence and account EXP gate.
- `tests/unlock_sync_test.cpp`: reducer, bitfield, bounds and malformed-array control-flow tests.
- `tests/backend_legacy_save_integration_test.cpp`: account-level migration and transaction rollback integration tests.

This helper is not yet called from `touch_enter_lobby()`. Original order requires full success of mandatory `custom_item_tables` step 7 before Signed Exchange step 8 and sync unlocks step 9. Runtime code has not yet been verified against a live Android IL2CPP process, and the lobby path remains fail-closed.
