import os
from pathlib import Path
import subprocess
import tempfile
import unittest

ROOT = Path(__file__).resolve().parents[1]


@unittest.skipIf(os.name == "nt", "Linux LD_PRELOAD tests")
class FilterTests(unittest.TestCase):
    @classmethod
    def setUpClass(cls):
        cls.tmp = tempfile.TemporaryDirectory()
        cls.addClassCleanup(cls.tmp.cleanup)
        cls.build = Path(cls.tmp.name)
        for args in [
            ["-shared", "-fPIC", str(ROOT / "tests/fake_cef.c"), "-o", str(cls.build / "fake.so")],
            [str(ROOT / "tests/driver.c"), str(cls.build / "fake.so"), "-o", str(cls.build / "driver")],
            ["-shared", "-fPIC", str(ROOT / "filter.c"), "-ldl", "-pthread", "-o", str(cls.build / "filter.so")],
        ]:
            subprocess.run(["cc", "-std=c11", "-Wall", "-Wextra", "-Werror", *args], check=True)

    def request(self, url, mode=0):
        return subprocess.run(
            [str(self.build / "driver"), url, str(mode)],
            env={**os.environ, "LD_PRELOAD": str(self.build / "filter.so")},
            text=True, capture_output=True, check=True,
        )

    def test_ad_requests_are_blocked_without_logging_query(self):
        for url in [
            "https://spclient.wg.spotify.com/ads/v3/ads?token=secret",
            "https://gew1-spclient.spotify.com/ad-logic/state/config?token=secret",
            "https://gae2-spclient.spotify.com:443/ads/v2/config",
            "https://SPCLIENT.WG.SPOTIFY.COM/ads/v3/ads",
        ]:
            with self.subTest(url=url):
                result = self.request(url)
                self.assertEqual(result.stdout.strip(), "blocked")
                self.assertIn("blocked", result.stderr)
                self.assertNotIn("secret", result.stderr)
                self.assertNotIn("?", result.stderr)

    def test_other_requests_and_lookalikes_are_forwarded(self):
        for url in [
            "https://gew1-spclient.spotify.com/storage-resolve/v2/files/audio/123",
            "https://spclient.wg.spotify.com/connect-state/v1/devices",
            "https://audio-fa.scdn.co/audio/123",
            "https://example.com/ads/v3/ads",
            "https://evil-spclient.spotify.com.evil.org/ads/v3/ads",
            "https://spclient.wg.spotify.com@evil.org/ads/v3/ads",
            "https://evil.org/?url=https://spclient.wg.spotify.com/ads/v3/ads",
            "https://spclient.wg.spotify.com/ads-are-music",
            "https://spclient.wg.spotify.com/track?path=/ads/v3/ads",
            "http://spclient.wg.spotify.com/ads/v3/ads",
            "https://spclient.wg.spotify.com:444/ads/v3/ads",
            "", "x" * 9000,
        ]:
            with self.subTest(url=url[:120]):
                result = self.request(url)
                self.assertEqual(result.stdout.strip(), "forwarded")
                self.assertEqual(result.stderr, "")

    def test_missing_url_and_incompatible_abi_are_forwarded(self):
        for mode in [1, 2, 3]:
            with self.subTest(mode=mode):
                self.assertEqual(self.request("https://spclient.wg.spotify.com/ads/v3/ads", mode).stdout.strip(), "forwarded")


@unittest.skipIf(os.name == "nt", "Linux launcher tests")
class LauncherTests(unittest.TestCase):
    def test_wrapper_preserves_arguments_and_preload(self):
        with tempfile.TemporaryDirectory() as folder:
            tmp = Path(folder)
            wrapper = tmp / "filter-app"
            wrapper.mkdir()
            script = wrapper / "spotify-filter"
            script.write_bytes((ROOT / "spotify-filter").read_bytes())
            script.chmod(0o755)
            subprocess.run(["cc", "-shared", "-fPIC", "-x", "c", "-", "-o", str(wrapper / "spotify-filter.so")], input="int placeholder;", text=True, check=True)
            (tmp / "pgrep").write_text("#!/bin/sh\nexit 1\n")
            (tmp / "spotify-launcher").write_text("#!/usr/bin/python3\nimport json,os,sys\nprint(json.dumps([sys.argv[1:],os.environ['LD_PRELOAD']]))\n")
            for name in ["pgrep", "spotify-launcher"]:
                (tmp / name).chmod(0o755)
            result = subprocess.run([str(script), "--skip-update", "spotify:track:a b"], env={**os.environ, "PATH": f"{tmp}:/usr/bin", "LD_PRELOAD": "libm.so.6"}, capture_output=True, text=True, check=True)
            import json
            args, preload = json.loads(result.stdout)
            self.assertEqual(args, ["--skip-update", "spotify:track:a b"])
            self.assertEqual(preload, f"{wrapper}/spotify-filter.so:libm.so.6")
            self.assertEqual(result.stderr, "")

    def test_wrapper_rejects_paths_that_loader_would_split(self):
        with tempfile.TemporaryDirectory(prefix="filter app ") as folder:
            script = Path(folder) / "spotify-filter"
            script.write_bytes((ROOT / "spotify-filter").read_bytes())
            result = subprocess.run(["bash", str(script)], capture_output=True, text=True)
            self.assertEqual(result.returncode, 1)
            self.assertIn("without spaces or colons", result.stderr)

    def test_wrapper_rejects_missing_build(self):
        with tempfile.TemporaryDirectory() as folder:
            script = Path(folder) / "spotify-filter"
            script.write_bytes((ROOT / "spotify-filter").read_bytes())
            result = subprocess.run(["bash", str(script)], capture_output=True, text=True)
            self.assertEqual(result.returncode, 1)
            self.assertIn("Build the filter first", result.stderr)


if __name__ == "__main__":
    unittest.main()
