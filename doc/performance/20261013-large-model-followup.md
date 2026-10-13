# Large native model follow-up, 2026-10-13

This is local Windows Release work on the shared C++ implementation, using
the pinned OCCT 8.0.0. It publishes no release and changes neither the native
format nor modeling precision. Linux execution remains unverified. The original
user documents are unchanged; probes use temporary or build-directory outputs.

## Inputs and method

The original native `Projects/02.prtz` contains 646,708,745 bytes, SHA-256
`3bd2725153af2efd7410c9c9b8e89845d1f56552029d395558aea56743683ca6`.
Its existing history and Body context are retained for native IO and navigation.

The STEP is
`C:/Users/vladi/Prace/ZIMA-Engineering/03-CAD/01-ACTIVE_PROJECTS/ZE0176-TOYOTA-G92K1/import/G92K1.stp`,
SHA-256 `b6c52c167676bc282b9a6a5d0ff11f6fcef98def0be72850c65a7992b1077411`.
A fresh import of this STEP is a different document from the user's edited
native file; their file sizes and histories are not a compression comparison.

Baseline source is `607cc6cd5b24933fdaa076dcd26451085d66fe47`, local identity
`2026101302`, with pre-existing local changes retained. Measurements run
serially, without a concurrent build or regression suite. Filesystem data is
warmed by copying/preceding reads. Single-run figures are observations on this
machine, not cold-disk results or universal performance promises.

## Native IO

The full Part reader scans INI lines in 64 KiB blocks, transfers strings rather
than repeatedly copying a large cache line, and parses cache text by const
reference. The INI cache string is moved into its std::map rather than copied
through const initializer-list entries. Inspection of the pinned nlohmann JSON
implementation confirmed its rvalue initializer lists already transfer their
owned values; speculative rewrites of those JSON paths were removed.
The normal full model validation and save-time
temporary-file reload remain mandatory. IO does not invoke OCCT calculation.

The optional metadata-test probe copies the input into a fresh temporary
directory, loads it, saves a separate document, reopens it and compares the
complete definition and every complete calculated-boundary packet. Copying
and the expensive independent packet hashes are outside operation timings.
Expected definition SHA-256 is
`2ee442d3d1c58c455aca619ef5908f24e5cc36dcffd3e432ae3d1faaafeb1508`;
combined complete packet SHA-256 is
`7fbcc212963b2fee1a5673733afea08ef69c8ab3bf1cc29092d6f5b346faf378`.

| Original native file operation | Before | After |
| --- | ---: | ---: |
| Full load | 19,758 ms | 16,665 ms |
| Save, including complete temporary-file validation | 59,570 ms | 55,616 ms |
| Reopen | 22,685 ms | 19,065 ms |

Input and saved file both contain 646,708,745 bytes, and both complete hashes
above match. INI scanning fell from approximately 3.9 s to 1.1 s; JSON
expansion, reconstruction, validation and destruction still cost most of the
operation. Intermediate after samples were 15,229/53,447/19,144 ms; variation
between runs is material, so a fixed speedup ratio is not promised. Logs are
`build/native-io-G92K1-before.log` and `build/native-io-G92K1-final.log`.

Reproduce with `zima_cpp_metadata_command_tests --native-io-performance
Projects/02.prtz`; enable `ZIMA_CPP_PROFILE_NATIVE_IO=1` for phase timings.
The metadata regression independently checks LF/CRLF, whitespace, duplicate
keys, unterminated final lines, Unicode and parameters crossing buffer bounds,
plus malformed input rejection. The shared-cache oracle requires the same
encoded bytes/block order, signed zero, empty data and rejection of cycles and
dangling references. Native documents, current templates/library examples and
parameter-only/full-reader equality are also covered.

## Selection rendering

The representative renderer uses screen-space rectangular segment fills for
confirmed display wire. General polygon tessellation and repeated painter
state setup are unnecessary for these convex rectangles. The optimized pass
retains the same ordered segments, projection, width, color and antialiasing,
uses convex polygon submission and holds one painter state across the pass.
Picking, persisted source/reference geometry and inspection behavior are
unchanged. An initial original-wire batching experiment did not address this
model's active path and is absent from the final implementation.

The navigation probe warms each mode, rotates the camera through twenty
samples, and records median/p90 framebuffer times for Wire, Shaded with Edges
and Shaded, both unselected and selected. The logical viewport is 662x699;
Windows uses its actual device pixel ratio. Two camera endpoints per state
are compared with baseline captures. Document state and mesh publication
revision must remain unchanged. All selected endpoint captures are exactly
equal; unselected shaded GPU rounding is bounded to one channel, consistent
with the established retained/fresh-buffer oracle.

| Display mode | Unselected before/after median | Selected before/after median | Selected before/after p90 |
| --- | ---: | ---: | ---: |
| Wire | 17.357 / 17.134 ms | 260.608 / 148.174 ms | 276.408 / 149.891 ms |
| Shaded with Edges | 15.958 / 16.226 ms | 260.097 / 147.408 ms | 287.226 / 152.485 ms |
| Shaded | 13.776 / 13.056 ms | 252.517 / 145.866 ms | 258.548 / 147.876 ms |

The final pass paints the same 55,980 selected display segments with one state
save/restore pair, replacing per-segment state pairs. It preserves their
individual overlap and antialiasing. The original source packet contains
39,661 source segments; this is a different set from the visible confirmed
display wire, and its size is not used as the optimized draw count. All six
selected endpoint comparisons have zero changed pixels; unselected shaded
comparisons differ on 3–13 pixels by at most one channel. Logs/captures are
`build/navigation-G92K1-before.log`, `build/navigation-G92K1-final.log` and
the corresponding `build/navigation-G92K1-before/` and `-final/` directories.

