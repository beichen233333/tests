# 单机版还原阶段进度

按用户确认的顺序执行，任何阶段不得用空实现或“返回 true”的占位代码跳过前置依赖。

| 阶段 | 目标 | 状态 | 当前证据 |
|---|---|---|---|
| 1 | 恢复 `touch()` 外层表/解锁初始化链 | 进行中 | `artifact_capacity_tables` 已移植并接入第 1 步，Host IL2CPP mock 测试通过；`character99_tables` 的 160 条 factors、619 条 original recipes、117 条 extended recipes 静态数据已从 DWARF/ELF relocation 恢复并作为 JSON 资产嵌入，数据结构单测通过；新增只读 IL2CPP 元数据探针会枚举运行时 DataManager/Account 及五个 progression 类型的字段偏移和方法签名，为 Managed 字典/缓存重建提供实机证据，但重建尚未实现。Seed、custom resource/item、signed exchange、unlock 同步尚未实现。已有 Raid/Battle 候选实现待按顺序接入。 |
| 2 | 完整 `bootstrap(account)` 及其 13 个嵌套恢复步骤 | 未开始（等待阶段 1） | 当前只有 Store/Backend 数据模型，尚无完整 Managed Account 状态移植。 |
| 3 | 登录事件、Guide 两阶段恢复、Installation 初始化 | 未开始（等待阶段 2） | 有部分 JSON 状态/Installation 候选代码，不能视为 Managed 缓存已恢复。 |
| 4 | 完整 Hook #5 请求/响应与主动 Managed 事件 | 部分接入，尚未完成 | Android 标准配置已将 Hook #5 放在 Hook #3 之前，并把未知请求转换为本地 ErrorPacket；当前 93 类请求路由仍未覆盖全部业务及主动 Managed 事件。 |
| 5 | 新档、旧档、重复启动、奖励、养成、战斗回归 | 未开始（等待阶段 1–4） | 当前只有 Host helper 单元测试；尚无 Android NDK 构建和设备回归。 |

## 阶段 1 当前实现

- `artifact_capacity_tables@0x24c2ec`：读取 `DataManager+0x10 -> root+0x140 -> wrapper+0x18 -> Common+0x10`，定位 `_AT.Shared.SecureInt.set_Value(Int32)`，对 `Common+0xc8` 设置 `9999`，再运行 `get_Inventory_Artifact_Max_Slots()` 并 unbox `Int32` 验证结果为 `9999`。空引用、反射失败、Managed 异常、null getter 和回读不一致都会失败关闭。
- `permanent_raid_tables@0x24e494`：已有候选实现，count 边界按反汇编为 1..128，并对每行 `+0x38` 写入两份 `0x2bca2875f4373fff`；Host fixture 已验证边界。
- `battle_multiple_tables`：已有候选实现，但严格排在 seed helper 之后，尚未提前执行。

## 阶段 1 后续顺序

1. 将已提取的 `character99` 静态记录映射到实际 DataManager 表、Managed Dictionary/List 和角色 stat cache；确认 key/value 类型及写回点后，实现运行时构建与回读校验。静态数据已提取，但函数长度 0x1ed4，运行逻辑仍未还原。
2. 还原 `seed_ticket_defaults` 和 `seed_table_defaults`，按原始 DataManager `+0x328` 路径与 Store 种子快照行为逐项验证。
3. 按序接入 `permanent_raid_tables`、`battle_multiple_tables`。
4. 还原 `custom_resource_tables`、`custom_item_tables`、`signed_exchange_tables`。
5. 还原 `earned_unlocks` 和 `sync_unlocks`，验证 Store account-unlock 快照一致性。

每个 helper 完成前，必须保留参考函数地址、关键偏移、返回条件、失败分支、对应单元测试和目标设备验证状态。

## 本轮新进展（静态数据阶段）

- 新增 `assets/tables/embedded_10_character99_progression.json`，由参考 ELF 中的 `character99_factors@0x295d50`、`character99_original_recipes@0x29b320`、`character99_recipes@0x297150` 静态数组恢复。DWARF 记录尺寸：factor 32 bytes、recipe 144 bytes；AArch64 `.rela.dyn` 的 `R_AARCH64_RELATIVE` addend 已解析为原始字符串 ID。
- 新增 `character99_asset_test`：验证 160/619/117 记录数量、factor 等级 60..99、4 类 stat id 每类 40 条、original recipe 主要 group 分布、extended recipe group 1001..1003 每组 39 条且等级连续。
- 该资产还未被写入游戏 Managed DataManager 的 Dictionary/List，也未重建实际角色 stat cache；因此 `character99_tables` 仍未完成，touch 仍 fail-closed。
- Host Release 测试目标显式加 `-UNDEBUG`，确保 `assert` 检查在 Release 配置下真正执行，而不是被 `NDEBUG` 消除。

