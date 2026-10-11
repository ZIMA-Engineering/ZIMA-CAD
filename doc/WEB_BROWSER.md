# Integrated web browser

The browser uses Qt WebEngine in ordinary workspace tabs. The globe button next
to Global Settings opens another web tab. Closing a browser tab does not close a
CAD document. Website popups open additional web tabs and share the browser
profile. CAD commands remain unavailable while a browser tab is active.

The desktop selects the OpenGL Qt Quick graphics API before creating the
application. WebEngine's embedded Quick view shares the top-level window with
the CAD OpenGL widgets; mixing Windows' default D3D11 Quick backend with those
widgets can produce a black page and recreate the native window. This follows
the [Qt Quick widget graphics API contract](https://doc.qt.io/qt-6/qquickwidget.html#graphics-api-support).
GPU rendering remains enabled. The browser does not change CAD rendering or
calculation precision.

The navigation bar contains Home, Back, Forward, Reload, the address field and
the bookmark pin, in that order. The left Tree contains bookmark folders and
links. Its gear opens the Browser page in Global Settings; its plus menu adds a
bookmark or folder. The context menu edits or removes items and opens links in
another tab. Removing a folder removes its descendants after confirmation.
Middle-clicking a link opens another tab. Links use their saved favicon when
available and otherwise use the shared globe icon.

## Settings and storage

The initial home page is the official ZIMA-CAD page at
<https://zima-engineering.cz/05-Software/01-ZIMA-CAD/0000-index/index_cs.html>.
Users may choose another home page or leave the address empty for a blank page.
Global Settings chooses an initial width that fits the complete translated tab
strip, subject to the main window's available width. Its shared internal-window
placement and manual resizing behavior remain in use.
Opening a web tab at application startup is optional. The official bookmark is
created only when the bookmark file is first initialized; deleting it is permanent.

Browser preferences belong to the shared application settings. Browser data
belongs to installation-root `config/www/`: `bookmarks.json`, favicon PNGs,
the persistent `profile/`, disposable `cache/`, and `credentials.ini` metadata.
Bookmarks are written atomically under a lock. A second application instance
uses a temporary web profile when the persistent profile is already locked.

Downloads use the current Working Directory at the time the download starts.
An optional preference asks for the destination each time. An existing target
also opens the destination chooser rather than being silently overwritten.

Third-party cookies are blocked by default. Browser settings provide separate
actions to clear cookies and cache. Saved passwords use QtKeychain 0.15.0 and
the operating system credential backend; the INI file contains account metadata,
not passwords. Secrets are not portable between machines. Password saving and
autofill can be disabled independently. The password manager can remove saved
accounts or a site's never-save choice. Form requests must match the current
page origin and form action; password handling requires HTTPS except for local
development hosts. HTTP authentication uses the same system credential store.

## CLI

`browser.open` opens a tab through the desktop command host, with an optional
`url`. The standalone CLI manages bookmarks but cannot display browser tabs.
`browser.bookmarks` lists links and folders. `browser.bookmark.add` takes `name`,
`url` and optional `parent`; `browser.folder.add` takes `name` and optional
`parent`. `browser.bookmark.edit` takes `id` and `name`, with optional `url` and
`parent`; omitted fields retain their previous values. `browser.bookmark.remove`
takes `id` and also removes a folder's descendants. These commands do not expose
password values.

## Implementation and release verification

Windows and Linux use the same browser implementation and pinned QtKeychain
source. The browser has no dependency on CAD modeling algorithms. Qt WebEngine,
WebChannel, its helper process, resources and locales must come from the same
Qt SDK as the application and be included in each native distribution.

`zima_browser_bookmarks_tests` covers bookmark persistence and validation.
`zima_browser_ui_tests` exercises local HTTP navigation, the bookmark Tree,
Working Directory downloads, live theme icons, credential origin validation,
the system credential backend and the internal password-manager window.
The UI test needs a graphical session and an available system credential store.
Linux verification must run on the Linux host; a Windows result does not verify
Linux WebEngine deployment or its credential service.

Windows verification on 2026-10-11 used the complete Qt 6.11.0 SDK. Bookmark and
browser UI contracts passed, including actual password-form autofill, system
credential roundtrip/removal, five-language controls, popup tabs, theme changes,
download contents and internal-dialog confirmation. The main-window contract
passed multiple browser tabs, desktop CLI opening, settings Cancel, closing tabs
and restoring the existing CAD context. These are finite local-fixture checks;
arbitrary enterprise portals and their authentication policies remain unverified.

Signed Windows [2026101108](releases/2026101108.md#signed-windows-publication)
also passed the browser workspace and browser harness against packaged libraries
with a clean runtime environment. A local HTTP fixture verified persistent
cookies across process restart, clearing cookies and second-instance profile
isolation. Linux credential services and packaged WebEngine remain native-host
verification requirements.
