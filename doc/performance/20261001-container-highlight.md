# View container highlight pass — 2026-10-01

## Cause and scope

Inputs are the current calculated viewer packet and the common picker's hover
or confirmed candidate. The output is the same frame, wire priorities and exact
occurrence identity. Existing rendering already avoids reuploading the base mesh
for ordinary selection. The demonstrated extra work was CPU wire classification.

`original_container_wire()` scans the displayed edges to distinguish an authored
container wire from current derived sheet-state geometry. `paintGL()` called it
inside per-edge highlight tests and again inside the confirmed-wire loop. For a
Container candidate without a derived display-owner match, each call scanned the
complete edge list. A pass therefore performed quadratic work in the edge count.
The ordinary Occurrence path returns before that scan and is not the beneficiary
of this change.

The same decision now runs once per line pass, and once before the confirmed-wire
loop. The mesh and candidate do not change within those synchronous passes.
No result is retained between frames: source changes, selection changes, camera
movement and document changes still use current data. Original-wire and edge-
treatment lookup logic is unchanged, including preference for current Unbend/
Bend Back display ownership. Every original drawing call, sample, reference,
color priority and depth operation remains in place.

The change is limited to MeshView rendering. It does not alter the picker,
geometry calculations, shared placement, native storage or Undo/Redo.

## Measurement

Baseline product source: `b0f90e13`. Both binaries use the expanded
`zima_cpp_assembly_view_benchmark` on this Windows host with MSVC Release and Qt
Fusion. The synthetic scenes contain 256 and 1,024 occurrences of the same
calculated box (3,072 and 12,288 triangles). Container selection exercises the
feature-level contract used when editing a Part in Assembly context; ordinary
Occurrence selection remains a separate control case.

Each time below is the arithmetic mean of three completed `grabFramebuffer()`
calls after a warm-up frame, at 1200 × 800 pixels. It includes painting and GPU
readback, not just the optimized CPU scan. No source geometry is recalculated by
this measurement. These are controlled stress cases, not timings from a user's
large production assembly or a general application speed claim.

| Occurrences | Candidate state | Mode, opaque | Before ms | After ms |
| ---: | --- | --- | ---: | ---: |
| 256 | Hover | Shaded with edges | 41.919 | 23.516 |
| 256 | Confirmed | Shaded with edges | 60.310 | 24.310 |
| 1,024 | Hover | Shaded with edges | 394.979 | 81.373 |
| 1,024 | Confirmed | Shaded with edges | 760.375 | 80.019 |
| 1,024 | Hover | Hidden edges | 685.014 | 83.993 |
| 1,024 | Confirmed | Hidden edges | 1055.260 | 79.673 |

The independent scale check agrees with the cause: removing repeated full-edge
scans helps the 1,024-occurrence case much more than the 256-occurrence case.
The confirmed Shaded-with-edges frame is approximately 9.5 times faster in this
run; that ratio does not apply to ordinary occurrence picking or regeneration.
Raw timings and frame hashes: [measurement log](20261001-container-highlight.txt).

## Equivalence and validation

All 100 before/after RGBA framebuffer SHA-256 values match exactly: 20 ordinary
frames and 80 hover/confirmed frames, covering both sizes, both candidate kinds,
five display modes and opaque/partly transparent materials. The benchmark checks
that hover and LMB confirmation consume the same candidate, including occurrence
path, and that highlighting does not change the base-mesh revision.

The viewer contract, five-language translation contract, sheet-state GUI contract
and selection-filter GUI contract pass. The viewer contract includes derived
sheet display ownership and Fillet/Chamfer candidate wire rules. Logs:
`build/highlight-contracts.log` and
`build/highlight-selection-ui.log`.

Localization review: no product text changes. New benchmark diagnostics and this
documentation are English. The development entry point remains `zima-cad.bat`.
Linux verification remains deferred to the Linux host. This is a scoped local
Windows build and source change; no release archive is produced by this step.

The initial additional edge-treatment GUI gate failed during `fillet.create`, before its
highlight checks, with an ambiguous calculated-input edge diagnostic. Rebuilding
with the original MeshView source reproduces the same failure in a separate clean
working directory. That initial run did not establish full Fillet/Chamfer GUI coverage, and
no modeling/picking code was changed to bypass it. Evidence:
`build/highlight-edge-ui.log` and `build/highlight-edge-ui-baseline.log`.
The optimized source is restored for the final local build.


## Edge-treatment GUI fixture repair

The fixture still supplied obsolete box-coordinate edge keys after its solid had
been converted to a Sketch-based Extrusion. It now uses the existing
`test::profile_key` helper to resolve the intended vertical or horizontal edge
from the authored Sketch parent IDs. No product reference-validation or picking
rules are relaxed.

The test uses a bounded window size and frames the rollback scene after opening
Properties, leaving room for both Chamfer dimensions outside the solid. Line-grip
drags follow the projected outward direction of the annotation plane: a fixed
screen-space drag could instead hit the valid minimum envelope-offset constraint.
All original value, source-geometry, persistence, Cancel/OK, Escape and Undo/Redo
assertions remain in place. No new product strings are introduced.

The complete repaired Windows GUI check passed in 55.48 seconds: two Fillet modes,
three Chamfer modes, all applicable grips and all four creation/Cancel/OK cases.
Evidence: `build/edge-ui-repair.log`. The five-language catalog check also passed.
