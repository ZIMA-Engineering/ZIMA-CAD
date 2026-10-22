# Symmetric Feature endpoints and complete variable Fillet contours

Status: Windows implementation, focused native checks and expanded GUI checks
completed. This follows the separately recorded baseline
failure in [connected Fillet curves](20261022-connected-fillet-curves.md).

## Inputs, cause and bounded correction

Inputs are the authored Feature sides and their original Sketch parents. The
required output is complete, unambiguous operational edge connectivity, while
both original side identities remain available independently. The existing
Boolean ancestry completion and persisted viewer endpoint fields provide the
means; no new document field or topology-key syntax is introduced.

A symmetric rectangular extrusion contains two authored half-operations. Their
union retains 16 logical-side edges, including eight half-rails. At each shared
profile corner, two original vertex references locate one runtime vertex. The
general reference index correctly rejects that ambiguous pair. Feature-group
union omitted the vertex-completion step already used by ordinary Add, leaving
every half-rail without its complete endpoint pair.

The initial correction completed operational vertices after the group's Fuse.
The [source-binding follow-up](20261022-sketch-intersection-source-bindings.md)
subsequently narrowed this: the shared `feature_endpoint_vertices` helper derives
inspection endpoints through `complete_boolean_vertices`, while native propagated
aliases stay unchanged for downstream source operations. A shared junction still
receives the established `boolean:add:vertex` ancestry from incident native faces
and edges. Packet endpoint inspection and variable Fillet use the same helper.
Original side identities and geometry remain intact; the expanded native matrix
checks 16 native parent bindings alongside 12 operational endpoints. No arbitrary
parent, proximity or enumeration determines identity. This runs only during an
explicit body calculation.

The dependent variable-radius Fillet check found another defect: the indexed
OCCT `SetRadius(R1,R2,contour,1)` assigns the law to the first constituent edge.
On a two-edge rail, reversing R1/R2 changed volume instead of reflecting the same
blend. Attach each radius to the corresponding actual contour-end vertex through
the existing vertex overload. The persisted ZIMA R1 identity and reverse flag
still determine which physical endpoint receives each radius. This gives one law
over the complete contour, without changing approximation tolerances.

## Lifecycle and persistence

Creation, changed OK and explicit Regenerate produce the corrected endpoint
cache. Hover, read-only edge/route queries, Properties drafts and unchanged OK
consume saved data; this adds no calculation to those paths. Existing native
files keep their saved geometry until an explicit Regenerate. Regenerate an old
affected model to refresh its incomplete endpoint cache. There is no bulk rewrite
of user files, format migration, new required cache file or placement change.

## Finite verification matrix

`zima_cpp_symmetric_endpoint_contract` failed before the production correction
with `Symmetric edge has no stable endpoints`. Its final native matrix passes:

| Scope | Independent checks |
| --- | --- |
| Symmetric 10 mm cube | 16 distinct edges, 12 distinct endpoints, four shared junctions; exact corner coordinates |
| Repeated half-length edits 5/7/3/5 mm | Stable edge and vertex identities; volume equals 200 times half-length |
| Direction reversal and 11 mm profile offset | Correct coordinates; stable identities; Undo/Redo |
| Every longitudinal seed on either side | Eight seeds each traverse two half-edges and exactly two exterior endpoints |
| Constant R1 Fillet | Volume equals 1000 minus 10 times (1 minus pi/4) mm3 |
| Variable R1/R2 = 1/2 and reversed | Actual cap radii, volume between constant-radius bounds, equal mirrored volume, Undo/Redo |
| Native reopening and cold regeneration | Endpoint identities and resulting volumes retained |
| Unequal Extrusion/Extrusion, unequal Revolution/Revolution, symmetric Revolution, mixed Extrusion/Revolution | Complete endpoint pairs, analytical volume, both direction states, Undo/Redo and native reopening/cold calculation |

The initial dependent run exposed the variable-law defect (996.595112 versus
992.973706 mm3 after reversal); it was not accepted as a tolerance difference.
The corrected endpoint-vertex law passes the mirrored-volume and both cap-radius
checks without weakening precision.

Six current native suites pass: the new endpoint contract, ordinary profile
commands, Assembly profile commands, Boolean Part fragments, and edge-treatment
commands/queries. The ordinary GUI treatment matrix passes in 60.37 s; the
expanded symmetric matrix passes in 52.82 s. They cover five Fillet/Chamfer modes,
every annotation grip, Cancel/OK, Escape, direct dimension entry, Undo/Redo and
native persistence. Symmetric creation explicitly picks a longitudinal half-rail
through the common hover/LMB path and requires both halves in its pending route,
for both Fillet and Chamfer and both Cancel/OK outcomes. The actual FORM topology
GUI matrix passes in 13.77 s. Five-language catalog validation passes in 13.72 s.

Evidence: `build/symmetric-endpoint-native-final.log`,
`build/symmetric-endpoint-gui-final.log`,
`build/symmetric-endpoint-gui-expanded.log`. The native log records the separate
FORM fixture failure below alongside six passing current suites; it must not be
reported as an entirely green seven-suite run.

The final six-suite run passes in 14.96 s (`build/symmetric-endpoint-current-native.log`).
A fresh actual `2000.prtz` connected-curve GUI regression also passes in 4.57 s,
with exactly two Fillet builds: creation and the committed radius change. Draft,
Cancel and unchanged OK add none. Both original/input copies retain SHA-256
`1fe7f4a358c54f87604b8e1073f86bed4a9c99bb9c1db06b0d492527014db72f`.
Evidence: `build/symmetric-endpoint-2000-gui.log`.

## Separate old FORM test limitations

The older `zima_cpp_boolean_form_tests` is not green. Its first failure passes a
compound native kernel archive directly to `BRepTools::Read`, which expects raw
BRep data. A temporary diagnostic using the established calculated-body reader
passed BRep validity/integral checks, then exposed stale golden integrals and an
old expectation of twelve particular edge lengths. Those test-only changes were
reverted; modernizing that separate fixture is not part of this correction.

An independent native CLI comparison explicitly regenerates isolated copies of
the committed FORM fixture with both accepted release 2026102201 and this build.
Both return exactly 18497.263565667618 mm3 and 8563.084389152944 mm2, with identical
centroid and inertia. Evidence: `build/symmetric-endpoint-verification/form-baseline.json`.
The original golden volume 18497.255433974693 mm3 already differs in the accepted
release; do not attribute that pre-existing mismatch to this change or silently
raise tolerance. The old failing suite remains visible pending its own audit.

No Sketch solver, native constraint, source point or dimension representation
changed. These checks manipulate Feature and Fillet parameters, not Sketch DOFs.
Arbitrary conic/spline contours, every nested Assembly mouse interaction and a
native Linux build are outside this finite verification. The published 2201
archive remains immutable; this is a subsequent development correction.

Localization review: no new or changed product UI text. New test diagnostics are
English only; existing cs/en/de/fr/ru catalogs remain unchanged.
