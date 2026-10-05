# Signed GreenRhythm updates

The downloader and standalone updater verify an Ed25519 release manifest against
public keys compiled into `go/release_auth/trusted_keys.json`. A checksum from
the download server alone no longer authorizes installation. Verification
precedes extraction and executable replacement.

The manifest signs `signingKeyId`, `platform`, `versionCode`, `version`, `url`,
`sha256`, `sizeBytes`, and `mandatory`. Changing any of those fields invalidates
the signature. Unknown keys, another platform, missing signatures, malformed
metadata, and mismatched archive bytes are rejected. Release notes and the
publication date are informational and unsigned.

## Signing a release

Keep the Ed25519 PKCS8 PEM private key outside repositories, download servers,
and CI. Restrict access and keep an offline backup. The repository contains only
public keys. Build the final archive first, then copy it to the signing machine:

```sh
cd go/release_auth
go run ./cmd/sign -key /private/release-ed25519-private.pem \
  -archive /releases/GreenRhythm-1.9.0-windows64.zip \
  -platform windows-x64 -version 1.9.0 -version-code 1900
```

Use the actual release version and its increasing code. The existing code scheme
is `major * 1000 + minor * 100 + patch` with `minor <= 9` and `patch <= 99`.
The signer hashes the file, derives the key ID, verifies that the key is pinned,
and creates `<archive>.manifest.json` without overwriting an existing sidecar.
The URL is `https://verdantvibe.ru/downloads/<archive-name>`; preserve the filename
when mirroring. Set `-mandatory` before signing when required: changing that flag
later requires a new signature.

Upload the public `.manifest.json` beside the exact archive as a GitHub release
asset, or import it through the owner's release administration interface. The
distribution service verifies its pinned signature and the mirrored file's size
and hash. It never needs the private key. An unsigned release can still be
downloaded manually, but this client will not automatically install it.

## First protected release and rotation

Existing binaries do not gain this verification merely because the server is
updated. The first release must ship **both** the updated core and updater with
the same pinned key. Distribute that bootstrap through an authenticated owner
release or a separately verified manual installation. Until it is installed,
older clients retain their previous trust model. The legacy `.sha256` sidecar
is retained to support this transition; the new updater requires `.manifest.json`.

This is update authorization, not Windows Authenticode or Apple notarization.
The current Windows ZIP update path is covered. The existing macOS DMG path
requires its own installation validation before claiming an end-to-end Mac update.

For rotation, first ship a release trusted by the current key that pins both the
current and next keys. Then switch the signer and retire the old key in a later
release. Silently replacing the only pinned key prevents installed copies from
updating. Losing the private key requires a separately trusted bootstrap.

## Verification

```sh
(cd go/release_auth && go test ./...)
(cd go/grpc_server && go test ./...)
(cd go/cmd/updater && go test ./...)
```

CI runs these suites and builds the core with its normal dependency overlay.
Tests use disposable keys; those keys are never accepted by production verification.
