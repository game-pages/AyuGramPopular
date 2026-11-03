# 构建 macOS arm64 增强版

目标为 Apple Silicon，最低 macOS 13。本次发布使用 Xcode 27、Qt 6.2.13、CMake 3.31.10 和 Ninja，关闭上游自动更新及崩溃报告。

先按[上游 macOS 指南](building-mac.md)准备依赖。源码目录应与 `Libraries`、`ThirdParty` 位于同一个父目录，递归子模块需完整。需要在 `Libraries` 中准备 Qt 6.2.13 以及其他依赖的 arm64 Release 库。全新环境的依赖准备耗时较长，本次发布复用了已经准备好的本机依赖。

在完整 Xcode 的开发环境中运行，将 `TDESKTOP_API_ID` 与 `TDESKTOP_API_HASH` 设置为进程环境变量。脚本仅在本次构建中读取这些值，并遮盖输出中的值。源码中的公开配置示例见 `docs/api_credentials.md`；自行分发时应使用自己的 Telegram 应用配置。

```bash
python3 tools/macos/build_release.py --developer-dir /Applications/Xcode.app/Contents/Developer
python3 tools/macos/package_release.py
```

如果 CMake 不在 PATH 中，可用 `--cmake /path/to/cmake` 指定。`--jobs` 控制编译并行数，默认为 6。`--configure-only` 只配置项目，`--package-only` 为已编译的应用补齐默认图标、备用图标与版本标记。两份脚本都支持 `--source`，可指向另一份完整的源码检出。

构建产物为 `out/AyuGram.app`。DMG 输出至 `releases/AyuGramPopular-6.7.8-PopularMessages2-macOS-arm64.dmg`，可用打包脚本的 `--output` 指定其他文件名。脚本拒绝覆盖已经存在的 DMG。

DMG 打包前会检查 arm64 架构、Popular Messages 2 版本标记和外部动态库依赖，移除调试符号，执行临时签名及严格签名验证，再检查磁盘映像完整性并生成 SHA-256 文件。DMG 中包含应用、Applications 快捷入口、许可证及安装说明。临时签名不等于 Apple 公证。

索引和缓存的独立测试：

```bash
xcrun clang++ -std=c++20 -O2 -I Telegram/SourceFiles \
  Telegram/SourceFiles/ayu/features/channel_popular/channel_popular_index_test.cpp \
  -o out/channel-popular-test
out/channel-popular-test
```
