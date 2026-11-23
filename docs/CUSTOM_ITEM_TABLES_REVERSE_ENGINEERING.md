# `custom_item_tables` / Custom Mail reverse engineering

## Function boundary

- `custom_item_tables`: `0x24f280`, function size `0x15ac`.
- `custom_item`: `0x269100`, returns a pointer into the original `custom_mail_items` array for mapped ParcelIds; unmapped IDs return null.
- `custom_item_reply`: `0x2691e8`, separate receive/reply flow. It edits incoming cJSON values, processes reward arrays/balances, can call `sync_unlocks`, `bootstrap`, `local_updated_parcels`, `quest_exp_receipt`, and sends a response through `wire_respond`. It is not equivalent to `custom_item_tables`.
- `custom_item_text`: `0x23ffa0`, separate text helper.

## Static data reconstructed from ELF + DWARF + relocations

`custom_mail_item` (32 bytes):

| Offset | Field | Type |
|---:|---|---|
| `0x00` | `id` | `uint64_t` |
| `0x08` | `type` | `uint32_t` |
| `0x10` | `name` | `char *` |
| `0x18` | `desc` | `char *` |

`custom_mail_items@0x2b1150` contains 22 entries. Text pointers were reconstructed from `.rela.dyn` `R_AARCH64_RELATIVE` relocation addends; the exact localized text is retained in the asset.

`custom_mail_choice` (24 bytes):

| Offset | Field | Type |
|---:|---|---|
| `0x00` | `item` | `uint64_t` |
| `0x08` | `id` | `uint64_t` |
| `0x10` | `type` | `uint32_t` |

`custom_mail_choices@0x20b4e0` contains 715 entries. Per-item cardinalities are validated by Host tests:

| ParcelId | Type | Name | Choice rows |
|---:|---:|---|---:|
| 900010001 | 1 | 全角色自選箱 | 18 |
| 900010002 | 3 | 全神器自選箱 | 42 |
| 900010003 | 4 | 全膠片自選箱 | 32 |
| 900010006 | 3 | 全角色簽名神器自選箱 | 18 |
| 900010004 | 10 | 帳號升級經驗券 | 1 |
| 900010005 | 11 | 全符石自選箱 | 302 |
| 900010111–115 | 11 | T1 三角/四邊/五邊/六邊/八邊形符石箱 | 7 each |
| 900010121–125 | 11 | T2 三角/四邊/五邊/六邊/八邊形符石箱 | 21/20/21/22/21 |
| 900010131–135 | 11 | T3 三角/四邊/五邊/六邊/八邊形符石箱 | 31/30/31/32/31 |
| 900010136 | 11 | T3 十字星符石自選箱 | 7 |

## `custom_item(id)` resolver

The source uses a 136-byte lookup table at ELF VMA `0x685ed` for the ParcelId range `0x35a51011..0x35a51098` (decimal 900010001..900010136). Its switch/jump-table dispatch maps exactly 22 offsets to `custom_mail_items[0..21]`; all other offsets return null.

Valid offsets are:

```text
0->0, 1->1, 2->2, 3->4, 4->5, 5->3,
110->6, 111->7, 112->8, 113->9, 114->10,
120->11, 121->12, 122->13, 123->14, 124->15,
130->16, 131->17, 132->18, 133->19, 134->20, 135->21
```

All 114 other offsets are null. `src/custom_mail_catalog.cpp` now loads and validates the embedded dataset once, implements `resolve_custom_mail_item(parcel_id)` with the same 136-slot mapping, and exposes a read-only choices view. It returns null for IDs outside the range and for unmapped slots. Tests verify the exact mapping—not only the number of mapped entries—and all 715 choices.

## `custom_item_tables` runtime shape and current rewrite

Confirmed by entry disassembly and the current C++ implementation:

