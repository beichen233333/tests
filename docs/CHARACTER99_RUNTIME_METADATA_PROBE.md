# Character99 运行时 Managed 元数据探针

## 目的

`character99_tables` 已恢复静态数据，但运行时还需确定 DataManager 中 factor/recipe 根对象及其闭合泛型 Dictionary/List 的准确类型，才能安全重建 Managed 数据和角色 stat cache。此探针只读取 IL2CPP 元数据，不创建或修改游戏对象。

## 输出范围

第一次 `touch()` 回调在 Controller、DataManager、Account 的运行时类型验证之后输出：

- 当前实例真实类型对应的 DataManager 类；
- 当前实例真实类型对应的 Account 类；
- `_AT.Core.DataSheet.Character_LevelFactorData`；
- `_AT.Core.DataSheet.Character_LevelFactorXmlData`；
- `_AT.Core.DataSheet.GrowthRecipeRoot`；
- `_AT.Core.DataSheet.GrowthRecipeData`；
- `_AT.Core.DataSheet.GrowthRecipeXmlData`。
- 实际 `DataManager._dataLoader`、`DataManager._dbLoader` 对象的运行时类型和字段/方法。
- 参考 `character99_tables` 使用的 `CustomDataLoader+0xb8` LevelFactor 路径及 `+0x1e0` GrowthRecipe 路径，包含 wrapper/root/data array/dictionary 的实际类名；数组同时记录长度。

每个类最多输出 192 个字段和 192 个方法，字段包括名称、偏移和 IL2CPP 类型；方法包括名称、参数数、返回类型和参数类型。每个元数据条目独立写入一条日志，避免 debug logger 的单条消息长度上限截断整份类描述。

## 日志标记

搜索 `character99 IL2CPP metadata snapshot begin` 和 `character99 IL2CPP metadata snapshot end`。所有目标类型都解析成功后会出现 `snapshot complete`。若有类型未找到，会记录 `class_lookup_failed`，并在下一次 `touch()` 时重试；探针不会因为一次类尚未加载就永久关闭。

## 安全约束

- 不读取或写入 Managed 实例字段值；只调用 IL2CPP 类/字段/方法元数据 API。
- 不分配 Managed 对象，不建立临时 GCHandle。
- 只在已附加的 Unity 主线程 Managed 回调中运行。
- 输出元数据本身不代表 `character99_tables` 已完成；必须继续核实字典键值类型、构造方式、列表替换方法、DataManager 写回字段和 stat cache 更新路径。
