# AstraeOfflineBackend

AstraeOfflineBackend 是对原版 `libastraeoffline.so` 的独立 C/C++ 复刻工程。原版二进制仅作为逆向分析的行为基准；本项目的构建输入和运行时依赖均不包含原版 `.so`。

## 目标与当前边界

当前工程以 Android ARM64 / Unity IL2CPP 为目标，生成 `libastraeoffline.so`。现有实现包含离线 Backend、JSON/存档、原版 Store/cJSON 兼容 ABI、Managed Wire、IL2CPP Runtime、AArch64 Hook 引擎，以及最小大厅入口替换：Hook #3 `touch` 被替换为直接调用 `UILoginPageController.EnterLobby()`，跳过原版首日教程入口。Hook #5 的请求替换可处理现有离线路由，未处理请求返回 `ErrorPacket`。

**最小大厅路径不等于完整恢复原版初始化。** `touch` 中仍有表播种、角色/资源表、解锁同步、完整 bootstrap 和引导缓存等逻辑尚未全部移植；Hook #5 的特殊请求、主动 Managed 消息与所有业务语义也未全部恢复。UI Hook 目前仍有 passthrough wrapper。能否在具体游戏版本中进入大厅，仍取决于 IL2CPP 版本、Hook 指纹、运行时对象状态和游戏端所需初始数据，必须在目标设备实测。

## 目录结构

```text
AstraeOfflineBackend/
├── android/              Android Java/JNI 桥接
├── assets/               协议 Schema、Hook Manifest、内嵌表和规则
├── compat/               原版 C ABI、Store/cJSON 兼容层及导出白名单
│   ├── include/
│   ├── original_abi/
│   └── src/
├── config/               外部配置示例
├── docs/                 架构、构建和逆向移植记录
├── include/              对外 C API
├── scripts/              Android/Host 构建、资源生成和源码打包
├── src/                  Backend、IL2CPP、Managed Wire、Hook 和大厅逻辑
├── CMakeLists.txt
└── README.md
```

## 环境要求

- CMake 3.22+
- C/C++17 工具链
- Android NDK r27d（27.2.12479018）或兼容版本
- Android 目标 ABI：`arm64-v8a`

## 构建 Android ARM64

```bash
export ANDROID_NDK_HOME=/path/to/android-ndk
bash scripts/build_android_arm64.sh
```

输出位置：

```text
artifacts/android-arm64-v8a/libastraeoffline.so
```

默认启用：

- `ASTRAE_ENABLE_RUNTIME_AUTOSTART=ON`：加载库后创建运行时 worker，等待 IL2CPP 初始化并安装大厅入口 Hook。
- `ASTRAE_ENABLE_LOCAL_REQUEST_DIRECT_HOOK=ON`：启用 Hook #5 当前已有的离线请求替换逻辑。

可通过环境变量关闭单项：

```bash
ASTRAE_ENABLE_RUNTIME_AUTOSTART=OFF bash scripts/build_android_arm64.sh
ASTRAE_ENABLE_LOCAL_REQUEST_DIRECT_HOOK=OFF bash scripts/build_android_arm64.sh
```

构建脚本检查 ARM64 ELF、SONAME、导出符号表和 Android 动态依赖。上述检查不等于游戏内行为验证。

## Host 开发构建

```bash
bash scripts/build_host.sh
```

Host 输出位于 `build-host/libastraeoffline.so`，仅供本机开发，不可替代 Android ARM64 库。

## 内嵌资源

`assets/` 下的 JSON 会在构建前通过 `scripts/generate_embedded_assets.py` 转换为 `src/embedded_assets.cpp`。正常 Android 构建脚本会自动执行生成步骤。修改 Schema、Hook Manifest 或表数据后，不要手工编辑 `src/embedded_assets.cpp`；应修改源 JSON 并重新生成。

## 源码归档

```bash
python3 scripts/package_source.py --output dist/AstraeOfflineBackend-source.zip
```

归档脚本自动排除构建目录、临时产物和预编译库。

## 关键设计约定

- 原版 SO 不得作为链接输入、`dlopen` 目标或运行时 fallback。
- Direct Replacement Hook 不得调用不存在的原函数 trampoline。
- Managed 对象访问必须处于有效 IL2CPP 线程上下文；跨调用保存对象时必须使用受控 GCHandle。
- 新增协议字段与业务分支时同步更新 `assets/schema/` 和对应 handler，不以 Schema 存在冒充业务语义已恢复。
- 表初始化和存档更新应先构建完整临时结果，再原子提交，避免半初始化状态。
- Hook 的目标偏移和指纹只适用于对应的目标游戏二进制版本；不匹配时必须拒绝 patch。

## 逆向移植计划

四阶段范围和当前实现清单见 `docs/PHASE_PLAN.md`、`docs/PHASE1_SOURCE_PORT.md`。协议、Hook、Store 和内嵌表矩阵保存在 `docs/`，用于后续持续移植，不参与运行时加载。