1. Load DataManager `_dataLoader` slots `+0x258` (`Item`) and `+0x270` (`Item_Reward`); each loader points to `Root` at `+0x18`, whose `DataList` and `DataDic` are at `+0x10` and `+0x18`.
2. The original helper guards both collection counts at `0x4000`; the rewrite applies that bound to existing and merged arrays.
3. The static reconstruction contains 22 `custom_mail_item` descriptors and 715 `custom_mail_choice` rows. The runtime rewrite generates Item and Item_Reward XML/Data rows from these records.
4. `ItemData.FromXml(ItemXmlData)` and `Item_RewardData.FromXml(Item_RewardXmlData)` are called with one XML row object each. The rewrite checks that the methods are static and the reflected parameter/return classes match the recovered types.
5. Item XML writes fields at `+0x10/+0x18/+0x20/+0x28/+0x30/+0x48/+0x50`; Reward XML writes `+0x10/+0x18/+0x20/+0x28`. After `FromXml`, the rewrite post-writes Item type at Data `+0x40` and Reward type at Data `+0x20`.
6. Both Root element types, every current row type, and both `ReplaceDataList(array)` parameter element types are preflighted before the first table mutation. The rewrite merges existing rows with missing generated rows, uses GC write barriers for references, calls `ReplaceDataList`, and confirms the new DataList count and DataDic are present.
7. The rewrite builds 21 `ItemUseSelectable.Request` Managed objects (non-type-10 descriptors) and checks JSON schema object round-trips. This verifies local Managed schema mapping only; it does **not** recreate the original native binary Wire serializer or prove protocol byte parity.
8. The original function continues into Gacha/Reward verification. That branch remains a hard blocker: `gacha_duplicate`, `gacha_converted_batch`, and `gacha_visual` have been disassembled, but their complete table lookups, reward merge/update semantics, JSON transformations, count/overflow boundaries and success/failure side effects are not yet reproduced. The dispatcher therefore remains fail-closed after table construction.

### Gacha/Reward reverse findings

- `gacha_duplicate@0x252f94` branches on request kind `1` or `4`. Kind `1` consults table slots `0x90` and `0x258`; kind `4` consults slot `0x4b8` and performs a `get_PieceItemId`/Wire-read path. It fills a compact output record only when the required records and values pass checks.
- `gacha_converted_batch@0x252044` first calls `gacha_duplicate`; then reads the relevant Gacha table and account object graph, traverses up to `0x1000` entries, identifies rows by type/id, converts Managed reward structures through JSON/Wire helpers, and accumulates 24-byte records `{descriptor:u64@0, table_id:u64@8, count:i32@16, auxiliary:i32@20}`. It guards integer multiplication/overflow and maximum destination capacity, merges matching low-32-bit descriptor type + table ID counts, and appends new records only when space remains. `src/gacha_reconstruction.cpp` now implements this recovered type/id merge, positive-count guard, signed-32-bit overflow check and append-capacity rule as a reusable helper.
- `gacha_visual@0x252a3c` now has an independent C++ payload builder and Host test. The exact JSON fields recovered from the referenced `.rodata` addresses are `Type` (number), `TableId` (decimal string), `AddedCount` (decimal string), `Count` (decimal string), `RewardFlag` (0 or 64), and `ReplaceInfos` (initially empty array). The original `gacha_converted_batch` appends these visual objects into a `ReplaceInfos` array. The payload builder is not yet integrated into the full batch conversion path.
- The `custom_item_tables` tail invokes `gacha_converted_batch` with a fixed static descriptor and checks a multi-field result record, including count values, identifier `10001`, and a successful helper return. Returning constants or bypassing this branch would not reproduce the original function.

### Remaining implementation work

1. Recover every `gacha_duplicate` table/key field and the `get_PieceItemId` method signature, including absent/invalid cases.
2. Integrate the recovered 24-byte accumulator merge helper into `gacha_converted_batch` and complete its table traversal, count scaling, append/overflow/error branches.
3. Integrate `build_gacha_visual` into the full batch conversion path and verify cJSON-equivalent ownership/append semantics.
4. Recreate the reference Wire/JSON conversions and `cJSON_Compare` semantics, then add deterministic Host tests for duplicates, count merges, capacity, conversion failure and JSON output.
5. Only after the Gacha/Reward gate returns the same result and side effects as the reference should step 7 be considered complete and step 8 enabled.
