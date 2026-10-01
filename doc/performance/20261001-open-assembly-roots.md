# Open Assembly root traversal — 2026-10-01

## Scope

Inputs are authoritative open Part/Assembly documents and their existing
calculated packets. The means are the existing recursive display refresh and
per-call source sharing. The output remains the same occurrence geometry and
metadata without solving mates or invoking OCCT during display refresh.

An open subassembly is both a recursive source and a Workspace root. Previously,
`Workspace::refresh_source_geometry()` traversed it from its parent and again
from the open-document list. A per-call set now records successfully refreshed
open documents. It avoids a second traversal in either insertion order and
avoids copying roots already processed recursively. Completed sources are marked
only after successful refresh/publication; active-stack cycle detection remains
independent and still precedes source reuse. The set expires on return, so later
edits, Undo/Redo, renames and missing sources are checked by the next call.

This does not add a persistent cache, change source ownership, narrow dependency
semantics or modify shared placement, regeneration, native formats or Undo.

## Measurement

Baseline production source: `646e2fc1`. The same expanded benchmark was compiled
against the baseline and changed `workspace.cpp`. Windows x64, MSVC Release,
Qt 6.11 and OCCT 8, on the same host. Each result below is the median of three
independent runs, each averaging twenty warm source-refresh calls. No build ran
concurrently with measurements. These are synthetic distinct open subassemblies,
each containing eight calculated cubes, with 256 or 1,024 Part occurrences.
No representative user Assembly was available in the repository Projects folder;
these results do not claim end-to-end user-model or frame-rendering speedups.

| Open-document order | Subassemblies / Parts | Before (ms) | After (ms) | Reduction |
| --- | ---: | ---: | ---: | ---: |
| Children before parent | 32 / 256 | 1.276 | 1.104 | 13.5% |
| Children before parent | 128 / 1,024 | 5.383 | 4.561 | 15.3% |
| Parent before children | 32 / 256 | 1.258 | 0.816 | 35.1% |
| Parent before children | 128 / 1,024 | 5.512 | 3.548 | 35.6% |

The absolute gain is about 0.2–2.0 ms per source refresh in these fixtures.
Remaining document copying, occurrence metadata and scene materialization are
still performed where required. No broader rendering improvement is claimed.
[Raw measurements](20261001-open-assembly-roots.txt).

## Verification

The benchmark checks unchanged shared geometry, occurrence metadata, placement,
revision/dirty/Undo state, unsaved source geometry changes and source Undo.
The Workspace contract adds three nested Assembly levels in both open-document
orders, checking propagated calculated volume, stable source sharing and source
Undo/Redo without creating Assembly edit transactions. Existing coverage retains
missing/reappearing files, relative paths, source appearance, hidden occurrences,
renames, native reopening and cycle rejection.

All eight focused suites passed: Workspace, Workspace publication, file rename,
component properties, Family tables, translations, Assembly refresh GUI and
presentation refresh GUI. Evidence: `build/open-root-tests.log`. This is not a
full-repository test claim. The GUI/CLI local build remains launched through the
unchanged root `zima-cad.bat`. Linux verification is deferred to Linux.

Localization review: no new or changed user-visible text. Existing five-language
catalog coverage passed. No Windows release archive is replaced by this change.

Reproduce the current benchmark by building and running
`zima_cpp_source_refresh_benchmark`; no timing threshold is used as a test gate.
