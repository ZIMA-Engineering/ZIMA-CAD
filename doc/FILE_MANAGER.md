# File Manager

ZIMA-CAD provides a native File Manager as a top-level application tab. Each
File Manager tab keeps its own selected source, current directory, navigation
history, view, filters and checked items. Opening another directory in a new tab
does not change an existing File Manager tab.

The navigation row starts with Home, followed by Back, Forward, Up and the
read-only application working directory. The icon after the field invokes the same
directory chooser as File > Set working directory (F12). Home always opens
the current ZIMA-CAD working directory. File >
Open, F2, Ctrl+O and the main Open toolbar action use the existing localized
document-selection dialog. They do not create a File Manager tab. File Manager
has its own action in the toolbar's utility menu and can open native documents
from Files. Cancelling document selection preserves the active CAD or File
Manager tab; choosing an already open document activates its existing tab.

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
Set as Working Directory, Directory Properties and Create/Edit Project Overview
actions as a directory row. This allows the root of a writable source to be configured even though it
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
- **Files** groups folders before files in both sort directions and lists entries with checkboxes and metadata parameter
  columns. Every textual column has its own contains filter in a second header
  row, matching the ZIMA-Parts workflow. Version 3 metadata cells and native
  Part or Assembly parameters can be edited in writable sources by clicking a
  selected cell. Double-clicking opens an entry only in the **File name**
  column, so parameter editing cannot open the document. Copy, Move and Delete
  operate on the checked rows; the separate Versions toggle controls numbered
  version files.

### Table interaction and thumbnails

The File name column contains its checkbox, compact file-type icon and name.
The following Preview column aligns thumbnails independently of filename length;
a thumbnail never replaces the file-type icon. There is no separate Select
column. Preview has no text filter or parameter editor. Initial name width follows
the contents within a 180–360 px range; every column can be resized and moved.
Its filter moves with its logical column. Column order and widths use semantic
column IDs in `file-manager-layout.ini`, beside local overrides or in the
installation-root `config` directory, never the immutable version directory.
Clicking a checkbox on an already selected row sets all selected rows to that
same checked/unchecked state and preserves the selected group. Unselected rows
remain unchanged. The bottom actions have additional clearance from the edge;
Clean precedes Copy, Move and Delete. Overview and Files have palette-aware icons.

The Files thumbnail slider controls 48–192 px presentation, defaults to 96 px,
and remembers its value. Its 300 px track provides finer pointer control without increasing PNG
storage size. Hovering over an actual thumbnail shows a bounded enlarged image.
Double-clicking a STEP/STP, IGES/IGS or DXF thumbnail opens its read-only main
preview tab; other thumbnails open an internal image inspector. File-name
double-click keeps the existing document/open-directory behavior.

The factory default initializes a new File Manager in Files. An explicit user
Overview/Files preference initializes a new File Manager only.
Opening a CAD document, returning to the File Manager, refreshing its directory,
changing the working directory or reloading settings preserves its selected view.

Directory ordering in the tree and the Files name column uses the actual disk
folder name, independent of localized labels. The initial order is ascending;
directories remain before files. Closing the first or last native-document tab
with browser/File Manager tabs remaining activates a real remaining utility page
and keeps later tab switching functional.

Thumbnails have their own narrow column after the file name and type icon. The
size slider retains 300 pixels of pointer travel. Images keep their aspect ratio
and use two pixels of cell clearance. Save/display removes only exactly uniform
outer image background, with a retained border and no clipping of foreground
pixels. Small decoded previews use a bounded cache keyed by actual image bytes;
changed thumbnails cannot reuse stale pixels. Saved PNG dimensions remain bounded
by 320 x 240 pixels. Eight symbol categories reuse their SVG tree logos as `01.svg`
Overview directory previews. The FORM category has an SVG logo/preview, and all
four factory FORM definitions have compact previews rendered from their native
geometry.

Directory cards in Overview retain their existing `0000-index/01_<language>` / `01` images;
native model thumbnails do not introduce file cards into Overview. HTML keeps
priority. Document-mode tabs and small margins remove the unnecessary outer frame.

