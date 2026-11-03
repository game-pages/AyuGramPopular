import argparse
import os
from pathlib import Path
import plistlib
import re
import shutil
import subprocess


def main():
    parser = argparse.ArgumentParser(description='Build the Popular Messages 2 macOS arm64 client.')
    parser.add_argument('--source', type=Path, default=Path(__file__).resolve().parents[2])
    parser.add_argument('--cmake', default=shutil.which('cmake') or 'cmake')
    parser.add_argument('--developer-dir')
    parser.add_argument('--jobs', type=int, default=6)
    mode = parser.add_mutually_exclusive_group()
    mode.add_argument('--configure-only', action='store_true')
    mode.add_argument('--package-only', action='store_true')
    args = parser.parse_args()
    if args.jobs < 1:
        parser.error('--jobs must be positive')
    source = args.source.resolve()
    build = source / 'out'
    build.mkdir(exist_ok=True)
    temporary = build / 'tmp'
    temporary.mkdir(exist_ok=True)
    modules = build / 'module-cache'
    modules.mkdir(exist_ok=True)
    env = os.environ.copy()
    env.update({'TMPDIR': str(temporary), 'CLANG_MODULE_CACHE_PATH': str(modules),
                'CMAKE_BUILD_PARALLEL_LEVEL': str(args.jobs), 'QT': '6.2.13'})
    if args.developer_dir:
        env['DEVELOPER_DIR'] = args.developer_dir
    api_id = env.get('TDESKTOP_API_ID', '')
    api_hash = env.get('TDESKTOP_API_HASH', '')

    def run(command):
        with subprocess.Popen(command, cwd=source, env=env, stdout=subprocess.PIPE,
                              stderr=subprocess.STDOUT, text=True, errors='replace') as process:
            for line in process.stdout:
                for value in (api_id, api_hash):
                    if value:
                        line = line.replace(value, '***')
                print(line, end='', flush=True)
            if process.wait():
                raise RuntimeError('macOS build or icon packaging failed.')

    if not args.package_only:
        if not re.fullmatch(r'[1-9][0-9]*', api_id) or not re.fullmatch(r'[a-fA-F0-9]{32}', api_hash):
            parser.error('Set TDESKTOP_API_ID and TDESKTOP_API_HASH in the process environment.')
        run([args.cmake, '-S', str(source), '-B', str(build), '-G', 'Ninja',
             '-DCMAKE_BUILD_TYPE=Release', '-DCMAKE_OSX_ARCHITECTURES=arm64',
             '-DCMAKE_OSX_DEPLOYMENT_TARGET=13.0', '-DDESKTOP_APP_MAC_ARCH=arm64',
             '-DDESKTOP_APP_DISABLE_AUTOUPDATE=ON', '-DDESKTOP_APP_DISABLE_CRASH_REPORTS=ON',
             '-DCMAKE_EXPORT_COMPILE_COMMANDS=ON',
             '-DTDESKTOP_API_ID=' + api_id, '-DTDESKTOP_API_HASH=' + api_hash])
        if not args.configure_only:
            run([args.cmake, '--build', str(build), '--target', 'Telegram', '--parallel', str(args.jobs)])
    if args.configure_only:
        return
    app = build / 'AyuGram.app'
    icons = sorted((source / 'Telegram/Telegram').glob('AppIcon-*.icon'))
    if not icons:
        raise RuntimeError('Application icon sources are missing.')
    output = build / 'icon-assets'
    output.mkdir(exist_ok=True)
    metadata = build / 'icon-assets-info.plist'
    run(['/usr/bin/xcrun', 'actool', *map(str, icons), '--compile', str(output),
         '--app-icon', 'AppIcon-Default', '--include-all-app-icons',
         '--enable-on-demand-resources', 'NO', '--development-region', 'en',
         '--target-device', 'mac', '--platform', 'macosx', '--minimum-deployment-target', '13.0',
         '--output-partial-info-plist', str(metadata), '--output-format', 'human-readable-text',
         '--warnings', '--notices'])
    for name in ('AppIcon-Default.icns', 'Assets.car'):
        asset = output / name
        if not asset.is_file() or not asset.stat().st_size:
            raise RuntimeError('Missing compiled icon asset: ' + name)
        shutil.copy2(asset, app / 'Contents/Resources' / name)
    info = app / 'Contents/Info.plist'
    values = plistlib.loads(info.read_bytes())
    values.update(plistlib.loads(metadata.read_bytes()))
    values['AyuGramPopularBuild'] = '2'
    info.write_bytes(plistlib.dumps(values))
    print('Built macOS arm64 client and packaged default and alternate icons.')


if __name__ == '__main__':
    main()
