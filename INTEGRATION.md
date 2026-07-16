# GreenRhythm integration — `greenrhythm://` one-click import

GreenRhythm registers a custom URL scheme so a website or Telegram bot can hand a
subscription (or a single profile) to the desktop client with one click. GreenRhythm
stays a universal VPN client — this is an optional convenience, not a lock-in.

> Windows x64. Scheme registration is per-user (`HKCU\Software\Classes`), applied
> on every start (self-heals if the executable moves), no administrator rights.

## URL contract

```
greenrhythm://import/<percent-encoded-payload>
```

`<percent-encoded-payload>` is a single percent-encoded value that is **either**:

- an `https://` subscription link (3x-ui style, returns base64 of `vless://` lines), or
- a single `vless://` profile link.

Only `https://` and `vless://` payloads are accepted. Anything else (`file://`,
`javascript:`, `http://`, plain text, …) is rejected with a polite message and nothing
is imported.

### Examples

Subscription link `https://51.250.99.2:2096/suber/abc123`:

```
greenrhythm://import/https%3A%2F%2F51.250.99.2%3A2096%2Fsuber%2Fabc123
```

Single profile:

```
greenrhythm://import/vless%3A%2F%2F<uuid>%40<ip>%3A443%3Fsecurity%3Dreality%26...%23sub_42
```

Generate the link by percent-encoding the whole payload once (e.g. JS
`encodeURIComponent(payload)`), then prefixing `greenrhythm://import/`.

## Behaviour

- **Client closed** → it launches and processes the link.
- **Client open** → the link is forwarded to the running instance over the existing
  single-instance channel; no second window opens.
- **`https://` payload** → imported as a subscription group named **«Зелёный Ритм»**
  and updated immediately. Re-importing the same URL updates that group instead of
  creating a duplicate. On success GreenRhythm asks **«Подписка добавлена (профилей: N).
  Подключиться сейчас?»**; if the update returns zero profiles it shows a renew hint
  («Подписка недоступна — возможно, срок истёк») instead.
- **`vless://` payload** → imported as a single one-off profile, then **«Профиль
  добавлен. Подключиться сейчас?»**.
- Both confirmation dialogs show the payload's source host first, and GreenRhythm
  **never auto-connects** — connecting is always a separate explicit click.
- Node certificates may be self-signed; subscription import does not fail on TLS
  verification (the client's existing "insecure subscription" path handles this).

## Security

The payload is untrusted input and is validated before use:

- percent-decoded exactly **once** (a double-encoded scheme fails the allow-list);
- scheme allow-list: only `https://` and `vless://`;
- length limit **8 KB** (measured in bytes);
- control characters and Unicode bidi / zero-width format characters are rejected,
  so the source host shown for confirmation cannot be visually spoofed (RTLO etc.);
- never passed to a shell;
- the import dialog shows the source host so the user can confirm it.

## Release asset

The GreenRhythm site pulls the latest Windows build via the GitHub `releases/latest`
API. Every release keeps a stable asset name:

```
GreenRhythm-v<tag>-windows-x64.zip
```
