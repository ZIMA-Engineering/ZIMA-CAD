# File Manager

ZIMA-CAD provides a native File Manager as a top-level application tab. Each
File Manager tab keeps its own selected source, current directory, navigation
history, view, filters and checked items. Opening another directory in a new tab
does not change an existing File Manager tab.

The navigation row starts with Home, followed by Back, Forward, Up and the
current path. Home always opens the current ZIMA-CAD working directory. File >
Open and the main Open toolbar action open a File Manager tab; native CAD files
open from its Overview or Files view.

## Data sources

The left side follows the source-oriented navigation used by ZIMA-Parts. A
compact toolbar links directly to File Manager settings. The working directory
is the fixed first source and is not part of the configurable source list. All
source rows remain visible above the active source's folder tree, so the active
source remains clear when the user navigates deeply. Clicking a source row
opens its root. The internal `0000-index` support directory is hidden from the
folder tree and content views.

Global Settings > File Manager controls source order, enabled state, path and
read-only state. Factory sources are provided for drawing formats, materials,
symbols, templates, sheet-metal assets, native ZIMA-CAD library content and
engineering tables. Custom local or network sources can be added. Factory
source names follow the application language; their paths remain configurable.

The context menu on a source row provides the same Open, Open in New Tab,
Directory Properties and Create/Edit Project Overview actions as a directory
row. This allows the root of a writable source to be configured even though it
is represented by the source row rather than by an item in its folder tree.

`config/lib/02-NATIVE` is reserved for native reusable engineering content such
as fasteners, gears and keyway definitions. `config/lib/03-ENGINEERING-TABLES`
contains the reviewed File Manager import of the engineering-table hierarchy.
The import preserves HTML/CSS/JavaScript navigation, documents, data files,
preview images and legacy metadata parameters. Shared web assets are copied
locally for independent pages, and links are rewritten to the imported short
English directory names. It excludes legacy Pro/ENGINEER CAD documents,
backups and desktop metadata. The Tree and Files views read human-readable
localized directory labels from metadata version 3. Label reads are cached per
File Manager source and invalidated after Directory Properties changes them.

## Views

The right side has two views:

- **Overview** renders `0000-index/index_<language>.html` for the active
  application language, falls back to `index.html` or `index.htm`, and otherwise
  displays the localized directory title, optional logo and large subdirectory
  cards, following the ZIMA-Parts automatic index. Card names appear above the
  images; one click opens the directory. Files remain in the Files view.
  Previews come from a named image beside the child, parent thumbnails, then
  the child's project preview or `0000-index/01`, then its branding. All use
  the active language where a localized preview is available.
  For a project, the same Overview includes the project number, localized
  title and description, customer and main image. Project editing stays in the
  directory/source context menu. There is no separate Project tab.
  The automatic cards can be disabled per directory. Cards and their text
  follow the live Qt palette in Light and Dark themes.
- **Files** lists folders and files with checkboxes and metadata parameter
  columns. Every textual column has its own contains filter in a second header
  row, matching the ZIMA-Parts workflow. Version 3 metadata cells and native
  Part or Assembly parameters can be edited in writable sources by clicking a
  selected cell. Double-clicking opens an entry only in the **File name**
  column, so parameter editing cannot open the document. Copy, Move and Delete
  operate on the checked rows; the separate Versions toggle controls numbered
  version files.

Directory Properties is an internal ZIMA-CAD properties window. It edits all
five localized directory labels, whether child folders appear in Files, the
automatic-overview fallback, the ordered parameter definitions, and SVG/PNG
branding. It also controls `Directory/PreventRemoval`, which prevents deletion
or moving of the directory's contents. A protected child directory also blocks
deletion or moving of an ancestor that contains it. `logo.svg` or `logo.png`
shows only the image; `logo-text.svg` or `logo-text.png` shows the image with
localized text. SVG has priority.

Directory Properties remains editable for a source marked read-only. This
allows an administrator to maintain localized labels, presentation and the
explicit removal lock without enabling native parameter edits or destructive
file operations for that source. A filesystem that is physically read-only
still reports an ordinary save error.

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
PreventRemoval=true

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

Sources marked read-only never create or update `metadata.ini` merely by being
browsed or by automatic native-parameter mirroring. Their native parameters
remain visible, their cells are not editable and file Move/Delete operations
remain disabled. An explicit Directory Properties confirmation may update only
the selected directory metadata, including its localized labels and
`PreventRemoval` setting.

The current implementation reads version 2 metadata without rewriting it during
browsing. Editing an authoritative native parameter in such a directory creates
version 3 directory metadata and its interoperability mirror. General version 2
conversion, Family-table editing and ZIMA-Parts conversion are intentionally
deferred until the version 3 behavior has been validated on synthetic data and
then on explicitly selected copies of real projects.

## Verification

`zima_file_manager_ui_tests` creates an isolated synthetic working directory and
library. It verifies the fixed source list, source-root navigation, hidden
`0000-index` directories, per-column filters, file-name-only opening, parameter
editing, project detection, Overview and Files content, mirrored parameter
updates and independent state in multiple File Manager tabs. The source-row
contract also creates a project overview at a synthetic source root, verifies
the removal lock, and edits directory metadata explicitly for a read-only
source while retaining its non-editable native parameters. It never reads or
writes production project data.

`tools/test_import_engineering_tables.py` verifies the deterministic filtered
import and metadata conversion on synthetic content. The committed factory
snapshot contains 1,448 verified files (155,953,349 bytes) and 312 verified
metadata files. Its imported content-tree SHA-256 is
`e3e1d5539769d5fa8c367c68e24ecacffc497d4c70d4cf1f6454dfedbf30853f`.
All 312 directories have nonempty Czech, English, German, French and Russian
labels from `tools/data/engineering-table-directories.json`. The catalog
separates full visible names from concise English physical identifiers.
Terminology follows the original Czech engineering subjects and official ISO
titles where applicable: [keys and keyways](https://www.iso.org/ics/21.120.30/x/),
[clevis pins](https://www.iso.org/standard/7178.html) and
[thread undercuts](https://www.iso.org/standard/10740.html).
Reimporting retains all 312 directory IDs, existing metadata settings,
removal protection, images, PDFs and reference data; only labels, storage names
and their web-navigation paths change. The importer now reads empty INI label
values without consuming the next line and quotes comma-containing labels so
QSettings reads one scalar string. The native label audit verifies all 1,560
factory labels through QSettings in addition to the importer checks.
File Manager uses canonical QSettings hierarchy keys for localized labels and
parameter values, matching the backslash-separated on-disk INI keys produced
by ZIMA-Parts. Earlier CAD-created literal escaped-key values remain readable;
explicit editing writes canonical keys. Both QSettings parameter lists and
comma-separated scalar parameter definitions are accepted.
All Overview index pages have valid local links. One unused legacy
`index-parts_cs.html` page retains two already-missing source references; the
import report records them and no active Overview page uses that file.
The original source inventory remained 2,806 files and 297,145,161 bytes before
and after the import.
