# Drawing sources and title-block ownership

Drawing Settings is the gear button below the sheet, before the Source chooser.
Its reference-style list registers native `.prtz` and `.asmz` files, including
files with no views yet. A family and all its variants share one source file.
Duplicate registration is rejected. Reading the list does not calculate geometry.

The red cross opens an internal confirmation listing the affected views. Removing
a file removes its variants' views on every sheet, dependent projections and
their dimensions and balloons. An unrelated view survives. Source files on disk
are never deleted. The confirmation changes only the pending Settings document;
Settings OK commits one undoable edit, while Cancel discards all pending changes.

Zero sources is valid. The chooser displays disabled “Bez zdroje”, the Tree's
Part/Assembly shortcut is hidden, and view insertion is unavailable. Adding a
file restores source selection without recreating deleted views or title bindings.

Each sheet stores its own selected source, including the exact family variant.
The Tree shortcut and its Part/Assembly icon follow this selection. The reverse
shortcut on Parts and Assemblies carries the Drawing icon. Source selection sets
the source for new views; existing independent views keep their own source.

A title block captures the selected source and variant when inserted. Its model
metadata and BOM retain that source after chooser changes. Removing the bound
file clears the title/BOM binding and dependent source values, while retaining
the title geometry and local text. It never silently rebinds to another file.
No separate title-source editor is introduced in this iteration.

The source registry and sheet selection are stored in `.drwz` (INI 19, payload
11). The immediately preceding Drawing schema (INI 18, payload 10) is read using
its existing source/view records and title binding. Part/Assembly schemas are
unchanged. There are no required sidecars or additional geometry files.

The start Part and Assembly templates use lowercase Czech parameter labels
without diacritics. The standard `ZE-RAZITKO.tblz` expressions use the matching
labels; other languages and stable internal parameter keys are unchanged.

Verification on 2026-09-19: all ten targeted native-document, Drawing, Family
Table and template contracts pass in `build/drawing-settings-acceptance-tests.log`.
The GUI checks include missing source files, zero-source recovery, separate
title/chooser persistence, confirmation/Settings cancellation, Undo/Redo,
navigation icons and creation from both start templates. Broader unrelated
failures are recorded explicitly in `SESSION_HANDOFF.md`.
