# Update notifications

The existing signed website manifest remains the only source of update availability.
The main window checks after one minute, then once a day. A failed check schedules a
five-minute retry, remains quiet when automatic, and records a failed attempt in
Settings. Failure does not erase a previously discovered version or claim that the
installed version is current.

The banner sits outside the page stack. Its update action uses the existing verified
downloader and installation confirmation; its download action opens the fixed
`https://verdantvibe.ru/apps` page. The same download entry is always in the More menu,
including when the version API is unavailable. Neither action reconnects or changes
routes. Installation still requires the user's confirmation and restart.

Closing the banner postpones that version for this window's lifetime. The sidebar
update action remains available. A different version or a new application launch
shows the banner again. No preferences or routes are written by dismissing it.

Release notes are informational, not part of the signed manifest. Render at most
16,000 characters in a read-only plain-text editor: do not interpret HTML, Markdown
images, scripts or remote links. The official website button always uses the compiled
service URL rather than a URL from release notes.

A per-window operation lease spans checking, queued dialogs and download workers.
All entry points share it; the lease releases only the matching generation on the GUI
thread. Retry starts a new generation, so a late callback cannot enable controls in
the middle of its replacement operation. Closing a window invalidates weak callbacks
without stopping the VPN. This is covered by the extracted real CheckUpdate method
in update_actions_test and by shell interaction tests in settings_page_test.
