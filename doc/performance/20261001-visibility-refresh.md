# Assembly visibility refresh — 2026-10-01

## Cause and scope

Inputs are the current calculated Assembly and a component visibility change.
The output must retain the same visible geometry, styles, selection candidates,
Tree state and Undo/Redo behavior. The available means are the existing validated
scene builder and the normal full presentation refresh.

A full refresh previously assembled the authoritative scene for appearance-path
collection, discarded it, then assembled the same document again for the View.
`Workspace::authoritative_viewer_mesh()` delegates directly to that Assembly
session document's `build_scene()`. The ordinary top-level Assembly refresh now
moves the first result into the display preparation instead of repeating the
build. The result is local to this one refresh and is consumed at most once.

Reuse is restricted to the exact source document object and unchanged session
generation. Properties, Sketch editing, active nested components, rollback and
construction/derived-copy previews retain their prior preparation paths. The
normal display annotations are still appended after obtaining the base scene.
Appearance resolution, source refresh, Tree rebuilding, mesh replacement and
picker rebuilding remain in place. No persistent geometry cache, new validation
shortcut, native format or container-placement change is introduced.

This is useful for hide/show and other ordinary full Assembly refreshes. It does
not implement incremental Tree updates or GPU visibility masks. Those remain
separate candidates requiring their own measurements and behavioral checks.

## Verification and measurements

Baseline: `68aac16d`, with the same six-toggle GUI verification added to both
builds. The synthetic fixture contains 256 calculated cube occurrences, including
an unsaved source geometry edit. Each timing includes the synchronous visibility
command and refresh, excluding queued painting/GPU time.

The initial separate-build runs did not establish an improvement (medians 312.5 ms
before and 315.7 ms after). To distinguish host variability from the change, a
temporary diagnostic switch disabled only scene reuse in the same executable.
Three before/after pairs then ran serially without concurrent compilation. The
switch was removed and the production executable rebuilt afterward.

| Pair | Before median ms | After median ms |
| --- | ---: | ---: |
| 1 | 301.346 | 282.448 |
| 2 | 331.142 | 279.174 |
| 3 | 320.331 | 314.942 |

Each cell is the median of six alternating hide/show commands. The median of the
three run medians improved from 320.331 to 282.448 ms: about 37.9 ms or 11.8%.
Individual paired improvements vary from about 1.7% to 15.7%. This is a bounded
synthetic result, not a universal speedup or representative user-model profile.
[Raw timings and image hashes](20261001-visibility-refresh.txt).

All six hide/show framebuffer captures are byte-identical PNGs between the actual
baseline and changed builds. The GUI verification checks actual occurrence
triangle visibility, source edits, Undo/Redo, regeneration, exact active-occurrence
selection restrictions, dimension picking and appearance restoration. Every
visibility change still performs one mesh replacement and one Tree reset.

Localization review: no new or changed user-visible strings. Existing shared
cs/en/de/fr/ru catalogs remain unchanged. Linux verification is deferred.


## Test results and remaining issue

**Follow-up:** the derived-copy failure described below was traced to missed
owned-Sketch frame convergence and a subsequent null dereference in the test.
The corrected broad GUI suite passes; see
[profile frame convergence and copy saving](20261001-copy-save-convergence.md).
The original observations below are retained as historical evidence.

Eleven distinct focused suites passed: refresh-scope GUI, Assembly-refresh GUI,
selection-filter GUI, component-properties GUI, appearance contracts, appearance
commands, viewer contracts, Workspace contracts, component-property commands,
Assembly cut history and translations. Appearance-command and cut-history tests
initially used stale executables that rejected the current native format; both
passed after rebuilding their test targets. Evidence is in
`build/visibility-tests.log` and `build/visibility-final-tests.log`.

At the original validation, the broader derived-copy GUI suite was **not passing**.
Its normal 120-second gate
timed out. A standalone run with a 300-second allowance reached the later in-Body
Mirror Origin check and exited with Windows access violation `0xC0000005` after
166.84 seconds. Rebuilding with scene reuse unconditionally disabled reproduced
the same code and last progress marker after 147.69 seconds. Thus this failure is
not eliminated by restoring the original double-build path; it requires separate
investigation, and this change does not claim full derived-copy GUI coverage.
The last marker is `Copy Origin GUI: Mirror, field 0, occurrence ` (empty path),
after nested Assembly and Part copy checks, near the in-Body Mirror check in
`verify_derived_copy_commands()` in `cpp/app/main_workspace.cpp`. This marker
narrows investigation but is not a confirmed crash location or root cause.
Logs: `build/visibility-derived-copy-extended.log` and
`build/visibility-derived-copy-baseline.log`. No production placement or copy
logic was changed to bypass this failure.

The baseline override was removed and the optimized local GUI rebuilt. GUI and
CLI remain available through the existing repository-root development launcher.
No Windows release archive was produced by this follow-up.
