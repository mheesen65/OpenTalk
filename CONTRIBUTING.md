# Contributing to OpenTalk

Thanks for helping. This project is GPL-3.0-or-later and aims to stay easy to
build on every supported platform.

## Getting set up

```bash
git clone https://github.com/mheesen65/OpenTalk.git
cd OpenTalk
cmake -S . -B build -DCMAKE_BUILD_TYPE=Release
cmake --build build -j"$(nproc)"
ctest --test-dir build --output-on-failure
```

Requirements are listed in the [README](README.md). If Qt Multimedia is not
installed, configure with `-DOPENTALK_ENABLE_AUDIO=OFF` — the client then builds
without microphone or speaker support.

## What to work on

Good first contributions:

- TLS transport (the highest-value gap; see [SECURITY.md](SECURITY.md))
- Store-and-forward file transfers so recipients can be offline
- Linux/macOS client polish and platform packaging
- Additional server configuration (channel definitions, per-channel limits)

## Code style

- C++17, Qt conventions: `m_` for members, `camelCase` functions, 4-space
  indentation, 100-column soft limit.
- Comments explain *why*, not *what*. Do not narrate obvious code.
- Never trust client-supplied identity, paths or sizes on the server; sanitise
  and bound everything that crosses the wire.
- Keep public headers free of third-party types (Opus is hidden behind
  `AudioEncoder`/`AudioDecoder` for this reason).
- New behaviour needs a test. Server changes belong in `tests/src/server_test.cpp`
  and should drive the real socket path.

## Commit messages

Short imperative subject, blank line, then a paragraph on the motivation. Keep
unrelated changes in separate commits. Sign-off is welcome but not required.

## Pull requests

1. Branch from `production`.
2. Make sure `ctest` passes locally, including the server integration suite.
3. Describe what changed, why, and what you verified — especially anything you
   could not test (Windows binaries, audio hardware) so reviewers can cover it.

## Licensing

By contributing you agree your work is licensed under GPL-3.0-or-later. Do not
paste code from projects with incompatible licences.
