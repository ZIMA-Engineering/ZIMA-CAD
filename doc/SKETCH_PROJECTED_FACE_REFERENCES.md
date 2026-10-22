# Projected face references

Status: Windows native and targeted GUI verification completed, 2026-10-18.

UI naming update, 2026-10-21: the Sketch command is **Reference** (Czech:
**Reference**), using the shared translation key in cs/en/de/fr/ru. Its selection,
completion and validation prompts use that name. Internal external-reference
identifiers, read-only ownership and serialized definitions are unchanged.

Sketch face references represent orthographic projected boundaries and apparent
contours in the Sketch frame, not the intersection between a source surface and
the Sketch plane. Moving the plane along its normal must not destroy an existing
reference while the selected source remains available. Source identity and
occurrence path stay unchanged. Projection consumes persisted calculated source
triangles and analytic supports, without OCCT or a geometry transaction on hover.

Complete side-on cylinder/cone bands use exact analytic generators and cap
limits after checking that their mesh boundary contains only the two caps.
Trimmed bands retain their actual boundaries. Other faces use calculated outer
boundaries and front/back contour edges; internal triangulation edges are not
reference geometry. Ordered planar outer and hole boundaries retain the existing
projection path. Edge references keep their established exact spline projection.

Projected outlines are finite. A face whose entire projection collapses to one
straight line retains the established infinite line support for C, H/V and
angular constraints. It is classified from the actual projected boundary, not
from an intersection of supporting planes. This preserves valid references to
an edge-on cap plane outside its finite extent (for example a bolt head wider
than its shank). The old plane-intersection route was removed from creation and
refresh. The separately named geometric
intersection utility is not used by external references. No placement solver,
source topology ID, source geometry, side identity or native serialization schema
was changed. Unsupported or degenerate projection is rejected instead of binding
to an unrelated face or a stale intersection.

## Individual curve selection (2026-10-18)

The 2026-10-21 correction offers the current visible Body face fragment in
Sketch reference entry. Hover, confirmation and inspection retain that exact
display candidate. Only reference validation/storage translates its explicit
persisted ancestry to the original source face. This consumes the existing
reference candidate policy without changing container placement or rebuilding
topology during picking. Both planar cap sides of Solid and Thin fixtures are
checked through real GUI selection; the untrimmed original face is no longer
substituted for the offered fragment.

The ordinary Sketch reference command offers real edges, vertices, axes and
individual apparent-outline curves. Whole Face picking was disabled in the
2026-10-18 UI change; the direct face/plane selection correction below supersedes
that restriction following the user's 2026-10-21 report.
A complete side-on cylindrical or conical band exposes both generators as two
independent finite curves. Their source Face identity and occurrence path remain
explicit; the `sketch-outline:negative:` / `sketch-outline:positive:` semantic
prefix selects one analytic generator, never a triangle or a screen-space guess.
The labels follow the Sketch normal/source-axis cross product and survive source
edits and normal displacement. Physical circular rims retain their own Edge IDs.

Cosmetic shaft-thread root and runout surfaces now persist exact analytic support
in the existing `contour_surface` field during explicit kernel calculation. Their
placement `surface` field remains empty: this does not expose new container
placement supports or change solving, offsets or orientation. Hover and picking
consume calculated data; neither invokes OCCT. Trimmed or unsupported apparent
bands fail closed; this is not complete analytic outline coverage for arbitrary
oblique, spherical, toroidal or NURBS surfaces.

The native `sketch.reference.replace` command changes a reference's source while
preserving its ID and attached contacts. It uses the existing transaction and
source-validation paths, including atomic failure, Undo/Redo and persistence.
Existing authored whole-Face references remain readable. Fitted factory profiles are explicitly reauthored
to individual shaft/root outlines and the exact runout-end Edge.

## Direct face selection and external trim boundaries (2026-10-21)

The user could not add cap planes while editing the last Feature in
`Projects/1000.prtz`. Native creation accepted all 38 eligible preceding faces,
including the Thin Feature's cap planes. All seven references to the last
Feature's own faces were correctly rejected as forward/self dependencies.
The GUI's reference-kind mapping and both Part/Assembly selection contracts
omitted Face/Plane candidates, and original face picking was disabled. Direct
Face/Plane selection is restored only in ordinary external-reference entry;
Reference to contour still accepts body edges. Individual apparent-outline
curves retain their existing identities and behavior.

The actual mouse-selection check passes on a private copy of `1000.prtz`,
including creation of an infinite cap-plane support, Undo/Redo and native
reopen. The user's original file remains byte-for-byte unchanged. Synthetic
Solid and Thin fixtures pass as well. Thin cap faces can lie behind their nearby
rim-edge candidates; the check uses ordinary right-click cycling before left
click confirmation. Language/theme checks exercise the updated guidance in all
five languages and both themes. Projection remains distinct from the approved,
not-yet-implemented [Sketch-plane intersection mode](SKETCH_INTERSECTION_REFERENCE_PLAN.md).

