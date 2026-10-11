# Native file dialog icon ownership — 2026-10-11

The user reported that Open and the library FORM picker became unresponsive
after opening the file selector. The custom file icon provider rendered shared
palette-aware SVG/QPixmap icons from QFileSystemModel's gatherer thread. The
provider was also stack-owned and declared after the dialog, so its lifetime
ended before destruction of the model/gatherer that retained its address.

The model now keeps its own standard provider. The existing proxy returns the
application document icons during GUI-thread DecorationRole painting, using
the model's cached QFileInfo. This removes the cross-thread shared icon cache
access and the dangling provider lifetime. Sorting, filters, names, file type
choices and the shared non-native dialog remain on their existing paths.
No new user-visible text or platform-specific modeling path is introduced.

The dedicated Windows contract creates 512 native file fixtures and an
uppercase Part extension, repeatedly opens the selector six times, waits two
seconds while its heartbeat runs, alternates OK/Cancel and checks
Light → Dark → Light icons. It passed in approximately 12.5 seconds. The FORM
GUI tests exercise the same chooser for insertion and replacement. These are
finite regression checks, not proof that every possible file-dialog stall has
the same cause. Repeat the contract on the native Linux host after reboot.
