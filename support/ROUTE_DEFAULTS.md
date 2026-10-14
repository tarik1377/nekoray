# Review bundled route defaults

Existing routing schemes are never upgraded during startup. DNS, media, QUIC
and game migrations now wait for the user's choice in Routes. Fresh installs
still start with the bundled `Routing(1)` preset. The separately requested ads
selector repair and explicit System stack selection remain independent.

The compact panel offers **Apply standard routes** and **Keep mine**. It shows
the bundled policy revision and the number of changed fields/added/removed
rules. Keeping rules commits only the review decision and never reloads a core.
Increment `RouteDefaultsPolicy::Revision` when a bundled policy needs review;
a decision from a newer version is not invalidated by a downgrade.

The main Settings page also exposes **Стандартные маршруты…**, **Мои правила…**
and, when a matching backup is available, **Вернуть прежние правила…** at the
top of the page. The first action opens the same guarded preview directly.
Its **Оставить как есть** button closes the preview without saving a review
decision, changing route priorities or restarting the connection. Enter defaults
to that button. Changed fields have readable labels; a preview with no changes
cannot be applied. Full reset requires an explicit acknowledgement, including
after switching between modes. Merely viewing Settings is read-only.

Apply opens a preview with two clearly different modes:

* **Safe update**, selected initially, retains custom process/domain/IP rules,
  their precedence, custom DNS objects/servers, scoped work DNS/hosts and
  unknown fields. It repairs only the complete inherited RU DNS tuple, applies
  compatible media defaults using the existing conflict checks, and appends
  missing QUIC guards after existing choices. Unrecognised schemes and custom
  rule JSON are not guessed. Standard game bypass rules never replace an
  explicitly selected game proxy rule in this mode.
* **Full reset of this scheme's routes** replaces its route lists, process
  rules and public DNS fields with the current `Routing(1)` preset. The user
  must additionally acknowledge that replacement. Scoped work DNS/hosts,
  unknown fields and global routing rules remain intact.

Only clicking Apply commits the prepared change. It updates active memory
after the disk transaction and uses the application's usual route-change
reload prompt for an existing connection. Merely opening preview, cancelling
it, choosing Keep or installing an update does not interrupt a connection.
The preview includes unsaved changes visible in the route editor; its backup
always captures the previously saved scheme.
Empty editor fields explicitly clear their saved values. Unsaved global-rule
changes must first be saved with OK before Apply or Restore can proceed.

Backups contain route JSON only, never profiles or subscriptions. They live in
`config/route_backups` outside `routes_box`, so they cannot appear as selectable
schemes. The scheme and settings marker use atomic `QSaveFile` replacements;
the settings marker is committed last. A failed marker replacement rolls the
scheme back to its exact previous bytes. Failed operations leave review pending.
Two separate files are not a power-loss-atomic transaction: after interruption
between commits, the immutable backup is retained for manual recovery.

The small **Return previous rules** link restores the last committed backup
byte-for-byte, including unknown fields, user process rules and formatting.
It refuses to overwrite later edits or load a backup for another scheme.
Restore failures also roll back; unlocking the affected file permits retry.
Active memory is rebuilt from the saved scheme's empty schema, including after
rollback failures. Startup uses that same empty schema for existing files;
omitted fields cannot silently reintroduce the bundled preset after a restart.

The policy reads the selected scheme and its settings file only. It does not
edit network interfaces, OS routes, system proxy, subscriptions or running
cores. A disconnected corporate VPN remains a separate network failure.
