# OpenTalk

An open-source voice, text and file-transfer platform, modelled on the core
functionality of [TeamTalk5](https://bearware.dk). Self-hosted server, native
desktop client, and a reusable SDK — all in C++ with Qt 6.

> **Status: early scaffold.** The wire protocol, server relay logic and a
> minimal client shell are in place and tested. Audio capture/playback and
> resumable file storage are not implemented yet; see
> [docs/architecture.md](docs/architecture.md) for the roadmap.

## Goals

- **Fully open source.** GPL-3.0 for the client, server and SDK alike.
- **Portable.** One ZIP, extract and run. No installer, no administrator
  rights, no registry writes, no service registration.
- **Self-hosted.** Anyone can run a server for their own community.
- **Native.** C++/Qt rather than a browser runtime, for predictable audio
  latency and access to real audio devices.

## Components

| Directory | Target | What it is |
| --- | --- | --- |
| `sdk/` | `OpenTalkSDK` | Wire protocol, framing, `Connection` — the library other applications build on |
| `server/` | `OpenTalkServer` | Headless server: sessions, channels, relay for text, voice and files |
| `client/` | `OpenTalkClient` | Qt Widgets desktop client |
| `tests/` | `opentalk_protocol_test` | Protocol encode/decode tests |
| `scripts/` | — | Portable ZIP packaging |
| `docs/` | — | Architecture and roadmap |

## Requirements

- CMake 3.21 or newer
- A C++17 compiler (MSVC 2019/2022 on Windows, GCC 11+ or Clang 13+ elsewhere)
- Qt 6.2 or newer with the `Core`, `Network`, `Widgets` and `Test` modules

On Windows, install Qt with the official online installer and pick the
*MSVC 2019 64-bit* (or 2022) kit; `windeployqt` ships with it and is used for
packaging.

## Building

Windows (Developer PowerShell, Qt on `PATH` or `CMAKE_PREFIX_PATH` set):

```powershell
cmake -S . -B build -G "Visual Studio 17 2022" -A x64 `
      -DCMAKE_PREFIX_PATH="C:/Qt/6.7.3/msvc2019_64"
cmake --build build --config Release
ctest --test-dir build -C Release --output-on-failure
```

Linux:

```bash
cmake -S . -B build -DCMAKE_BUILD_TYPE=Release
cmake --build build -j"$(nproc)"
ctest --test-dir build --output-on-failure
```

Build only part of the tree with `-DOPENTALK_BUILD_CLIENT=OFF`,
`-DOPENTALK_BUILD_SERVER=OFF` or `-DOPENTALK_BUILD_TESTS=OFF` — useful for a
server-only deployment where Qt Widgets is not installed.

## Running

```bash
./OpenTalkServer --port 10333 --name "My Server"      # headless
./OpenTalkClient                                       # desktop UI
```

The client connects to a host and port, sends a nickname, then joins one of
the server's channels. Text messages are echoed to everyone in the channel.

## Portable ZIP

```powershell
pwsh -File scripts/package_windows.ps1     # Windows: build + windeployqt + ZIP
./scripts/package_linux.sh                 # Linux: build + ZIP
```

The result is `dist/OpenTalk-<version>-portable.zip`. It contains the client,
the server and every Qt runtime library they need, laid out flat so a user can
unzip it anywhere, including a USB stick or a home directory, and run
`OpenTalkClient.exe` with standard user rights.

On Windows `windeployqt` also copies the Qt plugins. The Linux script ships the
`platforms`, `imageformats`, `iconengines` and `tls` plugins plus a `qt.conf`
pointing at them, but still expects the usual desktop libraries (`libxkbcommon`,
fontconfig, freetype, EGL/GLX) that every desktop distribution already has.
Windows is the fully self-contained target.

CMake can also produce a bare archive of the installed tree without the Qt
runtime:

```bash
cmake --install build --prefix dist/stage
cpack --config build/CPackConfig.cmake -G ZIP
```

## Protocol

Length-prefixed JSON control frames over TCP, defined in
[`sdk/include/opentalk/protocol.h`](sdk/include/opentalk/protocol.h). The
protocol is deliberately **not** wire-compatible with TeamTalk5, which frees
the design from its legacy constraints. Voice and file payloads currently
travel inside JSON control frames; moving them to raw binary frames is a
planned optimisation.

## Licence

GPL-3.0-or-later; the full text is in [LICENSE](LICENSE).

Qt is used under LGPLv3 and must therefore be linked **dynamically**, as the
portable package does. A statically linked single-file build would require a
commercial Qt licence or a different GUI toolkit. Contributors who hold no
copyright assignment can add a `Signed-off-by` line to commits, but the
project does not require a CLA.
