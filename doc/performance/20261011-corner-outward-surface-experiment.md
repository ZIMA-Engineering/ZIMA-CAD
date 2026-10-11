# Outward surface FORM investigation — 2026-10-11

Status: Windows trial candidates are available in the ordinary FORM library.
The user retains the final production style choice after practical evaluation.
The two original rounded/inward definitions remain as controls. The new corner
uses a solid cutter and a linked surface and deliberately leaves its transitions
sharp for ordinary manual Fillet. Automatic candidate rounding and native Linux
execution remain separate acceptance gaps. Earlier two-Sketch results below are
retained to explain the detected defect; they are not the delivered cutter.

## Inputs, means and required output

Use the current compact `CornerGusset90.prtz`, a genuine native 90-degree Bend
and two attached Flats. Keep its closed shape in a preceding `FORM_CUT` Body,
extract a linked outer surface into the later `FORM` Body, subtract the closed
solid, add outward normal thickness `t`, and Fuse. Ordinary downstream Fillets
are evaluated separately. The initial two-Sketch radius-depth proposal was
rejected after detecting excess removal in the curved Bend domain.
The target is a connected, valid result without a thickness-induced joint recess,
preserved reference/side intent and a complete calculation comparable in time to
the retained closed-solid implementation. The same outward authoring direction
is being investigated for Ventilation Window independently.

## Completed unrounded check

The fixture is 60 mm along the Bend and 30 mm along each Flat. The nine `(t, Ri)`
pairs in millimetres are `(0.5,0.5)`, `(1,1)`, `(2,2)`, `(3,3)`, `(4,4)`,
`(0.5,2)`, `(1,2)`, `(2,4)`, `(3,6)`. Outward walls of `0.5t` and `t` produce
18 strictly valid, connected, single-solid BReps with positive overlap with stock.
Direction is independently checked against the original closed tool: the
outward wall has zero volume inside it. Inward and symmetric controls are
also recorded, for 22 calculation rows in total.

Both contact Sketches reproduce the original closing-face profiles exactly:
native Extrusion versus closing-face prism has zero two-way difference volume.
Circular source arcs project to native elliptical arcs, rather than polylines.
The original FORM Body's persisted 180-degree X rotation applies to both tools
and skin. Native XZ uses -Z as its local vertical direction. Omitting either
convention gives an incorrect experimental result.

Boundary sampling uses eleven curve parameters per edge and depth fractions
`0.1`, `0.5`, `0.9`, `1.0` on both actual Cut walls, excluding points inside
the Bend domain. Original BRep distances verify stock contact and wall coverage.
Full `t` walls pass all tested contact depths within `1e-7 mm`. Half-thickness
walls remain connected but leave a local inner-edge recess: the largest sampled
clearance at `t=1 mm` is `0.207106781186546 mm`. A connected solid alone therefore
does not prove full-thickness joint contact. Full thickness still leaves sharp
transitions before Fillet; the experiment is not a strength qualification.

## Reproduction and pictures

`zima_cpp_sheet_form_geometry_tests --probe-corner-outward-form
config/lib/01-SHEETMETAL/01-FORM/CornerGusset90.prtz` writes the native authoring
copy under `Projects/test` and diagnostic data under
`build/form-diagnostic/outward-corner`. `ZIMA_CPP_OUTWARD_PREPARE_ONLY=1` prepares
only the authoring copy. These are disposable test outputs, not product sidecars.

The centre-section PNG/SVG figures derive from actual BRep sections. Display
polylines join nearby endpoints within `1e-4 mm`; this affects rendering only
and never changes geometry or acceptance tolerances. The thickness comparison
draws the independently calculated fused outline and marks the pre-Fillet sharp
transition separately from the half-thickness recess. Light and Dark figures
were visually inspected. Native 3D captures show the unrounded fused body.

## Pending native integration and performance

The candidate uses explicit native `form_outward` library intent, copied with
the independent embedded definition, rather than silently changing the meaning
of inward authoring. Outward corner tools depend on the actual outer Bend radius;
that depth participates in prepared-tool cache validity. Creation, reference
entry, inspection and Cancel continue to consume calculated viewer data.
All geometry work remains on explicit calculation paths and shares C++ code
between Windows and Linux. No shared container-placement equation is changed.

Unrounded geometry stages observed at roughly 0.38–0.48 s exclude profile setup,
reference ancestry, complete properties/display, Fillets and transaction costs.
They cannot establish a product speedup. Compare full old/new insertion on the
same stock, serially with identical dependency/property/display requirements,
and report cold preparation separately from repeated prepared definitions.
Do not choose the production style before the user evaluates the results.

