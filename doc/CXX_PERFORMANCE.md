# C++ performance measurements

The October 14 follow-ups are included in signed Windows
[2026101401](releases/2026101401.md#signed-windows-publication), with fresh
committed-source packaging, signed-package GUI/native checks and public/update
acceptance. The measurements below retain their original finite scope.

The subsequent [selection and requested-analysis follow-up](performance/20261014-step-selection-and-demand-analysis.md)
preserves Tree selection/icon behavior, defers per-face area integration to an
explicit inspector and reduces native-save allocation work. It is included in
signed Windows [2026101402](releases/2026101402.md#signed-windows-publication).
Its record separates cold calculations,
ordinary edits, exact-result checks and remaining serialization costs.

For new commands and feature extensions, follow the
[performance-by-design guide](FEATURE_PERFORMANCE_GUIDE.md). It translates the
measured optimizations into lifecycle, ownership and verification rules.

The 2026-10-13 follow-up is included in signed Windows
[2026101303](releases/2026101303.md#signed-windows-publication), with committed
source, signed-package workflows and public/update acceptance. Measurements
below remain scoped local results; the remaining G92K1 precise volume-integration
latency and documented GUI/Linux verification gaps remain open.

The local Windows 2026-10-14
[STEP scheduling follow-up](performance/20261014-step-gk-face-investigation.md)
reduces the measured G92K1 backend import from 692.0 to 520.6 seconds, with exact
saved properties and complete native-packet equivalence. It preserves OCCT,
precision and native references; the import still takes several minutes.
The GUI/CLI build includes the working-tree change. Linux execution and a new
published release are not claimed by this local result.

The [last STEP-backed Extrusion and native-save follow-up](performance/20261014-step-last-extrusion-and-save.md)
restores the pending Sketch wire, reduces the measured last subtraction from
96.1 to 53.3 seconds with an identical complete Body packet, and releases
unnecessary serialized/cache copies before full save validation. The first
verified native save sample decreased from 114.8 to 85.1 seconds; substantial
large-file serialization and validation costs remain. A repeat measured 95.5
seconds with the same exact native data. A subsequent exclusively owned JSON
allocation reuse reduced the measured save further to 63.4–74.4 seconds in two
verified runs, retaining the exact native data and full validation. The linked record separates these
successive implementations and their measurements.

## Deferred user observations (2026-10-13)

The user authorized implementation on 2026-10-13 in this order: foreign-model
preview without a thumbnail, File Manager startup, referenced directory metadata
and thumbnails, Templates cleanup and native icons, Extrusion plane visibility,
then large-model import/save/load and selected-solid navigation. Engineering
table revision remains deferred for joint work with the user.

The File Manager startup, foreign preview and directory-include follow-ups are
implemented and verified on Windows; see the
[follow-up record](performance/20261013-file-manager-followup.md).
Remaining large-model investigations below are still in progress.

The user selected **G92K1** as the representative large-model test input for
these follow-ups. The located original is
`C:/Users/vladi/Prace/ZIMA-Engineering/03-CAD/01-ACTIVE_PROJECTS/ZE0176-TOYOTA-G92K1/import/G92K1.stp`;
the user saved its native Part as `Projects/02.prtz`. Use this STEP and its
native counterpart consistently for import, native save/load, selected-solid
navigation and File Manager parameter/preview loading investigations. Work on
isolated copies, preserve the originals, record input hashes and build identity,
and distinguish cold versus warm runs. Do not commit or package these private
test models. Retain smaller deterministic regression fixtures alongside this
representative performance case.

- **File Manager startup:** the user reports approximately 20 seconds to open a
  File Manager tab. Confirmed on an isolated `02.prtz` copy: first usable content
  took 21,101 ms and a complete native parameter read took 21,078 ms. Inspecting
  only native parameter sections reduced these to 608/406 ms in the recorded
  Windows samples. Saved values still populate the metadata mirror; open
  documents retain authority over unsaved values. Cold disk and Linux timings
  are not claimed.
- **Selected large STEP solid navigation:** confirmed on `Projects/02.prtz`:
  unselected frames took approximately 14–17 ms, selected frames 253–261 ms.
  The actual selected display-wire path submits screen-space rectangles as
  general polygons and repeatedly sets painter state. The optimized pass
  consumes the same rectangles/projection/style through convex submission and
  a shared state scope. Baseline endpoint captures verify equivalent images;
  document state and mesh publication remain unchanged. See the
  [large-model record](performance/20261013-large-model-followup.md) for timings,
  exact image comparisons, method and verification limits.
- **Large STEP import:** G92K1 took more than ten minutes according to the user
  and eventually completed. During import, a read-only process sample observed
  9.45 seconds of CPU time over 5.03 seconds of wall time and approximately
  2,102 MiB working set. These measurements do not identify the slow stage.
  The new matched stage profile separates 4.4 s of reading, 9.9 s of transfer,
  11.5 s of topology capture/archive, 2.1 s of meshing and 607 s of span-aware
  GK volume integration. Backend import completed in 692 s; substantial total
  latency improvement is not established. The existing ordered tensor
  integration now runs concurrently at unchanged precision and complete saved
  properties pass serial/native oracles. GK integration remains the dominant
  unresolved performance cost. GUI scene publication and completed peak memory
  are not claimed by this backend probe.
- **Extrusion working-plane visibility on STEP faces:** fixed a demonstrated
  preview-context defect. Body-local reference geometry was being resolved
  against the complete Part, including a following empty Body, and its owning
  Body transform could be applied twice. Preview preparation now consumes the
  existing owning-Body document carrier. Shared placement solving and controls
  are unchanged. Actual imported-face clicks, signed offsets 0/+3/-2/0,
  independent plane/surface equations and unchanged-document Cancel passed on
  a two-Body fixture and the private G92K1 document. The existing full
  profile-frame and imported Extrusion context/Cancel regressions also passed.
- **Foreign-model preview without a thumbnail:** the user double-clicks the
  empty Preview cell for a STEP file and nothing opens; DXF needs the same
  behavior checked. Source tracing confirms that `FileTable::mouseDoubleClickEvent`
  dispatches preview requests only when the row already has a Preview icon.
  Remove that image prerequisite for supported STEP/STP, IGES/IGS and DXF preview
  files while retaining ordinary filename opening and image inspection behavior
  for other file types. Verify both empty and populated Preview cells, correct
  main-tab selection/reuse, and unchanged source files. Deferred at the user's
  request initially; this is now fixed. Empty/populated dispatch, uppercase
  extensions, STEP/DXF viewers, source integrity, and actual main-window DXF
  double-click/reuse/close passed. The independent datum-marker/camera check in
  the general preview suite remains unresolved and is not claimed as passing.
- **Referenced directory metadata and thumbnails:** the user reports that
  ZIMA-Parts supports `[Directory] IncludeParameters=../` and
  `IncludeThumbnails=../`, allowing an export directory to reuse another
  directory's parameter metadata and thumbnail images. No explicit handling of
  these keys was found in the current CAD implementation. Investigate and add
  equivalent behavior, first checking Parts' relative-path base and file/stem
  matching rules. Preserve local directory labels, localized parameter values,
  native-document authority and protection/editing ownership; define local
  overrides and handle missing targets and include cycles. Verify Windows/Linux
  relative paths and parameters and thumbnails independently. The supplied
  example uses metadata Version 2 with `Parameters=@Invalid()`; document the
  intended mapping to CAD metadata Version 3 rather than assuming support.
  Directory Properties must also let users create, change and clear these links
  through the GUI, selecting a source directory separately for parameters and
  thumbnails without manually editing INI files. Persist relative paths where
  applicable; make the resolved source visible and validate it before saving.
  Reuse the internal properties-window conventions and translate new controls
  and validation messages into cs/en/de/fr/ru.
  Implemented: separate GUI directory links, relative/absolute source traversal,
  local overrides, matching export stems, source-native authority, revision
  invalidation, missing/cyclic-link rejection, and independent thumbnails.
  Windows GUI and five-language checks passed; Linux execution is unverified.
- **Native save/load and post-OCCT import overhead:** full loading, saving and
  reopening of the same 646,708,745-byte private native file measured
  19,758/59,570/22,685 ms before and 16,665/55,616/19,065 ms after. Buffered INI
  scanning and removal of actual cache-string copies preserve all validation,
  definitions and complete calculated packets. Input/saved sizes and full
  definition/packet hashes match. IO does not invoke OCCT. The ordinary reader
  still reconstructs/validates complete geometry, unlike File Manager's bounded
  parameter-only reader. Native IO peak memory and Linux measurements remain
  unverified. Detailed phase logs, numerical variability and the still-expensive
  import integration are recorded in the
  [large-model follow-up](performance/20261013-large-model-followup.md).

Preserve the original user documents. These are Windows observations; timings
and equivalent behavior on Linux have not been verified. The user requested
local compilation only, with no new GitHub Windows release for this follow-up.

The [2026-10-13 Fillet/copy-history investigation](performance/20261013-fillet-copy-history.md)
records exact copy-operand and reference-boundary reuse, parallel property
integration during first full regeneration, demand-based aggregate analysis and
sheet-source preparation, verified runtime provenance for
repeated regeneration, and measured Fillet edits/deletion on a private
FORM/Pattern model.

The [2026-09-30 application audit](PERFORMANCE_AUDIT_20260930.md) records current
Windows Release baselines, a solved-input Sketch dimension-edit cross-check,
prioritized optimization candidates and the dead-code review. It proposes no
product-code changes. Older results below retain their original platform and
fixture scope; they are not direct comparisons with the current Windows build.

The first follow-up, [independent equation blocks](performance/20260930-rectilinear-blocks.md),
addresses the measured large-Sketch dimension-edit problem. The solved-input
1,000-branch fixture decreased from 44,902.1 ms to 247.365 ms with unchanged
precision and verified geometry; this is a fixture-specific result.

The next follow-up, [Assembly scene transfer and picking](performance/20260930-assembly-scene-picking.md),
removes redundant scene copies and repeated picker work, with a desktop
benchmark and exact baseline comparisons. It does not add a persistent cache.

[Selection-filter and dimension-inspection refresh](performance/20260930-presentation-refresh.md)
removes measured whole-scene/Tree rebuilds for two presentation-only actions,
with GUI coverage of context restrictions and subsequent model changes.

[Repeated subassembly source refresh](performance/20260930-source-refresh.md)
reuses completed source preparation within one refresh, with open/closed source
measurements and invalidation/ownership checks.

The startup-independent C++ model benchmark is built as
`zima_cpp_performance_benchmark`. It is deliberately not registered with
CTest: normal tests remain deterministic and do not depend on machine speed.

Build and run it from an existing CMake build directory:

```bash
cmake --build build/cpp-debug --target zima_cpp_performance_benchmark
build/cpp-debug/zima_cpp_performance_benchmark
```

The command prints a `ZIMA_PERF_V1` contract followed by mean wall-clock
times over three repetitions. The fixture is fixed at 24 Part history
features, 256 Assembly components, and three nested Assembly levels.
Measurements cover:

* Part history calculation through the OCCT kernel;
* Assembly scene construction and ordered viewer picking;
* explicit regeneration of the complete open nested dependency chain.

Reported times are observations, not pass/fail thresholds. Compare runs on the
same machine and build configuration; compiler, OCCT, CPU, and thermal state
substantially affect them. The fixture checks result sizes while running, so a
regression in the measured operation fails independently of its duration.

## Historical Python comparison

The Python application and its benchmark tool were removed on 2026-09-15.
The following results are historical observations, not a current test gate.
Their source can be recovered from Git history before that removal.
Python `build_active_shape()` returned only the final live OCCT shape;
C++ `evaluate_history()` also materialized persisted ZIMA viewer packets for
every history boundary. These two timings measure different amounts of work.

Representative Linux Release measurements for the same 24 overlapping boxes:

```text
Python shape only:                   238.658 ms
Python final viewer packet:          628.028 ms
Python all 24 boundary packets:     4436.083 ms
Python changed-final viewer packet:  407.774 ms

C++ all 24 boundary packets:         846.270 ms
C++ changed-final boundary packet:    56.574 ms
```

For the same persisted-boundary work, C++ is about 5.2 times faster for a full
history and 7.2 times faster for editing the final feature on this fixture.
Exact ratios remain observational and must be remeasured on the same machine.

The native benchmark additionally covers live topology ancestry. A 24-feature
history followed by Fillet was measured at roughly 36-39 ms when editing the
Fillet with the live boundary cache, versus roughly 938-971 ms through a cold
kernel that must reconstruct ancestry from the beginning. The live cache owns
the OCCT shape and shared persisted-identity maps for faces, edges and vertices;
the maps are not serialized into the document and are not copied for every
cached boundary.

Example output from the bundled Linux debug build (milliseconds):

```text
ZIMA_PERF_V1 repetitions=3
part_history features=24 boundaries=24 mean_ms=1533.297
assembly_scene components=256 triangles=9216 mean_ms=23.015
assembly_picking components=256 candidates=25 mean_ms=4.834
nested_regeneration levels=3 mean_ms=38.082
```

## Build and startup measurements

These complement the model-level benchmark above with the remaining
acceptance-gate observations: build time and full-application startup
behavior. Same caveats apply — these are observations on one machine and
build configuration, not pass/fail thresholds.

Measured on the same Linux Debug/Ninja build directory (`build/cpp-debug`,
24 logical CPUs, `-j$(nproc)`):

* Clean build (`ninja -t clean` then full `ninja`, 86 build steps): ~37 s.
* Incremental build after touching one already-compiled `.cpp` (`main_workspace.cpp`,
  recompile + relink only that translation unit and its executable): ~5.2 s.
* Full `--verify-startup` run (headless `QT_QPA_PLATFORM=wayland`, exercises
  every scripted GUI regression scenario end-to-end — Part/Assembly/Drawing
  creation, edit/cancel/undo/redo, nested and multi-level Assembly occurrence
  activation, RMB selection cycling, file-management rename/cleanup dialogs,
  and Deep Assembly mate creation/DOF/drag): ~7.6 s average over 3 runs
  (7.61-7.69 s), i.e. real end-to-end regression coverage completes well
  under 10 seconds, not just a bare window open.

## Profile handle updates (2026-09-20)

Extrusion length and Revolution angle handles update transient preview geometry
and dimensions. They do not change placement references or calculate a solid.
The shared placement section must retain its reference-row widgets while the
required row count and third-point station presentation remain unchanged.
Reference edits still refresh their content explicitly. DOF updates compare the
required presentation with the rendered table, including rotation-only changes;
comparing only the previous translation DOF would miss those transitions.

The previous implementation cleared and recreated the table multiple times per
extent update. On the Linux release build, 12 creation/editing scenarios covering
Extrusion and Revolution on XY/XZ/YZ planes took approximately 35–41 ms per
update. The geometric preview itself took approximately 1 ms. These are local
observations on simple fixtures, not guarantees for arbitrary documents.

After retaining unchanged reference rows, the same 12 scenarios measured
0.23–0.97 ms per update. All parameter/widget-identity checks and the
full profile-frame UI contract passed. Placement reference assignment, removal
and command tests also passed (3/3).

Reproduce the correctness checks and timing measurement with:

```bash
QT_QPA_PLATFORM=offscreen ZIMA_VERIFY_STABLE_PLACEMENT_ROWS_ONLY=1 \
  ./build/cpp-native-release/zima_cpp_ui_contract_tests
QT_QPA_PLATFORM=wayland ZIMA_VERIFY_PROFILE_DRAG_ONLY=1 \
  ./build/cpp-native-release/zima-cad-cpp --verify-startup
```

The first check covers retained widget identity, row visibility after translation
and rotation DOF changes, the third-point station label, and reference replacement.
The second exercises the same extent callback used by the purple handles and
checks parameter updates and retained reference editors. Timing is reported
without a machine-dependent pass/fail threshold. The full profile-frame UI
contract additionally checks preview frames, Sketcher round trips and committed
geometry.
