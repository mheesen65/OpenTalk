# Security policy

## Reporting a vulnerability

Open a private security advisory on GitHub, or email the maintainer listed on
the repository profile. Please do not file a public issue for anything
exploitable. Expect an acknowledgement within a few days and a coordinated
disclosure once a fix is ready.

## Supported versions

| Version | Supported |
| --- | --- |
| 1.0.x | Yes |
| < 1.0 | No |

## Threat model

OpenTalk is a self-hosted voice and chat system. The server is assumed to be
reachable by untrusted clients, and the client is assumed to receive untrusted
input from the server and other users.

Controls implemented in 1.0:

- **Identity is server-owned.** Sender id, nickname and channel are taken from
  the connection on every relayed message, so a modified client cannot
  impersonate another user or post into a channel it has not joined.
- **Bounded input.** Frames above 4 MiB close the connection; voice payloads,
  file chunks, text and nicknames are individually capped and sanitised.
- **Path safety.** File offers are reduced to a bare filename; a peer cannot
  steer where the receiver writes.
- **Abuse limits.** Per-session frame and byte budgets, handshake and idle
  timeouts, and a hard maximum user count.
- **Integrity.** Transferred files are verified against a SHA-256 advertised in
  the offer; a mismatch deletes the partial file.
- **GPL/LGPL compliance.** Qt is dynamically linked in the distributed package.

## Known gaps (must be addressed before exposing to the public internet)

1. **No transport encryption or authentication.** Traffic is plain TCP and
   anyone who can reach the port can join. Deploy behind a VPN or SSH tunnel.
   TLS is the top roadmap item.
2. **No accounts or permissions.** All channels are open to all members.
3. **No server-side storage.** File transfer is peer relay only; nothing is
   retained, and a recipient must be online.
4. **No audit logging** beyond connection and channel events on stdout.

If you need any of these guarantees today, OpenTalk 1.0 is not the right tool.