Use `zima_cpp_import_navigation_ui_contract` and optionally set
`ZIMA_VERIFY_IMPORTED_BODY_FILE`, `ZIMA_VERIFY_IMPORT_NAVIGATION_CAPTURE` and
`ZIMA_VERIFY_IMPORT_NAVIGATION_COMPARE`. `ZIMA_CPP_PROFILE_VIEW_PAINT=1`
records scene/annotation/selection phases and segment/state counts. Hover and
actual picking/selection are covered by the existing UI/import contracts.

## Complete STEP preparation and physical properties

The import reuses one OCCT transfer for product inspection and topology
capture. It retains complete, current physical properties. Imported immutable
faces now consume the existing ordered parallel tensor integration, at the
same 1e-12 tolerance, while the span-aware GK volume integration proceeds.
This overlaps independent work; it does not postpone properties or simplify
geometry. Error paths reuse the existing OCCT integration error text.

The preserved serial probe was intentionally stopped after a recorded 711.8 s
while still integrating volume/inertia. This is an incomplete-import lower
bound, not a completed baseline time. Its observed process peak working set
was 1008.10 MiB. The modified probe completed backend Part import in
692,036 ms and native save including validation in 41,402 ms. Its output
contains one calculated boundary, 205,818 vertices, 284,803 triangles and
17,216 original edges, and occupies 409,117,316 bytes.

| Modified import phase | Time |
| --- | ---: |
| STEP reading | 4,400 ms |
| OCCT transfer | 9,940 ms |
| Source/product inspection, including those two phases | 14,506 ms |
| Topology capture and native archive | 11,479 ms |
| Source disposal and history ownership | 427 ms |
| Meshing | 2,097 ms |
| Gauss full tensor integration, overlapped with volume | 43,493 ms |
| Span-aware GK volume integration | 607,169 ms |
| Surface properties | 4,775 ms |
| Calculated BRep persistence | 3,536 ms |
| Face packets | 5,251 ms |
| Edge packets | 34,525 ms |
| Body viewer aggregate | 223 ms |

These overlapping/nested times must not be added indiscriminately. The
completed profile identifies GK volume integration as the dominant remaining
cost, rather than the initially suspected tensor calculation. The import
still takes more than eleven minutes on this case; substantial improvement
to its total latency is not established. Final GUI scene publication is not
part of this backend probe. Modified process peak working set was observed
at 1008.40 MiB during integration; the completed save-time peak was not
recorded, so this is not a complete peak-memory comparison.

Logs are `build/STEP-G92K1-import-profile.log` and
`build/STEP-G92K1-import-parallel.log`. Reproduce with
`zima_cpp_step_model_contract_tests --import-native-performance <STEP>
<new-output.prtz>`, `ZIMA_CPP_IMPORT_PROFILE=1` and
`ZIMA_CPP_CALC_PROFILE=1`. Output must not already exist.

### Property equivalence

The rational STEP regression covers NURBS cylinders/tori, translated and
rotated/repeated/mixed sources, complete local properties and native save/reopen.
Local GK volumes, Gauss tensors, centroids and areas match independent serial
OCCT calculations exactly. Analytic tensor checks retain the previously
recorded 1e-6 bound: requested quadrature tolerance alone is not proof of
tensor accuracy. Whole-shape Gauss versus placed-Body aggregation uses this
same tensor norm bound, because their established reference-point arithmetic
differs even in the serial runtime. Local comparisons are not weakened.

Separate imports through the preserved old executable and the modified
executable compare every saved aggregate/local/input/output property snapshot
exactly on a translated rational model, repeated components and a rotated
mixed compound: 4, 8 and 8 snapshots respectively. The optional metadata probe
`--compare-native-properties <before.prtz> <after.prtz>` performs this check.
Baseline/modified frozen geometry comes from the same generated STEP files;
native loading supplies the complete saved values. These small exact oracles
do not claim a completed serial G92K1 comparison.

## Remaining verification limits

No Linux runtime or timing result is claimed. Peak memory for native IO was
not recorded. The general model-preview datum-marker/camera regression already
fails independently; focused foreign-preview checks pass without claiming
that full suite passed. See the companion
[File Manager and work-plane record](20261013-file-manager-followup.md).

The final fourteen-test Windows suite passed thirteen tests: File Manager,
focused STEP/DXF preview, strict rational properties, shared-cache encoding,
native documents/metadata, file-dialog icons, all-language translation
coverage, actual import interaction, Extrusion context, work plane, synthetic
navigation and the workspace integration. The general UI contract stops at
its existing `Command hover differs from the native Qt style` assertion on
this host, also with fixed screen scaling. No command-button styling changed.
The interaction-color-only branch now dispatches before that unrelated
precondition so the changed viewer pass can be verified independently; this
does not make the general UI suite pass. The initial native suite's other
seven contracts, including complete Body property workflows and the OCCT
high-span interval regression, passed; the expanded rational property matrix
subsequently passed after correcting its aggregate oracle to match the actual
Body hierarchy and the existing numerical tensor norm bound.

Focused interaction-color checks passed on the final renderer independently
(`build/focused-viewer-final.log`), while actual imported picking/confirmation
passed in `build/followup-final-regressions.log`. Translation verification
scans production source coverage, validates catalogs/placeholders and opens
affected dialogs in all five languages; it passed in the final suite.
The separate curve-presentation matrix also passed with its own focused
environment (`build/curve-presentation-final.log`). The development launcher
still selects the rebuilt GUI in `build/cpp-windows-release`; the CLI was
rebuilt alongside it. Existing user configuration, native models and unrelated
working-tree changes are retained. No GitHub release was created.
