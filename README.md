# Spotify request filter

A small launcher that filters ad-related Chromium requests in desktop Spotify.
This branch adds Windows x64 support; the Linux launcher remains available.

- Ad filtering runs inside Spotify through a library loaded by the launcher.
- Spotify's installed files, audio routing, firewall and DNS are unchanged.

## Downloads and releases

The **Build and release** GitHub Actions workflow builds and tests Windows x64
and Linux x86_64 on every push and pull request. Its artifacts contain:

- `spotify-filter-windows-x64.zip`: the Windows launcher and DLL.
- `spotify-filter-linux-x64.tar.gz`: the Linux launcher and shared library.

Both archives include this README and the license. Extract the archive and keep
the launcher beside its library. Linux downloads still require `spotify-launcher`
with Spotify installed, Bash, and `pgrep`; no compiler is needed.

To publish both archives on GitHub Releases, tag the desired commit and push it:

```sh
git tag v1.0.0
git push origin v1.0.0
```

A release is published only after both platform builds and tests succeed.
Manual workflow runs produce artifacts; version-tag runs also publish a release.

## Windows x64

Use the desktop Spotify installer from Spotify. Microsoft Store installs, 32-bit
clients, and ARM64 clients are unsupported. No administrator access is required.

Build in the **MSYS2 MinGW64** terminal with GCC and Make installed:

```sh
pacman -S --needed mingw-w64-x86_64-gcc make mingw-w64-x86_64-python mingw-w64-x86_64-lld
make windows WINDOWS_CC=gcc
python -m unittest discover -s tests -v
```

Keep `spotify-filter.exe` and `spotify-filter.dll` together. Quit Spotify,
including its tray icon, then double-click `spotify-filter.exe` or run:

```powershell
.\spotify-filter.exe
.\spotify-filter.exe --spotify "C:\path to\Spotify.exe" "spotify:track:TRACK_ID"
```

The default client path is `%APPDATA%\Spotify\Spotify.exe`. Other arguments
are forwarded to Spotify with Windows quoting, including spaces and Unicode.
`--skip-update` is a Linux spotify-launcher option and is unnecessary here.

The launcher starts a new process suspended, loads the filter DLL, then resumes
startup with an import hook installed. It handles direct CEF imports and the
`Spotify.dll` loaded through the executable's `LoadLibraryExW` import, including
its delayed `cef_urlrequest_create` import. Inspection mappings are left alone.
If the initial hook cannot be installed, the new process is stopped. If the
loaded core lacks a supported CEF import, its load fails with an error. Spotify files and system settings are unchanged. To disable filtering,
quit Spotify and start it normally. Security software may block DLL loading.

The DLL and delay-import layout was verified in the official Windows x64 package
for Spotify `1.3.6.1921.g7086c071a617`. Other loader paths, dynamic symbol lookup,
other modules, subprocesses, and other networking APIs are outside this hook's
coverage. A future Spotify build may change the layout.
Windows playback has not been verified in this Linux workspace; the CI workflow
runs native integration tests for direct imports and dynamically loaded cores
with delayed CEF imports.
Download its `spotify-filter-windows-x64` artifact after a successful run.

## Linux

### Requirements

For ad filtering:

- Linux x86_64 and `spotify-launcher` with its Spotify client installed.
- Bash and `pgrep`.
- A C compiler and Make to build the library.

### Build and launch

From the cloned project directory:

```sh
make
./spotify-filter --skip-update
```

Quit Spotify completely before using this launcher. An existing Spotify process
cannot acquire the preload library from a second launch request.

Other arguments are forwarded to `spotify-launcher`. Omit `--skip-update` to let
it check for Spotify updates. Keep the project directory at a path without
spaces or colons because the dynamic loader splits `LD_PRELOAD` on those
characters. Existing preload libraries are retained for Spotify.

To disable filtering, quit Spotify and start `spotify-launcher` normally. No
restoration step is needed.

## Ad filtering

The library intercepts `cef_urlrequest_create` before HTTPS encryption. It rejects
only `/ads/` and `/ad-logic/` paths on `spclient.wg.spotify.com` and regional
`*-spclient.spotify.com` hosts, using the default HTTPS port or explicit port 443.
Other requests are forwarded unchanged. Blocked paths are logged to stderr
without query strings or fragments.

Filtering covers requests using that exported CEF API. Other network paths,
cached ads, sponsor audio embedded in podcasts, and future Spotify changes are
outside its verified coverage. Failed ad requests may cause retries or stalled
playback. If playback fails, quit and relaunch normally to compare.

## Validation (Linux)

```sh
make test
make -B
bash -n spotify-filter
```

Tests cover request boundaries, CEF string ownership, launcher argument
forwarding and preload behavior. Windows-only integration tests are skipped
on Linux. The existing Linux implementation had a short playback check that
encountered no ads while the filter logged blocked requests.