Linux native verification remains pending on the Linux host. New product-visible
text has not been introduced by the calculation experiment; localization review
must accompany any subsequent UI additions.

## Native candidate integration and current limits

The closed-tool factory corner keeps its existing automatic transition radii.
The outward surface candidate deliberately leaves its real transitions sharp,
following the user's request on 2026-10-11 to try ordinary manual Fillet after
insertion. This is explicit candidate behavior, not an automatic fallback after
a failed Fillet. Selecting a production style remains the user's decision.

The initial Fillet seed requested by the user is the actual FORM–Bend junction,
not the earlier Bend–Flat boundary used for positioning. Diagnostic full-route
rounding of the outward candidate failed with OCCT WalkingFailure on the Flat
segments. The retained closed-tool control succeeded. Reversing stage order,
regularity checks, face/edge unification and offset-join variants did not qualify
an automatically rounded candidate. An oblique-Cut trial was rejected by the
ordinary native Elliptical Arc Extrusion contract before calculation; the
candidate retains normal Cuts and does not alter that general contract.

The Ventilation Window outward candidate passed native placement on both stock
sides at rotations 0, 35, 90 and 180 degrees. Each of these eight cases checked
unchanged OK, a subsequent position edit, Undo/Redo, Unbend, save/reopen,
regeneration, embedded-definition/reference preservation and read-only source
history. Thicknesses 0.5, 1, 2 and 3 mm produced strict valid single solids;
4 mm failed the thickening solid gate and still needs a matched baseline check.
The outward skin is shifted by one stock thickness from its outer attachment
datum so that its rim engages the full sheet thickness. This is a deliberate
candidate geometry change, not evidence of equivalence to the inward window.

The complete Window GUI contract passed in 81.54 seconds, covering face picking,
reference entry, placement preview, resizing, OK/Cancel, rollback, replacement,
Undo/Redo and reopening. One GUI confirmation took 1.555 seconds. General UI and
five-language catalog contracts also passed. These are verification observations,
not controlled old/new performance measurements. Linux execution remains pending.

## Sharp corner candidate acceptance

The checks in this section concern the earlier two-Sketch cutter prototype.
They establish native validity and editing behavior, not absence of notches in
the Bend. The contact-depth sampling above deliberately excluded the Bend and
therefore did not detect the user's subsequent defect in `Projects/01.prtz`.

The locally built candidate passed all nine native `(t, Ri)` cases listed above:
strict BRep validity, exactly one solid, independently integrated volume and
inertia. Complete FORM kernel calculation was 0.311–0.374 s per case on this
Windows machine. This includes native tools, thickness, trim, Cut/Fuse, ancestry,
properties and viewer data; it excludes input-stock/source preparation and the
ordinary downstream Fillets. It must not be presented as an equivalent rounded
corner speedup.

The practical Part is
`Projects/test/CornerGusset90-outward-unrounded-placed.prtz`; its independent source
is `Projects/test/CornerGusset90-outward-surface-experiment.prtz`. Replacement
preserved the exact attachment references and passed unchanged OK, Undo/Redo,
save/reopen and regeneration with independently verified geometry. Both files
are disposable local experiment outputs; the factory library is unchanged.

The complete outward corner GUI contract passed in 45.79 s. It checked actual
hover/click outer-face selection, creation, centroid axis, placement edits,
inspection/rollback, Cancel, unchanged OK, definition replacement, Pattern,
Mirror, Unbend/Bend Back, Undo/Redo and reopening. Creation confirmation took
1.176 s; these are single command observations, not a controlled speedup claim.
The retained factory corner roundtrip/copy fixture passed separately in 68.27 s.
Manual transition Fillet remains for the user's practical evaluation; both
complete automatic radius orders were tested and neither qualified the new
candidate. The source flag changes candidate behavior explicitly and is not a
hidden environment-dependent fallback.

## Correction: the corner cutter must follow the three-dimensional shape

On the user's `Projects/01.prtz`, a read-only independent comparison retained
the exact source history and placement, omitted downstream Fillet only in a
calculated copy, and compared the normal two-Sketch cutters with the original
closed solid. The actual stock thickness was 1 mm and outer Bend radius 2 mm.
The two prisms remove 16.503756936 mm3 more stock than the shaped solid; after
adding the same outward wall, 0.028942546 mm3 of that additional removal remains
unfilled. Both results are strict valid single solids. This demonstrates why
connectivity and BRep validity alone did not qualify the earlier prototype.

