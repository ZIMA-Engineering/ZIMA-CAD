# Fillet regeneration after source radius changes

## Report and isolated reproduction

The user changed the three driving radii of the Sketch owned by `Tažení 02`
in `50.prtz`. Recalculation was slow and the subsequent `Zaoblení 02` was
marked uncalculated. Investigation uses a hash-verified copy under the ignored
`build/fillet-radius-replay-20261123/` directory. The original is not modified.
The copied history contains seven operations, including four R2 Fillet routes
with fourteen selected edges, preceding rounds, a subtractive profile and a
subsequent Chamfer.

With the previously installed `f10b87780` implementation, cold history replay
spent approximately 19 seconds in the failing Fillet and another 19 seconds
retrying it during error recovery. Preceding operations took about one second.
These are reproduction timings, not a measurement of the user's original GUI
session. The first baseline ran while an unrelated native build was active.

## Changes

A solved tangent Sketch left a roughly -2e-11 mm circle-contact residual.
Fitting its artificial square-root cusp caused repeated station refinement
without convergence. Normalize only a residual within one tenth of the
existing 1e-9 mm center-fit accuracy. Keep the authored support surfaces and
check the fit against both actual offset-surface equations as well as its
analytic samples. The lightweight preview uses the same residual handling.

A Boolean trim could leave two separate contact edges with the same sewn end
vertices. Reuse the envelope contact only for an unambiguous matching edge,
after checking its pcurve against the actual support. Compose this replacement
into topology history. Sewing tolerance is unchanged; closed-solid, exact
topology, curve accuracy and final self-intersection checks remain required.

Each of four route replacements previously ran the complete Body
self-intersection check, followed by the same check on the final result.
Intermediate routes retain their exact topology and pcurve checks. The full
self-intersection check now runs once, after all routes and same-domain cleanup.
This is operation-local scheduling, not cached reuse across changed inputs.

## Measured native checks

Four successive changes multiply the original driving radii by 1.01, 1, 0.99
and 1. They use the production `commit_profile` operation and Sketch solver.
The same sequence was run serially before and after validation scheduling changed.

| Radius factor | Repaired geometry, repeated full checks | Repaired geometry, one final full check |
| --- | ---: | ---: |
| 1.01 | 14,488 ms | 9,667 ms |
| 1 | 16,341 ms | 10,413 ms |
| 0.99 | 14,211 ms | 9,654 ms |
| 1 | 15,459 ms | 9,969 ms |

Every changed commit calculates one Body. Unchanged OK performs zero Body
calculations and creates no document revision. Undo/Redo restores the same
calculated snapshot without calculation. Each result has no failed history
operation and passes exact BRep validity. Native save/reopen retains authored
history and Sketch definitions; cold regeneration agrees with the saved volume.
The four resulting volumes agree before and after the scheduling optimization.
The original R2 Fillet's BRep is also byte-identical before and after scheduling
changed (SHA-256 `0ba4df751bf10a30af56c547ed004ec02357827ba4746f1c426fa0c70cb39d2f`).

The native preview has sixteen strokes and zero Body calculations. Maximum
sampled distance to the calculated Fillet boundary is approximately 1.03e-10 mm
before and after native reopening. Forty-two support seams were checked;
maximum sampled normal-angle difference was approximately 1.21e-6 degrees.
These finite sampled checks are not global error bounds.

## GUI checks and remaining scope

The same four changes also pass through the actual Feature Properties dialog,
Sketch button, mouse double-click dimension editor and final OK. Measured OK
times are 10,431 / 11,330 / 11,525 / 11,366 ms. Each OK calculates one Body;
Sketch entry, the three radius edits and return to Properties calculate none.
All seven history rows remain valid. The test verifies unchanged treatment
definitions/reference choices, Undo/Redo and native close/reopen after each edit.
Unchanged OK creates no revision and performs no calculation.

The first Properties opening took 348 ms and materialized the preceding input
boundary once through the established deferred-history path. Subsequent openings
took 165 / 175 / 238 ms without calculation. This is distinct from the expensive
downstream Fillet replay; this change does not redesign history materialization.

The treatment GUI regression retains the shaded Hole input through repeated
Cancel/unchanged OK, then radius edits, Undo/Redo and reopening. Its opening
calculation count is zero. The four-route preview publishes all sixteen expected
strokes and ten source-node labels exactly. The Sketch exit matrix passes twelve
plane/radius-diameter/fixed-free cases with repeated edits/drags, draft/document
Undo/Redo, Cancel and reopen. The arc-size GUI matrix passes forty
language/arc/click variants and 120 value edits. Native dimension tests additionally
cover native/external constraints and atomic rejection. These are the applicable
existing regression matrices, not a claim of exhaustive Sketcher coverage; the
Sketch solver and editing implementation are unchanged.

The ordinary toroidal fixture at R1/R3/R5/R8 and the -2e-11 mm residual at R2
passed in both route orientations during focused investigation. A separate
+2e-11 mm fixture still fails strict contact validation. Its unweakened
reproduction remains available as `ZIMA_VERIFY_TOROIDAL_POSITIVE_RESIDUAL=1`;
it is not represented as supported by this fix. The user deprioritized further
Fillet edge cases while requesting an approximation-precision study similar
to the existing Sweep controls.

The complete native planar Fillet regression, thin feature wire regression and
Sketch dimension command matrix pass. The separate command-core driver passes
command persistence and node wires; the full command executable retains an
unrelated two-sided sheet conversion failure documented by the earlier work.
The full command executable still reports `Flat profile conflicts with its sheet
endpoint references.` in that unrelated sheet-conversion case. The assertion is
unchanged. No new Fillet setting or native document field has been introduced.
Fillet changes introduce no user-visible text; diagnostic output is English.
Catalog coverage and runtime language checks pass for cs/en/de/fr/ru.
The shared C++ implementation uses OCCT 8.0.1. Linux execution has not been run.

## Private approximation study

The existing Sweep precision controls separate approximation from Boolean and
display tolerances. Follow that distinction when assessing a Fillet control.
Private copies of the pipe builder changed only its approximation tolerance;
the 1e-9 mm center fit and strict contact validation remained in place.

| Pipe approximation | First measured run | Second measured run | Complete history |
| --- | ---: | ---: | --- |
| Existing default, at most 1e-10 mm | 15,367 ms | 15,796 ms | Valid |
| 1e-7 mm | 13,571 ms | 11,900 ms | Valid |
| 1e-5 mm | — | — | Contact validation rejected |
| 1e-3 mm | — | — | Contact validation rejected |

These serial process timings include native loading and verification, after one
warmup. The second pass reverses case order. They are not GUI OK timings and
cannot be compared directly with the preceding commit-only table. Rejected
results are not speedups. On this fixture, the accepted approximation reduced
mean elapsed time by approximately 18 percent. Both accepted results pass exact
BRep validity. Bidirectional distances from 514/516 viewer samples to the other
exact shape are at most 9.2e-9 mm, and their volume difference is approximately
3.4e-11 percent. Sampling is not a global geometric error bound.

The separate experimental variants remain private build copies. They do not
change installed defaults, source documents or the published release. A usable
coarser option needs further work on contact construction and regression
coverage; simply increasing the pipe tolerance does not provide it.
