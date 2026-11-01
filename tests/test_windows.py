import os
from pathlib import Path
import shutil
import subprocess
import tempfile
import time
import unittest

ROOT = Path(__file__).resolve().parents[1]


@unittest.skipUnless(os.name == 'nt', 'Requires Windows x64 and MinGW gcc')
class WindowsLauncherTests(unittest.TestCase):
    @classmethod
    def setUpClass(cls):
        cls.tmp = tempfile.TemporaryDirectory(prefix='filter app ')
        cls.addClassCleanup(cls.tmp.cleanup)
        cls.build = Path(cls.tmp.name)
        for source in ('spotify-filter.exe', 'spotify-filter.dll'):
            shutil.copy2(ROOT / source, cls.build / source)
        subprocess.run(['gcc', '-shared', str(ROOT / 'tests/fake_cef.c'),
                        '-Wl,--out-implib,' + str(cls.build / 'libcef.a'),
                        '-o', str(cls.build / 'libcef.dll')], check=True)
        cls.client = cls.build / 'fixture.exe'
        subprocess.run(['gcc', '-municode', str(ROOT / 'tests/windows_driver.c'),
                        str(cls.build / 'libcef.a'), '-o', str(cls.client)], check=True)

        subprocess.run(['gcc', '-shared', '-DTEST_SPOTIFY_DLL', '-fuse-ld=lld',
                        str(ROOT / 'tests/windows_driver.c'), str(cls.build / 'libcef.a'),
                        '-Wl,--delayload=libcef.dll', '-ldelayimp',
                        '-o', str(cls.build / 'Spotify.dll')], check=True)
        cls.loader = cls.build / 'loader.exe'
        subprocess.run(['gcc', '-municode', str(ROOT / 'tests/windows_loader.c'),
                        '-o', str(cls.loader)], check=True)

    def launch(self, *args):
        return subprocess.run([str(self.build / 'spotify-filter.exe'),
                               '--spotify', str(self.client), *args],
                              capture_output=True, text=True, timeout=100)

    def test_requests_and_unicode_paths_with_spaces(self):
        for url, expected in [
            ('https://spclient.wg.spotify.com/ads/v3/ads?token=secret', 'blocked 1'),
            ('https://gew1-spclient.spotify.com/ad-logic/config', 'blocked 1'),
            ('https://spclient.wg.spotify.com/connect-state/a b', 'forwarded 1'),
            ('https://spclient.wg.spotify.com.evil.org/ads/config', 'forwarded 1'),
        ]:
            with self.subTest(url=url):
                output = self.build / 'result α.txt'
                output.unlink(missing_ok=True)
                result = self.launch(url, str(output))
                self.assertEqual(result.returncode, 0, result.stderr)
                deadline = time.monotonic() + 10
                while time.monotonic() < deadline:
                    if output.exists() and output.read_text() == expected:
                        break
                    time.sleep(0.05)
                self.assertEqual(output.read_text(), expected)

    def test_dynamically_loaded_core_with_delayed_cef_imports(self):
        original = self.client
        self.client = self.loader
        try:
            for url, expected in [
                ('https://spclient.wg.spotify.com/ads/v3/ads', 'blocked 1'),
                ('https://gew1-spclient.spotify.com/ad-logic/config', 'blocked 1'),
                ('https://spclient.wg.spotify.com/connect-state/a b', 'forwarded 1'),
            ]:
                with self.subTest(url=url):
                    output = self.build / 'delayed result.txt'
                    output.unlink(missing_ok=True)
                    # First show that the same native fixture runs without the filter.
                    plain = subprocess.run([str(self.loader), url, str(output)],
                                           capture_output=True, text=True, timeout=10)
                    self.assertEqual(plain.returncode, 0, plain.stderr)
                    self.assertEqual(output.read_text(), 'forwarded 0')
                    output.unlink()
                    result = self.launch(url, str(output))
                    self.assertEqual(result.returncode, 0, result.stderr)
                    deadline = time.monotonic() + 10
                    while time.monotonic() < deadline:
                        if output.exists() and output.read_text() == expected:
                            break
                        time.sleep(0.05)
                    self.assertEqual(output.read_text(), expected)
        finally:
            self.client = original

    def test_missing_dll_does_not_start_client(self):
        dll = self.build / 'spotify-filter.dll'
        hidden = dll.with_suffix('.hidden')
        dll.rename(hidden)
        try:
            self.assertNotEqual(self.launch('url', 'unused').returncode, 0)
        finally:
            hidden.rename(dll)

    def test_unsupported_client_is_stopped_before_entrypoint(self):
        source = self.build / 'unsupported.c'
        source.write_text(
            '#include <stdio.h>\n#include <wchar.h>\n'
            'int wmain(int argc, wchar_t **argv) { if (argc != 2) return 1; '
            'FILE *f = _wfopen(argv[1], L"w"); if (!f) return 2; '
            'fputs("started", f); return fclose(f); }\n')
        unsupported = self.build / 'unsupported.exe'
        subprocess.run(['gcc', '-municode', str(source), '-o', str(unsupported)], check=True)
        original = self.client
        self.client = unsupported
        try:
            output = self.build / 'must-not-exist.txt'
            result = self.launch(str(output))
            self.assertNotEqual(result.returncode, 0)
            self.assertIn('Filter initialization failed', result.stderr)
            self.assertFalse(output.exists())
        finally:
            self.client = original