The untrimmed independent Cut/Fuse results had volumes 54007.180346058 mm3
(Sketch cutters) and 54007.209289467 mm3 (solid cutter). One serial observation
of the Cut/Fuse stage was 0.257 s and 0.134 s respectively. This is diagnostic
evidence, not a repeated full-command benchmark or a claim of complete visual
acceptance. Reports and display packets are disposable outputs under
`build/form-diagnostic/user-01-cutter-comparison/`; the user's Part is unchanged.

The agreed authoring structure is a preceding FORM_CUT Body containing the
closed negative tool, followed by a FORM Body with linked exact surfaces from
that solid. Zero-thickness surface extraction gains an explicit preceding
source Body; it preserves the source and stores the dependency in native Part
history. The ordinary planar FORM retains its cut Sketch. The first delivery
must verify source edits, Body frames/order, rejection of forward/self sources,
Undo/Redo, unchanged OK, save/reopen and native source/child identity, together
with GUI selection and all five localized catalogs. Verification of this new
structure is recorded in [Linked Body surfaces](../LINKED_BODY_SURFACES.md).

## Delivered solid-cutter trial

The ordinary library picker now offers
`CornerGusset90-EXPERIMENTAL-unrounded.prtz` and
`VentilationWindow-EXPERIMENTAL-outward.prtz`. The corner retains its complete
editable native cutter and a zero-thickness Shell in the following FORM Body.
The two XY/XZ closing faces are omitted by their original persisted identities.
The native profile-centroid axis is preserved. The Window retains its cut
Sketch with CUT before FORM; reordering preserves each calculated role's area,
volume and face identity. A user's earlier saved two-Sketch trial was retained
in a disposable Projects/test copy before replacing the library candidate.

All nine `(t, Ri)` cases listed above pass strict BRep validity, one solid,
independent properties and two-way geometric difference against separately
executed solid Cut, ordinary outward Surface Thickness and Fuse. The complete
Bend domain is included; no contact samples are excluded near its radius.
Observed complete FORM kernel calculations were 0.160–0.190 seconds before
the source-centroid presentation follow-up. These exclude source/input-stock
preparation, GUI publication and downstream manual Fillets.

Five alternating serial cold observations on identical 1 mm stock with
`Ri = 2 mm` gave medians 1.067990 seconds for the retained rounded control and
0.166012 seconds for the new unrounded full-thickness candidate. The source
geometry and rounding rules intentionally differ, so this is a cost comparison,
not equivalent final geometry or a promise for every model. Comparison equations
run after the timed native calculation. Source-assembly, rendering and complete
interactive confirmation must be reported separately.

Replacing the FORM in a read-only diagnostic copy of the user's `01.prtz`
preserves exact attachment references and passes unchanged OK, Undo/Redo,
independent save/reopen and regeneration. Downstream manual Fillet is omitted
only from that diagnostic copy. The original user Part remains untouched.

Final Windows native regression passed five suites in 256.50 s: Shell command,
FORM definition, FORM geometry, ordinary copies and copies after Unbend. The
new linked corner and outward Window passed their complete GUI contracts in
34.59 s and 85.09 s respectively. The start-template GUI check passed in
37.41 s. Source profile centroid axes are carried from the calculated cutter;
when authoring hides those axes, explicit FORM preparation uses the existing
native profile-centroid calculation rather than reconstructing them in the UI.
The five-language rendered-control check, unchanged ordinary surface thickness,
portable settings and final Shell checks also passed.

The final inside/outside preview of the diagnostic `01.prtz` copy still shows
sharp projecting junctions at the FORM–Bend connection. Their agreement with the
independent Cut/Thickness/Fuse construction is not evidence of a finished
rounded appearance. Manual Fillet and the user's visual/style decision remain
open. No automatic rounding success or complete absence of visible projections
is claimed.

## Matched Window timing follow-up

Five serial cold observations at 1 mm on the same 200 mm stock gave medians
1.35993 s for the inward Window and 1.41033 s for the outward candidate (+3.7%).
Their geometry intentionally differs, so this is not an equivalence benchmark.
Both 4 mm variants failed the same valid-solid thickening gate. The inward
3 mm complete probe exceeded 150 s; a bounded profile reached result meshing
but did not finish the mass-properties phase. The outward 3 mm calculation
completed in 4.846 s. The baseline timeout remains an unresolved limitation.