Each GUI Save of the visible top-level Part or Assembly captures the current
framebuffer as a PNG of at most 320 × 240 px with its aspect ratio retained.
It does not fit the view, regenerate geometry or create an Undo transaction.
The exact-extension path is `0000-index/thumbnails/<filename>.png` (for example
`bracket.prtz.png`), with `Files/<filename>/Thumbnail` recording the relative
path. A basename alias (`bracket.png`) is written for ZIMA-Parts only when there
is one native document with that stem; exact-extension images keep Part/Assembly
names independent. Existing Parts basename thumbnails remain readable.
Saving a nested activated Part does not capture its enclosing Assembly as that
Part's preview. Thumbnail failure cannot roll back a successful native save.
The GUI command console has the same capture hook. Headless CLI saves have no
interactive framebuffer and do not promise these current-view thumbnails.

### Clean

Clean is available in the directory/source context menu and before Copy in
Files. It adopts the ZIMA-Parts archive rule: numbered `.prtz`, `.asmz`, `.drwz`,
`.frmz`, `.tblz` and `.symz` files are archives, including orphaned archives and
numbers larger than uint64. Current unnumbered documents are retained.
The recursive plan excludes protected ancestors/descendants, read-only sources,
links and paths outside the canonical root. The confirmation lists the planned
files. Cancel changes nothing. Before moving each file to the OS recycle bin,
its path, inherited protection and SHA-256 are checked again. No permanent-delete
fallback is used when the recycle operation fails.

### Foreign-model inspection

One STEP/IGES/DXF path owns one preview main tab; repeated requests select it.
Preview tabs do not create native documents, source Part tabs or editable CAD
history. STEP uses one transferred source, shared definition meshes and global
occurrence placement, then the existing ZIMA mesh renderer. Its tree exposes the
product hierarchy, visibility and double-click framing. DXF uses the source-only
ZIMA-libqdxf/libdxfrw snapshot from Parts, with layer visibility/colors, units,
dimensions, middle-button pan, wheel zoom and Fit. Closed/bulged polylines,
block instances and rational spline weights are supported; unsupported entities
are explicitly reported so a partial preview is not mistaken for a complete one.
The supported scope is inspection, not automatic manufacturing acceptance.

DXF stays an independent 2D tool. Future editing needs an entity document for
lines/arcs/polylines/text/blocks/layers, reversible edits and preservation of
unsupported source entities; rendered Qt items/pictures are not an editable
source of truth. Native Body/history/Sketcher transactions do not own this tool.
Gap/duplicate/intersection checking for cutting and DXF writing remain future work.

### Factory labels and camera behavior

`tools/update-library-directory-metadata.py` regenerates the 34 repository-owned
Formats, Templates, Native Library, Materials, Symbols and Sheet Metal directory
metadata files, with 170 labels in cs/en/de/fr/ru and eight `logo-text.svg`
symbol-category icons. Physical directory
names stay unchanged. The shared label reader serves both File Manager and
ordinary file/directory choosers while keeping real paths and filenames intact.

The Part/Assembly mesh viewport preserves screen position on width changes from
side panels. A projection-space shift crops the visible area without changing
orientation, zoom or stored camera state, including perspective/fly projection.
The common picker uses the same projection. Explicit Fit clears this anchor.
Height changes retain ordinary vertical-scale behavior. No geometry work occurs
solely because a panel changes width.

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

The integrated File Manager workspace contract verifies that File Open uses
the existing localized document chooser with all six native document/library
filters. Cancel preserves the active CAD or File Manager tab, including a
workspace containing only a File Manager. Selecting an already open document
activates that document without inserting a utility tab. The independent
File Manager action still creates multiple tabs explicitly. The shared file
dialog contract additionally checks repeated selection/Cancel, uppercase
extensions, dense-directory responsiveness and Light/Dark/Light presentation.
These checks are verified on Windows; native Linux desktop verification remains
assigned to the Linux host.

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

Delete moves checked files and directories to the operating system recycle bin after confirmation. Cancel leaves them unchanged. A failed recycle operation reports an error and never falls back to permanent deletion. Directory protection still applies.

The navigation row displays the application working directory as read-only text.
Browsing does not change it. A directory or source context menu offers **Set as
working directory**, using the existing application-wide directory and instance
ownership path; every open File Manager receives the change. The current folder
stays selected in its tree, whose tooltip shows its full platform-native path.
Overview content extends to the right and bottom edges; only its text header
retains a small inset. HTML uses the same content boundary.

