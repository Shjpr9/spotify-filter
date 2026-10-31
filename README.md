# Spotify request filter

A small Linux x86_64 launcher for the Spotify client managed by
`spotify-launcher`. It filters ad-related Chromium requests.

- Ad filtering runs inside Spotify through an `LD_PRELOAD` library.
- Spotify's installed files, audio routing, firewall and DNS are unchanged.

## Requirements

For ad filtering:

- Linux x86_64 and `spotify-launcher` with its Spotify client installed.
- Bash and `pgrep`.
- A C compiler and Make to build the library.

## Build and launch

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

## Validation

```sh
make test
make -B
bash -n spotify-filter
```

Tests cover request boundaries, CEF string ownership, launcher argument
forwarding and preload behavior. A short playback check encountered no ads
while the filter logged blocked ad requests.

