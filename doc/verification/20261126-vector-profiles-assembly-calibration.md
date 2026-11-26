# Vector profiles, Assembly references and bend calibration

Status: implementation and the finite Windows checks below completed.
Committed-source packaging and publication are still pending. Baseline: `51b5982fc` on `codex/sketcher-mechanisms`.

## Scope and durable data

DXF insertion retains one embedded Sketch symbol definition and a native insertion
point. The existing View/Tree Explode symbol operation creates editable native
curves. The visible insertion label is now Symbol in all five languages.

SVG images have an explicit solid-profile checkbox. The original SVG remains
embedded and editable as an image. Filled paths and geometric shapes are converted
to private exact calculation profiles; the authored Sketch is not exploded.
Circular/elliptical arcs use rational curves. Separate painted elements are united.
The supported subset includes path commands M/L/H/V/C/S/Q/T/A/Z, rectangles,
rounded rectangles, circles, ellipses, polygons, polylines, affine transforms,
nonzero/even-odd nesting and transparent/hidden fills. Unsupported strokes,
clipping/masks, external images, CSS classes, SVG text/use and crossing subpaths
are rejected explicitly. This is not a complete SVG painting engine.

Selected TTF/OTF font bytes live inside the native text object. FreeType/HarfBuzz
shape the original font; quadratic spans are exactly elevated to cubic Beziers.
Extrusion retains editable text and its ordinary native anchor. Private topology
IDs include source text/font content, avoiding accidental rebinding after a glyph
change. Bounded caches depend on the authored inputs, direction and symmetric
height. The existing bundled font path remains available.

Assembly Project curve and Wrap curve store exact calculated results with their
definitions in `.asmz`. Component faces are read-only native sources, addressed by
owner, semantic key and relative occurrence path. New Assembly material features
remain disallowed. Sphere mates use a common exact center and retain three
rotational degrees of freedom. Plane mates now use exact plane metadata rather
than triangle-derived normals.

Native extensions are optional font/image fields, the sphere reference kind and
the required result field only on the newly supported Assembly curve operations.
No sidecars or new document extensions are introduced. Empty start templates were
rewritten and reopened with the changed implementation and remain byte-identical.
The GUI template-creation contract passes. Existing assets without new definitions
retain their existing serialized fields.

## Interaction lifecycle

Creation and editing reuse shared internal Properties windows and reference cells.
Reference picking, inspection, previews, Cancel and unchanged OK do not calculate
bodies or create history. Explicit OK/regeneration computes the result. New
components use New, component placement, then deferred activation after acceptance.

Container origins use the purple grip and the existing placement resolver. Drag
projection preserves references, signed offsets, selected branches and locks. The
old offset grip is hidden during this interaction; the offset dimension remains
editable. The separate purple rotation grip remains available and was checked
with repeated actual mouse drags, including Cancel restoration. The solver itself is unchanged. Unchanged construction OK is a no-op.

Technical opening-thread surfaces are intersected with the actual post-bore and
post-chamfer solid, so a curved entrance cannot expose the sheet outside material.
A disabled thread uses the authored plain-hole diameter. Windows application icons
use a taller mark. Surface tools begin with General Surface, dimension locking
uses closed/open lock icons, and component Properties offers a white Default Origin.

## One-bend K-factor calibration

Correction is a signed length for **one measured test bend**, never a total divided
among all bends. The pending K factor is updated by

`K_new = K_old + correction / (thickness * abs(angle_radians))`.

Global sheet settings expose the measured bend angle (90 degrees initially).
A Bend with its own K factor uses that Bend's pending angle and effective thickness.
The correction is limited to +/-1 mm in physical units, including inch input.
Invalid thickness/angle or a result outside [0,1] is rejected without changing K.
The small OK button updates the pending form and resets correction; only the main
Properties OK commits. Locked or inherited K factors cannot be calibrated.

## Finite Windows verification

MSVC Release, Qt 6.11 and OCCT 8.0.1. Linux execution remains unverified.

