import argparse
import hashlib
from pathlib import Path
import plistlib
import shutil
import subprocess
import tempfile


SOURCE = Path(__file__).resolve().parents[2]


def run(*command):
    return subprocess.check_output(command, text=True, stderr=subprocess.STDOUT)


def main():
    parser = argparse.ArgumentParser(description='Verify, sign and package the macOS arm64 client.')
    parser.add_argument('--source', type=Path, default=SOURCE)
    parser.add_argument('--output', type=Path)
    args = parser.parse_args()
    source = args.source.resolve()
    app = source / 'out/AyuGram.app'
    output = args.output or source / 'releases/AyuGramPopular-6.7.8-PopularMessages2-macOS-arm64.dmg'
    info = plistlib.loads((app / 'Contents/Info.plist').read_bytes())
    binary = app / 'Contents/MacOS' / info['CFBundleExecutable']
    if run('lipo', '-archs', str(binary)).strip() != 'arm64':
        raise RuntimeError('Expected an arm64-only client.')
    if info.get('AyuGramPopularBuild') != '2':
        raise RuntimeError('Popular Messages 2 package marker is missing.')
    if 'Popular Messages 2'.encode('utf-16-le') not in binary.read_bytes():
        raise RuntimeError('Enhanced client version string is missing.')
    dependencies = run('otool', '-L', str(binary))
    for line in dependencies.splitlines()[1:]:
        library = line.strip().split(' (', 1)[0]
        if not library.startswith(('/System/Library/', '/usr/lib/')):
            raise RuntimeError('Unexpected external dependency: ' + library)
    print(run('strip', str(binary)))
    print(run('codesign', '--force', '--deep', '--sign', '-', str(app)))
    print(run('codesign', '--verify', '--deep', '--strict', str(app)))
    output.parent.mkdir(exist_ok=True)
    if output.exists():
        raise RuntimeError('Refusing to overwrite an existing release image.')
    with tempfile.TemporaryDirectory(prefix='ayugram-release-', dir=source / 'out') as directory:
        stage = Path(directory)
        print(run('ditto', str(app), str(stage / app.name)))
        (stage / 'Applications').symlink_to('/Applications')
        (stage / '安装说明.txt').write_text(
            'AyuGram 6.7.8 · Popular Messages 2\n\n'
            '适用于 macOS 13 及更新版本的 Apple Silicon（arm64）Mac。\n'
            '退出 AyuGram，将 AyuGram.app 拖入 Applications。\n'
            '包含热门消息排序、数量筛选、分页扫描、停止/继续及 30 天加密缓存。\n'
            '反应数是扫描时的快照，可点击刷新更新。\n'
            '已关闭上游自动更新和崩溃报告。\n'
            '应用使用本地临时签名，未经 Apple 公证。\n'
            '安装包不包含账号、登录凭据或聊天数据。\n', encoding='utf-8')
        for name in ('LICENSE', 'LEGAL'):
            shutil.copyfile(source / name, stage / name)
        print(run('hdiutil', 'create', '-volname', 'AyuGram Popular Messages 2',
                  '-srcfolder', str(stage), '-format', 'UDZO', str(output)))
    print(run('hdiutil', 'verify', str(output)))
    digest = hashlib.sha256(output.read_bytes()).hexdigest()
    output.with_suffix('.dmg.sha256').write_text(digest + '  ' + output.name + '\n', encoding='ascii')
    print('Verified macOS arm64 release:', output)
    print('SHA-256:', digest)


if __name__ == '__main__':
    main()
