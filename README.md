# AstraeOfflineBackend

这是一个独立的 Android ARM64 / Unity IL2CPP 离线复刻工程。原版 `libastraeoffline.so` 只作为逆向行为基准；本工程不会链接、加载或复制原版 SO。

## ShadowHook 地址 Hook 初始化修复（单 SO 部署）

设备日志中 `ShadowHook initialization failed: code=12 (Init linker mod failed)` 对应 ShadowHook v2.0.1 的 `SHADOWHOOK_ERRNO_INIT_LINKER`。上游 `shadowhook_init()` 在初始化指令重定位、Island 和 Switch 模块后，会调用 `sh_linker_init()`；该函数需要解析 Android 私有 Linker 符号，并 Hook `soinfo::call_constructors()` / `call_destructors()`，再通过探测 ELF 扫描 `soinfo` 内部结构。此路径会受 Android 系统版本、Linker 实现和私有结构布局影响。你当前的 Hook #3/#5 并不按库名等待新 ELF，而是在 `libil2cpp.so` 已加载后，先解析绝对函数地址和 RVA，再执行 `shadowhook_hook_func_addr_2()`，因此不需要这套 Linker 装载监控。

本版本改为 **direct-address-only** ShadowHook 构建：`scripts/patch_shadowhook.py` 对固定版本 v2.0.1 做两处可重复应用的源码补丁。第一处在 `shadowhook.c` 中跳过 `sh_linker_init()`，避免进入导致 code 12 的私有 `soinfo` Hook/探测流程；第二处在 `sh_task.c` 中，针对直接目标地址调用 `sh_linker_get_addr_info_by_addr()`，通过 xDL 获取目标所属 ELF 的 load bias、Program Header 和必要的符号信息，供 ShadowHook 原有 Island 分配、指令重定位、跳板和 unhook 路径使用。任务初始化也不再注册依赖 Linker 回调的延迟任务监听。ShadowHook 的核心 AArch64 inline Hook 实现仍由上游静态源码构建并链接进唯一的 `libastraeoffline.so`。

该构建配置只支持本工程当前使用的**已加载 ELF 绝对地址 Hook**。ShadowHook 的按库名/符号名延迟 Hook、等待后续 `dlopen()` 的自动回调，以及依赖 Linker 的加载/卸载通知不会工作；新增此类功能前必须另行实现并验证加载监控机制。当前游戏目标 `libil2cpp.so` 在 Hook 运行前已经加载，且本工程通过 `dladdr()`、`dl_iterate_phdr()`、RVA 和目标指令指纹完成地址验证，因此当前 Hook 路径不依赖 Linker 私有结构。整个构建不再生成、嵌入或加载 `libshadowhook_nothing.so`，游戏目录只部署 `lib/arm64-v8a/libastraeoffline.so`。

上一版存在两个高风险面：自研 AArch64 patcher 只重定位有限指令并手工拼接绝对跳转；运行时又在库加载后自动启动并较早安装 Direct Replacement。当前版本已移除该 patcher，Android inline hook 改用 ByteDance 官方 ShadowHook v2.0.1。Hook 不会在 `dlopen()` / `System.loadLibrary()` 构造阶段安装。标准 Android 构建脚本默认启动延迟 Runtime worker，并启用大厅入口 Hook #3；只有等到 IL2CPP domain、`_AT.UI.UILoginPageController` 和 `EnterLobby/0` 方法可用，再额外等待 8 秒后才初始化 ShadowHook 和安装。`_AT.Network.NetworkHandler` 只作为诊断状态，不再阻塞 #3 大厅路径。回调会检查 `this` 对象是否为 `UILoginPageController` 或其子类；UI 回调中不再写磁盘；所有 Direct Replacement 回调均捕获 C++ 异常，避免异常越过 hook ABI。

ShadowHook 官方手册也明确要求先初始化再调用 Hook API，并提示初始化早于 App native crash handler 注册可能增加可避免的崩溃概率。因此这里采用延迟初始化；8 秒只是保守缓冲，不是对第三方 crash SDK 注册完成的证明。

## 当前运行状态（重要）