## 本轮新增：character99 运行时元数据探针

- 新增 `docs/CHARACTER99_RUNTIME_METADATA_PROBE.md`，记录探针目标、日志标记和只读安全边界。
- `touch_enter_lobby` 现在同时枚举实际运行时 DataManager、Account 与五个 progression 类型；每类最多 192 字段/方法。
- 如果任一类型首次未解析成功，不会永久置位完成标记，会在下一次 touch 回调重试。
- 探针只读取元数据，不创建 Managed 对象、不写入字段、不创建/释放 GCHandle。
- 该探针仍是取证步骤，不得标记为 `character99_tables` 已实现。


## 本轮：设备反馈确认 Hook #3 消费了登录输入

- 新上传的 `debug.log`（Build ID `f6b3be8929b7-a2f7eea92f87cbf9`）显示 Hook #3 `touch` 回调进入 9 次，DataManager/Account 有效，`artifact_capacity_tables` 第 1 步成功；每次回调都因 `character99_tables` 等未完成步骤在 `touch_enter_lobby()` 返回失败。该日志的 `local_request_direct_hook=OFF`，是未采用当前 Hook #5 前置安装策略的旧构建。
- 该设备旧构建的 Hook #3 是直接替换（direct replacement），且未安装 Hook #5，`touch_replacement()` 不调用原始游戏方法，因此旧构建会消费登录输入。当前源码已加入条件回退：Hook #5 安装成功时调用 `SHADOWHOOK_CALL_PREV` 转发原 touch；Hook #5 未成功时不安装 Hook #3。
- 增加只读运行时对象图探针：基于参考 `character99_tables` 中确认的 `CustomDataLoader+0xb8` 与 `+0x1e0` 路径，输出真实 DataLoader/DBLoader、LevelFactor slot/root/data、GrowthRecipe root/data/dictionary 的运行时类型、字段方法元数据和数组长度，为实现 Managed root/list/dictionary 写回提供依据。
- `artifact_capacity_tables` 成功后仅执行一次，避免每个 UI touch 都重复写同一 SecureInt；缺失初始化步骤仍 fail-closed，不调用 `EnterLobby` / `PlayFirstDay`。
- 这次改动不会让登录页面立即进入大厅；必须先取得下一版对象图元数据并移植 `character99_tables`，随后依照参考序列完成 seed、Raid、Battle、custom resource/item、signed exchange、unlock sync，再进入 Account Bootstrap。

## 本轮：登录输入回退与构建安全约束

- 最新设备日志 Build ID `f6b3be8929b7-a2f7eea92f87cbf9` 的构建标志是 `local_request_direct_hook=OFF`。该二进制中的 Hook #3 direct replacement 在恢复链未完成时直接返回，因而吞掉登录点击；这与当前源码已修改但设备仍运行旧构建相符。
- 当前 Android 默认配置启用 Hook #5，运行时先安装 Hook #5，再安装 Hook #3。若自定义 touch 前置链未完成，Hook #3 仅在 Hook #5 已成功安装的情况下调用 ShadowHook `SHADOWHOOK_CALL_PREV` 转发原游戏 touch。
- 如果 Hook #5 被关闭而 Hook #3 仍开启，Android CMake 配置和构建脚本现在会立即失败，防止重新产出吞点击的组合。
- Hook #5 将请求送入本地 Backend；未映射请求返回本地 ErrorPacket，不会调用原网络实现。当前路由覆盖仍部分，不等价于完整离线。
- 最新 debug.log 中 DataManager 的 `_dataLoader+0x1e0` 指向 `GrowthRecipeLoader`，其 `errorMessage` 在 `+0x10`，`Root` backing field 在 `+0x18`；旧运行时探针将 Loader 误标为 Root。当前源码对象图已经按此偏移纠正，并新增通过 `FromXml` 参数类型解析 XML Root Managed class 的反射探针。
