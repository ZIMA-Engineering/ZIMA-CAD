# File Manager

ZIMA-CAD provides a native File Manager as a top-level application tab. Each
File Manager tab keeps its own selected source, current directory, navigation
history, view, filters and checked items. Opening another directory in a new tab
does not change an existing File Manager tab.

The navigation row starts with Home, followed by Back, Forward, Up, the current
path, the name filter and a direct link to File Manager settings. Home always
opens the current ZIMA-CAD working directory. File > Open and the main Open
toolbar action open a File Manager tab; native CAD files open from its Overview
or Files view.

## Data sources

The left side follows the source-oriented navigation used by ZIMA-Parts. The
working directory is a fixed first source and is not part of the configurable
source list. Each additional source has a persistent header and its own folder
tree, so the active source remains visible even when the user navigates deeply.

Global Settings > File Manager controls source order, enabled state, path and
read-only state. Factory sources are provided for drawing formats, materials,
symbols, templates, sheet-metal assets, native ZIMA-CAD library content and
engineering tables. Custom local or network sources can be added. Factory
source names follow the application language; their paths remain configurable.

`config/lib/02-NATIVE` is reserved for native reusable engineering content such
as fasteners, gears and keyway definitions. `config/lib/03-ENGINEERING-TABLES`
is reserved for the reviewed engineering-table library. Legacy Pro/ENGINEER CAD
documents are not copied into that library.

## Views

The right side has three views:

- **Project** is shown only for a directory identified as a project. It displays
  the project number, localized title and description, customer, main image and
  optional project or company logo. A click-based editor writes version 3
  metadata without requiring the user to edit INI or HTML.
- **Overview** automatically builds image-backed tiles from directory contents,
  project images and `0000-index/thumbnails`. It does not render `index_*.html`.
- **Files** lists folders and files with checkboxes, metadata parameter columns,
  a name filter and ASMZ, PRTZ, DRWZ and version filters. Version 3 metadata
  cells and native Part or Assembly parameters can be edited in writable
  sources. Copy, Move and Delete operate on the checked rows.

The older `index_*.html` files remain untouched. A legacy directory is detected
as a project when `0000-index` contains both an index HTML file and a `01.*`
image, but its project fields remain read-only until a controlled metadata
conversion is implemented.

## Metadata version 3

File Manager metadata is stored in `0000-index/metadata.ini`. It is optional
supplementary directory data; native `.prtz`, `.asmz` and `.drwz` files remain
the authoritative storage for CAD geometry and document parameters.

```ini
[Directory]
Version=3
Kind=project
Id=6c8a563c-3ed1-4be5-8c28-76d802f5e858
Label\cs=Ukázkový projekt
Label\en=Example project
Parameters=material,note

[Project]
Number=DEMO-001
Customer=Example customer
Title\cs=Ukázkový projekt
Title\en=Example project
Description\cs=Popis projektu
Description\en=Project description
Preview=01.png
Logo=logo.svg

[Parameters]
material\Label\cs=Materiál
material\Label\en=Material
note\Label\cs=Poznámka
note\Label\en=Note

[Parts]
BRACKET\material=S355
BRACKET\note=Approved

[Files]
BRACKET\Provider=zima-cad
BRACKET\Path=BRACKET.prtz
BRACKET\ModifiedUtc=2026-10-12T10:00:00.000Z
```

`Directory/Kind=project` is the canonical project marker. Localized values use
the requested application language with `cs`, `en`, `de`, `fr` and `ru`
fallbacks. File rows use a stable logical name without the native extension or
legacy Pro/ENGINEER numeric version suffix. Parameter updates use atomic
`QSettings` synchronization.

For native Parts and Assemblies, `Provider=zima-cad` declares that the native
document is authoritative. The File Manager reads its user-parameter order,
values and localized labels without running OCCT, mirrors those values into
metadata version 3 for other systems, and routes edits back to the native
document. Open documents are updated through the shared workspace metadata
transaction; closed documents are loaded and saved with their persisted
calculated boundaries or assembly snapshots intact. Normal native saving creates
the established numbered archive. Manual metadata values remain available for
folders and file types that have no native parameter provider.

The current implementation reads version 2 metadata without rewriting it during
browsing. Editing an authoritative native parameter in such a directory creates
version 3 directory metadata and its interoperability mirror. General version 2
conversion, Family-table editing and ZIMA-Parts conversion are intentionally
deferred until the version 3 behavior has been validated on synthetic data and
then on explicitly selected copies of real projects.

## Verification

`zima_file_manager_ui_tests` creates an isolated synthetic working directory and
library. It verifies separate source pages, project detection, Overview and
Files content, mirrored parameter reading and editing, and independent state in
multiple File Manager tabs. It never reads or writes production project data.
