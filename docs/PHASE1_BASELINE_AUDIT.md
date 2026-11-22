# 阶段一报告：原始二进制基线与当前阻塞诊断

审计日期：2026-11-22
基准文件：用户上传的 `libastraeoffline.so`
基准 SHA-256：`86f65f4f11cf5657143ec848fab6ad7c444ed880ed3e04039add8acead7121e1`

## 1. 审计结论

本阶段已固定原版 ELF、动态 ABI、协议、Managed Wire、内嵌表和 Hook 清单，并生成逐项 CSV。审计脚本直接对本会话中的原始 ELF 做了 SHA-256、readelf 架构、SONAME、依赖及调试段校验。

本阶段发现当前“安全加载版”无法接管大厅请求的**确定代码级原因**：

1. `ASTRAE_ENABLE_RUNTIME_AUTOSTART` 默认 `OFF`，`runtime_bootstrap.cpp` 的 constructor/worker 仅在 Android 且此宏启用时编译。
2. 原始 Hook #5 `local_request` 在原版清单中是 **direct replacement**，不是 trampoline Hook；原版 39 项中共有 10 项采用这种模式。
3. 当前 `install_local_request_hook()` 把 `has_trampoline=false` 当成不允许安装，直接返回失败。即便手动打开 autostart，worker 也会在安装 #5 处退出，后续 19 个 UI 透传 Hook 的等待阶段不可能开始。

因此，当前版本进入游戏后不能收到离线后端构造的大厅响应，停在加载动画与该代码路径完全吻合。这是从当前源码控制流得出的判断；没有目标设备 logcat/tombstone，不能据此解释历史上的所有闪退。

**关键纠正：`has_trampoline=false` 代表原版直接替换，不等于 Hook 本身不支持。** 直接替换回调不能调用被替换函数的原实现，必须按原版函数体恢复业务逻辑。之前将 #5/#37 等入口一概排除的处理不符合原版 Hook 安装模型。

## 2. 原始 ELF 基线

| 项目 | 原始值 |
|---|---|
| 文件大小 | 4,083,840 bytes |
| 格式 | ELF64 / little-endian / ET_DYN |
| 架构 | AArch64 |
| SONAME | `libastraeoffline.so` |
| DT_NEEDED | `libc.so`、`libdl.so`、`liblog.so` |
| 调试信息 | 保留 `.debug_info`、`.debug_line` 等 DWARF 段 |
| 全局动态导出 | 126 |
| 文本函数符号 | 390 |
| DWARF 子程序签名 | 333 |

126 个动态导出分布：79 个 `cJSON_*`、44 个 `store_*`、2 个 JNI 入口、1 个 `__errno_location`。

原始二进制未剥离，DWARF 可恢复函数名、部分参数/返回类型、类型布局、编译单元与行号信息；但没有原始 C 源代码文本。因此最终验收应以可观察行为等价为目标，而不是承诺逐字源码或机器码相同。

## 3. 协议与托管数据基线

| 项目 | 数量 | 说明 |
|---|---:|---|
| Request 协议 | 93 | `protocol_index.json` 中的具体协议名 |
| Response 协议 | 116 | `protocol_index.json` 中的具体响应名 |
| Managed Wire 类型 | 359 | 所有具体 Request/Response 类型在 Schema 中均有对应定义 |
| JSON 数据块 | 9 | `assets/tables/embedded_*.json` |
| 表名 | 46 | 合并后的顶层表名 |
| 表记录 | 4,578 | 按每个数组元素计数 |

当前工程将 93 个请求映射到 14 个 handler group，路由数量见 `PHASE1_AUDIT.json`，逐协议详情见 `PHASE1_PROTOCOL_MATRIX.csv`。**路由已映射、Schema 已存在，不表示业务语义已经与原版一致。** 当前 coverage 文件将 semantic parity 标为 `not_verified`，本报告保留这一状态。

## 4. Hook 基线

原版共 39 个 Hook，29 个有 trampoline，10 个 direct replacement；37 个替换回调有 DWARF 签名，2 个缺失签名。10 个 direct replacement 的编号为：

`#1、#2、#3、#4、#5、#8、#11、#27、#36、#37`

其中 #5 `local_request` 是请求接管入口。原版的设计是直接将游戏网络请求入口改为 `local_request`，并不要求调用原始网络方法。

