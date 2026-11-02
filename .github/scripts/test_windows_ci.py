import os
from pathlib import Path
import stat
import tempfile
import unittest
from unittest.mock import patch

import windows_ci


class DependencyCleanupTest(unittest.TestCase):
    def test_removes_read_only_git_files_and_preserves_build_inputs(self):
        with tempfile.TemporaryDirectory() as directory:
            root = Path(directory)
            libraries = root / 'Libraries/win64'
            retained = [
                'ada/out/singleheader/ada.h',
                'ada/out/singleheader/Release/ada-singleheader-lib.lib',
                'Qt-5.15.18/lib/Qt5Core.prl',
                'Qt-5.15.18/lib/cmake/Qt5Core/Qt5CoreConfig.cmake',
                'tg_owt/src/rtc_base/event.h',
                'include/nested/generated.inl',
                'cache_keys/ada',
                'patches/build_libvpx_win.sh',
                'lib/objects-Release/component/example.obj',
            ]
            removed = [
                'ada/.git/objects/pack/example.idx',
                'ada/.git/objects/pack/example.pack',
                'ada/out/example.obj',
                'ada/out/example.pdb',
                'Qt-5.15.18/qtbase/example.cpp',
            ]
            for relative in retained + removed:
                path = libraries / relative
                path.parent.mkdir(parents=True, exist_ok=True)
                path.write_bytes(b'build fixture')
            for relative in removed[:2]:
                (libraries / relative).chmod(stat.S_IREAD)
            try:
                with patch.object(windows_ci, 'ROOT', root):
                    windows_ci.trim()
                for relative in retained:
                    self.assertEqual((libraries / relative).read_bytes(), b'build fixture', relative)
                for relative in removed:
                    self.assertFalse((libraries / relative).exists(), relative)
                self.assertFalse((libraries / 'ada/.git').exists())
            finally:
                for directory, _, names in os.walk(root):
                    for name in names:
                        path = Path(directory) / name
                        path.chmod(path.stat().st_mode | stat.S_IWRITE)


if __name__ == '__main__':
    unittest.main()
