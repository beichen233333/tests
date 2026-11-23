# Reference `touch()` initialization sequence

本文件记录用户提供的 `libastraeoffline.so` 中 `touch(controller, method)` 的 AArch64 反汇编/DWARF 恢复结果。原 ELF 只作为离线分析输入，不得链接、加载、复制进应用或随源码包交付。

## Outer sequence

原版 `touch()` 不是直接跳到 `EnterLobby()`。它从 Controller 取得 DataManager/Account，执行以下顺序并检查结果：

1. `artifact_capacity_tables(DataManager)` — mandatory。
2. `character99_tables(DataManager)` — mandatory。
3. `permanent_raid_tables(DataManager)` — mandatory。
4. `seed_table_defaults(DataManager)` — helper 返回 false 时走非致命路径，继续 Battle；helper 内部副作用仍然需要独立还原。
5. `battle_multiple_tables(DataManager)` — mandatory。
6. `custom_resource_tables(DataManager)` — mandatory。
7. `custom_item_tables(DataManager)` — mandatory。
8. `signed_exchange_tables(DataManager)` — mandatory。
9. `sync_unlocks()` — mandatory；比较 `earned_unlocks()` 与持久化快照，必要时调用 `store_apply_account_unlocks()`。
10. `bootstrap(Account)` — mandatory，包含 13 个嵌套恢复步骤。
11. `local_account_event_login()` — mandatory。
12. `initialize_guide_cache(Account)` — 调用 `guide_restore_cache(Account, true)`。
13. `office_wire_probe()` — 独立辅助步骤。
14. `installation_native_tables(DataManager)` — 失败按降级/日志处理，不等同于前面的 hard stop。
15. 检查 Store content bits / first-day 状态后调用 `EnterLobby()` 或 `PlayFirstDay()`。

## `bootstrap(Account)` nested sequence

原版 `bootstrap` 不是单一字段赋值。其前置逻辑涉及首日 prologue 礼物、至多 30 个 type-5 reward balance 的 `rune_reward_resolve()` / `store_purchase_rewards()` 转换；之后按序调用以下 mandatory restorers：

1. `restore_balances(Account)`
2. `restore_foundation(Account)`
3. `pack_objects_cache(...)`
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

`bootstrap` 成功后，外层还会执行 `local_account_event_login()` 和 `initialize_guide_cache(Account)`（即 `guide_restore_cache(Account, true)`）。这两次 Guide 恢复不能合并成一个 no-op。

## Reconstruction status