当前复刻源码的 `kUiBindings` 只有 19 个带 trampoline 的透传包装函数。这些函数仅调用原游戏方法，没有复刻旧库中的 UI 副作用，不能作为完整 UI 实现。另有 10 个 direct replacement 不能套用“透传原函数”的模型，需要还原其实际业务逻辑。

逐项偏移、指纹、模式、签名和当前包装状态见 `PHASE1_HOOK_MATRIX.csv`。

## 5. 存档 ABI 基线

原始 ABI 要求 44 个 `store_*` 导出函数。当前主机 Release DSO 可见全部 44 个 store 导出；源码审计也检测到 44 个函数体。这里的“有函数体/有符号”只是存在性，不等价于已证明每个状态分支、数据校验、事务和回滚都与原库一致。逐项清单见 `PHASE1_STORE_API_MATRIX.csv`。

此前归档的 `FINAL_PHASE_2_PROGRESS.md` 将 `store_queue_prologue_gift` 记为占位；当前被审计源码已在 `compat/src/store_core.cpp` 中加入函数体。该历史进度文档不再代表当前函数存在性，但业务语义仍需阶段二逐分支对照原版反汇编与状态差分。

## 6. 当前复刻工程的主要缺口

| 子系统 | 当前证据 | 当前结论 |
|---|---|---|
| 导出 ABI | 126 个原始符号有严格 export map；host DSO 的 44 个 store 符号存在 | 主机符号面基本对齐；Android NDK 尚未在当前环境运行 |
| Store | 44 个源码函数体、44 个 host 导出 | 语义差分未完成 |
| Protocol | 93 个路由全部映射到 14 组 handler；359 个 wire shape | 仅能证明 schema/路由存在，逐协议语义未证明 |
| 默认账号状态 | `Backend::seed_defaults()` 初始化空的角色、阵容、物品等数组 | `AccountLogin` 响应数据完整性/与原版默认存档一致性仍是风险 |
| Managed Wire | 有 schema 驱动的读写与托管对象回归测试 | 仍需目标 IL2CPP 运行时/回调验证 |
| Core Hook #5 | 原版 direct replacement；当前安装器拒绝 | 当前无法自动接管核心请求 |
| UI Hook | 19 个 passthrough wrapper | 原版自定义副作用未复刻 |
| 启动 | autostart 默认关闭 | 安全加载版不会启动运行时后端 |
| 真机 | 当前环境无 Android NDK、目标游戏进程和 tombstone | 无法证明设备端加载、ABI 与行为等价 |

## 7. 本阶段验证

- 原始 ELF SHA-256、文件大小、AArch64、SONAME、DT_NEEDED、`.debug_info`：通过。
- 原始 Hook manifest：39 项、29 trampoline、10 direct replacement：通过。
- Protocol index：93 Request、116 Response；359 个 Schema 类型全部可匹配：通过。
- 嵌入表：9 个 JSON、46 个表名、4,578 条记录：通过。
- 44 个 `store_*` 声明、源码函数体与主机 DSO 导出：通过存在性检查。
- 从本阶段工作树重新配置 Release 主机工程、重新编译后，CTest：23/23 通过。该结果不覆盖 Android ARM64 运行时，也不表示原版语义差分通过。

审计脚本：`scripts/phase1_baseline_audit.py`
重跑命令：

```bash
python3 scripts/phase1_baseline_audit.py --root . \
  --original-elf /path/to/original/libastraeoffline.so \
  --replica-so /path/to/host/libastraeoffline.so
```

## 8. 阶段一结论与移交

阶段一完成：原始基线、请求/响应矩阵、Hook direct/trampoline 分类、Store API 存在性矩阵、启动卡大厅的当前代码级原因均已记录并可重复审计。本阶段只固化证据与修正审计工具，不启用新的运行时 Hook，避免在 ABI/语义尚未恢复前再次引入启动风险。

阶段二从业务后端开始，优先处理 AccountLogin 所需状态与初始化数据，再逐协议恢复 93 个 handler 与 44 个 Store API 的状态转换/错误分支。阶段三再恢复 39 个 Hook 的真实副作用与完整 Managed 回调，阶段四进行 Android NDK 构建和真机差分验收。
