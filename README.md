# OpenTalk

An open-source voice, text and file-transfer platform for self-hosted
communities, modelled on the core functionality of
[TeamTalk5](https://bearware.dk). Native C++ with Qt 6 — client, server and SDK
in one build tree.

**Version 1.0.0** · GPL-3.0-or-later · Windows-first desktop client, portable
ZIP distribution with no installer and no administrator rights.

## Features

| Area | What works today |
| --- | --- |
| **Voice** | 48 kHz mono Opus at 28 kbps, 20 ms frames, push-to-talk and mute, per-sender jitter buffers, packet-loss concealment, server-side channel fan-out |
| **Text** | Per-channel chat with server-stamped identity and timestamps |
| **Files** | Chunked transfer (64 KiB chunks), SHA-256 verification, progress reporting, sanitised filenames, decline support |
| **Channels** | Three default channels, membership rosters, join/leave notifications, duplicate-nickname disambiguation |
| **Hardening** | Per-session frame and byte budgets, heartbeat + idle timeouts, handshake timeout, frame-size caps, server-owned identity on every relay |
| **SDK** | `OpenTalkSDK` exposes protocol, framing, connection, codec, jitter buffer and transfer helpers for third-party clients |
| **Packaging** | Portable ZIP with the Qt runtime and Opus bundled, dynamic linking as required by LGPLv3 |

## Requirements

- CMake 3.21+, Ninja or Make, a C++17 compiler (MSVC 2019/2022, GCC 11+, Clang 13+)
- Qt 6.2+ with `Core`, `Network`, `Widgets`, `Test`, and `Multimedia` for audio
- [Opus](https://opus-codec.org/) 1.3+ (required — it is the only audio codec)

Audio device support is optional at build time: configure with
`-DOPENTALK_ENABLE_AUDIO=OFF` to build a text-and-files client when Qt
Multimedia is unavailable.

## Building

Linux (Debian/Ubuntu):

```bash
sudo apt-get install cmake ninja-build qt6-base-dev qt6-multimedia-dev libopus-dev
cmake -S . -B build -DCMAKE_BUILD_TYPE=Release
cmake --build build -j"$(nproc)"
ctest --test-dir build --output-on-failure
```

Windows (Developer PowerShell). Install Qt 6 with the online installer
(MSVC 2019 64-bit kit, including *Qt Multimedia*) and Opus via vcpkg
(`vcpkg install opus:x64-windows`):

```powershell
cmake -S . -B build -G "Visual Studio 17 2022" -A x64 `
      -DCMAKE_PREFIX_PATH="C:/Qt/6.7.3/msvc2019_64" `
      -DCMAKE_TOOLCHAIN_FILE="$env:VCPKG_ROOT/scripts/buildsystems/vcpkg.cmake"
cmake --build build --config Release
ctest --test-dir build -C Release --output-on-failure
```

Targets: `OpenTalkSDK`, `OpenTalkServer`, `OpenTalkClient`, plus the test
binaries. Use `-DOPENTALK_BUILD_CLIENT=OFF` for a server-only deployment.

## Running

```bash
./build/server/OpenTalkServer --port 10333 --name "My Server" --max-users 200
./build/client/OpenTalkClient
```

In the client: enter host, port and a nickname, connect, pick a channel, then
chat, hold **Talk** to transmit, and **Send file...** to offer a file to the
channel. Accepted files land in your Downloads folder.

Server options: `--port`, `--name`, `--max-users`, `--idle-timeout`. The server
shuts down gracefully on Ctrl+C, notifying connected clients.

## Portable ZIP

```powershell
pwsh -File scripts/package_windows.ps1 -QtDir C:\Qt\6.7.3\msvc2019_64
```

```bash
./scripts/package_linux.sh
```

Produces `dist/OpenTalk-1.0.0-portable.zip`: executables, `OpenTalkSDK`, the Qt
runtime, Opus, platform/multimedia plugins and a `qt.conf`, flat in the archive
root. Unzip anywhere — including a USB stick or a home directory — and run
`OpenTalkClient.exe` with standard user rights. Nothing writes to the registry,
`Program Files`, `/etc`, or installs a service.

Windows packaging uses `windeployqt` and expects `opus.dll` next to the binaries;
the script looks for it beside the Qt kit or in `VCPKG_ROOT`, and warns if absent.

## SDK

```cpp
#include "opentalk/connection.h"

opentalk::Connection connection;
QObject::connect(&connection, &opentalk::Connection::connected, [&] {
    connection.send(opentalk::messages::hello(QStringLiteral("ada")));
});
QObject::connect(&connection, &opentalk::Connection::messageReceived,
                 [](const opentalk::Message &message) { /* dispatch */ });
connection.connectToServer(QStringLiteral("127.0.0.1"), opentalk::kDefaultPort);
```

The SDK owns the wire format, the audio jitter/mixing path (`VoiceReceiver`)
and file chunking (`OutgoingTransfer` / `IncomingTransfer`), so a third-party
client does not reimplement them.

## Protocol

Length-prefixed JSON frames over TCP: 4-byte big-endian length, one type byte,
JSON body; Opus payloads and file chunks travel base64-encoded inside that
envelope. The protocol is deliberately **not** wire-compatible with TeamTalk5.
See [docs/architecture.md](docs/architecture.md) for the message table and
design rationale.

## Testing

`ctest` runs four suites:

- **protocol** — framing, round-trips, malformed input, forward-compatible types
- **audio** — Opus encode/decode energy, concealment, jitter ordering, gap
  reporting, multi-sender mixing
- **transfer** — chunk round-trip, hash verification, corruption detection,
  out-of-order and oversized chunks
- **server** — real sockets: handshake, relay, spoofing resistance, nickname
  disambiguation, filename sanitising, chunk caps, heartbeats

## Known limitations

These are deliberate, documented boundaries of 1.0 rather than surprises:

1. **No transport encryption.** Traffic is plain TCP; run it on a trusted
   network or tunnel it (WireGuard/SSH). TLS is the next milestone.
2. **No accounts or roles.** Anyone who can reach the port can join; channel
   permissions and moderation come with identity work.
3. **File transfer is relay-only.** Both peers must be online; the server stores
   nothing, so there is no resumable or offline delivery yet.
4. **Audio device I/O is untested in CI.** Codec, jitter and mixing logic are
   covered by tests, but microphone/speaker opening needs real hardware — verify
   on your machine before relying on it.
5. **One server process, single event loop.** Sized for the 50–200 user target;
   not tested beyond it.

## Licence

GPL-3.0-or-later; the full text is in [LICENSE](LICENSE).

Qt is used under LGPLv3 and is linked **dynamically**, as the portable package
requires. A statically linked single-file build would need a commercial Qt
licence or a different toolkit. No CLA; see [CONTRIBUTING.md](CONTRIBUTING.md).
