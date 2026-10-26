# Architecture

## Component map

```
                 TCP, length-prefixed JSON frames
  OpenTalkClient  <------------------------------>  OpenTalkServer
        |                                                  |
        +------------------> OpenTalkSDK <-----------------+
     (protocol, audio, transfer, connection)
```

Three targets, one shared library:

- **`OpenTalkSDK`** (`sdk/`) — the contract. Message types, JSON serialisation,
  framing, `Connection`, the Opus codec wrappers, `VoiceReceiver` (jitter +
  mixing) and the file-transfer state machines. A third-party client or bot
  links only this.
- **`OpenTalkServer`** (`server/`) — headless, `QCoreApplication`. Identity,
  channels, relay, rate limits and timeouts. It decodes nothing: audio and file
  payloads are opaque to it, which keeps the server cheap and codec-agnostic.
- **`OpenTalkClient`** (`client/`) — `QApplication` + Widgets. Talks only
  through the SDK; it never touches a socket directly.

Dependency direction is strictly one way: `client` and `server` depend on the
SDK, never the reverse. Opus is hidden behind `AudioEncoder`/`AudioDecoder` so
no public header includes third-party types.

## Wire format

| Bytes | Content |
| --- | --- |
| 0-3 | payload length, unsigned big-endian, *excluding* these 4 bytes |
| 4 | message type (`MessageType`) |
| 5.. | UTF-8 JSON object body |

Frames above `kMaxFrameSize` (4 MiB) cause an immediate disconnect: once a
length prefix is wrong the stream cannot be resynchronised, so guessing is
worse than dropping the connection. Media payloads (`data`) are base64 inside
the JSON envelope — a deliberate trade of ~33% overhead for debuggability and
one parsing path. Moving voice to binary frames is a future optimisation.

`Hello` must be the first message; anything else is refused and the connection
closed. A mismatched `kProtocolVersion` is rejected at handshake rather than
failing subtly later.

## Messages

| Type | Direction | Purpose |
| --- | --- | --- |
| `Hello` / `Welcome` | client → server / server → client | Handshake: nickname and version in, user id and server name out |
| `Error` | server → client | Machine-readable `code` plus human `description` |
| `Ping` / `Pong` | both | Liveness; nonce echoed back |
| `ChannelList` | server → client | Channels with live user counts |
| `JoinChannel` / `LeaveChannel` | client → server | Membership changes |
| `UserList` | server → client | Full roster of one channel |
| `UserJoined` / `UserLeft` | server → client | Incremental roster updates |
| `TextMessage` | both | Chat; server stamps sender and timestamp |
| `VoiceFrame` | both | One Opus packet: `seq`, `timestamp`, `codec`, base64 `data` |
| `FileOffer` | both | Metadata: transfer id, name, size, SHA-256, chunk size |
| `FileResponse` | both | Accept/decline with reason |
| `FileChunk` | both | Ordered chunk: `index`, `last`, base64 `data` |
| `FileComplete` | both | Success or failure with reason |

Trust boundary: the server **never** trusts a client-supplied sender id,
nickname or channel on relayed messages. It overwrites all three from the
session before forwarding, so a modified client cannot impersonate another user
or inject into a channel it has not joined. File names are reduced to their
basename before relay, chunk indices and sizes are bounded, and text is
truncated to the configured limit.

## Threading

Single event loop. Per-connection handlers only parse, look up and write; no
blocking work happens on the loop. For the 50–200 user target this removes an
entire class of races in channel state.

Scaling path, when needed: shard channels across worker threads with an
explicit queue, keeping each channel's state owned by one thread — not a thread
per connection.

## Audio pipeline

1. Capture: `QAudioSource` at 48 kHz mono, 16-bit.
2. Frame into 960-sample (20 ms) blocks; encode with Opus at 28 kbps, VOIP mode,
   in-band FEC on, DTX on.
3. Each packet carries a per-sender sequence and capture timestamp.
4. Server fans out to the rest of the channel. No mixing server-side.
5. Receive: `VoiceReceiver` keeps one jitter buffer **and** one Opus decoder per
   sender (decoder state is per stream), pops one packet per 20 ms tick,
   conceals gaps via Opus PLC, and sums the decoded frames with clamping into a
   single PCM frame for `QAudioSink`.

Push-to-talk and mute gate transmission at the capture stage, so nothing is
encoded or sent while the user is silent. Playback keeps a bounded queue
(≈500 ms) and drops the oldest audio rather than growing memory if the device
stalls.

Device I/O is the only untested-by-CI part: codecs, jitter buffering and mixing
are unit-tested, but opening a real microphone needs hardware.

## File transfer

Relay-only, peer to peer through the server:

1. Sender computes SHA-256 while opening the file, sends `FileOffer`.
2. Server sanitises the filename and rebroadcasts within the channel.
3. Each recipient may accept (`FileResponse`) and pick a local path; the sender
   streams 64 KiB chunks to anyone who accepted.
4. Receiver enforces order, bounds and declared size; on `last` it verifies the
   SHA-256 and deletes the partial file on mismatch.
5. Both sides exchange `FileComplete` so failures are visible, not silent.

Limits of this design, stated plainly: the recipient must be online, nothing is
stored server-side, and there is no resume. A store-and-forward mode with
per-channel quotas is the next step and needs a data directory and eviction
policy.

## Hardening

| Control | Value |
| --- | --- |
| Frame ceiling | 4 MiB, connection dropped |
| Frames per second per session | 400 |
| Bytes per second per session | 2 MiB |
| Handshake timeout | 15 s, then disconnect |
| Heartbeat / idle timeout | ping every 15 s / drop after 90 s |
| Voice payload | 8 KiB |
| File chunk | 256 KiB |
| Text | 4000 characters |
| Nickname | 32 characters, control characters stripped, duplicates suffixed |

Ratings are checked as bytes arrive, before parsing, so malformed floods cost
almost nothing.

## Packaging

`scripts/package_windows.ps1` builds Release, stages via CMake install, deploys
the Qt runtime with `windeployqt`, copies `opus.dll`, and zips flat.
`scripts/package_linux.sh` does the same natively: bundles `libQt6*`, the SDK,
ICU and Opus, copies the `platforms`/`imageformats`/`iconengines`/`tls`/
`multimedia` plugins, writes `qt.conf`, and relies on the `$ORIGIN` rpath set in
CMake so extracted binaries resolve their own directory.

No registry writes, no services, no administrator rights. Because the project is
GPL-3.0 and Qt is LGPLv3, Qt stays **dynamically linked**; a single-file static
build would need a commercial Qt licence. That trade-off is recorded rather
than hidden.

## Roadmap

1. **TLS** with certificate pinning or a trust-on-first-use prompt.
2. **Store-and-forward files** with quotas and resume.
3. **Identity**: accounts, channel permissions, moderation.
4. **Scale**: binary media frames, optional UDP media, channel sharding.
5. **Platforms**: macOS packaging, container image for the server.
