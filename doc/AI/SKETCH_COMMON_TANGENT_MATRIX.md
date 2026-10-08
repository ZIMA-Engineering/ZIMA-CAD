# Circle common tangents and Sketch Mirror, 2026-10-08

## Defects and implementation

The saved `FORM-EDGE.prtz` Sketch reproduces two independent defects: creating
the left common tangent reports a redundant point-on-curve constraint, and
adding the small-circle tangent to the existing left segment reports a conflict.
Its construction axis shares the small circle's center and positions the other
circle by Symmetric. The checked-in native Sketch fixture retains these identities
and equations; the personal Part is not rewritten by the tests.

Common tangent creation publishes both C+T pairs in one native transaction.
Intermediate rank checks must not omit a contact or a tangent at a singular
configuration. Both relations are needed after radius or position changes.
The original document remains unchanged if the complete system cannot solve.

A pure ZIMA geometry query enumerates the available inner and outer circle
tangents and selects the contact pair nearest the two click locations. Inference
and confirmation share this query. Contact-distance intent avoids amplifying
a small quadrant snap by the segment's entire length. Preview displays the same
two exact contacts that confirmation creates. Ordinary explicit K remains
available when the two-click geometry does not offer a valid double C+T.

The existing two-circle projection accepts persisted external circle centers and
unbound segment ends. An interior contact has its own C point and line incidence;
the segment keeps its longitudinal endpoint freedom. Existing side choices and contact identities remain
distinct. A read-only circle contact is seeded analytically from the dragged segment
endpoint; the closest existing tangent branch and source geometry stay fixed.
Length/X/Y/angle edits retain their equations in driving, reference and locked
states. Other constraint rank equations and calculation tolerances are unchanged.

Sketch Mirror axis acceptance no longer depends on Offset dialog state or rejects
its own active command. Escape retires the command and restores ordinary picking.
The established native reflection implementation, point deduplication and
dimension ownership remain unchanged.

## Dimensioned arcs and zero-length extrusion ends

The later FORM-EDGE fixture contains three trimmed arcs, two tangent segments,
an axial construction line, symmetry, two radii, height and width. The existing
coupled circular seed fitted its radii and tangencies but omitted the signed
point-to-line height equation. Include that equation in the existing retry path;
do not relax rank, residual or degeneracy tolerances. External Axis contacts may
translate the native arc group but never the read-only support.

Planar Up To accepts zero distance where the authored profile touches the limit,
including an entire straight edge in that plane. Crossing the plane remains an
error. Validate the authored path separately from Thin thickness overhang, then
clip the complete wall at the exact limit. Open Thin closure follows connected
wire traversal; unordered stored edge iteration must not silently omit an offset
side. Persisted source-curve and endpoint parents remain unchanged. The related Surface
regression also exposed generic automatic-axis endpoints remaining selectable
after their surface was hidden. Remove those calculated path-point roles together
with the already hidden axis, while retaining unrelated origins/references.

The cyan preview projects each source sample to its actual limit, retains zero
contacts, omits zero-length longitudinal connectors, and clips Thin preview
polylines to the same planar halfspaces. It uses only ZIMA native geometry and
resolved references; it never calculates an OCCT body or commits the document.
Surface and Thin accept the user's open contour. Solid requires a closed region;
a separately authored triangular region tests a whole edge in the two limits.

## Verification matrix

The focused native test is `zima_cpp_sketch_mirror_tangent_tests`; GUI tests are
`zima_cpp_sketch_mirror_ui_contract` and
`zima_cpp_sketch_common_tangent_ui_contract`. The broad existing Sketcher,
dimension, curve-command, snapping and external-reference tests remain required.

| Matrix | Required checks |
| --- | --- |
| Original FORM-EDGE | Both T selection orders; left common tangent; three successive radius edits; serialization/reopen; independent line/circle distance and finite-contact equations |
| Click/size/orientation | Three translations; eight center-line orientations; six equal/unequal radius pairs, including reversed sizes and small radii; both selection orders; all sixteen quadrant pairs; closest valid branch; unchanged source centers and radii |
| Circle boundaries | Overlap, external touching and either side of touching; eight cardinal/quadrant clicks per circle; both orders; concentric/nested circles rejected atomically |
| Size dimensions | Radius and diameter; driving/reference; locked/unlocked; both orders; all four branches; native and external point anchors; successive edits and actual center displacement; reference values match geometry |
| Segment dimensions | Length, X, Y and angle; successive value changes; both tangent equations retained |
| Native/external supports | Native/external and external/external circle pairs; four branches; both orders; three source refreshes; immutable reference data; reopen |
| Explicit external-circle T | Both selection orders, contact sides and endpoint/interior contacts; Length/X/Y/angle in driving/reference and locked/unlocked states, successive edits, actual endpoint drags, native reopen and independent C/T/dimension equations; locked reference creation rejected atomically |
| Interior contacts | Explicit T creates C on the circle and line incidence at a stable point; all four branches; actual center drag; finite tangent segment; endpoint freedom retained |
| Trimming | Explicit T with endpoint/interior contacts; both orders and sides; circle divided at contacts; retained arc keeps both T relations; successive radius edits and reopen |
| Rejection | Locked reference dimension, duplicate T, impossible tangent and fully constrained endpoint drag leave the document unchanged |
| GUI common tangent | All four branches and both orders with Common Tangent, Segment and explicit T; native/native and native/external supports; actual clicks, Undo/Redo, inline radius edit, native Part save/reopen and both C+T pairs |
| Dimensioned arc edits | 64 combinations of X/Y reflection, native/external Axis support, reversed tangent/symmetry order, locked/unlocked and R/D; 768 successive dimension edits, 240 actual unlocked drags and 320 fully constrained rejected drags; signed height/width, arc incidence, both tangencies, symmetry, reference/read-only states and reopen |
| GUI dimensioned arcs | All four user dimensions edited three times, Undo/Redo after each change, actual center drag, native reopen and independent arc equations |
| Zero-length Up To | Tilted FORM-EDGE Surface and three Thin sides; a closed Solid with a whole top edge on the limits; positive area/volume, independent halfspace checks, preview persistence and repeatability; existing wrong-side/crossing rejection |
| GUI cyan preview | Convert the saved Sketch feature to Surface and all three Thin sides in Properties, inspect actual transient wires and zero contacts, Cancel and save/reopen without committing the draft |
| GUI Mirror | X/Y axes, translated and oblique native construction segments; connected source selection; Escape; axis confirmation; shared point identity; Undo/Redo; actual source-point drag; native Part save/reopen and independent reflection equations |

## Acceptance and limits

Final test results and signed package acceptance are recorded in
[release 2026100804](../releases/2026100804.md). The matrix is a finite regression
scope, not a proof of every possible Sketch. General ellipse and B-spline tangency
use the existing paths and their existing regression coverage; the dimensioned
trimmed-arc matrix covers the FORM-EDGE family. The expanded quadrant matrix targets full circles. Extremely small geometry below
the established degeneracy tolerances remains unsupported. Additional rotated
3D Sketch frames are covered by the existing external-reference and FORM studies,
not by every combination of this circle matrix. Linux compilation and GUI
verification remain separate.

Localization review: the change reuses existing labels, instructions and errors;
no user-visible text or native format fields are added. All five catalogs and
localized UI contracts must pass before release.
