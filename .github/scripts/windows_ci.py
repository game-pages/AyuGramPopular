import argparse
import hashlib
import os
from pathlib import Path
import re
import shutil
import struct
import subprocess
import sys
import zipfile


SOURCE = Path(__file__).resolve().parents[2]
ROOT = SOURCE.parent
BUILD = SOURCE / 'out'
RELEASES = SOURCE / 'releases'
PRODUCT = 'AyuGramPopular-6.7.8-PopularMessages2-Windows-x64'


def run(command, label):
    logs = SOURCE / 'ci-logs'
    logs.mkdir(exist_ok=True)
    secrets = [os.environ.get(name, '') for name in ('TDESKTOP_API_ID', 'TDESKTOP_API_HASH')]
    with (logs / (label + '.log')).open('w', encoding='utf-8') as log:
        with subprocess.Popen(command, cwd=SOURCE, stdout=subprocess.PIPE, stderr=subprocess.STDOUT,
                              text=True, encoding='utf-8', errors='replace', bufsize=1) as process:
            for line in process.stdout:
                for secret in secrets:
                    if secret:
                        line = line.replace(secret, '***')
                print(line, end='', flush=True)
                log.write(line)
            result = process.wait()
    if result:
        raise RuntimeError(label + ' failed with exit code ' + str(result))


def prepare():
    run([sys.executable, str(SOURCE / 'Telegram/build/prepare/prepare.py'), 'silent'], 'prepare')


def trim():
    libraries = ROOT / 'Libraries/win64'
    keep = {'.lib', '.a', '.exe', '.dll', '.h', '.hpp', '.inc', '.cmake', '.pc', '.prl', '.pri', '.json'}
    keep_dirs = {'include', 'cache_keys', 'patches', 'objects-Release', 'objects-Debug', 'nv-codec-headers'}
    for directory, subdirs, filenames in os.walk(libraries):
        folder = Path(directory)
        if keep_dirs.intersection(folder.relative_to(libraries).parts):
            subdirs.clear()
            continue
        for name in filenames:
            path = folder / name
            if path.suffix.lower() not in keep:
                path.unlink()
    for directory, _, _ in os.walk(libraries, topdown=False):
        path = Path(directory)
        if not any(path.iterdir()):
            path.rmdir()
    print('Trimmed dependency sources and debug symbols after successful preparation.')


def build():
    api_id = os.environ.get('TDESKTOP_API_ID', '')
    api_hash = os.environ.get('TDESKTOP_API_HASH', '')
    if not re.fullmatch(r'[1-9][0-9]*', api_id) or not re.fullmatch(r'[a-fA-F0-9]{32}', api_hash):
        raise RuntimeError('Missing or invalid Telegram API repository secrets.')
    run(['cmake', '-S', str(SOURCE), '-B', str(BUILD), '-G', 'Ninja Multi-Config',
         '-DCMAKE_CONFIGURATION_TYPES=Release', '-DCMAKE_SYSTEM_VERSION=10.0.26100.0',
         '-DDESKTOP_APP_USE_PACKAGED=OFF', '-DDESKTOP_APP_DISABLE_AUTOUPDATE=ON',
         '-DDESKTOP_APP_DISABLE_CRASH_REPORTS=ON', '-DCMAKE_MSVC_DEBUG_INFORMATION_FORMAT=Embedded',
         '-DCMAKE_CXX_FLAGS=/FS', '-DCMAKE_C_FLAGS=/FS',
         '-DTDESKTOP_API_ID=' + api_id, '-DTDESKTOP_API_HASH=' + api_hash], 'configure')
    run(['cmake', '--build', str(BUILD), '--config', 'Release', '--target', 'Telegram',
         '--parallel', '2'], 'build')


def check_binary(binary):
    with binary.open('rb') as file:
        if file.read(2) != b'MZ':
            raise RuntimeError('Missing Windows executable header.')
        file.seek(0x3C)
        offset = struct.unpack('<I', file.read(4))[0]
        file.seek(offset)
        if file.read(6) != b'PE\0\0\x64\x86':
            raise RuntimeError('The executable is not Windows AMD64.')


def package():
    release = BUILD / 'Release'
    check_binary(release / 'AyuGram.exe')
    d3d = release / 'modules/x64/d3d/d3dcompiler_47.dll'
    if not d3d.is_file():
        raise RuntimeError('Required x64 D3D compiler module is missing.')
    RELEASES.mkdir(exist_ok=True)
    portable = RELEASES / (PRODUCT + '-Portable.zip')
    with zipfile.ZipFile(portable, 'w', zipfile.ZIP_DEFLATED, compresslevel=6) as archive:
        archive.write(release / 'AyuGram.exe', 'AyuGram.exe')
        for path in sorted((release / 'modules/x64').rglob('*')):
            if path.is_file():
                archive.write(path, path.relative_to(release).as_posix())
        for name in ('LICENSE', 'LEGAL'):
            archive.write(SOURCE / name, name)
    iscc = shutil.which('ISCC.exe') or str(Path(os.environ['ProgramFiles(x86)']) / 'Inno Setup 6/ISCC.exe')
    if not Path(iscc).is_file():
        raise RuntimeError('Inno Setup is missing from the Windows runner.')
    run([iscc, '/DReleasePath=' + str(release), '/DSourcePath=' + str(SOURCE), '/O' + str(RELEASES),
         str(SOURCE / '.github/installer/windows-x64.iss')], 'installer')
    installer = RELEASES / (PRODUCT + '-Setup.exe')
    if not installer.is_file():
        raise RuntimeError('Installer output is missing.')
    for path in (portable, installer):
        digest = hashlib.sha256(path.read_bytes()).hexdigest()
        path.with_suffix(path.suffix + '.sha256').write_text(digest + '  ' + path.name + '\n', encoding='ascii')
    summary = os.environ.get('GITHUB_STEP_SUMMARY')
    if summary:
        with open(summary, 'a', encoding='utf-8') as file:
            file.write('### Windows x64 Release\n\nPortable ZIP and installer built from `' +
                       os.environ['GITHUB_SHA'] + '`. Automatic updates and crash reports are disabled.\n')


def main():
    parser = argparse.ArgumentParser()
    parser.add_argument('action', choices=['prepare', 'trim', 'build', 'package'])
    args = parser.parse_args()
    if sys.platform != 'win32':
        parser.error('This build helper requires Windows x64.')
    globals()[args.action]()


if __name__ == '__main__':
    main()