| Helper | 当前源码状态 | 证据/缺口 |
|---|---|---|
| `artifact_capacity_tables` | 已重写并接入 step 1 | Host mock 通过；用户设备日志观察到 9999 写入/回读。 |
| `character99_tables` | 已重写并接入 step 2 | ELF 静态资源 + IL2CPP FromXml；用户设备日志记录 LevelFactor 880、GrowthRecipe 736 成功。 |
| `permanent_raid_tables` | 已重写并接入 step 3 | Host mock 通过；用户设备日志报告完成。 |
| `seed_ticket_defaults` / `seed_table_defaults` | 未重写 | 原版 false 返回非致命；当前源码只跳过并标记，不具备该 helper 的副作用。 |
| `battle_multiple_tables` | 已重写并接入 step 5 | Host mock/对象验证；用户设备日志报告完成。 |
| `custom_resource_tables` | 源码重写并接入 step 6 | 32 Vignette、Challenge Gate、Story Pack expiry、Sigrid Mastery；Host mock 通过。新代码尚未取得设备验证。 |
| `custom_item_tables` | 部分重写，仍为 hard gate | Managed Item/Item_Reward rows 已构造并合并；运行时 FromXml/字段/Root.ReplaceDataList ABI 预检和 21 组 Managed schema object round-trip 已接入。还缺 Gacha/Reward conversion、完整 cJSON/wire 比较语义及设备验证；当前 round-trip 不等于原版二进制 Wire parity。 |
| `signed_exchange_tables` | 独立 runtime helper 已移植，未接入 touch | 入口 `0x25082c`；DataLoader 槽 `+0x338`；DataList 长度 `1..4096`；调用 `get_ParcelId()` 查找 `900010006`。缺失时构造 `Recruit_AdjustStoreXmlData`（Id=100、Type=2、ParcelId=900010006、偏移 `+0x30` 的 8-byte 原始 payload、Icon 与两项 LocalizeId），校验静态 `Recruit_AdjustStoreData.FromXml` 和 Root `ReplaceDataList`，追加后回读 ParcelId。Host 仅测规格和边界；尚无设备验证。 |
| `earned_unlocks` / `sync_unlocks` | Reducer、IL2CPP 提取和 Backend 同步适配已移植，未接入 touch | `earned_unlocks@0x2531ac` 读取 DataLoader `+0x4a0` / `+0x3c8` 的 Unlock / ScenarioPack DataList，按 Store quest-rank、ReqUnlockGroupId/Value 及 mode 1/2 合并，`mode=2 && TableId=200` 固定为 99。`sync_unlocks@0x250da4` 用紧凑 JSON 比较，变化时调用等价 Backend 事务；Host 覆盖 reducer 与账号 Exp/回滚。Android Managed 元数据、设备行为尚未验证。 |
| `local_mail_count` | 独立 runtime helper 已移植，未接入 bootstrap | `local_mail_count@0x243928` 遍历 Store `mails`，读取 `Receipt` 的 cJSON `valueint`，只计数 `>1`；随后通过 Account `+0x60` 调用 `SetMailCount(Int32, Boolean)`，第二个参数为 `count>0`。当前实现验证 `(System.Int32, System.Boolean) -> System.Void` ABI 后调用。Host 仅验证计数语义，未做 Android 设备调用验证。详见 `LOCAL_MAIL_COUNT_REVERSE_ENGINEERING.md`。 |
| `bootstrap` 与 13 个嵌套 restorers | 未完成 | 没有完整 Managed Account 初始化链。
| `local_account_event_login` | 未完成 | 活动事件路径未实现。 |
| `guide_restore_cache` / `initialize_guide_cache` | 未完成 | 有 JSON 侧数据模型，不代表 live Managed cache 已恢复。 |
| `office_wire_probe` | 候选代码存在，未接入完整顺序 | 需验证运行时触发语义。 |
| `installation_native_tables` | 候选代码存在，未接入完整顺序 | 需验证原版降级语义及设备对象图。 |

## `custom_resource_tables` pseudocode recovered

下面是当前已经移植的核心控制逻辑摘要；不是原函数逐指令的完整伪代码：

```text
custom_resource_tables(dm):
    if not repair_vignette_icon_fallbacks(dm):
        return false
    challenge_ok = challenge_gate_tables(dm)
    story_ok = permanent_story_pack_tables(dm)
    sigrid_ok = sigrid_mastery_tables(dm)
    return challenge_ok && story_ok && sigrid_ok
```

- **Vignette fallback**：查 DataManager root `+0x4b8`，以 `0x2714` 找到目标 row；为 ELF 恢复出的 32 条资源路径创建 Managed String，并通过 `gc_wbarrier_set_field` 写入 row `+0xd0`。源常量和 Host 测试覆盖 32 条路径。
- **Challenge Gate**：DataManager root `+0x2f0`，目标 Id `0x0f47b9` 和 `0x0f4ba1`。分别查找行，校验 Groups 首元素 `1000`、Values 首值允许 `(3,6)` 或 `(2,5)`、`ReqAccountLevel=20`，再将 Values 首值规范化为 `(2,5)`。原版对两个 ID 分别重扫列表。
- **Story Pack expiry**：root `+0x3a8`，查 GroupId 1001/1002，将 `+0xc8` 与 `+0xd8/+0xe0` 两个 DateTime 值设为 DateTime.Max。
- **Sigrid Mastery**：root `+0x400`，GroupId `0x0f4845`，SkillGroupId 100154104/100154105，匹配 LocalizeId `0x35e85afbe859839b`、Node 字段和 Enable/Disable 参数后，将两个目标行 `+0x30` 设置为 4。注意原函数的 Enable 参数比较基于变体索引 0/1，对应值 4/5；Disable 为 5/99999。
- 参考控制流在 Challenge 失败后仍继续尝试 Story expiry 和 Sigrid，再在末尾合并结果；当前实现和 Host regression test 保留了这个副作用顺序。

