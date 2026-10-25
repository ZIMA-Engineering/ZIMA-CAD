# Sketch junctions, selection and placement performance — 2026-10-25

This follow-up uses shared C++ with OCCT 8.0.1 and Qt 6.11.0. The user approved
repairs to the common Sketch solver and batching of whole-Origin scene refreshes.
Native document formats, placement equations and reference/side contracts remain
unchanged. Private construction models were tested through isolated copies only.

## Causes and focused changes

- A modeled feature with a visible internal construction axis had a complete
  authored preview in the GPU cache, but the painter incorrectly treated that
  feature as a native Sketch and skipped its material strokes. Classify the
  preview from its own strokes. Tree/View selection and inspection retain the
  same authored source without calculating a solid.
- A shared profile/construction endpoint retained its native profile role in a
  construction-only passive display packet. Mark that displayed helper endpoint
  as construction; retain its actual native incidence and active-Sketch role.
  Axis points now hide both passive endpoints. Newly assigned construction curves
  default to hidden outside Sketcher; explicit Show in 3D and existing saved
  visibility remain authoritative. Revolution's calculated axis is independent.
- Equal-length and symmetry residuals produced spurious numerical rank when
  differentiating dependent nonlinear rows forward. Use centered derivatives for
  these smooth rows only. Zero contact residuals retain their established
  one-sided derivative. The existing bounded simultaneous fitter handles failed
  symmetry/perpendicular solves and symmetric dimension edits/handle movement.
  Precision, rank tolerances, native points and read-only supports are retained.
- Whole-Origin primitive input now defers intermediate scene publication while
  performing every reference solve and initialization in its original order.
  Publish the completed scene once. Body Properties retains its separately owned
  synchronous preview; nested deferral remains owned by its outer caller.
- Edge-packet preparation reuses each fixed native face's classification explorer
  within one result packet. The cache expires with that packet. Projection,
  sampling, gap policy, classification tolerance and side metadata are unchanged.
  The optional `ZIMA_CPP_VERIFY_FACE_CLASSIFIER_REUSE` diagnostic evaluates the
  original OCCT classifier alongside every reused query and requires exact states.
  See OCCT's [classifier](https://github.com/Open-Cascade-SAS/OCCT/blob/V8_0_1/src/ModelingAlgorithms/TKTopAlgo/BRepClass/BRepClass_FaceClassifier.cxx)
  and [face explorer](https://github.com/Open-Cascade-SAS/OCCT/blob/V8_0_1/src/ModelingAlgorithms/TKTopAlgo/BRepClass/BRepClass_FaceExplorer.cxx).

## Finite native matrix

The new symmetric-junction matrix passes 384 cases: native fixed or external
planar support, two coordinate offsets, both support sides, both selection orders,
both constraint insertion orders, Distance/X/Y, driving/reference and
locked/unlocked states. Each case independently checks equations after repeated
value edits and three same-side drags, actual free movement, fully constrained
immobility, unchanged external data/native incidence, atomic rejection of an
impossible parallel constraint and exact serialized reopening. The saved real
junction also accepts perpendicular and point-vertical input in both orders.

Sixteen final native suites pass: junctions, 768 view-relative H/V cases, 144
coordinate-frame cases, relation commands, keypoint contacts, broad Sketcher
contracts, profile centerlines, placement assignment/commands/removal, edge
queries/treatments, Bend, Shell identity, FORM geometry and Thin feature wires.
The broad Sketcher suite includes repeated dimension/drag/contact/trim matrices;
it is additional regression evidence, not exhaustive coverage of every graph.
The placement-removal fixture now measures actual native volume rather than
comparing an uncomputed physical-analysis cache value with an analyzed result.

The classifier oracle passes edge treatments, Bend, Shell, FORM and Thin suites
and a fresh 17-operation calculation of the real model. All 17 boundary reference
packets compare exactly against the pre-optimization calculation after removing
only disposable enumeration order: coordinates, winding, curve direction,
identities and side metadata are retained. Authored history and Sketch definitions
are unchanged; native save/reopen is exact. Integrated mass-property roundoff
remains below 7e-13 relative for the model's volume.

## Measured scope

Measurements use isolated Windows Release binaries and a saved copy of the
17-operation construction model, serially on the same host. They include the
explicitly edited final feature and its valid cold reference-ancestry rebuild.
They are observations, not portable timing thresholds.

| Interaction | Before | Final | Deterministic check |
| --- | ---: | ---: | --- |
| New feature: Default Origin input | 2,850 ms | 1,612 ms | Five scene publications reduced to one; same references/frame; Cancel unchanged |
| Edited junction: feature OK | 40,864 ms | 37,794 ms | Same 17 boundary reference packets; authored data retained |
| Native local-history calculation in that OK | 38,196 ms | 35,045 ms | Same operation count; cold ancestry remains required |

Opening, Sketch entry, constraint editing, Undo/Redo and save/reopen were timed
separately. Save/reopen still takes about 24.6 seconds in this model; this patch
does not claim to eliminate physical-property integration or native I/O costs.
An earlier explorer-per-edge experiment gave a similar improvement; the final
bounded per-face implementation was verified independently.

## GUI, localization and coverage limits

Real Qt mouse-event diagnostics exercise all four selection/insertion-order
combinations on the isolated saved junction, successive drags, feature commit,
Undo/Redo and save/reopen. The four cases pass individually; an initial combined
run exceeded its overall timeout after three successes, so the fourth was run
separately. Feature selection additionally checks rendered stroke pixels, not
only prepared arrays. The original failing axis-only presentation was reproduced.

Twelve final GUI suites pass: Default-Origin entry on the real model, whole-Origin
Feature/Sketch button/Tree input (one publication, Cancel, OK, save, Undo/Redo),
H/V, dimension input, hover, authored selection, axis endpoints, construction-role
menus/visibility, profile-frame creation/editing, surface placement, Body offsets
and Loft. The separate GPU test verifies actual selection pixels in all five
rendering modes, including the occluded feature with its internal helper axis.

An additional placed-Sketch-reference return diagnostic fails because its expected
external point is absent from the displayed packet. The unchanged published 2501
executable reproduces the same failure before these changes. This remains an
unresolved baseline diagnostic/product uncertainty; rotation/re-entry of that
specific external-point fixture is not claimed as verified. The affected
whole-Origin and surface-placement matrices above pass independently. Detailed
private logs retain the baseline and final results; the failure is not omitted
from the final matrix.

File Manager source branding uses the shared ordinary-folder renderer and
localized metadata for factory and custom sources, with Home fixed to its normal
icon/label. Seven factory roots now carry logo-text.svg; engineering-table column
labels are complete in cs/en/de/fr/ru. Tests cover all five languages, logo-only,
logo-with-text, missing/unavailable sources, navigation, row bounds and theme
changes; the full File Manager suite passes. No new untranslated product messages
are introduced. All five translation catalogs validate.

Arbitrary drags across a selected support side are not exhaustively covered:
one exploratory crossing request was rejected atomically. This is not evidence
of unrestricted cursor projection. Symmetric-junction trimming, every dependent
assembly graph and all free-rotation configurations are not exhaustively tested.
The existing broad baseline failures remain documented in the OCCT upgrade
record. Native Linux execution remains unverified on this Windows host.
Desktop automation's runtime failed to initialize on this host; GUI evidence
comes from purpose-built Qt event/pixel diagnostics, not manual app control.
