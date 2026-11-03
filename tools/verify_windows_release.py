import argparse
import hashlib
import json
from pathlib import Path
import struct
import zipfile


PRODUCT = 'AyuGramPopular-6.7.8-PopularMessages2-Windows-x64'


def verify(directory):
    portable = directory / (PRODUCT + '-Portable.zip')
    installer = directory / (PRODUCT + '-Setup.exe')
    result = {}
    for path in (portable, installer):
        digest = hashlib.sha256(path.read_bytes()).hexdigest()
        expected = path.with_suffix(path.suffix + '.sha256').read_text().split()
        if expected != [digest, path.name]:
            raise RuntimeError('Checksum mismatch: ' + path.name)
        result[path.name] = {'sha256': digest, 'bytes': path.stat().st_size}
    with zipfile.ZipFile(portable) as archive:
        if archive.testzip():
            raise RuntimeError('Portable archive integrity check failed.')
        required = {'AyuGram.exe', 'modules/x64/d3d/d3dcompiler_47.dll', 'LICENSE', 'LEGAL'}
        if not required.issubset(archive.namelist()):
            raise RuntimeError('Portable archive is missing required files.')
        binary = archive.read('AyuGram.exe')
        if binary[:2] != b'MZ':
            raise RuntimeError('Client is not a Windows executable.')
        offset = struct.unpack_from('<I', binary, 0x3C)[0]
        if binary[offset:offset + 6] != b'PE\0\0\x64\x86':
            raise RuntimeError('Client is not an AMD64 executable.')
        if 'Popular Messages 2'.encode('utf-16-le') not in binary:
            raise RuntimeError('Enhanced client version marker is missing.')
        result['client'] = {'architecture': 'AMD64', 'enhanced_version_marker': True,
                            'bytes': len(binary), 'archive_files': len(archive.namelist())}
    with installer.open('rb') as file:
        if file.read(2) != b'MZ':
            raise RuntimeError('Installer is not a Windows executable.')
    return result


if __name__ == '__main__':
    parser = argparse.ArgumentParser()
    parser.add_argument('directory', type=Path)
    args = parser.parse_args()
    print(json.dumps(verify(args.directory), indent=2))
