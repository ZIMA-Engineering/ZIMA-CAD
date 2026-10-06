# Desktop integration

## Agreed direction (2026-09-27)

The user approved one shared desktop-integration service, offered on first
launch and available later in Settings. A future setup program may call that
same service. Do not maintain independent registration implementations for
setup and the application.

Supported desktop targets are Windows, KDE Plasma and GNOME. Other Linux
desktop environments are outside the supported scope. The native implementation
is in `cpp/app/desktop_integration.cpp` and shipped in Windows build 2026092801.
Linux desktop acceptance remains required on Linux.

## User interaction

First launch checks the existing integration and offers the relevant changes
before applying them. Respect a declined offer and existing default-app choices;
do not repeatedly prompt or silently take over associations. Settings provides
registration, repair and removal. Portable operation without registration
remains available.

Integration covers the application launcher, supported native document types,
application icons and document icons. Prefer current-user registration without
requiring administrator privileges. Registration must point to the stable
installation-root launcher, so selecting another installed version does not
leave associations pointing at a retired version directory. Account for moved
installations and multiple copies; remove only integration owned by the selected
installation and preserve unrelated associations and user files.

## Platform boundaries

`tools/register-windows-file-types.ps1` delegates to the native service used by
Settings. It supports `-Action register`, `repair`, `remove` and `status`.
The application exposes the same operations as
`--desktop-integration=register|repair|remove|status`; status exits with 0 for
complete registration, 1 for missing/incomplete registration, and 2 for errors.
These operations do not open or change CAD documents.

Tools > System Setup and Global Settings contain the same Desktop integration
page. Select an operation and confirm
with OK; Cancel leaves registration unchanged. A new unconfigured installed
root offers System Setup once, including optional registration. Completing or
skipping setup is persisted outside version directories; updates preserve that
choice. See [System Setup](SYSTEM_SETUP.md). Development builds expose both
menu commands without an automatic offer and register their current executable.

Installed runtimes resolve their installation using the existing validated
runtime layout and register `ZIMA-CAD.exe` or `ZIMA-CAD.sh` in its root.
The launcher path determines a separate registration identity for each copy.
Version changes within that root retain the same identity. Moving a copy gives
it a new identity and a fresh offer; entries for the previous location are not
silently taken over. Remove a registration from its original location before
moving or deleting that installation.

Windows writes only current-user application capabilities, per-installation
ProgIDs and OpenWith entries, document icons and a Start menu shortcut.
It does not assign extension defaults or change protected UserChoice entries.
The Default applications button opens the Windows settings page. Removal
checks the stored owner before removing registration records, and checks the
shortcut target before deleting it. Existing registrations from the former
standalone script are not silently removed.

Linux writes an application desktop entry, a shared-MIME-info package and SVG
icons under the user's XDG data directory, then refreshes the MIME and desktop
databases using `update-mime-database` and `update-desktop-database`.
These tools must be installed on the Linux host. No `mimeapps.list` defaults
are overwritten. Common document MIME icons are retained on removal because
another installation can still use them. User-authored documents are never
part of registration or removal.

The six native extensions are `.prtz`, `.asmz`, `.drwz`, `.frmz`, `.tblz` and `.symz`.
Document icons reuse the application's existing Part, Assembly, Drawing,
Format, Title Block and Symbol assets. Windows ICO files embed a 256-pixel PNG and
Linux uses the original SVG assets. Icons are derived user-local resources,
not native-document storage dependencies.

