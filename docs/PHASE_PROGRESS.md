# 单机版还原阶段进度

本文件区分三种证据：**ELF/反汇编证据**表示已恢复原版的静态控制流或数据；**Host 测试**表示重写逻辑在 mock 对象图中通过；**设备证据**表示用户提供的 Android `debug.log` 实际执行成功。三者不能互相替代。不得使用空实现、直接返回成功或跳过 mandatory helper 来制造“已完成”状态。

| 阶段 | 目标 | 状态 | 当前证据 |
|---|---|---|---|
| 1 | `touch()` 表初始化、解锁与大厅前置链 | 进行中 | 当前源码已接入 step 1–7 的部分逻辑；`character99_tables`、Permanent Raid、Battle Multiple 在用户设备日志中成功；`custom_resource_tables` 与 `custom_item_tables` 新重写尚无设备验证。step 7 已能按恢复的数据构造 Managed Item/Item_Reward rows，但 Gacha/Reward 与原版二进制 Wire 路径仍缺，故大厅入口继续 fail-closed。 |
| 2 | 完整 `bootstrap(account)` 及 13 个嵌套恢复步骤 | 未完成 | `local_mail_count` 的独立 reducer/runtime helper 已恢复，但尚未进入 bootstrap 调用链；余额、基金会、Pack、卡池、农场、Raid、日期、设施、Guide、每日任务、剧情包任务、谜题的 Managed 恢复链尚未完整实现。 |
| 3 | 登录事件、Guide 两阶段恢复、Installation 初始化 | 未完成 | JSON/Store 数据结构或候选代码不代表 live Managed cache 已恢复。 |
| 4 | Hook #5 完整请求/响应与主动 Managed 事件 | 部分接入 | Hook #5 安装成功，但最新设备日志没有 `local_request entered`，还未证明目标请求确实经过本地路由；协议覆盖也不完整。 |
| 5 | 新档、旧档、重复启动、奖励、养成、战斗真机回归 | 未开始 | 当前只有 Host 自动化测试，没有本轮源码对应的新 Android NDK 构建/设备回归。 |

## 阶段 1 当前实现

