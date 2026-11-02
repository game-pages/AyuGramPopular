# Windows x64 Popular Messages 2 build

The `Windows x64 Release` workflow builds the enhanced 6.7.8 client on a GitHub-hosted `windows-2022` x64 runner with Visual Studio 2022 and Windows SDK 10.0.26100.0. It includes the popular messages source snapshot, Chinese translations, encrypted 30-day cache, pause/resume scanning, filtering and reaction ranking.

Configure these repository Actions secrets before running the workflow:

- `TDESKTOP_API_ID`
- `TDESKTOP_API_HASH`

The initial build uses the upstream public example credentials. Replace them with your own Telegram application credentials for distribution. Secrets are read only during validation and client configuration. Dependency caches do not contain the application build directory or API configuration.

Run the workflow manually from Actions, or push to `popular-windows-x64`. It prepares dependencies using the unmodified upstream `Telegram/build/prepare/prepare.py`, including Release libraries. The cache layout follows Telegram Desktop's Windows workflow at `v6.7.8`: ThirdParty, libraries and Qt are cached separately. Cache keys include dependency script versions, Qt selection, SDK, compiler and Python version. Failed preparation saves a partial cache under a separate key so another run can resume completed stages.

GitHub caches are scoped to repositories. A workflow running in this fork cannot directly restore the upstream repository's Actions caches. The first run prepares its own dependencies; later runs reuse this fork's caches. The upstream Docker environment builds Linux clients and is not used for Windows.

The workflow runs the existing standalone popular messages index/cache tests with MSVC, builds the Release client with two parallel compilation tasks, validates the executable's AMD64 PE header and bundles its D3D module. Automatic updates and crash reporting are disabled.

Successful builds upload one Actions artifact containing:

- `AyuGramPopular-6.7.8-PopularMessages2-Windows-x64-Portable.zip`
- `AyuGramPopular-6.7.8-PopularMessages2-Windows-x64-Setup.exe`
- SHA-256 checksum files for both packages

Artifacts are retained for 14 days. The installer and executable are unsigned. Extract the entire portable ZIP before starting `AyuGram.exe`. Check login, popular-message scanning, stopping/resuming and cached results on a Windows machine before distributing a release.