## `custom_item_tables` reverse findings

- Entry: `custom_item_tables@0x24f280`，size `0x15ac`。
- 从 DataManager root `+0x258` 与 `+0x270` 取得两个表/集合入口；读取的列表计数分别限制为 `<=0x4000`。
- 路径中使用 0x800-byte JSON scratch buffer、`GetData`、`ItemData`、`Item_RewardData`、`Item_RewardXmlData`、wire read/build 与 cJSON compare 等操作；后段还有 Gacha/Reward 转换校验。
- ELF/DWARF 静态结构 `custom_mail_item` 为 32 bytes：`id:u64@0`、`type:u32@8`、`name:pointer@16`、`desc:pointer@24`；共 22 条。
- 静态结构 `custom_mail_choice` 为 24 bytes：`item:u64@0`、`id:u64@8`、`type:u32@16`；共 715 条。
- `custom_item@0x269100` 的 136 槽 ParcelId resolver 覆盖 `900010001..900010136`，22 个槽有效，其余返回 null。静态资产 `assets/tables/embedded_11_custom_mail.json` 保留原中文字符串、22 descriptors、715 choices 与 resolver；`src/custom_mail_catalog.*` 提供只读 C++ 查询，Host test 验证全部 136 槽映射和全部 choice 数量。
- 当前源码已完成第一阶段 Managed DataSheet/list 生成：`ItemXmlData` / `Item_RewardXmlData` 单行对象经静态 `FromXml` 转换为 Data 行，校验返回类并补写恢复出的类型字段；预检两个 Root 的元素类型及 `ReplaceDataList` 数组参数后才开始修改；再将新行合并并验证 `DataDic`。另外对 21 个非 type-10 parcel 构造 `ItemUseSelectable.Request` Managed 对象并完成 schema JSON 往返。
- 上述检查不是原版底层 Wire serializer 的二进制等价实现；Gacha duplicate/conversion/visual/reward 检查、完整 JSON/cJSON compare 及失败时全部副作用仍未实现。大厅入口必须继续 fail-closed。

## Current device log versus current source

最新用户上传的 `debug.log` 设备 Build ID 为 `4b8fe8d415dd-216374b60bb84369`，时间 2026-11-24 01:03:40。它确认 step 1–5 运行成功，Character99 的 FromXml 两条路径均成功；但这个设备二进制在 `custom_resource_tables` 新重写接入前构建，日志在 step 6 开始前退出。因此不能把当前 step 6 的 Host 通过误报为设备通过。当前源代码 step 6 成功后会尝试 step 7 Managed Item/Item_Reward 重建；即使这些行重建成功，仍因 Gacha/Reward conversion parity 未完成而在后续大厅步骤前 fail-closed。

## Gate policy

未完成所有 mandatory helper、`bootstrap` 子步骤、Managed Account/cache 状态校验和 Hook 网络路径前，不得调用自研的 `EnterLobby()` / `PlayFirstDay()` dispatcher。登录入口的 Hook #3 在前置链未完成时，只能在 Hook #5 确认已安装的情况下 `SHADOWHOOK_CALL_PREV` 转发原游戏 touch；这是防止点击被吞的兼容回退，不代表离线流程完成。Hook #5 安装成功也不代表请求已到达；必须观察到真实 `local_request` 回调和完整业务响应。