Windows registration and the user's default-app selection are distinct.
Register supported handlers and icons using supported mechanisms. When the
user wants to change the default handler, open the Windows Default Apps UI;
do not write protected default-app choices directly.
See [Microsoft's default-app platform guidance](https://learn.microsoft.com/en-us/windows/apps/develop/windows-integration/default-apps-platform).

Linux should share the freedesktop desktop-entry, MIME-association and icon
mechanisms, with actual verification in both KDE Plasma and GNOME. The
[Desktop Entry specification](https://specifications.freedesktop.org/desktop-entry/latest-single/)
and [MIME Applications specification](https://specifications.freedesktop.org/mime-apps/latest-single/)
define the common integration layer. Complete Linux execution and acceptance on
the Linux host; Windows tests cannot establish Linux desktop behavior.

## Acceptance for implementation

Windows development verification on 2026-09-27 passed the native GUI build,
the focused CTest contracts (desktop integration, translation coverage and
construction reference Tree), and a separate native-registry run. The registry
run used only `HKCU\Software\ZIMA-CAD-Tests\<temporary-id>` and temporary
shortcuts/assets, then removed the test records. It covered registration,
damaged-command repair, removal, foreign-owner protection, two installation
identities, preserved existing defaults, Unicode/spaced launcher paths, and
Windows loading all five ICO files. Five-language page checks and the real
Settings page/Cancel GUI check passed. First-offer Cancel and declining without
repeat were also checked. The existing registration wrapper passed `-WhatIf`.

These checks do not establish file-manager/default-application acceptance on a
real installed release. No actual user associations were changed by verification.
KDE/GNOME execution and file-manager acceptance remain outstanding as described
in [the Linux handoff](LINUX_RELEASE_HANDOFF.md).

The 2026-09-28 follow-up added the missing `.symz` Symbol handler and icon to
the shared registration list and updated all five translations. The native
Windows application and desktop-integration test targets built successfully.
The translation and desktop-integration CTest contracts passed, followed by
the separate `--native-registry` test. These checks covered all six ICO files,
the Symbol capability mapping, detection and repair of a missing Symbol icon,
and the existing ownership, removal and default-preservation behavior.

A read-only inspection of the development machine found older generic
`ZIMA.CAD.*` handlers for the five previously registered extensions. Their
`DefaultIcon` entries all pointed to the application executable, explaining
the shared application icon instead of distinct document icons. `.symz` had
no handler, and the per-installation registration was absent. This audit did
not change the machine's actual registrations or defaults. Register the
current installation from Desktop integration in Settings, then select its
handler through Windows Default applications where necessary. Registering a
new handler alone deliberately does not replace an existing default handler.

The subsequent requested repair registered the current development installation,
replaced the old handlers' shared executable icons with their document icons,
and pointed the existing ZIMA-CAD extension entries at this installation's
handlers after verifying their executable ownership and absence of UserChoice.
The previously unassigned `.symz` extension received its registered Symbol
handler. Original legacy icon and extension values were backed up locally before
the repair; no protected UserChoice entries were written.

External startup also needed `.symz` in its accepted argument extensions. The
startup contract now checks all six extensions, including uppercase and Unicode
paths, and opens actual Frame, Title Block and Symbol copies in GUI processes
in addition to the existing Part, Assembly and Drawing cases. The Windows build
and expanded `zima_cpp_instance_startup_contract` passed on 2026-09-28. No new
user-visible strings were introduced by this argument-parser correction.

A real Windows Shell open successfully launched the requested `.symz` document;
the document tab and normal process exit were verified. The other five extensions
still resolved to Windows `OpenWith.exe` through `AssocQueryString`, despite
valid registered ZIMA-CAD commands. Their final default-app selection therefore
remains pending in Windows Settings. Do not report successful double-click or
Explorer icon acceptance for these five extensions until that choice and the
shell checks have completed. The Windows UI automation runtime could not start
because its sandbox failed to apply deny-read ACLs, including after a reset.

Verify registration, repair, removal and the unregistered portable path.
Exercise opening each supported native file type from the file manager, paths
with spaces and Unicode, correct application/document icons, version updates,
relocated installations and existing user-selected defaults. Verify KDE Plasma
and GNOME separately and record desktop/session versions. New UI text must be
localized in all five supported application languages.
