# AstraeOfflineBackend

这是一个独立的 Android ARM64 / Unity IL2CPP 离线复刻工程。原版 `libastraeoffline.so` 只作为逆向行为基准；本工程不会链接、加载或复制原版 SO。

## ShadowHook 地址 Hook 初始化修复（单 SO 部署）

设备日志中 `ShadowHook initialization failed: code=12 (Init linker mod failed)` 对应 ShadowHook v2.0.1 的 `SHADOWHOOK_ERRNO_INIT_LINKER`。上游 `shadowhook_init()` 在初始化指令重定位、Island 和 Switch 模块后，会调用 `sh_linker_init()`；该函数需要解析 Android 私有 Linker 符号，并 Hook `soinfo::call_constructors()` / `call_destructors()`，再通过探测 ELF 扫描 `soinfo` 内部结构。此路径会受 Android 系统版本、Linker 实现和私有结构布局影响。你当前的 Hook #3/#5 并不按库名等待新 ELF，而是在 `libil2cpp.so` 已加载后，先解析绝对函数地址和 RVA，再执行 `shadowhook_hook_func_addr_2()`，因此不需要这套 Linker 装载监控。

本版本改为 **direct-address-only** ShadowHook 构建：`scripts/patch_shadowhook.py` 对固定版本 v2.0.1 做两处可重复应用的源码补丁。第一处在 `shadowhook.c` 中跳过 `sh_linker_init()`，避免进入导致 code 12 的私有 `soinfo` Hook/探测流程；第二处在 `sh_task.c` 中，针对直接目标地址调用 `sh_linker_get_addr_info_by_addr()`，通过 xDL 获取目标所属 ELF 的 load bias、Program Header 和必要的符号信息，供 ShadowHook 原有 Island 分配、指令重定位、跳板和 unhook 路径使用。任务初始化也不再注册依赖 Linker 回调的延迟任务监听。ShadowHook 的核心 AArch64 inline Hook 实现仍由上游静态源码构建并链接进唯一的 `libastraeoffline.so`。

该构建配置只支持本工程当前使用的**已加载 ELF 绝对地址 Hook**。ShadowHook 的按库名/符号名延迟 Hook、等待后续 `dlopen()` 的自动回调，以及依赖 Linker 的加载/卸载通知不会工作；新增此类功能前必须另行实现并验证加载监控机制。当前游戏目标 `libil2cpp.so` 在 Hook 运行前已经加载，且本工程通过 `dladdr()`、`dl_iterate_phdr()`、RVA 和目标指令指纹完成地址验证，因此当前 Hook 路径不依赖 Linker 私有结构。整个构建不再生成、嵌入或加载 `libshadowhook_nothing.so`，游戏目录只部署 `lib/arm64-v8a/libastraeoffline.so`。

上一版存在两个高风险面：自研 AArch64 patcher 只重定位有限指令并手工拼接绝对跳转；运行时又在库加载后自动启动并较早安装 Direct Replacement。当前版本已移除该 patcher，Android inline hook 改用 ByteDance 官方 ShadowHook v2.0.1。Hook 不会在 `dlopen()` / `System.loadLibrary()` 构造阶段安装。标准 Android 构建脚本默认启动延迟 Runtime worker，并启用大厅入口 Hook #3；只有等到 IL2CPP domain、`_AT.UI.UILoginPageController` 和 `EnterLobby/0` 方法可用，再额外等待 8 秒后才初始化 ShadowHook 和安装。`_AT.Network.NetworkHandler` 只作为诊断状态，不再阻塞 #3 大厅路径。回调会检查 `this` 对象是否为 `UILoginPageController` 或其子类；UI 回调中不再写磁盘；所有 Direct Replacement 回调均捕获 C++ 异常，避免异常越过 hook ABI。

ShadowHook 官方手册也明确要求先初始化再调用 Hook API，并提示初始化早于 App native crash handler 注册可能增加可避免的崩溃概率。因此这里采用延迟初始化；8 秒只是保守缓冲，不是对第三方 crash SDK 注册完成的证明。

## 当前运行状态（重要）