| Area | Verified evidence |
| --- | --- |
| SVG | Twelve shape/fill fixtures, two extrusion heights, rational circle equation, separate-element overlap, explicit crossing rejection, Add/Subtract volumes, Undo/Redo and exact native result readback |
| Fonts | Bundled font plus Arial, Times New Roman and Calibri; exact Bezier area vs extrusion volume; Forward/Reverse/Symmetric and three heights; repeated free-direction drags with one coordinate locked, fully locked rejection, text/height/rotation edit preserving anchor and embedded font |
| Vector GUI | SVG checkbox in cs/en/de/fr/ru, Cancel/OK/Undo/Redo/save; DXF import, retained block and Tree explode; actual file chooser and Arial replacement/Cancel/OK/persistence |
| Placement | 120 oblique-plane drags, cylinder offsets, Cartesian locks, 80 exact rational-circle drags with deliberately corrupted display chords, fixed and missing references |
| Placement GUI | Real mouse drags on offset plane with locked X and on circle in translated/rotated Body; Escape, Cancel, OK, no-op revision, native save and Undo/Redo |
| Work-plane GUI | 54 actual owned XY/XZ/YZ picks via arrow and text across Feature, 2D Sweep, Helical Sweep, Section, Sketch, Holes, Extrusion, Revolution and Plane; overlapping datums via RMB; independent offset equations, Cancel, OK and native reopen; Mirror/Pattern arrow and text routes in Part/Assembly; inherited Loft parent references |
| Sphere/plane mates | Native and GUI common-center checks; exact plane references remain valid when display triangles are distorted |
| Assembly curves | Plane projection/wrap and positioned-cylinder wrap; unchanged Part snapshots, exact BSplines, native save/reopen, Undo/Redo, no-op OK, explicit regeneration after component movement and retained output on missing target |
| Assembly curve GUI | Actual face picking and occurrence identity, independent inspection, draft preview, creation Cancel, Properties Cancel and unchanged OK with zero additional body calculations |
| Components | New Part/Assembly, placement Cancel/OK, activation, persistence and Undo/Redo |
| K calibration | Nine native angle/correction cases (30/90/135 degrees, -0.2/0/+0.5 mm), independently measured unfolded length; five-language GUI, mm/in, override, own angle, value lock and invalid-result rejection |
| Opening threads | Synthetic radial hole on radius-20 cylinder with 0/1.2 mm chamfer; thread points inside body and outside chamfer; unchanged solid volume; plain holes 8/8.5 mm |
| Existing contracts | Assembly history cursor, existing cuts, profile commands, symbol explosion and template creation pass after rebuilding their executables |
| Localization | Catalog/coverage contract passes for cs/en/de/fr/ru |

The final local runs passed Work plane GUI (116.56 s), Loft GUI (37.06 s),
2D Sweep GUI (27.17 s), Helical Sweep GUI (36.19 s), placement-origin GUI
(79.78 s), native work-plane contracts (1.30 s), and localization (18.87 s).
These are finite suite durations, including automated screen search, not user
operation benchmarks. The extended work-plane matrix has a 180 s test timeout.
Helical Properties was also inspected in its captured image: the View field is
readable without scrollbars and the original side-by-side Sketch layout remains.
The final owned-Sketch follow-up also ends the internal work-plane entry state
when Default Origin leaves the plane combo unchanged. Its Loft GUI regression
passed in 43.02 s, including the explicit inactive command-state assertion.
This follow-up changes no user-visible text.

The user's `02.prtz` was read and regenerated to an isolated copy. Seven history
boundaries reopen without errors; history and sketches are unchanged. Volume is
18,674,383.697969954 mm3 (saved baseline 18,674,383.69796996 mm3). The original was
not overwritten. This is calculation/readback evidence, not a manual visual review.

## Measurements and limits

Earlier serial OB8 measurements recorded cold Arial/Times preparations around
0.78/0.79 seconds and Calibri around 2.45 seconds; warm height-only changes were
about 0.11/0.115/0.185 seconds. They are finite local observations, not universal
speedup claims. Later font correctness checks ran concurrently with factory
verification and are not used as performance comparisons.

GUI coverage does not yet establish every cylinder/fixed/nested-occurrence drag
variant, every custom-font dimension kind, View-based DXF explosion, every SVG
viewport/clipping feature, or wrapping across multiple external support faces.
The shared ordinary solver paths have broader existing tests; this report does not
claim exhaustive new-feature coverage from those tests.

All 407 affected factory opening-thread assets were regenerated with the changed
native implementation and installed from the verified outputs. Authored history,
structure, Sketches, native result readback and physical properties passed; solid
volumes remain equivalent. Per-file hashes and measurements are recorded in
[the factory report](20261126-opening-thread-factory.json). This report does not
claim an exhaustive topology-ancestry comparison. Dependent Assembly points were
verified again after reference-curve regeneration and missing-source retention.

The separate proposed Sketcher Take geometry command was explicitly deferred by
the user and is outside this release. Work-plane fields remain plane selectors.

Committed-source packaging, signed Windows validation and public verification
are complete. See [publication evidence](20261126-windows-2602-publication.json).

## Committed-package gate follow-up

The first clean candidate correctly stopped at two mandatory sheet gates. The
Sheet Exchange test now creates its own disposable build parent in a fresh source
stage. Transition end-profile and end-Origin datums publish analytic plane
supports from their authored frames, including transformed origins and directions.
The Assembly solver's existing angular-departure search also considers rotation
about a coincident authored point; candidates still must reduce the complete
constraint residual. This resolves the antiparallel point/plane stationary case
without changing tolerances or accepting unsatisfied mates.

Sheet Exchange passed in 55.43 s. The expanded Transition contract passed in
44.57 s, covering repeated opposite/same normal choices with a fixed common point,
plane invariance under corrupted display vertices, exact end-frame equations,
native reopen and cold regeneration. Existing Assembly contracts passed in 0.93 s.
These fixes add no user-visible text and no new serialized field or format.