<p align="center">
  <img src="res/public/greenrhythm.png" width="120" alt="GreenRhythm">
</p>

<h1 align="center">GreenRhythm</h1>

<p align="center">
  <b>Fast, modern proxy client powered by sing-box 1.13</b>
</p>

<p align="center">
  <a href="https://github.com/tarik1377/nekoray/releases/latest">
    <img src="https://img.shields.io/github/v/release/tarik1377/nekoray?style=for-the-badge&color=2ea043&label=Download" alt="Latest Release">
  </a>
  <a href="https://github.com/tarik1377/nekoray/releases">
    <img src="https://img.shields.io/github/downloads/tarik1377/nekoray/total?style=for-the-badge&color=2ea043&label=Downloads" alt="Downloads">
  </a>
  <a href="https://github.com/tarik1377/nekoray/actions">
    <img src="https://img.shields.io/github/actions/workflow/status/tarik1377/nekoray/build-nekoray-cmake.yml?style=for-the-badge&color=2ea043&label=Build" alt="Build">
  </a>
</p>

---

## What is GreenRhythm?

A cross-platform proxy client with Qt GUI, built on the **sing-box 1.13.x** core. Designed for speed, privacy, and ease of use.

### Key Features

- **sing-box core** — VLESS+Reality performance (exact version: see `go/cmd/nekobox_core/go.mod`)
- **Smart routing** — RU sites direct, everything else through proxy
- **TUN mode** — system-wide VPN with one click
- **Auto config** — optimized defaults out of the box
- **Ad blocking** — built-in geosite ad filter
- **DNS splitting** — Yandex DNS for RU, Cloudflare for international

### Supported Protocols

| Protocol | Status |
|----------|--------|
| VLESS + Reality + XTLS-Vision | Recommended |
| VMess | Supported |
| Trojan | Supported |
| Shadowsocks | Supported |
| SOCKS 4/5, HTTP(S) | Supported |
| Hysteria2, TUIC | Supported |
| WireGuard | Supported |
| Custom configs | Supported |

---

## «Зелёный Ритм» service

GreenRhythm is a universal client and also the desktop app for the **«Зелёный Ритм»**
VPN subscription service ([verdantvibe.ru](https://verdantvibe.ru) ·
[@VerdantVibeBot](https://t.me/VerdantVibeBot)). Subscriptions from any other provider
keep working exactly the same — the service ties below are optional convenience only,
there is no lock-in and no telemetry.

- **First-run onboarding** — paste a subscription link, or open the site / Telegram to get one.
- **One-click import** — `greenrhythm://import/<link>` adds a subscription or a single
  profile straight from the website or bot. See [INTEGRATION.md](INTEGRATION.md) for the
  URL contract, behaviour and security model.
- **Renew hint** — when a subscription update returns nothing, the group row shows a
  soft "renew" link. Never modal, never nagging.

---

## Quick Start

### 1. Download

Download the latest release from [**Releases**](https://github.com/tarik1377/nekoray/releases/latest).

### 2. Unzip & Run

Extract the archive. Run `greenrhythm.exe` **as Administrator** (required for TUN mode).

### 3. Add Profile

- Click **Server** → **New profile** → **VLESS**
- Enter your server details
- Double-click the profile to connect

### 4. Enable TUN

Go to **Settings** → **TUN Mode** → Enable. All system traffic will be routed automatically.

### macOS

The macOS build is **not signed**, so the system will refuse to open it on the
first run — right-click → **Open**, then **Open** again. This is expected for
anything not from the App Store, not a sign of a virus.

Drag the app to **Applications** first: launched straight from Downloads, macOS
runs a read-only copy and the app cannot remember servers or sign-in.

Full instructions, both traffic modes and their trade-offs, and what happens
when you already have a home VPN running: **[docs/Run_macOS.md](docs/Run_macOS.md)**
(на русском).

---

## Default Configuration

GreenRhythm comes pre-configured for optimal use:

```
Direct (no proxy):     .ru .su .рф + VK, Yandex, Mail.ru, Avito, Ozon, WB, Sber...
                       + Microsoft, Windows Update, Office, Bing
Via proxy:             Everything else (YouTube, GitHub, Discord, Claude, etc.)
DNS:                   Cloudflare (international) + Yandex (RU)
Process routing:       Discord, Telegram, Claude → always proxy
Ad blocking:           geosite:category-ads-all
```

---

## Build from Source

### Requirements

- Qt 6.7+ (MSVC 2022)
- Go 1.24+
- CMake + Ninja
- MSVC 2022

### Build

```bash
# libneko is wired in through a `replace` directive pointing next to the
# repository, so it must be cloned first — `go build` fails without it.
git clone --depth 1 https://github.com/MatsuriDayo/libneko.git ../libneko

# Build the Go core. Use the script rather than a bare `go build`: it sets the
# output name the GUI looks for and stamps the sing-box version into the binary.
GOOS=windows GOARCH=amd64 bash libs/build_go.sh

# Build the Qt GUI
mkdir build && cd build
cmake -GNinja -DQT_VERSION_MAJOR=6 -DCMAKE_BUILD_TYPE=Release ..
ninja
```

Tests live in `test/` and `go/`; see [docs/CHANGES-vs-nekoray.md](docs/CHANGES-vs-nekoray.md)
for what this fork adds on top of the original project.

---

## Credits

Built on the shoulders of giants:

- [SagerNet/sing-box](https://github.com/SagerNet/sing-box) — core engine
- [MatsuriDayo/nekoray](https://github.com/MatsuriDayo/nekoray) — the original project this
  client is forked from (archived upstream, last release 4.0.1). GreenRhythm is a derivative
  work and stays under GPL-3.0; see [docs/CHANGES-vs-nekoray.md](docs/CHANGES-vs-nekoray.md)
  for what we changed and [THIRD-PARTY-NOTICES.md](THIRD-PARTY-NOTICES.md) for everything else
  we ship
- [Qt](https://www.qt.io/) — GUI framework

---

<p align="center">
  <sub>GreenRhythm is licensed under GPL-3.0</sub>
</p>