当前版本**尚未实现完整大厅启动链，不是可正常游玩的完整单机版**。最新设备日志已证明，Hook #3 以 direct-replacement 模式消费登录输入：每次 `touch` 都进入自研回调，但由于 `character99_tables` 及后续 helper 未完成而 fail-closed 返回；该模式不会调用原游戏 trampoline，因此登录动画保持不变是当前诊断构建的预期行为，不是点击控件或 ShadowHook 故障。新版本额外枚举 DataManager 的实际 CustomDataLoader/DBLoader 和 Character_LevelFactor/GrowthRecipe 运行时对象图，为完成步骤 2 提供字段/泛型依据。Hook #3 在 Unity 主线程验证 `controller+0x10` 的 DataManager 与 `controller+0xb0` 的 Account 后，已执行参考顺序中的第 1 步 `artifact_capacity_tables`（Common+0xc8 的 SecureInt 容量设为 9999 并通过 Getter 回读验证）。回调会借用正在执行的 Managed 调用栈对象引用，不在同步预检中创建或释放临时 GCHandle。第 2 步 `character99_tables` 和后续强制前置链尚未全部移植，因此回调仍明确 fail-closed，不调用 `EnterLobby()` / `PlayFirstDay()`。这是一种防止将不完整状态送入大厅的保护状态；登录/加载界面继续停留属于当前预期结果，不能把它当成单机运行完成。

参考 ELF 中 `touch()` 的恢复顺序与依赖已记录在 `docs/REFERENCE_TOUCH_SEQUENCE.md`。其中 `bootstrap(account)` 不是一个简单赋值操作，而是串联账户、余额、基金会、邮件、卡池、农场、Raid、日期、设施、Guide、每日任务、剧情包任务和谜题的多级恢复。只有这些步骤逐项移植并且每步的返回状态、Managed 对象和持久化结果均验证成功，才允许恢复大厅分支。Hook #5 本地请求替换默认关闭且仍属部分业务实现；在协议请求/响应闭环经过实际游戏路径验证前，不应启用它。

```bash
export ANDROID_NDK_HOME=/path/to/android-ndk
bash scripts/build_android_arm64.sh
```

输出：`artifacts/android-arm64-v8a/libastraeoffline.so`。Hook 启用时使用 direct-address-only ShadowHook，不生成或加载伴随 ELF，游戏中只部署这一份 `.so`。

## 分阶段启用

标准 Android 脚本默认启用 Runtime 和大厅 Hook #3，#5 仍保持关闭。只有需要隔离定位加载/初始化故障时才使用前两种诊断配置：

**1. 仅加载 SO（显式诊断模式，非大厅模式）**

```bash
ASTRAE_ENABLE_RUNTIME_AUTOSTART=OFF ASTRAE_ENABLE_TOUCH_DIRECT_HOOK=OFF bash scripts/build_android_arm64.sh
```

**2. 启动 Backend，但不改写游戏代码**

```bash
ASTRAE_ENABLE_RUNTIME_AUTOSTART=ON \
ASTRAE_ENABLE_TOUCH_DIRECT_HOOK=OFF \
bash scripts/build_android_arm64.sh
```

**3. 启用 Hook #3 预检（不会进入大厅；首次启用时自动获取官方 ShadowHook v2.0.1，并启用 direct-address-only 补丁）**

```bash
ASTRAE_ENABLE_RUNTIME_AUTOSTART=ON \
ASTRAE_ENABLE_TOUCH_DIRECT_HOOK=ON \
ASTRAE_ENABLE_LOCAL_REQUEST_DIRECT_HOOK=OFF \
bash scripts/build_android_arm64.sh
```

**4. Hook #5 请求替换（目前仍是部分业务实现，默认关闭）**

```bash
ASTRAE_ENABLE_RUNTIME_AUTOSTART=ON \
ASTRAE_ENABLE_TOUCH_DIRECT_HOOK=ON \
ASTRAE_ENABLE_LOCAL_REQUEST_DIRECT_HOOK=ON \
bash scripts/build_android_arm64.sh
```

当前预检版本正常情况下应看到 `touch managed fields recovered` 和 `lobby dispatch blocked after object validation`，不应再看到 `invoking UILoginPageController.EnterLobby`。如果设备仍闪退，必须同时抓取 Android crash/tombstone 栈；如果不闪退但停留在加载画面，这是当前 fail-closed 的预期限制，不代表已经完成大厅还原。Hook #5 不应在业务处理尚未完整时打开。

## ShadowHook 来源

