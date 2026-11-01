# Spotify request filter

A small launcher that filters ad-related Chromium requests in desktop Spotify.
This branch adds Windows x64 support; the Linux launcher remains available.

- Ad filtering runs inside Spotify through a library loaded by the launcher.
- Spotify's installed files, audio routing, firewall and DNS are unchanged.

## Windows x64

Use the desktop Spotify installer from Spotify. Microsoft Store installs, 32-bit
clients, and ARM64 clients are unsupported. No administrator access is required.

Build in the **MSYS2 MinGW64** terminal with GCC and Make installed:

```sh
pacman -S --needed mingw-w64-x86_64-gcc make mingw-w64-x86_64-python
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

The launcher starts a new process suspended, loads the filter DLL, replaces
Spotify's `cef_urlrequest_create` import in memory, then resumes startup. If the
hook cannot be installed, the new process is stopped and the launcher reports
failure. Spotify files and system settings are unchanged. To disable filtering,
quit Spotify and start it normally. Security software may block DLL loading.

Coverage is limited to the main executable's direct `libcef.dll` import. Dynamic
symbol lookup, other modules, subprocesses, and other networking APIs are outside
this hook's coverage. A future Spotify build may not expose the expected import.
Windows playback has not been verified in this Linux workspace; the Windows CI
workflow builds both binaries and runs a native CEF fixture integration test.
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

