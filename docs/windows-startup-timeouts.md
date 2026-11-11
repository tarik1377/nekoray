# Windows startup deadlines

GreenRhythm 2.1.5 used two independent 15-second deadlines: waiting for the core
to announce RPC readiness, and waiting for `Start` to initialize a profile. The
first deadline killed the core and discarded the queued profile. The second
aborted the RPC, then restarted the core while TUN creation could still be running.

Wintun itself can wait up to 15 seconds for its device interface, in addition to
driver/device creation and configuration. A 15-second outer deadline can prevent
the native operation from finishing or reporting its own error:
<https://github.com/WireGuard/wintun/blob/0.14.1/api/adapter.c#L414-L478>.

Both startup deadlines are now 60 seconds. They remain finite; Stop retains its
5-second default, and explicitly supplied RPC deadlines still take precedence.
On Windows, the paired `create adapter` / `open existing adapter` failure gets
one recovery request in the same core after the native operation returns. The
same name was reproduced after killing a disposable test core: native failure
after 15.167 seconds, then successful Start in the same core after 241 ms.
The classifier recognizes the paired native create/open failure stages, not
localized OS error text. It cannot reliably distinguish every underlying cause
at the string-only RPC boundary; a persistent paired permission error is also
returned after the single bounded recovery. Unpaired TUN errors and non-Windows
starts do not get this recovery. Earlier port retries do not consume its budget. The existing
three-attempt port limit still bounds the whole loop, and lifecycle cancellation
and core-generation checks run before each attempt. Success is published only
after a successful Start response.

On 2026-11-11, a real Windows sign-in still exceeded the 60-second readiness
limit, before any version output or RPC listener was observed. The GUI discarded
the queued profile and required a manual restart. Windows now gets one automatic
core replacement when this deadline expires with a connection still queued.
The original profile remains queued and only a successful readiness event releases
it. The replacement's deadline reports the normal final failure; the recovery
budget is one per CoreProcess lifetime, so neither a crash nor a manual restart
can replenish it indefinitely. Missing executables and cancelled connections do
not get this recovery, and application exit suppresses the watchdog entirely.
Retiring-process output cannot release the queue while `waitForFinished` drains
its pipes; a failed stop prevents a replacement from starting over the old core.
The deadline remains 60 seconds per process. Fixed startup milestones record
process creation, first stdout/stderr and readiness durations without logging
environment values, authentication tokens or additional raw stderr.

This handles a demonstrated transient startup failure; it does not identify the
Windows component responsible for the pre-readiness delay. Event logs did not
show a corresponding Defender/Code Integrity block. A version-only child took
12.366 seconds initially and 90/62 ms on two subsequent runs; this is consistent
with a load-time delay but is not proof that antivirus or disk I/O caused it.

The shell's last-resort watchdog covers the bounded startup budgets so it cannot
report failure before a valid slow request or its recovery completes.

Regression checks use a core that announces readiness after 16 seconds and a
loopback HTTP/2 peer that answers Start after 16 seconds. Both fail with the old
deadlines. A silent core must still time out, clear its queued profile, and avoid
an endless restart loop. RPC fixtures cover successful stale-adapter recovery,
its one-retry limit, and unrelated failures. The test peers create no VPN interfaces or external
network connections.

With the repository's normal Qt build configured:

```powershell
cmake --build build --target core_process_lifecycle_test rpc_deadline_test connection_lifecycle_test --parallel 2
ctest --test-dir build -R '^(core_process_lifecycle_test|rpc_deadline_test|connection_lifecycle_test)$' --output-on-failure
```

Passing these tests proves the deadline behavior, not the exact cause of an
individual Windows device-installation delay. Validate actual sign-in startup
on the affected computer before claiming that the reported boot failure is fixed.