**Parameter protection.** `Directory/PreventParameterEdit=true` disables File
Manager parameter and note editing in that directory and its descendants.
This is independent of `PreventRemoval`. Directory Properties can change the
setting; source read-only permissions still apply. Blocked cells cannot create
an editor or save through the delegate.

**Formats example.** `ZE-TITLE-BLOCK.tblz` and `ZE-DRAWING-FRAME.frmz` each own a
native `name` parameter with a shared English value and cs/en/de/fr/ru labels and
values. `config/formats/0000-index/metadata.ini` shows their same-language mirrors
under `Parts/<stem>/name/<language>` and the `zima-cad` provider under `Files`.
Files reads the native value for the active language; editing a localized value
updates that language while preserving the shared value and other translations.
Shared-only parameters retain their ordinary shared-value editing behavior.
The same native parameter path supports `.prtz`, `.asmz`, `.frmz`, `.tblz`,
`.symz` and material libraries (`.matz`). This adds no required sidecar and
performs no model regeneration.
The title block keeps all five embedded SVG logos; the redundant loose import
SVG and two explicitly unwanted FR backup files were recycled.

The icon after the working-directory field binds the same QAction as File → Set working directory (F12), including its icon, enabled state and shared localized directory chooser.

Parameter cache validity includes the revision of any authoritative open native document in addition to file modification time and size. Unsaved metadata edits therefore appear without reloading closed files or recalculating geometry.

### Material library parameters

File Manager reads `.matz` with the shared native material reader. The material
name and physical properties supply values for the parameter columns explicitly
configured by directory metadata. Existing material
descriptions supply cs/en/de/fr/ru column labels; units appear in the header
when the files sharing a column use the same unit. Cell tooltips retain each
file's unit, including mixed-unit directories. Raw numeric values are preserved;
this view does not silently convert material units.

Writable directories mirror configured values and localized labels into
`0000-index/metadata.ini`: `Parameters/<key>/Label/<language>`,
`Parts/<stem>/<key>`, `Parts/<stem>/<key>/Unit` and the existing `Files` provider
records. The `.matz` file remains authoritative. The index is a derived mirror,
not a required source of material properties.

Editing a material cell validates the complete pending material, writes the
existing native UTF-8 representation atomically, then refreshes the mirror.
Unchanged values do not rewrite the material. Invalid values leave the file
unchanged. Saving preserves property strings, units and all descriptions;
the material file's layout is serialized into canonical sections. Directory and
source edit protection continue to apply. Already assigned Part and Assembly
materials are self-contained and do not change when a library entry is edited.

The File Manager GUI contract checks all five languages, native values and units,
the metadata mirror, a real delegate edit, unchanged input and invalid density.
The material-library contract round-trips every one of the 49 tracked factory
materials, as well as Unicode and literal text. This feature does not add a
standalone material-library editor or New/Save As commands.

Unsaved native values appear in the UI but are not published into the persistent file metadata mirror. The cache key includes the saved/dirty state; mirroring resumes after a successful native save. Directory-property changes refresh inherited protection and factory-source labels in the current view immediately.

## Window menu and row sizing

The Window menu lists all native, browser, File Manager and preview tabs. Only
native documents have a save state; utility tabs must not enter native save-state
queries. Selecting a menu entry uses the ordinary active-tab switching path.
F10 closes the active tab through its existing close handler, retaining F9 and
Ctrl+W. Native documents retain Save/Discard/Cancel protection.

Files rows have one uniform height derived from the thumbnail-size control,
including directories and files without previews. Preview aspect ratio does not
change row height; image rendering keeps its aspect ratio and minimal margins.

Preview painting fits small raster images as well as SVGs to the selected cell
size, including high-DPI displays. Exact uniform and monotone vertical-gradient
backgrounds can be removed when their exterior borders agree. Every differing
foreground pixel, including near-background strokes, is retained with a
two-pixel margin; images touching a gradient border are left uncropped. The
same crop is applied when saving native model thumbnails, which remain bounded
to 320 by 240 pixels. Display enlargement does not increase stored PNG sizes.

## File Manager follow-up (2026-10-13)