Standalone external line and certified exact circular references now participate
as read-only trim boundaries. Refinement uses their exact geometry, not display
chords. New native segment/arc endpoints retain C to the selected external
support. An overlapping circular reference is a support rather than a set of
sample intersections. Finite arc domains do not cut on an absent opposite branch.
See the [finite verification matrix](verification/20261021-external-reference-trim.md)
for results and unverified variants. No native serialized field, source identity
or shared container-placement behavior changes in these two fixes.

The expanded matrix passes 240 surface/contour-only, whole/individual, plane,
offset and side configurations with repeated source edits, dimensions and drags.
The reference-command suite verifies replacement identity, failed replacement,
Undo/Redo, source displacement and save/reopen. Actual GUI picking confirms both
root outlines and the physical end rim in a Sketch displaced 100 mm from the
source. Native Sketcher, dimension and thread suites pass. All five translation
catalogs and their actual language-switching GUI checks pass.

Construction and helper curves are green only in their active Sketch or the
editing feature's profile annotations. Finished and passive profiles retain the
established brown centerline/purple helper styles. Selection and hover remain
azure; ordinary feature-preview wires remain azure. Targeted framebuffer tests
cover active/passive/re-entry and explicitly green editing-preview annotations.
The user's last Revolution has a 1 mm axis inside overlapping origin/endpoint
hit regions at whole-model fit: actual RMB cycling and zoomed direct LMB picking
both pass. Origin selection priority is unchanged.

A fresh cold factory calculation verifies all 212 affected Parts, preserving
BRep volume/area, authored history, role Bodies and reference identities before
save/reopen. The factory audit deliberately does not reuse saved calculation
boundaries. Current evidence is under `build/individual-thread-factory/`,
`build/individual-*-final*` and `build/individual-face-curves-final-ui.*`.

## Previous projection acceptance

- 60 side-on cylinder/cone configurations: XY/XZ/YZ frames, offsets -100,
  -10, 0, 10 and 100 mm, and both sides. Analytic generators remain exact
  despite deliberately misaligned tessellation. Repeated source-radius edits,
  driving dimension edits, locked rejection, unlocked dragging, read-only source
  geometry and native serialization pass independent equations.
- Four planar line-support cases cover both triangle orientations and contact
  sides, repeated source translations, and zero/nonzero dimension edits. The
  persisted distance side survives every step.
- Native Sketcher, dimension-command and external-reference suites pass,
  including 288 trim variants, the 128-case FORM sweep study, 64 circular
  contacts, exact rational edge projection, refresh/detach atomicity, nested
  occurrences, Undo/Redo and persistence. The unrelated broader Coincident
  circle preselection GUI gap is recorded in
  [the endpoint investigation](SKETCH_AXIS_CONTACT_20261018.md).
- Actual GUI face and end-rim picking passes with a longitudinal Sketch
  displaced 100 mm outside a generated shaft. The reference is drawn on the
  displaced Sketch plane, not left on the source surface. Existing reference
  filters, overlap cycling, Properties Cancel/OK and Undo/Redo remain covered.
- A read-only inventory of 233 tracked native factory documents found 425 face
  references in 212 Parts (211 fastener assets and VentilationWindow FORM).
  All 212 were calculated, saved, reopened and installed after verification.
  Exact BRep volume and surface area, authored history, role Bodies, constraints
  and reference identities match. Only derived face projection cache fields are
  excluded from the embedded-profile history comparison. Untouched native
  factory assets and the user Project retain their byte hashes.
- Eleven dependent factory Drawings were checked on candidates: all 101
  dimensions retain their identities and numerical values through regeneration
  and save/reopen. The original Drawings were not replaced.
- Localization/catalog checks pass for cs/en/de/fr/ru. The new projection error
  is translated in all five catalogs.
- The rebuilt FORM definition, command and geometry suites pass against the
  installed library: all three thickness modes, cold regeneration, repeated
  edits, Undo/Redo, drawing symbols, dependent cuts, spatial/flat restoration
  and native save/reopen retain their established behavior.

Evidence is under `build/projected-face-*.log`,
`build/projected-face-offset-ui.*`, `build/projected-face-translations.*`,
`build/face-projection-factory-audit/`, and
`build/projected-face-factory-candidates/` (inventory, native audit, candidate
files, per-Drawing command logs and the installation SHA-256 receipt).

Linux execution, comprehensive oblique/trimmed torus/sphere/NURBS projection
matrices, and every tangent/dimension interaction on those general outlines
remain unverified. General nonanalytic contours consume the already calculated
source mesh; they do not acquire an invented exact spline or topology identity.