- 上游：<https://github.com/bytedance/android-inline-hook>
- 固定版本：`v2.0.1`
- 获取与补丁脚本：`scripts/fetch_shadowhook.sh`、`scripts/patch_shadowhook.py`
- 集成方式：将上游 ARM64 C/汇编源编译为静态目标并并入最终 `libastraeoffline.so`；补丁跳过 `sh_linker_init()`，并让直接目标地址分支通过 xDL 获取目标 ELF 元数据。
- 当前工程只使用已经加载的 `libil2cpp.so` 绝对地址 Hook；不要调用依赖 Linker 回调的按库名延迟 Hook API。
- 最终游戏目录只需要 `lib/arm64-v8a/libastraeoffline.so`。不生成、嵌入或部署 `libshadowhook_nothing.so`，最终 SO 不应存在该伴随库的 `DT_NEEDED`。
- 链接使用 `-z defs` 拒绝未解析引用。构建脚本验证最终 ELF 架构、SONAME、依赖和原始 ABI 导出表，并检查不存在 `libshadowhook_nothing.so` 的动态依赖。
- 首次启用 Hook 需要联网并安装 Git；也可预先设置 `ASTRAE_SHADOWHOOK_SOURCE_DIR` 指向已检出的 v2.0.1 源码目录，构建脚本会自动应用 direct-address-only 补丁。

## 目录结构

```text
android/          Android/JNI 桥接
assets/           协议 Schema、Hook Manifest 与内嵌数据
compat/           原版 C ABI、Store/cJSON 兼容层
config/           外部配置示例
docs/             架构、构建和逆向移植记录
include/          对外 C API
scripts/          构建、资源生成、ShadowHook 获取脚本
src/              Backend、IL2CPP、Managed Wire、大厅与 Hook 运行时
```

## 环境要求

- CMake 3.22+
- Android NDK r27d 或兼容版本
- Android ABI：`arm64-v8a`
- 启用 Hook 时需要 Git 和 GitHub 访问（或本地 ShadowHook v2.0.1 源码目录）

## Host 开发构建

```bash
bash scripts/build_host.sh
```

Host 产物只用于本机开发，不能替代 Android ARM64 SO。

## 资源更新

修改 `assets/` 下 JSON 后执行：

```bash
python3 scripts/generate_embedded_assets.py
```

不要直接编辑生成的 `src/embedded_assets.cpp`。

## 当前功能边界

Hook #3 已恢复参考 ELF 中 `touch(controller, method)` 使用的关键对象偏移：`DataManager=controller+0x10`、`Account=controller+0xb0`，并验证二者的 Managed 类型。预检路径只借用活动 Managed 调用的根引用，不创建/释放临时 GCHandle。上一版曾合成 first-day 完成标记；本版已删除这类强制跳过行为。Hook #3 当前按参考顺序执行第一步 `artifact_capacity_tables`：设置 `Common+0xc8` 的 `_AT.Shared.SecureInt` 值为 9999，并用 `get_Inventory_Artifact_Max_Slots()` 回读验证。Host mock 测试覆盖成功、空对象及回读不一致；设备验证仍待完成。`character99_tables` 的静态因素/配方记录已恢复并加入数据校验（160 factors、619 original recipes、117 extended recipes），但运行时 Managed Dictionary/List 构造及角色 stat cache 写回仍未完成，因此仍 fail-closed，不调用 `EnterLobby()` / `PlayFirstDay()`。`permanent_raid_tables`、`battle_multiple_tables`、`office_wire_probe`、`installation_native_tables` 已有候选，但必须按参考顺序集成；其余缺失项及 `bootstrap(account)` 的嵌套恢复函数详见 `docs/REFERENCE_TOUCH_SEQUENCE.md`。Hook #5 仍有专用请求分支、主动 Managed 消息等缺失，不应启用为“完整离线”路径。Hook 指纹只匹配对应游戏二进制版本；不匹配时会拒绝安装。Android ARM64 编译、目标设备加载与进入大厅必须在目标环境实测，主机编译不能证明真机稳定。

## SO 体积优化

