# Architecture

## Component map

```
                 TCP, length-prefixed JSON frames
  OpenTalkClient  <------------------------------>  OpenTalkServer
        |                                                  |
        +------------------> OpenTalkSDK <-----------------+
                       (protocol + framing + Connection)
```

Three targets, one shared library:

- **`OpenTalkSDK`** (`sdk/`) — the contract. Owns message types, JSON
  serialisation, framing and a `Connection` wrapper that turns a byte stream
  into message objects. Any third-party app can link it to become an OpenTalk
  client or server.
- **`OpenTalkServer`** (`server/`) — headless, `QCoreApplication`. Accepts
  connections, tracks identity and channel membership, and relays text, voice
  and file payloads between members of a channel.
- **`OpenTalkClient`** (`client/`) — `QApplication` + Widgets. Talks only
  through the SDK; it never touches sockets directly.

Dependency direction is strictly one way: `client` and `server` depend on the
SDK, never the reverse.

## Wire format

Every frame is:

| Bytes | Content |
| --- | --- |
| 0-3 | payload length, unsigned big-endian, *excluding* these 4 bytes |
| 4 | message type (`MessageType`) |
| 5.. | UTF-8 JSON object body |

Frames above `kMaxFrameSize` (4 MiB) cause an immediate disconnect: once a
length prefix is wrong, the stream cannot be resynchronised, so guessing is
worse than dropping the connection.

JSON was chosen for the control channel because it is debuggable with nothing
more than `tcpdump` and a text editor, and because the SDK can relay message
types it does not understand yet. Voice and file chunks are the exception —
they will move to raw binary frames later (see Roadmap).

`Hello` must be the first message; the server rejects anything else and closes
the connection. The server refuses mismatched `kProtocolVersion` at handshake
rather than failing in subtle ways mid-session.

## Messages

| Type | Direction | Purpose |
| --- | --- | --- |
| `Hello` / `Welcome` | client → server / server → client | Handshake: nickname and protocol version in, user id and server name out |
| `Error` | server → client | Machine-readable `code` plus human `description` |
| `Ping` / `Pong` | both | Liveness, nonce echoed back |
| `ChannelList` | server → client | All channels with user counts |
| `JoinChannel` / `LeaveChannel` | client → server | Membership changes |
| `UserList` | server → client | Full roster of one channel |
| `UserJoined` / `UserLeft` | server → client | Incremental roster updates |
| `TextMessage` | both | Chat; server stamps sender identity and timestamp |
| `VoiceFrame` | both | Relay of one encoded audio packet |
| `FileOffer` / `FileResponse` / `FileChunk` / `FileComplete` | both | Relay of a peer-to-peer transfer |

Trust boundary: the server **never** trusts a client-supplied sender id,
nickname or channel on relayed messages. It overwrites all three from the
session before forwarding, which is why a modified client cannot impersonate
another user or inject into a channel it has not joined.

## Threading

The server is single-threaded by design. One event loop multiplexes all
sockets; per-connection handlers only do cheap work (parse, look up, write).
For the 50-200 user target this is comfortably enough, and it removes an entire
class of races from channel membership.

The expected scaling path, when needed, is *not* to add a thread per
connection but to shard channels across worker threads with an explicit message
queue, keeping each channel's state owned by exactly one thread.

## Voice pipeline (planned)

Not implemented yet; the button in the client is deliberately inert.

1. Capture at 48 kHz mono (Qt Multimedia `QAudioSource`).
2. Frame into 20 ms blocks, encode with Opus at 24-32 kbps.
3. Send one `VoiceFrame` per packet (binary frame, not base64 JSON).
4. Server fans out to other channel members; no mixing for small channels.
5. Client decodes and feeds a jitter buffer keyed on sequence numbers, then
   plays through `QAudioSink`.

Two open design questions: whether the server mixes per channel once channel
sizes grow, and whether to add a UDP path for latency-sensitive media while
keeping TCP for control.

## File transfer (planned)

The protocol already defines offer/response/chunk/complete, and the server
relays them with size caps. What is missing is durability:

- Offers are relayed but not persisted, so a recipient must be online.
- Files are not stored server-side, so there is no resumable download.
- The planned `--data-dir` option will hold per-channel uploads with quotas and
  an eviction policy; chunking then optionally routes through the server.

## Portable packaging

The target is extraction-to-running with no installer and no administrator
rights:

- `scripts/package_windows.ps1` builds Release, stages via CMake install,
  copies the Qt runtime next to the executables with `windeployqt`, then zips.
- `scripts/package_linux.sh` does the same natively, bundling non-system Qt
  libraries resolved with `ldd`.
- `cpack -G ZIP` produces a bare archive of the installed tree.

Nothing writes to the registry, `Program Files`, `/etc`, or installs a service.
The server keeps all state in memory for now; once file transfer lands it will
need a data directory next to the executable and a documented config file.

Because the project is GPL-3.0 and Qt is LGPLv3, **Qt must stay dynamically
linked** — which the packaging above does. A truly single-file executable would
require a commercial Qt licence or a different toolkit; that trade-off is
recorded here rather than hidden.

## Roadmap

1. **Now** — protocol, framing, server relay, client shell, tests, packaging.
2. **Audio** — Opus capture/playback, jitter buffer, push-to-talk, mute.
3. **File transfer** — durable offers, resumable transfers, quotas.
4. **Identity** — optional accounts, channel permissions, moderation.
5. **Scale** — binary media frames, optional UDP media, channel sharding.
6. **Portability** — Linux and macOS client builds, headless server container.
