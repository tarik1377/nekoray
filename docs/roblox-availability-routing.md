# Roblox availability routing

Windows internal TUN has a new, explicitly selected **Авто: выбирать рабочий путь** option for Player and Studio independently. Existing `smart` choices keep their previous TCP VPN / UDP DIRECT behavior and are labelled accordingly. Forced DIRECT, forced VPN and existing site/IP rules remain available.

## Scope and precedence

`roblox_auto_processes` is a validated list containing only `RobloxPlayerBeta.exe` and/or `RobloxStudioBeta.exe`. Its schema-1 `game_app_rules` entries retain a full-VPN fallback for older clients. Choosing another mode removes only the selected application's adaptive marker. The existing atomic preference transaction, original-file backup and stale-editor guard also cover the new marker. No factory routes, browser rules, DNS options, OS routes, game preferences or subscriptions are migrated.

Only opted-in processes receive the `roblox-auto` outbound. Normal and whitelist TUN use the same explicit selection. A system HTTP proxy alone cannot capture a program's UDP. Roblox web traffic from an unrelated browser continues to follow that browser's existing scheme.

Standard sing-box exports use the compatible full-VPN application fallback; they do not contain a GreenRhythm-only outbound type.

## HTTPS decisions

For a recognized service on TCP 443, the outbound compares direct and VPN HTTPS using the existing detours. The built-in catalog covers the supplied Roblox suffix roots and exact shared-CDN names; it never matches all of CloudFront, AWS, Arkose or Cloudflare. Unknown plugin hosts retain the previous VPN fallback. Local Studio addresses go directly and are not probed.

Probes use a fixed public GET, strict TLS verification, no cookies, tokens or captured application URLs, no redirects and at most 4 KiB of response data. Known user/version endpoints have content validation. Other services require a successful HTTPS response or a recognizable JSON API error. 403/429 remain ambiguous, not proof of censorship. Authenticated asset permissions, CAPTCHA and login success are outside this availability check.

DIRECT is preferred when confirmed. A confirmed VPN route returns to DIRECT after two successful recovery checks while VPN also works; if VPN itself fails, a confirmed direct route can be used immediately. Failed socket establishment or a new stream failing to obtain its first server response invalidates the cached choice for a subsequent new connection. Both paths failing produces an unconfirmed result, not a fabricated successful route. Actual application connections are never replayed on an alternate path.

At most two services are checked concurrently (four HTTPS probes), one flight per service, with a global twelve-service-check budget per minute. Successful decisions live for ten minutes; ambiguous results retry after thirty seconds when new traffic needs them. Stale successful decisions can serve new connections during rechecks. An initial uncached decision has a three-second bound. No periodic service scans run while the applications are idle.

## Actual-destination UDP feedback

UDP starts directly and observes replies from the actual destination, not an unrelated STUN service. Four seconds with sent data but no reply, or a failed send, records one failed attempt. Two separate failed flows allow the next flow to try the other detour. A response confirms that route. If neither route gets a reply, the status stays unconfirmed. Delayed feedback from an older flow does not overturn a newer confirmed path.

Payloads are neither inspected nor duplicated. Existing sockets remain pinned for their lifetime; only a new flow can use a new choice. There is consequently no transparent rescue of an already-established, unresponsive UDP session: the game's next attempt learns the alternative. A one-way UDP stream cannot prove availability and is reported as unconfirmed.

The pinned choice is resolved before the router's immutable accounting snapshot, so the connection table shows the actual selected direct/VPN tag. The small routing hook is limited to outbounds implementing `SelectConnection`; all other outbounds retain their upstream path.

## Local cache and status

Decisions are stored under the configuration directory's `.roblox-auto` folder. Scope is a SHA-256 fingerprint of the profile outbounds; network identity also covers interfaces/addresses, default-interface information and any selected profile-failover member. A changed scope/network cannot reuse another result. Old in-flight observations are fenced out. Entries expire, are bounded to 128, and only unexpired verified choices are restored. No credentials, request bodies or tokens are persisted. Writes are atomic and throttled; cache failure leaves in-memory routing usable. The UI shows bounded, plain-text last results and their time.

## Validation

Owned loopback fixtures exercise route selection, strict TLS, recognizable responses, 403/429 ambiguity, cache isolation/expiry/corruption, concurrency, cancellation, UDP replies/failures, stale feedback, no duplication and live-flow pinning. A real router test checks accounting after adaptive selection. C++ tests exercise the real controls, atomic save/backup/stale detection, and generated ordinary/whitelist runtime configuration. CI also runs the existing platform builds and Windows TUN regressions; new feature fixtures do not create a TUN, alter OS routes, load a subscription or authenticate an account.

Upstream reference: [URLTest](https://sing-box.sagernet.org/configuration/outbound/urltest/) evaluates a configured URL; it does not establish per-service Roblox or actual-destination UDP availability. This outbound implements those separate checks and never interrupts existing connections to change a result.