- **Cleanup in Templates:** the user reports that enabling Versions in the
  Templates source shows version/backup files, but Clean cannot be invoked.
  Reproduce this state and check the toolbar and directory/source context
  actions, their enablement, source permissions and inherited removal
  protection. Distinguish intentional protection from an incorrect disabled
  action. Preserve current templates, the cleanup scope and recycle-bin
  confirmation/Cancel behavior. Recorded for later investigation; no cause
  is now confirmed: factory Templates metadata sets `PreventRemoval=true`.
  Cleanup keeps this protection and now explains it in its tooltip. Toolbar,
  source menu and directory menu respect the same inherited protection. Removing
  the protection explicitly through Directory Properties enables cleanup.
  Windows checks cover Versions enabled, protection, explicit unprotection,
  retained current files, cleanup confirmation/Cancel and recycling of only
  unprotected synthetic archives. Factory/user template protection is not
  silently changed.
- **Native file-type icons:** in Templates, the user sees Pro/ENGINEER icons
  for `.prtz` and `.asmz` files. The user suspects their Windows file associations;
  this cause is unverified. Check the File Manager icon provider and use the
  application's own Part/Assembly icons for native ZIMA formats independently
  of operating-system associations, with correct Light/Dark presentation on
  Windows and Linux. Check version/backup filename variants as well. Preserve
  file opening behavior and the user's system associations; this observation
  does not authorize changing Windows defaults. File Manager and the ordinary
  file chooser now share the native icon mapping, including uppercase extensions
  and numeric archive suffixes for Part, Assembly, Drawing, frame, title block
  and symbol. Light/Dark/Light checks match the application's own icons and leave
  foreign formats to the existing system provider. No system association is
  changed. Windows checks passed; Linux desktop execution remains unverified.

## Referenced parameters and thumbnails

Directory Properties exposes independent `IncludeParameters` and
`IncludeThumbnails` lists. Each line names one source directory. Browse adds a
relative path where possible; resolved paths remain visible. Clear removes only
that list. OK validates existing readable targets and rejects cycles or more than
64 nested sources; Cancel preserves metadata. The controls use the existing
internal Properties window and all five UI languages.

The directory's read-only, copyable path appears above the tabs. The source-directory
editors show one compact text line, with keyboard and wheel scrolling for
additional entries. Empty resolved-path labels reserve no space. The window
opens at its natural content height; resizing keeps form rows anchored while
the Parameters table can use additional height.

Includes resolve relative to the directory containing the link. Sources are
searched in list order, with local definitions taking precedence. Parameter
columns include inherited definitions and match export stems to source parts;
STEP and DXF can therefore use the same native Part's parameters. Current native
source data overrides its source's metadata mirror, including open-document
revisions. Local export edits write local overrides, never the included source.
Explicit empty local values remain valid overrides. Inherited directory labels,
protection and ownership do not replace the local directory's settings.

Local thumbnails take precedence; linked thumbnail directories are independent
of parameter sources. Missing/cyclic links produce a visible Files status and
retain usable local content. Existing version-2 include metadata is readable;
an explicit local parameter override uses the existing version-3 writer.

Saved native documents publish shared and localized values, labels and the
`zima-cad` provider for configured columns into writable, unprotected directory
metadata, even though inspection avoids loading geometry. Browsing does not
initialize a parameter list or add unconfigured columns.
Unsaved values remain transient until native saving. Included source directories
are read-only consumers for this traversal; browsing an include never writes
their mirrors. Other programs can read the saved metadata without loading CAD.

Windows tests cover language changes, native revision/cache invalidation, local
and empty overrides, independent thumbnails, missing/cyclic/absolute/duplicate
links, GUI Browse/Clear/Cancel/OK/reopen, rejection before commit and anchored
resizing. The same matrix passed with the real Windows Qt platform, and the
properties window was visually inspected. Linux runtime verification remains
outstanding. Details and timings are in the
[performance follow-up](performance/20261013-file-manager-followup.md).

## File type filters and folder creation

The native type buttons next to Versions independently show or hide Part,
Assembly, Drawing, material, frame, title-block and symbol files. Filtering
combines with the existing per-column text filters without reloading native
documents. Column filter editors follow horizontal scrolling, section resizing
and reordering. The header context menu can hide the Type column; this choice
is stored with the existing local column layout.
Type buttons and extension values in the Type column use lowercase. Checked
type and Versions buttons use the shared azure command-button state.

