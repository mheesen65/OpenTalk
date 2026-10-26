# OpenTalk 1.0.0

Self-hosted voice, text and file transfer for a home network or small
community. This release ships two archives.

## What is in each archive

**`OpenTalk-1.0.0-portable.zip`** — ready to run, no installation:

| Path | Contents |
| --- | --- |
| `OpenTalkServer` | Headless server executable |
| `OpenTalkClient` | Qt desktop client executable |
| `libOpenTalkSDK.so*` | SDK runtime, linked by both binaries |
| `sdk/include/` | SDK public headers, for building your own client |
| `docs/` | Architecture, protocol table, roadmap |
| `README.md`, `CHANGELOG.md`, `CONTRIBUTING.md`, `SECURITY.md`, `LICENSE` | Project documentation |
| `libQt6*.so.6`, `libicudata/i18n/uc.so.56`, `libopus.so.0` | Bundled dependencies |
| `plugins/`, `qt.conf` | Qt platform, image, TLS and multimedia plugins |

**`OpenTalk-1.0.0-source.zip`** — the complete source tree for commit
`dc3bf6b`, matching this release.

Verify with `sha256sum -c SHA256SUMS.txt`.

## Host and join on your home network

No port forwarding is needed for devices on the same Wi-Fi or router.

1. On the machine that will host, extract the archive and start the server:

   ```bash
   ./OpenTalkServer --port 10333 --name "Living Room"
   ```

   It prints the addresses other devices should use:

   ```
   Use one of these addresses in the client, from any device on the same network:
       192.168.1.42:10333
   ```

2. On another device, run `OpenTalkClient`, enter that address and port
   `10333`, choose a nickname, connect, then pick a channel.

3. Allow the server through the host firewall on **private** networks when
   prompted. Port forwarding is only required for access from outside your
   network, which this release does not set up.

Text, voice and file transfer all work over the LAN. File transfers require the
recipient to be online while the sender transmits.

## Platform note — read this first

The compiled binaries in the portable archive are **Linux x86_64**. They were
built and tested on the machine that produced this release.

**Windows executables are not included**: this release was assembled on Linux,
which cannot produce signed MSVC binaries. To get the Windows client and
server:

- Build locally with the steps in `README.md` (Qt 6 MSVC kit plus Opus from
  vcpkg), or
- Run the `CI` workflow in `.github/workflows/ci.yml`, which builds and tests on
  `windows-2022` and can be downloaded as a workflow artifact.

## Known limitations in 1.0.0

- No transport encryption: traffic is plain TCP. Use it on a trusted network or
  tunnel it; do not expose the port to the internet.
- No accounts or channel permissions; anyone who can reach the port can join.
- File transfer is relay-only, with no server-side storage or resume.
- Audio device opening is not covered by automated tests; it needs real
  hardware.