Android Release 构建对内嵌 JSON 使用 zlib level 9 压缩：当前 14 份 JSON 从 2,804,050 bytes 压缩到约 104,092 bytes（资源原始数据减少约 96.3%，实际最终 SO 体积仍以 NDK 报告为准）。发布脚本先从未剥离 ELF 单独保存 `libastraeoffline.so.debug`，再对设备运行库执行 `llvm-strip --strip-all`；动态导出表会在剥离前后分别对照 ABI manifest 校验。编译启用 `-Oz`、函数/数据分段、`--gc-sections`、安全 ICF 和可用时的 LTO。`SIZE_REPORT.txt` 会记录原始 ELF、部署 ELF 和独立调试符号文件的实际字节数。游戏里只部署 `libastraeoffline.so`；`.debug` 文件仅用于符号化 tombstone，不是运行时依赖。`libz.so` 是 Android 系统库，不是随包增加的第二个应用 SO。

## 维护规则

- 不得把原版 SO 作为链接输入、运行时 `dlopen` 目标或 fallback。
- 不重新引入自研机器码 patcher；Android inline Hook 统一走 ShadowHook。
- Direct Replacement 必须确认函数 ABI、实例类型、线程上下文和完整替换语义。
- 所有 Hook 回调不得让 C++ 异常越过 ABI 边界。
- Hook 默认 opt-in；新增 Hook 必须加入指纹、ABI 证据和分阶段运行开关。
- Managed 对象跨调用保存时必须使用经过实测的 GCHandle 生命周期；禁止在 Unity 原生/Managed 互调栈上随意释放临时句柄；Unity UI 操作应发生在原始 UI 回调线程。
- 存档更新先构造临时结果，再提交，避免半初始化状态。


## Android runtime diagnostics

On Android, the ELF constructor initializes the native diagnostic logger. It writes an append-only, rotating log to:

`/storage/emulated/0/Android/data/com.nc.astraeoratio.singleplayer/files/debug.log`

The file is app-specific external storage and is opened with owner-only permissions. Logcat receives the same messages under tag `AstraeOffline`. If Android refuses the file path, `debug.log` cannot be created; the path and errno are still reported to logcat. The logger never aborts the game when file output is unavailable.

The standard Android build script now defaults to Runtime Autostart and Hook #3 preflight enabled. The callback currently blocks lobby dispatch until the recovered touch initialization chain is complete; the incomplete request Hook #5 stays disabled. The output folder contains only `libastraeoffline.so`; ShadowHook is statically linked in direct-address-only mode, with its private linker monitor disabled and no companion probe ELF. Override explicitly for diagnosis: set `ASTRAE_ENABLE_RUNTIME_AUTOSTART=OFF` or `ASTRAE_ENABLE_TOUCH_DIRECT_HOOK=OFF` to create a passive SO. A passive SO does not enter the offline lobby.

The log records constructor/build flags, IL2CPP load/domain readiness, resolved symbol counts, ManagedWire schema load, Backend and Store initialization, login controller and `EnterLobby` readiness, ShadowHook version, target RVA/fingerprint, install result, touch callback count, managed request/response outcomes and a 15-second Hook watchdog.

## Character99 运行时元数据取证（当前阶段）

第 1 阶段正在恢复 `character99_tables`。静态 factors/recipes 数据已由参考 ELF 的 DWARF 与重定位记录恢复并压缩嵌入，但不能仅凭静态记录猜测 Managed Dictionary/List 的实际泛型类型与 DataManager 写回字段。当前 `touch()` 预检增加了只读元数据探针：它会导出当前运行时 DataManager、Account 以及 `Character_LevelFactorData`、`Character_LevelFactorXmlData`、`GrowthRecipeRoot`、`GrowthRecipeData`、`GrowthRecipeXmlData` 的字段偏移、字段类型、方法参数和返回类型。每个条目单独写入 `debug.log`，探针不创建 Managed 对象、不修改字段，也不创建/释放临时 GCHandle。首次运行后请从 `debug.log` 中提取 `character99 IL2CPP metadata snapshot` 相关行；相关说明见 `docs/CHARACTER99_RUNTIME_METADATA_PROBE.md`。在 Managed 字典/列表、根节点替换和角色 stat cache 全部实现并回读验证前，`touch()` 继续 fail-closed，不会调用大厅入口。

当前 Host Release 自动化测试共 4 项：永久 Raid 边界、Artifact 容量反射写回、Character99 静态资源结构、IL2CPP 类元数据枚举。它们均通过，但不能替代目标设备上的 Android ARM64 构建及游戏回归。阶段 1–5 的准确状态见 `docs/PHASE_PROGRESS.md`。