当前版本**尚未实现完整大厅启动链，不是可正常游玩的完整单机版**。最新上传的设备日志 Build ID `f6b3be8929b7-a2f7eea92f87cbf9` 明确显示 `local_request_direct_hook=OFF`；同时旧版 Hook #3 direct-replacement 在初始化链未完成时直接返回，不调用原游戏 touch，因此登录动画不响应是该旧二进制的直接结果。当前源码已经改为 Android 标准构建先安装 Hook #5，再安装 Hook #3；自定义前置链未完成时，只有在 Hook #5 确认安装成功后，Hook #3 才通过 ShadowHook `SHADOWHOOK_CALL_PREV` 转发原游戏 touch。Hook #5 将请求送入本地 Backend，未映射请求返回本地 ErrorPacket，不会放行到原网络实现；但其业务覆盖仍不完整，不能宣称已完成离线单机。

参考 ELF 中 `touch()` 的完整恢复顺序与依赖已记录在 `docs/REFERENCE_TOUCH_SEQUENCE.md`。`bootstrap(account)` 串联账户、余额、基金会、邮件、卡池、农场、Raid、日期、设施、Guide、每日任务、剧情包任务和谜题等多级恢复。只有这些步骤逐项移植并且每步的返回状态、Managed 对象和持久化结果均验证成功，才允许自研大厅 dispatcher 调用 `EnterLobby()` / `PlayFirstDay()`。

```bash
export ANDROID_NDK_HOME=/path/to/android-ndk
bash scripts/build_android_arm64.sh
```

输出：`artifacts/android-arm64-v8a/libastraeoffline.so`。Hook 启用时使用 direct-address-only ShadowHook，不生成或加载伴随 ELF，游戏中只部署这一份 `.so`。

## 分阶段启用

标准 Android 脚本默认启用 Runtime、Hook #5 本地请求拦截和 Hook #3。Hook #5 必须先成功安装，Hook #3 才会安装；如果 Hook #5 被显式关闭，构建脚本会拒绝同时启用 Hook #3。

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

**3. 启用 Hook #3 入口回退和 Hook #5 本地请求拦截（初始化链不完整时，点击会转发到原 touch；不会伪称已完整离线）**

```bash
ASTRAE_ENABLE_RUNTIME_AUTOSTART=ON \
ASTRAE_ENABLE_TOUCH_DIRECT_HOOK=ON \
ASTRAE_ENABLE_LOCAL_REQUEST_DIRECT_HOOK=ON \
bash scripts/build_android_arm64.sh
```

**4. 只诊断 Hook #5（不安装 Hook #3，保留原登录入口）**

```bash
ASTRAE_ENABLE_RUNTIME_AUTOSTART=ON \
ASTRAE_ENABLE_TOUCH_DIRECT_HOOK=OFF \
ASTRAE_ENABLE_LOCAL_REQUEST_DIRECT_HOOK=ON \
bash scripts/build_android_arm64.sh
```

当前阶段应看到 `Hook #5 local_request installed before Hook #3`，以及在前置链不完整时的 `forwarding this tap to the original game touch`。如果日志仍显示 `local_request_direct_hook=OFF`，说明没有使用本源码标准 Android 构建配置；新的 CMake 会拒绝这种不安全组合。Hook #5 只保证请求经过本地 Backend，不保证尚未实现的协议能够成功；未知请求返回 ErrorPacket。

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

Hook #3 已恢复参考 ELF 中 `touch(controller, method)` 使用的关键对象偏移：`DataManager=controller+0x10`、`Account=controller+0xb0`，并验证二者的 Managed 类型。预检路径只借用活动 Managed 调用的根引用，不创建/释放临时 GCHandle。上一版曾合成 first-day 完成标记；本版已删除这类强制跳过行为。当前源码按顺序执行：step 1 `artifact_capacity_tables`（设置 `Common+0xc8` 的 SecureInt 为 9999 并回读验证）；step 2 `character99_tables`（通过已确认的 XML 类型和 `FromXml` 构造 LevelFactor/GrowthRecipe Root，代码接入但尚待设备验证）；step 3 `permanent_raid_tables`；step 4 `seed_table_defaults` 尚未移植，参考 `touch` 对 false 返回走非致命分支；step 5 `battle_multiple_tables`；step 6 `custom_resource_tables` 仍未实现，因此当前仍 fail-closed，不调用 `EnterLobby()` / `PlayFirstDay()`。Hook #5 仍是部分请求路由；最新设备日志虽显示安装成功，却没有任何回调证据，因此不能称为已经接管网络。Hook 指纹只匹配对应游戏二进制版本；不匹配时会拒绝安装。Android ARM64 编译、目标设备加载与进入大厅必须在目标环境实测，主机编译不能证明真机稳定。

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

