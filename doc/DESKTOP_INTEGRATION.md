# Desktop integration

## Agreed direction (2026-09-27)

The user approved one shared desktop-integration service, offered on first
launch and available later in Settings. A future setup program may call that
same service. Do not maintain independent registration implementations for
setup and the application.

Supported desktop targets are Windows, KDE Plasma and GNOME. Other Linux
desktop environments are outside the supported scope. This document records
the agreed follow-up; the complete workflow is not yet implemented or verified
and is not part of Windows build 2026092701.

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

Windows already has `tools/register-windows-file-types.ps1` for current-user
registration of `.prtz`, `.asmz`, `.drwz`, `.frmz` and `.tblz`. It currently points
at a selected executable. Trace and reuse its supported type definitions while
adding the stable-launcher and application workflow; its existence does not
establish completion of this design.

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

Verify registration, repair, removal and the unregistered portable path.
Exercise opening each supported native file type from the file manager, paths
with spaces and Unicode, correct application/document icons, version updates,
relocated installations and existing user-selected defaults. Verify KDE Plasma
and GNOME separately and record desktop/session versions. New UI text must be
localized in all five supported application languages.
