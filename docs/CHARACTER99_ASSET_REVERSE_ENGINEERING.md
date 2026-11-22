# `character99_tables` 静态数据恢复笔记

## 来源与边界

参考 ELF 仅用于静态分析，不加入本项目链接或 Android 运行时依赖。函数 `character99_tables` 位于 `0x24c5c0`，DWARF 函数范围为 `0x1ed4` bytes。该函数不只是数据表复制，还创建/重建 Managed Dictionary/List，并刷新角色成长和 stat cache；本笔记只记录可从 ELF/DWARF/relocation 无歧义恢复的静态记录，不宣称已还原整个函数。

## 恢复的数据

产物：`assets/tables/embedded_10_character99_progression.json`

| ELF symbol | VA | Record count | Record size | 内容 |
|---|---:|---:|---:|---|
| `character99_factors` | `0x295d50` | 160 | 32 | 组 1001 的等级因子，等级 60–99 |
| `character99_recipes` | `0x297150` | 117 | 144 | 角色 60–99 扩展配方，组 1001/1002/1003 |
| `character99_original_recipes` | `0x29b320` | 619 | 144 | 原始配方/奖励记录 |

Factor 结构（DWARF `character99_factor`）：`group:uint64@0`、`level:uint32@8`、`next:uint32@12`、`rate:int64@16`、`stat:const char*@24`。

Recipe 结构（DWARF `character99_recipe`）：`group:uint64@0`、`current:uint32@8`、`next:uint32@12`、`type:const char*@16`、`pt[3]@24`、`pid[3]@48`、`amount[3]@72`、`personal[3]@96`、`personal_amount[3]@120`。配方字段中的字符串保留为字符串，避免将可能超过有符号 64-bit 的 ID 转成 JSON 数值。

记录中的指针字段由 `.rela.dyn` 的 `R_AARCH64_RELATIVE` addend 还原到只读字符串表；JSON 仅保留字符串内容，不保留参考 ELF 的虚拟地址/指针。

## 已验证约束

- factors：160 条，group=1001，level 60–99；4 个 stat ID，每个 40 条。
- extended recipes：117 条，group 1001/1002/1003 各 39 条；current 60–98，next=current+1；type ID 为 `8941476925601368782`。
- original recipes：619 条；group 1001、1002、1003、1004 各 59 条，其余为多个原始小组。
- `tests/character99_asset_test.cpp` 从 `read_embedded_asset()` 读取压缩内嵌资源并校验上述数量及关系。

## 尚未恢复的运行逻辑

以下关键内容仍须根据 `character99_tables` 的调用和结构访问逐一移植：

1. DataManager 中 factor/recipe 原始 roots 的准确关联和缺失分支；
2. Managed 泛型 `Dictionary<,>` / `List<>` 的实际 key/value 类型、构造方式和写入方法；
3. `factor_custom`、`recipe_custom`、`level_custom` 与原始记录的合并/替换规则；
4. factor/max/recipe/stat dictionary 写回 DataManager 的真实字段；
5. 角色等级 60–99 stat cache 的构建、刷新和回读验证；
6. 函数尾部对 mission/puzzle roundtrip 及 batch original object 的验证条件。

在这些项目完成并通过 mock + 目标版本设备测试之前，不得将 `character99_tables` 标为已实现，不得允许 `touch()` 跳过此步骤继续 `EnterLobby()`。
