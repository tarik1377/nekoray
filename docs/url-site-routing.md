# Saved sites and dependency discovery

Open **Sites** in the left sidebar, paste an HTTP/HTTPS URL, and select
**Analyze and save**. The connected profile stays running. The application
loads the public page twice in disposable Chromium sessions: directly and
through the current VPN profile. It records observed request origins, including
JavaScript, XHR/fetch, stylesheets, iframes, redirects and WebSocket handshakes.
The root and critical dependencies contribute to the suggested path; a root
HTTP 200 alone does not establish that its API works.

The result becomes a saved card, with quick **Auto / Direct / VPN** choices in
the sidebar and a dependency table, rescan action and history. Manual mode
applies to the whole saved set. Auto keeps per-origin availability decisions,
scoped to the network, profile and endpoint policy. It prefers confirmed direct
HTTPS, uses VPN when direct fails, retries expired decisions and invalidates
failed TCP choices for subsequent connections. Existing connections keep their
selected path. The first eight cards have sidebar shortcuts; all cards are on
the Sites page. History is bounded and can restore a prior mode.

## Boundaries

- Discovery requires an installed Edge, Chrome, Chromium or Yandex browser.
  It never uses its existing profile, account, cookies or extensions. The
  window stays headless and does not take the desktop mouse.
- Only the observed public load is discovered. Dependencies behind login,
  interactions, long timers, service-specific challenges or a different page
  may differ. Rescan the relevant URL when needed. This is an availability
  check, not a successful sign-in check. Arbitrary APIs can respond differently
  to anonymous requests; manual mode remains available.
- The entered path, query and SPA fragment are transient and used only for that
  isolated load; only the origin (scheme, exact hostname/IP and port) is saved. No headers,
  response bodies, credentials, full request URLs or cookies are retained.
- No broad CDN wildcard is generated. Shared endpoint conflicts prefer VPN,
  then Direct, then Auto, and the effective mode appears in the table.
- Existing explicit program/site/IP/block rules, private destinations and work
  VPNs retain priority. Advanced domain lists remain unchanged and available
  from the Sites page. The new matcher precedes only the legacy availability
  lists/final fallback. It handles saved TCP web endpoints; it does not change
  game UDP, TUN settings, system proxy, DNS settings or OS routes. HTTP and
  unconfirmed paths use VPN conservatively; manual Direct remains available.
- Saving updates only `site_services`, atomically, with an original-settings
  backup and stale-editor check. If live application fails, the UI says that
  the saved policy is pending. Generic exports contain compatible static rules;
  latency-test configs never start this manager or discovery.

## Isolation and implementation

`sitebook` uses the installed Chromium DevTools Protocol to observe a real page,
not an HTML-link parser. Its authenticated loopback HTTP/CONNECT proxies pin
DNS results to public IPs, reject private/local destinations and bound each
load to 24 connections, 256 requests and 32 MiB per path. An analysis has a
55-second deadline. The browser guardian owns its process group/Windows job and
private temporary profile, cleaning them on cancellation, normal completion or
parent exit. The guardian also has its own deadline. It never kills processes
by name or edits an existing browser profile.

The asynchronous authenticated `SiteTools` RPC is scoped to the active profile
and has a five-second UI deadline. It never invokes Start/Stop or the generic
core-repair path. Applying a card replaces only the dedicated immutable inline
`site-services` matcher and manager policy. A short removed-policy grace period
protects routing already in progress. Cache and decision history are stored in
the application's `.site-services` directory; bounded card history is in the
existing settings file. No external monitoring service is required.

Tests exercise a real installed Chromium against an owned page with CSS, JS,
API, iframe and delayed script requests, an API that fails only on the direct
path, browser cleanup after parent exit, proxy private-address/auth rejection,
the real sing-box router with hot replacement and pinned streams, generated
runtime/export configs, actual sidebar controls, unchanged route/settings data,
and h2c protobuf RPC responsiveness when a core request hangs. Native browser
tests explicitly skip if no supported browser is installed.

Primary references: [Chromium CDP](https://chromedevtools.github.io/devtools-protocol/),
[Network events](https://chromedevtools.github.io/devtools-protocol/tot/Network/),
and [sing-box rule sets](https://sing-box.sagernet.org/configuration/rule-set/).
The runtime overlay targets the repository-pinned sing-box v1.13.18.
