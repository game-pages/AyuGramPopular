# AyuGramPopular · 频道热门消息增强版

基于 [AyuGram Desktop](https://github.com/AyuGram/AyuGramDesktop) 6.7.8，新增频道热门消息查找与本地缓存。当前版本为 **6.7.8 · Popular Messages 2**，Windows x64 与 macOS arm64 已于 2026-11-03 发布。

## 本次新增与优化

- **频道热门消息入口**：从频道顶部菜单打开“热门消息”，点击结果可跳转到原消息。
- **按反应数排序**：汇总消息的普通反应数量并降序排列，不计付费星星。
- **最低反应数量筛选**：设置最低数量，输入 `0` 表示不限。
- **扫描范围切换**：支持“近 30 天”和“全部历史”，通过分页读取频道历史。
- **停止与继续**：扫描过程中可随时停止，已有结果保留；继续扫描时接着当前进度读取。界面会显示已扫描数量和结果是否完整，遇到请求限流会显示等待状态。
- **30 天本地加密缓存**：保存扫描结果与进度，按账号、频道和扫描范围隔离；重启后可读取有效缓存并继续未完成的扫描。
- **手动刷新**：反应数是扫描时的快照，点击“刷新”可重新扫描并更新结果。
- **中文文案**：新增热门消息、筛选、缓存、扫描状态及操作按钮的中文翻译。
- **macOS 图标修复**：打包默认与备用图标，修复应用图标配置。
- **保留增强功能**：本次双平台构建关闭上游自动更新与崩溃报告，避免更新覆盖本分支的优化。

## 功能截图

热门消息面板支持扫描范围切换、反应数量筛选，并显示查询进度、缓存时间和按反应数排序的结果。

![频道热门消息面板：范围切换、反应数量筛选、缓存时间与排序结果](.github/demos/popular-messages.png)

## 下载与安装

[打开双平台发布页面：v6.7.8-popular2](https://github.com/game-pages/AyuGramPopular/releases/tag/v6.7.8-popular2)

| 平台 | 下载 | 安装方式 |
| --- | --- | --- |
| Windows 10/11 x64 | [安装 EXE](https://github.com/game-pages/AyuGramPopular/releases/download/v6.7.8-popular2/AyuGramPopular-6.7.8-PopularMessages2-Windows-x64-Setup.exe) | 运行安装程序 |
| Windows 10/11 x64 | [便携 ZIP](https://github.com/game-pages/AyuGramPopular/releases/download/v6.7.8-popular2/AyuGramPopular-6.7.8-PopularMessages2-Windows-x64-Portable.zip) | 完整解压后运行 AyuGram.exe，保留 modules 目录 |
| macOS 13+ · Apple Silicon arm64 | [DMG](https://github.com/game-pages/AyuGramPopular/releases/download/v6.7.8-popular2/AyuGramPopular-6.7.8-PopularMessages2-macOS-arm64.dmg) | 退出 AyuGram 后，将应用拖入 Applications |

Windows 安装包未签名；macOS 应用采用本地临时签名，未经 Apple 公证。安装包不包含账号、登录凭据或聊天数据。

发布附件提供 [SHA256SUMS.txt](https://github.com/game-pages/AyuGramPopular/releases/download/v6.7.8-popular2/SHA256SUMS.txt)、每个安装包的独立校验文件，以及 [BUILD-MANIFEST.json](https://github.com/game-pages/AyuGramPopular/releases/download/v6.7.8-popular2/BUILD-MANIFEST.json) 构建验证记录。

## 使用热门消息

1. 打开要查看的频道，在频道顶部菜单中选择“热门消息”。
2. 选择“近 30 天”或“全部历史”，设置最低反应数量。
3. 等待分页扫描，查看按反应数排列的结果；需要中断时点击“停止扫描”，之后点击“继续扫描”。
4. 点击结果跳转到原消息；需要最新反应数时点击“刷新”。

扫描未完成时，列表仅代表已经扫描到的消息。缓存保留 30 天，不会自动同步最新反应数。

## 构建与验证

- [Windows x64 Actions 构建说明](docs/building-windows-actions.md)：GitHub 官方 Windows Runner，复用上游依赖流程，分别缓存 ThirdParty、Libraries 与 Qt。
- [macOS arm64 构建与打包说明](docs/building-macos-arm64.md)：复用已准备的依赖，编译 Release、打包应用图标并生成 DMG。
- [Windows 成品校验脚本](tools/verify_windows_release.py)：检查 SHA-256、ZIP 完整性、AMD64 架构、必需模块与增强版标记。

Windows 和 macOS 的热门消息索引、分页及缓存独立测试均已通过。Windows 成品已检查 AMD64 架构与必需模块；macOS DMG 已只读挂载，核对应用二进制、arm64 架构、图标及严格签名验证。构建使用上游公开 API 示例配置，自行分发时请替换成自己的 Telegram 应用配置。

[已发布的 Windows 构建记录](https://github.com/game-pages/AyuGramPopular/actions/runs/41412644599)

主要功能源码位于 [Telegram/SourceFiles/ayu/features/channel_popular](Telegram/SourceFiles/ayu/features/channel_popular)，包含界面与扫描逻辑、排序索引、缓存编解码及独立测试。

## 社区认可与致谢

**认可并支持 [LINUX DO 社区](https://linux.do/)**，欢迎访问与交流。

## 上游与许可

感谢 [AyuGram Desktop](https://github.com/AyuGram/AyuGramDesktop) 和 [Telegram Desktop](https://github.com/telegramdesktop/tdesktop) 及其贡献者。上游功能与使用说明见 [AyuGram 文档](https://docs.ayugram.one/desktop/)，完整致谢见[上游仓库](https://github.com/AyuGram/AyuGramDesktop#credits)。

本项目保留上游开源许可，详见 [LICENSE](LICENSE) 和 [LEGAL](LEGAL)。
