# Windows updater: progress and recovery

The GUI keeps working throughout download and verification. It shows bytes and
percentage from `greenrhythm.zip.progress.json`; partial downloads use a separate
`.partial` file and do not truncate an earlier verified package. Download errors
show the reason and a Repeat button. The installer must start successfully before
the GUI begins its normal shutdown.

The core extracts `GreenRhythm/updater.exe` from the authenticated archive as
`updater.next.exe`. The GUI starts that version, so an old installed updater is not
responsible for interpreting the next package. All three components (GUI, core,
updater) must be shipped together for this handoff.

On Windows, the updater runs from a unique copy in the user's temporary directory.
Its own progress window remains available through verification, extraction,
waiting, installation, recovery and startup. It prevents closing during work;
the error dialog offers Retry or Cancel after the operation stops. A named OS mutex
serializes installers for each installation. Waiting checks every target file,
including updater.exe and DLLs, rather than only GUI/core/Xray executables.

Each replaced file is moved to a backup before replacement. A handled installation
failure restores originals in reverse order and removes newly added files. A
failed startup or exit during the first two seconds also rolls back. Failed
recovery retains the backup and reports its path; another installation is refused
while a nonempty backup remains. The archive is retained after failure for retry.
Settings, profiles and route definitions are skipped regardless of bundled
templates; only root-level bundled `.srs` assets under config are updated.

## Why an old updater can survive

The d79d9f35 implementation waits only for GUI/core/Xray, then recursively renames
files without rollback. A programmatic reproduction against that exact source
locks updater.exe in a temporary installation: waiting returns, GUI/core are
replaced, replacement of updater.exe fails, and the previous updater survives.
Its bootstrap also reuses updater.old and ignores copy/start errors. These are
verified failure mechanisms; the actual lock responsible for a past installation
cannot be established without that attempt's evidence.

## Validation on Windows

- `go test ./...`, `go test -race ./...`, `go vet ./...` in go/cmd/updater:
  signed manifest and checksum rejection, archive traversal, actual exclusive DLL
  lock, replacement of updater.exe, preservation of config, all 11 rename failure
  positions, successful retry, failed rollback retaining backups, concurrent
  installer rejection, immediate process exit and successful startup observation.
  The native window is tested hidden by pumping messages and reading its own
  control text; no screenshots are created. One pre-existing tar symlink test is
  skipped because Windows symlink creation needs privileges.
- `go test` and `go test -race` for grpc_server using the core's prepared overlay
  and release build tags: DNS/TLS/HTTP/header/body outages, slow transfer, retry,
  signature and integrity rejection, bootstrap extraction/failure, progress and
  preservation of a previous package on failed download. `go vet` also passes.
- `update_actions_test`: 25 GUI action checks pass offscreen, including error
  Retry, blocked installer preserving the running GUI, successful handoff and
  late responses after window deletion.
- `test/updater/CMakeLists.txt` builds a standalone offscreen progress test; CTest
  verifies transfer percentage, verification/preparation and partial status data.
- GUI builds with MSVC/Qt6 and Ninja. The base project's Visual Studio generator
  configuration has an unrelated dependency on a nonexistent greenrhythm_autogen
  target; Ninja is used without changing the shared CMakeLists.txt.
- Core uses `_overlay/prepare.sh`, a local go.overlay.mod replacement for
  C:/libneko, CGO_ENABLED=0 and the normal release tags:
  with_clash_api,with_gvisor,with_quic,with_wireguard,with_utls,with_v2ray_api.

## Limits

No live installation, VPN, proxy setting, route, or real user config was touched.
The full release was not installed or published. This patch does not repair an
already installed old updater until the new components are delivered. Startup
observation checks that the process stays alive for two seconds; it does not
confirm GUI readiness or connectivity. Windows may display a loader dialog while
the process remains alive. Power loss or forced process termination during
replacement has no persistent transaction journal: originals remain in the backup
but recovery is manual, and subsequent installation is blocked. Temporary helper
executables are retained in the temp directory because Windows locks the running
executable. Full Linux/macOS installation behavior was not tested.
