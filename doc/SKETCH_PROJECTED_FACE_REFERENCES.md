# Projected face references

Status: Windows native and targeted GUI verification completed, 2026-10-18.

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

## Verification

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