The standard Android build script defaults to Runtime Autostart, Hook #5 local-request replacement and Hook #3 touch replacement. Hook #5 is installed first, but installation alone does not prove the target method was called: the supplied device log showed zero Hook #5 callbacks after installation. When the custom lobby prelude is incomplete, Hook #3 forwards the click to the original game touch via `SHADOWHOOK_CALL_PREV` only when Hook #5 installation succeeded; this is a compatibility fallback, not proof that the original online flow is offline. The replacement answers mapped requests through the local Backend and unsupported requests with a local ErrorPacket only when the hooked target is actually invoked. Android configuration rejects Hook #3 enabled with Hook #5 disabled. The Hook #5 route coverage is partial and must not be described as a complete offline game. The output folder contains only `libastraeoffline.so`; ShadowHook is statically linked in direct-address-only mode, with its private linker monitor disabled and no companion probe ELF. Override explicitly for diagnosis: set `ASTRAE_ENABLE_RUNTIME_AUTOSTART=OFF` or `ASTRAE_ENABLE_TOUCH_DIRECT_HOOK=OFF` to create a passive SO. A passive SO does not enter the offline lobby.

The log records constructor/build flags, IL2CPP load/domain readiness, resolved symbol counts, ManagedWire schema load, Backend and Store initialization, login controller and `EnterLobby` readiness, ShadowHook version, target RVA/fingerprint, install result, touch callback count, managed request/response outcomes and a 15-second Hook watchdog.

## Character99 运行时元数据取证（当前阶段）

第 1 阶段正在还原 Character99 和其余 touch 前置步骤。静态 factors/recipes 数据已由参考 ELF 的 DWARF 与重定位记录恢复并压缩嵌入；当前源码已新增 Managed XML `FromXml` 构建流程，验证 `FromXml` 参数/返回类型，并将 LevelFactor 与 GrowthRecipe Root 写回 DataManager Loader。该流程尚无新 Build ID 的设备运行证据，必须在实机确认 LevelFactor 总数 880、GrowthRecipe 总数 736 且两个 DataDic 非空后才能标记为真机完成。当前 touch 会在 `custom_resource_tables`（step 6）处 fail-closed，不调用大厅入口。

当前 Host Release 自动化测试共 6 项：永久 Raid 边界、Artifact 容量反射写回、Character99 静态资源结构、Character99 对象图、IL2CPP 类元数据枚举、Touch fallback policy。它们均通过，但不能替代目标设备上的 Android ARM64 构建及游戏回归。阶段 1–5 的准确状态见 `docs/PHASE_PROGRESS.md`。


## 当前 Touch 恢复进度（2026-11-23）

当前源码按参考 ELF 的顺序执行 touch 前置流程：step 1 Artifact 容量、step 2 Character99 XML Root 重建、step 3 Permanent Raid、step 4 Seed（尚未移植；原始返回 false 为非致命，继续 step 5）、step 5 Battle Multiple。下一道必须完成的步骤是 step 6 `custom_resource_tables`；在它及后续 mandatory helpers 完成前，不会伪称已成功进入单机大厅。

`Hook #5 installed` 只代表安装完成，不等于已处理请求。日志会分别报告 Hook #5 是否观察到回调；截至最新用户日志，Hook #5 安装后没有观察到任何 `local_request` callback，因此本地协议接管尚未得到设备证据。

## Crash containment and save migration

See [`docs/CRASH_AND_SAVE_RECOVERY.md`](docs/CRASH_AND_SAVE_RECOVERY.md) for the confirmed Character99 `FromXml` crash containment, legacy compat-store migration, V3 save precedence, and tutorial-state preservation rules. The Android build must be regenerated after this source change; an older binary may still execute the unsafe `FromXml` path or initialize Backend before opening the compatibility Store.
