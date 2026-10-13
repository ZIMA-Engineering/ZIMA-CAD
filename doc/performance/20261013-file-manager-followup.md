# File Manager follow-up, 2026-10-13

The user requested this order: foreign preview activation, File Manager startup,
directory includes, Templates cleanup/icons, Extrusion plane visibility, then
large-model import/save/load and selected navigation. Engineering-table revision
is reserved for joint work with the user. This work does not publish a release.

## Native parameter inspection

File Manager previously loaded each closed Part or Assembly in full to obtain
its user parameters. The new shared C++ inspection reader scans the native INI
file in 64 KiB blocks and retains only header and parameter sections. It neither
decodes calculated geometry nor constructs model history. It validates the
current format and parameter data; it is not a substitute for loading or
validating the complete model. Open documents still supply authoritative values
and their revision/dirty state invalidates the existing tab-local cache.

The native file format, geometry precision, saved data, and ordinary document
opening/editing paths are unchanged. No persistent cache or sidecar is required.

### Representative Windows measurements

Baseline source: `607cc6cd5b24933fdaa076dcd26451085d66fe47`, local build identity
`2026101302`, Windows Release, with existing local changes retained. The after
build includes the changes documented here. Both runs copied the same private
`Projects/02.prtz` into a fresh temporary working directory before measurement.
The private document is not committed or packaged.

- Size: 646,708,745 bytes.
- Input SHA-256: `3bd2725153af2efd7410c9c9b8e89845d1f56552029d395558aea56743683ca6`.
- Ten ordered native parameters.

| Operation | Before | After |
| --- | ---: | ---: |
| First usable File Manager, including event flush | 21,101 ms | 608 ms |
| Independent native parameter read | 21,078 ms | 406 ms |
| Repeated navigation using the tab cache | 52 ms | 52 ms |

These are single-run observations with warmed filesystem data after copying,
not cold-disk measurements or universal speedup guarantees. The first tab's
parameter cache is empty. Fixture copying and the independent complete-load
equivalence check are outside the reported interaction times. Peak memory was
not recorded in these runs.

The final retained implementation repeated the representative probe at
546/366/52 ms, again with ten parameters, complete-load equivalence and the
saved mirror's Version 3/order/provider/shared/localized-value assertions.
This run is recorded in `build/file-manager-G92K1-final.log`; the original
608/406/52 ms after sample above is retained to show numerical variability.

The representative check compared order, shared values, and all localized
values with a complete load of the same native file. The metadata command
regression also compares complete parameter data for generated Part/Assembly
documents and the current start Part, start Assembly, title block, frame, and
text symbol. A synthetic 16 MiB ignored cache line and a parameter spanning a
64 KiB read boundary cover exact Unicode, escaped text, empty values, CRLF,
and a final line without a newline.

Reproduce the interaction probe through the existing
`zima_cpp_file_manager_workspace_ui_contract` with
`ZIMA_FILE_MANAGER_PERFORMANCE_SOURCE` set to the private native model.
Set `ZIMA_FILE_MANAGER_COMPARE_FULL_METADATA=1` for the independent complete
load comparison. Unset these variables for the ordinary GUI regression.

## Foreign preview dispatch

A Preview-cell double-click now reaches the existing supported-format handler
even when no thumbnail exists. The source filename's extension selects the
STEP/STP, IGES/IGS, or DXF viewer. Other files retain their image-inspection and
ordinary filename-opening paths.

The File Manager UI regression passed the dispatch matrix for empty/populated
cells, uppercase suffixes, STEP/STP/IGES/IGS/DXF, and unsupported text files;
source contents remained unchanged. The ordinary existing image-inspector
checks are retained. Actual empty-cell DXF double-click, main-tab reuse and
close, STEP/DXF viewer rendering and source-integrity checks also passed.

The general model-preview test currently has an independent datum-marker/camera
restoration failure. `ZIMA_VERIFY_FOREIGN_PREVIEW_ONLY=1` selects the foreign
viewer checks without claiming the datum test passed. The factory branding
inventory assertion was corrected to the actual nine current SVG logos; all
34 metadata directories remain required.

## Platform and localization scope

This is one shared C++ implementation. Windows verification is local; Linux
execution and native desktop verification remain unverified. Parameter
inspection and preview dispatch add no user-visible text. Directory-include
controls and validation messages are translated into cs/en/de/fr/ru.

## Directory includes and Templates

Directory Properties exposes separate parameter and thumbnail sources, stores
relative links when possible and displays their resolved paths. Local values
(including explicit empty values) and images override inherited data. Matching
export stems consume authoritative source-native parameters, with open revision
invalidation. Missing/cyclic links are rejected before a dialog commits;
unavailable existing sources produce a visible status. Included sources are
read-only. Saved local native parameters populate a discoverable Version 3
metadata mirror; unsaved values remain authoritative only in memory.

The File Manager regression covers Version 2 source metadata, nested/duplicate
links, independent thumbnails, editable local overrides, native cache
invalidation, source integrity, dialog browse/OK/reopen/Cancel/clear/resize and
all five UI languages. Both offscreen and real Windows runs passed. The latter
was visually inspected using `build/file-manager-includes-properties-windows.png`.

Templates cleanup was disabled by the existing `PreventRemoval=true` content
protection. That setting remains meaningful; the toolbar and menus consistently
respect ancestor protection and explain it in a localized tooltip. Explicitly
changing protection in Directory Properties enables cleanup. Native filename
icons now reuse the ordinary file-dialog resource mapping, including archive
suffixes and theme changes. Protection, icon and existing cleanup/recycle
regressions passed.

## Imported Extrusion work plane

The command supplied Body-local geometry but resolved its pending profile in
the complete Part. A following empty Body exposed an unavailable-reference
failure and the owning Body transform could be applied twice. Preparation now
uses the existing owning-Body document carrier; the protected shared placement
contract is unchanged.

Actual GUI face selection and successive signed offsets 0/+3/-2/0 were checked
against independent planar/analytic surface equations. Source identity, preview
border identity, unchanged document state and Cancel camera restoration passed
for a generated two-Body model and G92K1's `step:face:#1771436`. The latter run
took 74.10 s including isolated fixture loading, not a command-time claim.
The existing profile-frame matrix and imported Extrusion context/Cancel tests
also passed (3/3, `build/import-plane-local-context-tests.log`). The private
representative run is in `build/import-plane-G92K1-local-context.log`, with a
visually inspected capture in
`build/import-extrusion-plane-G92K1-local-context.png`.