- `artifact_capacity_tables@0x24c2ec`：读取 Common 路径，对 `Common+0xc8` 的 `_AT.Shared.SecureInt` 设置 9999，并调用 Getter 回读。Host mock 验证字段路径、setter/getter、缺失对象与错误回读；设备日志已观察到 `requested=9999 actual=9999`。
- `character99_tables@0x24c5c0`：从 ELF 恢复 factors/recipes 静态数据，当前源码经 IL2CPP `FromXml` 构造 LevelFactor 与 GrowthRecipe Root，合并 160 条 factor 并创建 736 条 GrowthRecipe。设备 Build ID `4b8fe8d415dd-216374b60bb84369` 于 2026-11-24 01:03:40 记录 `factor_extension=160`、LevelFactor 总量 880、GrowthRecipe 736，属于已观察到的设备成功证据。
- `permanent_raid_tables@0x24e494`：Host 验证 1..128 行数边界与两份 64 位常量写入；设备日志报告 step 3 完成。
- `seed_table_defaults@0x24e50c`：尚未移植。参考 `touch()` 对 helper 返回 false 的分支是非致命的，会继续到 Battle；当前源码没有调用该 helper，只记录明确的 skipped 警告。因此当前控制流继续是参考行为，但 seed helper 的潜在副作用并未复刻。
- `battle_multiple_tables@0x24e914`：更新 Common 两个 inline SecureInt 为 99 并回读验证；设备日志报告 step 5 完成。
- `custom_resource_tables@0x24ebf4`：当前源码已按反汇编拆分并接入 4 个子步骤：32 条 Vignette 资源路径、Challenge Gate、永久剧情包过期时间、Sigrid Mastery。Host mock 测试覆盖对象字段写入、GC write barrier、Challenge 失败时仍继续执行 Story/Sigrid 的原始控制流；**尚无本轮源码的 Android 实机验证**。设备日志 Build ID `4b8fe8d415dd-216374b60bb84369` 来自更早构建，日志停在该步骤之前，不能用它证明新实现已在设备执行。
- `custom_item_tables@0x24f280`：部分重写并接入 step 7。已恢复 22 个 descriptor、715 个 choice、136 槽 ParcelId resolver；实现运行时 ABI preflight、Managed Item/Item_Reward XML→Data 行构造、DataList 合并/ReplaceDataList 与 21 组 `ItemUseSelectable.Request` Managed schema round-trip。该 round-trip 不是原版底层二进制 Wire parity；Gacha/Reward conversion、完整 JSON/cJSON compare 与失败副作用仍未复刻。Host 测试不执行 Android IL2CPP 写入，尚无设备验证。
- `signed_exchange_tables@0x25082c`：已移植独立 IL2CPP runtime helper。恢复 DataLoader 槽 `+0x338`、DataList `1..4096` 门槛、按 `get_ParcelId()` 幂等查重、`Recruit_AdjustStoreXmlData` 固定字段/8 字节原始负载、静态 `FromXml` 和 `Root.ReplaceDataList` ABI 检查及追加后回读。Host 静态规格与边界测试通过；未接入 touch（step 7 的 Gacha/Wire 必要路径仍未完成），无 Android 实机证据。
- `earned_unlocks@0x2531ac` / `sync_unlocks@0x250da4`：已恢复 Reducer、IL2CPP DataList 提取、PackState quest-rank 适配、存档比较及 Backend 事务提交。Host 覆盖 bitfield 模式、异常数组停止条件、账号 Exp 升级及 TableId=200 非法值回滚；运行时 DataManager / IL2CPP 结构尚无设备验证，且未接入 touch（需先通过完整 step 7）。
- `local_mail_count@0x243928`：已恢复 Store `mails[*].Receipt` 的 cJSON `valueint > 1` 计数语义，并新增 `Account+0x60` 的 `SetMailCount(Int32, Boolean)` IL2CPP 方法反射/签名校验与调用。Host 覆盖缺失数组、非数字字段、1.9 截断、2.9 截断等边界；当前 Backend mails 状态与原 Store mails 字段语义尚需设备数据对照，该 helper 尚未接入 `bootstrap`。

## 后续强制顺序

1. 完成 `custom_item_tables` 的 Gacha duplicate/conversion/visual/reward 分支、原版 Wire 序列化与 JSON/cJSON 比较语义；不能把当前 Managed schema round-trip 当成完整 Wire parity。
2. 在完整 step 7 通过后，将已独立实现的 `signed_exchange_tables` 与 `sync_unlocks` 依原序接入 touch，并用当前 Android IL2CPP 元数据校验字段和 DataList 后置状态。
3. 推进 `bootstrap(Account)` 的 13 个嵌套 Managed restorers；`local_mail_count` 已单独恢复但尚未入序，当前优先研究 `restore_balances` / `restore_foundation` 和首日前置奖励转换。
4. 逐项移植 `bootstrap(account)` 的 13 个子恢复函数，然后实现 `local_account_event_login`、两阶段 Guide cache、`office_wire_probe` 和 `installation_native_tables`。
5. 完成 Hook #5 请求、响应与主动 Managed event 的覆盖，再执行 Android ARM64 与设备回归。

## 自动化验证

当前 Host Release CTest 为 15 项，全部通过：Permanent Raid、Artifact 容量、Character99 静态资源、Character99 对象图、IL2CPP 类元数据、Touch fallback、旧存档迁移、Character99 Invoke 参数编组、Backend 旧存档集成、Custom Resource 修复、Custom Mail 静态目录、Signed Exchange 规格/边界、earned_unlocks reducer、Gacha Visual payload、local_mail_count predicate。全部通过只证明对应 Host 测试切片，不代表 IL2CPP runtime helper 已在 Android 设备执行，也不代表全部原库功能已复刻。

```bash
cmake -S . -B build-host -DCMAKE_BUILD_TYPE=Release -DBUILD_TESTING=ON
cmake --build build-host -j2
ctest --test-dir build-host --output-on-failure
```
