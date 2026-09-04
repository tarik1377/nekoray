# What GreenRhythm changes vs nekoray

GreenRhythm («Зелёный Ритм») is a fork of
[MatsuriDayo/nekoray](https://github.com/MatsuriDayo/nekoray). It is a derivative
work and stays under **GPL-3.0**, like the original.

Upstream's last release is **4.0.1 (2024-12-12)** and the project is archived.
This document exists because the diff is large enough that "a fork of nekoray"
stops being a useful description, and because a stranger evaluating this
repository deserves to know what was actually done rather than take our word for
it.

Every number below is measured, and the command that produces it is printed next
to it. Nothing here is an estimate.

## Scale

```bash
git remote add upstream https://github.com/MatsuriDayo/nekoray.git
git fetch upstream
git diff --shortstat upstream/main HEAD -- . ':(exclude)qtsdk' ':(exclude)build'
```

| | |
|---|---|
| Files touched | 178 |
| Lines added / removed | +19 026 / −1 001 |
| Files added outright | 89 (11 864 lines) |
| Files of theirs modified | 88 |

There is no shared git history with upstream — the fork was re-imported — so
comparison is done tree-to-tree rather than by ancestry. To check whether any
individual file is ours or theirs:

```bash
git cat-file -e upstream/main:<path> && echo upstream || echo ours
git diff upstream/main HEAD -- <path>
```

## What is new

### Fallback connection

A second way in, used when the ordinary servers cannot be reached. It is bound to
the account rather than to a server address, it is activated with a short code
from the site, and it is deliberately slower than a normal connection — it exists
so that access is not lost, not so that video plays smoothly. The client says so
in the interface rather than pretending the connection is normal.

`main/RelayComponent.cpp`, `main/RelayActivation.cpp`, `main/RelayActivationParse.cpp`,
`main/RelayTrace.cpp`, `fmt/RelayBean.hpp`, `ui/dialog_relay_activate.cpp`,
`ui/edit/edit_relay.cpp`. Credentials are kept in a sealed per-machine store
(`main/SealedStore.hpp` with separate Windows, macOS and Linux backends) and are
never written to a profile, a log, or an exported config.

### Diagnostics — answering "it doesn't work"

Upstream has no answer to that question at all. This fork has several, and each
one was written after a real failure that took hours to find by hand:

- **`main/PortHealth.cpp`** — reads the system's reserved TCP port ranges. A
  widened dynamic range plus a driver reservation can take the ports a game or a
  launcher needs, and the failure surfaces as an unrelated crash. The client now
  names the cause and prints the two commands that fix it.
- **`main/ProgramTrouble.cpp`** — decides whether a given program's traffic went
  through the tunnel, went direct, or was never seen. It answers the actual
  question ("is my game going through the VPN?") instead of showing a connection
  table and hoping.
- **`sys/ForeignTunnels.cpp`** — finds other people's tunnels (a home WireGuard,
  a work VPN) and what routes they own, so that "repair network" can report them
  instead of switching them off.
- **`ui/dialog_whatbroke.cpp`** — the dialog that ties the three together.
- **`support/diagnose-tun.ps1`**, **`support/fix-network.ps1`**,
  **`support/collect-report.ps1`** — the same checks outside the client, for
  support.

### macOS

An entire platform upstream does not build for: `sys/macos/MacProxyController.cpp`,
`sys/macos/PacBuilder.cpp`, `sys/macos/PacServer.cpp`, `cmake/macos/`,
`libs/deploy_macos.sh` (477 lines), and a `build-macos` CI job. Both a TUN mode
and a system-proxy mode with a generated PAC file, because the two fail
differently and one of them has to keep working.

### Updates

`go/grpc_server/update.go` and `go/cmd/updater/` were largely rewritten:
the update manifest is served by our own site rather than a code-hosting
provider, the download does not go through the tunnel it is about to replace, the
archive is checked against a sha256 from the manifest, the extractor refuses
paths that escape the destination directory, and the updater waits for the
previous instance to release its files instead of failing on a locked binary.

The honest limit, stated in the code as well: sha256 over TLS is not a signature.
A compromised site could serve a different binary. The real fix is an offline
key, and it is not in yet.

### Live connections

`go/cmd/nekobox_core/connections.go` — the connection list, including recently
closed connections, which the Clash API keeps but never exposes over HTTP.
Without them a blocked connection is invisible: the router rejects it before it
is ever tracked, so "nothing appears in the list" and "nothing was attempted"
look identical.

### Interface

`ui/MainShell.cpp` and `ui/ServerCardDelegate.cpp` replace the row of eight
buttons and the six-column table with a sidebar, a connect page and a server list
drawn as cards. The existing table widget is re-parented rather than rebuilt, so
sorting, drag-reordering, the context menu, search and latency checks keep
working — the wiring is the same objects, not a reimplementation.

### Routing

`main/NekoGui.cpp` and `db/ConfigBuilder.cpp` carry a rule chain that puts named
programs ahead of category rules, keeps the DNS hijack above every user rule, and
excludes local and CGNAT ranges from the tunnel. sing-box matches first-rule-wins
and ANDs the fields inside one rule; getting the order wrong is silent, which is
why `test/RouteOrderTest.cpp` exists.

## What we changed in their files

The largest edits, by line count:

| File | +/− | What |
|---|---|---|
| `ui/mainwindow.cpp` | +2580 / −38 | diagnostics, network repair, autopilot reconnect, onboarding, scheme import |
| `.github/workflows/build-nekoray-cmake.yml` | +665 / −138 | test steps, macOS job, pinned xray with a checksum |
| `main/NekoGui.cpp` | +372 / −14 | routing preset and its migrations |
| `go/grpc_server/update.go` | +332 / −55 | update checks and download |
| `fmt/Bean2External.cpp` | +254 | external-core launch |
| `db/ConfigBuilder.cpp` | +237 / −23 | rule ordering, DNS, tunnel exclusions |

`ui/mainwindow.cpp` being that large is a defect, not an achievement: at 4 562
lines it is well past this project's own 800-line limit, and splitting it is
outstanding work.

## Tests

Upstream's `test/` directory contains two shell scripts that build the project.
There are no unit tests in it.

| | |
|---|---|
| C++ suites | 12 files, 1 822 lines |
| Go test files | 6 files, 1 018 lines |

They are not there for a coverage number. Each one stands in front of a failure
that does not announce itself:

- `RouteOrderTest` — a named program must beat a category rule, or a game breaks
  and the routing table still looks correct.
- `TunExcludeTest`, `TunReportTest` — the tunnel must not swallow local ranges,
  and the core must not replace the system DNS behind our back.
- `PortHealthTest` — parses real `netsh` output, not a hand-written sample.
- `ProgramTroubleTest` — one connection must not be counted thirty times.
- `DeviceCredentialsTest` — nothing readable may remain on disk.
- `PacBuilderTest` — a wrong PAC file sends traffic somewhere else silently.
- `extract_test.go` — an update archive must not be able to write outside its
  directory.
- `neko_config_transform_test.go`, `connections_test.go`, `update_test.go`.

## What we did not change

The GPL-3.0 licence, the upstream copyright, and the attribution stay, and are
meant to be visible rather than buried. The protocol implementations, the profile
formats and the `nekoray://` share-link format are upstream's work and are
deliberately kept compatible. Internal names — the `nekobox_core` directory, the
`neko.qrc` resource file — are left alone: renaming them would produce an
enormous diff, a real risk of breaking the build, and no benefit to anyone who is
not already reading the source.

See [THIRD-PARTY-NOTICES.md](../THIRD-PARTY-NOTICES.md) for everything else that
ships inside the binary.
