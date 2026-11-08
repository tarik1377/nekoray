# Website availability routing

The application now has **Settings → Умный выбор пути для сайтов → Настроить** and a second entry in **Connection → Помощь браузеру → Умный выбор пути для сайтов…**. This extends availability selection to domains in browser traffic; it is separate from the existing Player/Studio process policy. It does not enable itself on update.

Paste a whole `domain:`/`full:` list into Auto, Direct or VPN. **Добавить Roblox и CDN в «Авто»** adds the supplied Roblox catalog in one click: suffix roots cover the API/edge subdomains, and shared CDN hosts stay exact. Repeated entries are deduplicated; a duplicate in conflicting modes blocks the whole save with an error. Specific child-host exceptions precede parent suffixes inside this new set. There is a 512-domain limit. Unicode names become IDNA ASCII; URLs, regexps, IPs, local-name suffixes, whole shared providers and malformed names are rejected.

**Выбрать для всего набора** moves the whole draft to Auto, Direct or VPN in one action, avoiding per-domain edits. This intentionally replaces per-host modes inside this new set; nothing is saved until Save.

The opt-in schema-1 `site_routing` setting is saved atomically with an exact pre-save backup and a stale-editor guard. It preserves every other preference and the route scheme. Save never reconnects, edits a browser/extension, changes DNS, modifies OS routes, or moves an established connection. The next profile connection loads it. Viewing, adding a preset to a draft and cancelling cause no write.

## Scope and precedence

Configured entries match site domains, not all traffic from `browser.exe`. HTTP CONNECT supplies the host when using the system proxy; TUN traffic also needs the existing domain recognition/DNS path. A browser extension selecting its own external proxy can bypass GreenRhythm. Browser-wide process overrides must be removed explicitly with **Помощь браузеру → По правилам сайтов**; this editor never silently removes them.

Existing explicit process rules, scheme rules, global rules, domain/IP lists, DNS interception, private/work-VPN handling and built-in protocol exclusions retain precedence. The new set is inserted before only the final compatible application/default fallback. Consequently an earlier explicit route or block can prevent an availability check; the dialog explains that no decision is expected for such a connection. Unselected sites and all saved old routes remain unchanged. Direct/VPN site modes apply to all protocols when that site is reached in this position.

Auto is specifically HTTPS over TCP 443. It does not infer HTTP-port, QUIC, voice, game UDP or authenticated-session health from a successful HTTPS probe. QUIC/UDP keeps the existing scheme. Player/Studio's existing adaptive UDP learner remains independently available and unchanged. A generic sing-box export uses a compatible VPN fallback for Auto site entries and contains no GreenRhythm-only outbound. Latency/diagnostic test builds do not start site availability probing.

## Decisions and limits

The new `greenrhythm-site-auto` outbound reuses the existing bounded availability engine through the configured direct/VPN detours. Only opted-in hosts can generate probes. It uses strict TLS, a fixed anonymous public GET, no cookies/Authorization/application request URL, no redirects and bounded response reads. Known Roblox user/version endpoints retain content validation. HTTP 403/429 are ambiguous rather than proof of filtering. For arbitrary websites, an anonymous HTTPS response proves network/service reachability, not successful account access or correct page behavior. Geo-restricted accounts and sites with browser challenges may need the manual VPN mode.

Confirmed DIRECT is preferred; VPN is selected when direct fails or stays ambiguous and VPN is confirmed. If neither is confirmed the result is marked unconfirmed, with a short retry rather than a fabricated success. Successful decisions live for ten minutes, ambiguous decisions for thirty seconds; returning from a working VPN to DIRECT requires two successful recovery checks. At most two services/four HTTPS requests are checked concurrently, with twelve service checks per minute and one in-flight check per host. Idle applications cause no background sweep. When the budget is exhausted an unknown host uses VPN; it is not reported as verified.

The `.site-auto` cache is separate from `.roblox-auto`, bounded, atomic, scoped to the profile/catalog and network, and restores only still-valid configured hosts. The dialog shows bounded last saved decisions with time and explicitly identifies that they can belong to an earlier connection. Actual first-response/connect failures invalidate a cached choice for the next connection. Actual application data is never replayed, duplicated, transparently retried or moved to a different route. Connection accounting resolves selection before snapshotting, showing the selected direct/VPN tag.

## Validation

`site_routing_test` exercises real draft/preset/Save/Cancel controls, validation, shared-CDN boundaries, specificity, stale saves, exact backups, future-schema protection, footer accessibility and preservation of unrelated fields. `import_integration_test` builds actual runtime/export/latency configurations. Go tests use owned loopback HTTPS/echo fixtures for strict TLS, no account replay/redirects, cache catalog/network isolation, real selected detours and flow pinning. Existing Roblox engine, concurrency, cancellation, race, UDP and route tests continue to run. No test requires modifying a user's current connection or browser account. Real authenticated Roblox/ChatGPT browsing is a separate end-to-end check and is not claimed by these fixtures.
