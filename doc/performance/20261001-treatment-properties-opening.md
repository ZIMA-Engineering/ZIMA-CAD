# Fillet and Chamfer Properties opening - 2026-10-01

## Scope

Inputs are the calculated Part, a stored treatment and its edge routes. Outputs
must retain the input-body rollback, selected edges, treatment wire, dimensions,
reference identities, camera and unchanged OK/Cancel behavior. The existing
command-owned rollback refresh supplies the required geometry.

Baseline: `a44387dd`. Tree dispatch refreshed the final body immediately before
Properties installed and published its input-body rollback. Fillet and Chamfer
now join the existing guarded path that skips this unused first refresh. The
production change adds only these two kinds to that path. No command-local
selection, dimension preparation, route-start/R1 handling or preview callback is
removed. These callbacks have geometry-dependent side effects, so their repeated
invocations are not assumed to be interchangeable.

The guard requires a valid Part history container and resolved occurrence, no
open Properties window and no active Sketch. Creation, kernel calculation,
precision, shared placement, persistence, transaction ownership and dialog
close handlers are unchanged.

## Measurements

The opt-in native GUI probe in `edge_treatment_ui_verification.cpp` uses
`ZIMA_VERIFY_CONSOLE_ONLY=1`, `ZIMA_VERIFY_EDGE_TREATMENT_ONLY=1` and
`ZIMA_VERIFY_TREATMENT_OPENING=1`. It constructs native Sketch-based extrusions
of regular 4- and 128-sided polygons (circumradius 100 mm, height 40 mm), with a
0.5 mm Fillet or Chamfer on a top rim edge. Models are calculated and saved
before timing; only Properties opening and event processing are timed.

Windows Release/Fusion, fixed 1500 x 950 window, serial runs. Each fixture has
six openings: three unchanged OK and three Cancel. Warm means omit the first
opening. These are synthetic fixtures, not measurements of a supplied user
model or the complete regeneration time.

| Fixture | Before | After | Reduction |
| --- | ---: | ---: | ---: |
| Fillet, 4-sided prism | 35.565 ms | 33.733 ms | 5.1% |
| Chamfer, 4-sided prism | 36.308 ms | 34.676 ms | 4.5% |
| Fillet, 128-sided prism | 159.788 ms | 89.815 ms | 43.8% |
| Chamfer, 128-sided prism | 139.948 ms | 83.389 ms | 40.4% |

Every opening now publishes one base scene instead of two. The large fixtures
show substantially lower opening times; the roughly 5% differences on small
fixtures are close enough to timing variation that no broad speedup is inferred.
No claims are made about nested Assembly performance or total application speed.

## Equivalence and limitations

All 24 corresponding preview framebuffer hashes and 24 restored-frame hashes
match exactly. Every cycle verifies exact serialized viewer-packet restoration,
unchanged camera, document status and Undo/Redo availability. The accepted probe
asserts a single publication. A preliminary synthetic longitudinal-edge Fillet
on the 128-sided polygon was rejected by OCCT during fixture setup; the measured
before/after comparison consistently uses a valid top-rim-edge treatment.

The existing treatment GUI contract independently covers all five parameter
modes, edge/dimension picking, every annotation grip, orientation flipping,
Escape, creation, Cancel, parameter-plus-layout and layout-only commits, native
save/reopen, Undo/Redo and direct View dimension editing. No UI text changes;
localization review uses the five-language catalog contract. Added diagnostics
and documentation are English. The local Windows build and root launcher remain
the development entry point. Linux and portable releases are outside this task.

Evidence: [before/after probes and regression results](20261001-treatment-properties-opening.txt).

Final validation: all eight selected contracts passed in 82.33 seconds:
treatment commands, queries and GUI; refresh scope; selection filters; surface
profiles; Boundary Surface GUI; and translation catalogs.
