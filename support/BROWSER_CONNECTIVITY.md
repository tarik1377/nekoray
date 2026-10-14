# Browser connectivity diagnostics

The connection page's **Помощь браузеру** opens a read-only comparison of a
user-entered HTTP(S) URL. It runs one request with the Windows system proxy
selection and another with the active GreenRhythm HTTP proxy. The latter keeps
the destination hostname on the proxy path. Each request has a total deadline
of four seconds, supports cancellation, and requests only HTTP headers (`HEAD`).
Some sites handle HEAD differently from a browser GET. HTTP 401 or 403 confirms
that the diagnostic request received a server response; it does not prove the
user's browser session, login or account is working. Redirects are reported and
not followed. Responses, request/response headers and account cookies are not
written to logs or disk. URL credentials are rejected. Any display of a checked
URL must use `maskedWebCheckUrl`, which removes the path, query and fragment.

DNS/TCP/TLS/deadline failures have separate results. Certificate trust, hostname
and validity errors are distinguished without ignoring TLS errors or installing
certificates. A local proxy cannot fix an untrusted corporate CA.

On Windows, hints read only `dns_over_https.mode` and the presence of configured
templates from Yandex/Chrome/Edge **Local State** files. They do not open profiles,
cookies, sessions, passwords or browser account preferences. Secure browser DNS
can differ from Windows/core DNS and may not resolve corporate VPN resources.
The actual browser DNS/extension/session request path is not reproduced.

**Подключить браузеры** emits a controller request to enable the existing
GreenRhythm system proxy mode. The dialog does not edit Windows or browser
settings itself. It is enabled only with a running GreenRhythm profile on
Windows. **Оставить как есть** closes the dialog without an apply request.
Browser secure DNS, cookies and authentication remain unchanged.

`browser_network_hints_test` uses local TCP fixtures to test real HTTP statuses,
deadlines, cancellation/retry, proxy hostname preservation and strict HTTPS
failure. It also checks DNS-hint redaction, URL validation and UI action signals
without screenshots or changes to browser/Windows/VPN settings.