Filters, next to Versions, edits `0000-index/filters.ini`. Its `[Filters]`
`Hide` list contains exact names or wildcard patterns (`*` and `?`), matched
against complete file and folder names without case sensitivity. This is the
current ZIMA-Parts local filter convention. Rules apply to that directory only;
ancestor filter files and obsolete `files.ini` visibility groups are not read.
Unknown `filters.ini` settings and existing `files.ini` files are preserved.

Settings > File Manager > Global file filters supplies defaults. The factory
list contains `0000-index`; File Manager has no special hard-coded exclusion
for that name. The local Use global filters option (`Filters/InheritGlobal`,
default true) combines these defaults with local rules. Turn it off to define
an independent local list, including making `0000-index` visible. The separate
Materials Manager tree continues to omit its internal metadata directories.
One pattern per line is used in both editors; empty lists are supported.

Native buttons atomically update local `Filters/Hide` extension rules. Enabling
a type hidden by an exact global extension rule copies the effective rules
locally and disables global inheritance, retaining the other exclusions.
`Filters/ShowZimaVersions` persists Versions. Pattern expressions are compiled
once per refresh. Read-only or content-protected directories permit transient
native-button changes without writing policy; their local rule editor is
read-only. Parameter-edit protection alone does not block filter configuration.

Versions, Filters, type buttons, New Folder, thumbnail size and the remaining
file actions share one row. The size slider follows New Folder. Native type
and Versions buttons have icons; active states are azure. All current factory
symbol definitions have geometry-based PNG previews in their local thumbnail
metadata directories; `tools/generate-symbol-thumbnails.py` regenerates and
checks these without modifying native definitions.

New Folder creates one immediate child using the shared internal name editor.
Invalid, reserved or existing names are rejected. Source read-only status and
inherited content protection block creation, including a repeated check on OK.
Parameter-edit protection alone does not block adding a folder. Existing deletion
and move protection continues to apply to both files and directories.

## Material library manager

Opening a `.matz` file opens Material Properties, containing only its parameter
table, OK, Cancel and Save As. It does not construct a folder tree or enumerate
other materials. Tools > Material Manager retains its folder tree, material
list, New Folder, New Material and Save As. Both modes reuse the same internal
Material dialog, property table, validation and atomic saving implementation as
document material editing. They keep document assignment separate: changing a library file does
not modify materials already stored in existing Part or Assembly documents.
The material folder tree excludes `0000-index` metadata directories at every
depth, including folders added while the manager is open.

OK validates and atomically saves pending edits to the selected library file;
unchanged OK performs no write. Cancel discards pending edits. Selecting another
file, folder or New Material asks before discarding pending table changes.
Save As chooses a writable destination and a new `.matz` name, saves a copy and
reopens it in the same editor. Existing destination files are rejected. New
Material starts a pending material that is named through Save As. Explicitly
saved copies and folders remain after Cancel.

Library edits preserve units and descriptions in other languages, use native
material validation, respect source read-only and parameter-edit protection,
and reject saving over a file changed externally since it was opened. Creating
files and folders respects content protection. The dialog uses the existing
shared properties-window confirmation and layout mechanisms, including middle
double-click confirmation over its owning view. New controls and errors are
localized in all five languages. Windows GUI regression checks cover these
paths; Linux desktop execution still requires verification on Linux.

## Engineering-table revision context

The revision selected on 2026-10-13 subsequently consolidated preferred numbers,
normal sizes, tolerances and thread tables, corrected redundant navigation and
added category logos. Generated pages retain all five languages and independent
local resources. The original engineering-table source remains preserved.

## Metadata owns the displayed parameter columns

The five fixed columns (filename, preview, type, size and modification time)
remain available. Additional columns and their order come exclusively from
`Directory/Parameters` in `0000-index/metadata.ini`, including explicitly linked
`Directory/IncludeParameters` sources. Native documents supply the current values,
units and localized labels for those configured columns; discovering another
native parameter does not create a column or rewrite the configured list.
Refreshing or reopening a tab preserves this distinction. An absent or empty
parameter list gives only the fixed columns unless an included metadata source
explicitly configures parameters. Directory Properties edits the list explicitly.

Existing factory directories have explicit lists preserving the previously
displayed native parameters, including material properties. Saved value mirrors
continue to update for configured parameters without changing document ownership,
read-only protection or the authority of open unsaved documents.
