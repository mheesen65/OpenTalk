# Changelog

All notable changes to OpenTalk are recorded here. The format follows
[Keep a Changelog](https://keepachangelog.com/en/1.1.0/) and the project uses
[Semantic Versioning](https://semver.org/spec/v2.0.0.html).

## [1.0.0] - 2026-10-26

First complete release: voice, text and file transfer over a self-hosted
server, with an SDK that third-party clients can build on.

### Added

- **Voice**: Opus 48 kHz mono capture and playback (Qt Multimedia), 20 ms
  frames, push-to-talk, mute, per-sender jitter buffers, packet-loss
  concealment, multi-sender mixing (`VoiceReceiver`), server channel fan-out.
- **File transfer**: chunked offers, accept/decline responses, progress,
  SHA-256 verification on completion, sanitised filenames, size and chunk caps.
- **Server hardening**: per-session frame and byte budgets, heartbeat pings,
  idle-session expiry, handshake timeout, global frame-size ceiling,
  server-stamped identity on every relayed message, graceful shutdown on
  SIGINT/SIGTERM (Windows console handler included).
- **Client**: channel roster, chat, voice controls with input level meter, file
  transfer panel, persisted connection settings, duplicate-nickname handling.
- **SDK**: public `audio.h` (encoder, decoder, jitter buffer, voice receiver)
  and `transfer.h` (outgoing/incoming transfers, hashing) alongside the existing
  protocol and connection classes.
- **Tests**: four CTest suites (protocol, audio, transfer, server integration
  over real sockets) — 30+ assertions covering relay, spoofing, validation and
  media ordering.
- **Packaging**: portable ZIP scripts that bundle Qt, Opus, plugins and
  `qt.conf`; `$ORIGIN` rpath so extracted Linux binaries find their libraries.
- **Docs**: architecture notes, protocol table, contribution guide, security
  policy and CI workflow.

### Changed

- Protocol gains `VoiceFrame` payload structure (sequence, timestamp, codec) and
  typed file-transfer messages.
- Project version raised to 1.0.0 across CMake, client and server.

### Known limitations

- No TLS yet: plain TCP, suitable for trusted networks or a VPN tunnel.
- No accounts or channel permissions.
- File transfer requires both peers online; nothing is stored server-side.
- Audio device opening is not exercised in CI (no hardware); codec and mixing
  logic are unit-tested.

## [0.1.0] - 2026-10-26

### Added

- Initial scaffold: SDK (protocol, framing, connection), headless server with
  channels and text relay, Qt Widgets client shell, GPL-3.0 licence, portable
  ZIP packaging, protocol tests and architecture documentation.
