# Sketch dimension edit: independent equation blocks

## Cause and change

The [application audit](../PERFORMANCE_AUDIT_20260930.md) found a 44.9-second
dimension edit in an already solved Sketch with 1,000 independent branches.
Direct measurement isolated the delay to `seed_rectilinear_equations()`:
46,558 ms for its dense row-space QR, followed by 170.912 ms for the full Sketch
solve/check. These are separate runs, not an additive decomposition of the
44.9-second sample.

The seed previously projected each equation against all previous basis rows,
including rows using completely unrelated coordinates. The change partitions
equations by connected nonzero coordinate support and applies the same twice
reorthogonalized QR inside each block. Original row/column order, thresholds,
line search, branch checks, conflict handling and final validation are retained.
Nonlinear distance support is rebuilt on every Newton iteration. No coordinates
are published before the entire seed succeeds.

Only `cpp/modules/sketcher/src/rectilinear_template_solver.cpp` changes product
behavior. This is not a general Sketch solver redesign, skipped validation or a
lower-precision calculation. Dense equation construction remains; fully connected
systems can still be expensive. No persistence, placement or reference contract
changes are required. No user-visible text was introduced; five-language
localization coverage passed.

## Measurements

Same Windows Release environment and temporary solved-input driver as the audit:
Core Ultra 9 285HX, MSVC 14.51.36231, `/O2 /Ob2 /DNDEBUG`. Each solved-input edit
uses an independent copy; every edited and unedited endpoint is checked. These
are synthetic fixture timings, not a promised application-wide speedup.

| Operation | Before | After | Repetitions per result |
| --- | ---: | ---: | ---: |
| Already solved Sketch, 100 branches, dimension edit | 27.960 ms | 2.163 ms | 3 |
| Already solved Sketch, 1,000 branches, dimension edit | 44,902.1 ms | 247.365 ms | 3 |
| Initially unsolved Sketch, 1,000 branches, dimension edit | 62,616.498 ms | 279.015 ms | 1 |
| Direct seed, solved input with one changed value, 1,000 branches | 46,558 ms | 249.821 ms | 1 |

The measured solved-input 1,000-branch case improved by approximately 181 times.
Small timing differences elsewhere in the complete benchmark are not attributed
to this change: it does not alter OCCT calculation, Assembly scene construction
or picking. The existing complete benchmark passed its result assertions again.

Original measurements are in [the audit baseline](20260930-windows-baseline.txt)
and [solved-input cross-check](20260930-windows-solved-sketch.txt). New results:
[full benchmark](20260930-rectilinear-after.txt) and
[solved-input cross-check](20260930-rectilinear-solved-after.txt).
Direct seed probes are retained locally as `build/profile-dimension-before.log`
and `build/profile-dimension-after.log`.

## Verification

The new `zima_cpp_rectilinear_solver_tests` checks:

- 1,000 independent branches from solved and unsolved input; all anchors and
  endpoints, full solve status/DOF, native Sketch round trip and reversed distance;
- EqualLength connecting separate segments while preserving their free midpoint;
- contradictory and zero-support equations, with no partial publication;
- independent nonlinear distances preserving their direction;
- authored positive/negative zero while another block changes.

There is no machine-dependent timing threshold in the correctness test.

An additional temporary differential driver compared the old implementation from
Git with the new function on 500 deterministic cases, including disconnected and
coupled equations, free/fixed coordinates, reversed branches, nonlinear distances,
conflicts and permuted point order. Acceptance results matched, coordinates agreed
within 1e-8 mm and failures left input unchanged. The old implementation was not
retained in product or test source. Local evidence: `build/compare-rectilinear.log`.

Focused tests passed for the complete Sketcher contracts, dimension commands
(including Undo/Redo and side behavior), Sketch relations, templates, model
calculation, profile commands, and localization. GUI acceptance covers dimension
entry, Relations/View insertion, template editing and profile frames. This is
focused regression coverage, not a claim that the entire repository suite passes.

Local GUI and CLI were rebuilt. Repository-root `zima-cad.bat` remains the normal
development entry point. Published Windows 2026093004 remains immutable and does
not contain this later optimization.
