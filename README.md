## 项目结构

```text
AstraeOffline/
├── .github/workflows/        # GitHub Actions：构建、打包、上传附件
├── android/                  # Android Java/JNI 桥接
├── assets/                   # 运行时协议、Schema、规则及原始表数据
├── compat/
│   ├── include/              # 兼容 ABI 与 cJSON 头文件
│   ├── src/                  # cJSON / store / errno 兼容实现
│   ├── tests/                # ABI 与存档回归测试
│   ├── original_abi/         # 必需的原始导出符号名单
│   └── original_exports.map  # 兼容库导出白名单
├── config/                   # 示例配置
├── include/                  # 项目 C API
├── src/                      # Backend / JSON / IL2CPP / Hook / Wire
├── tests/                    # 主机及协议回归测试
├── scripts/                  # 正式构建及源码打包脚本
├── CMakeLists.txt
└── README.md
```

## 环境要求

- CMake 3.22 或更高版本（项目最低要求 3.18）
- C/C++17 编译器
- Android NDK（建议 r27d / `27.2.12479018` 或兼容版本）
- Python 3（仅用于主机回归和 ELF/打包辅助脚本）

## 源码打包

```bash
python3 scripts/package_source.py --output dist/AstraeOfflineBackend-source.zip
```

脚本会打包源码、运行资源、测试、构建脚本、GitHub Actions 和 `useless/` 归档资料；自动排除 `.git`、构建目录、`artifacts/`、`dist/`、缓存和编译产物。不会把任何预编译 `.so` 混入源码包。

## GitHub Actions

工作流位于 `.github/workflows/android-ndk-release.yml`：

- 在 GitHub Actions 页面手动运行 `workflow_dispatch`，或推送 `v*` 标签；
- 自动运行 Host Release 构建和 CTest；
- 安装固定版本 Android NDK，构建 `arm64-v8a`；
- 生成源码 ZIP 和 Android ARM64 二进制 ZIP；
- 通过 Actions Artifacts 上传；推送版本标签时同时附加到 GitHub Release。
