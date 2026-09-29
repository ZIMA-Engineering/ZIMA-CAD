# Half-round sheet transition preview

The rectangle-to-half-circle transition uses the same ZIMA material construction
for its cyan preview and its explicit body calculation. The preview draws both
panel skins, thickness edges and finite-radius bend boundaries from `SheetResult`.
It does not call OCCT or create persistent reference identities.

Construction closure segments remain in the authored Sketch, but are omitted
from the cyan preview. Rectangular L/U transitions now use the same finite-radius
material wire, including both skins and thickness edges, without a dashed envelope.
Unsupported material geometry
reports the existing translated validation message instead of silently showing
only the two profiles as if the preview were complete.

The saved `PLECH.prtz` example (5-degree X tilt, 3 mm thickness) produced one valid
solid before the preview change. Its material-preview geometry took about 2.6 ms
in the local Windows build. This is geometry preparation time, not a complete GUI
frame measurement. The preview itself does not change body-kernel calculation.

`sheet_transition_command_tests.cpp` checks preview coverage of both skins and
bend junctions for parallel, positive/negative tilt and compound tilt. The optional
`ZIMA_VERIFY_TRANSITION_SOURCE` path checks an existing native file without
overwriting it. The GUI verification uses a test copy for inspection and Cancel.

## Preferred endpoint-plane construction

The half-round transition prefers endpoint-plane construction over diagonal
subdivision when tilted profile endpoint tangents are incompatible.
Endpoint planes preserve both profile endpoints and the rounded-rectangle
tangent; exact endpoint tangency to the circular profile is relaxed. Interior
planes retain the existing common-tangent construction. Planarity, unfolding
metrics, intersections and profile approximation bounds remain checked.

The application uses this construction for both preview and calculation.
Compatible common-tangent profiles keep their existing construction. If the
endpoint-plane construction fails the strip checks, the existing triangulated
construction remains available for geometries that require it. Rectangular L/U
transitions retain their existing behavior.

The user approved prioritizing removal of diagonal folds over exact circular
endpoint tangency on 2026-09-29, and explicitly waived support for old files for
this change. The changed construction changes generated operation fingerprints;
old cached tilted-transition documents may therefore be rejected. No migration,
cache-validation bypass or additional persistent field is introduced. New
documents must be verified through native save/reopen and regeneration.

For `PLECH.prtz` (X tilt 5 degrees, thickness 3 mm), the endpoint-plane construction
has 11 panels and 6 bends without added diagonal folds. Separate OCCT evaluations
of formed, unfolded and restored states each produced one valid solid. At K=0.5,
formed and unfolded volumes agree within the existing 0.02% tolerance. At the
file's K=0.3183, bend allowance intentionally changes the constant-thickness flat
volume (approximately 138408 versus 137556 mm^3); Bend Back restores the formed
volume within 1e-5 mm^3. The optional test combines
`ZIMA_VERIFY_TRANSITION_SOURCE` with `ZIMA_VERIFY_ENDPOINT_PLANES=1`.

Quarter-surface checks also cover X tilts -15, -5, 5, 15 and 30 degrees. These
checks do not establish full-sheet validity for every possible input. Regression
tests cover ordinary surface and half-transition behavior, finite-radius material
and native command workflows. The GUI test creates an X-tilted 3 mm transition.
This change adds no user-visible strings.

Validation on 2026-09-29 passed the surface, half-transition, finite-radius sheet,
native command and GUI contracts, plus localization coverage. The GUI test
verified creation with X=5 degrees and thickness=3 mm, saved parameter retention,
reopening, Cancel, Undo/Redo, subsequent dimension edits, Unbend and Bend Back.

## Transition endpoint datums

Both half-round and rectangular L/U transitions expose `axis:start` and
`axis:end` points at the actual profile centres, independent of the axis display
extension. The existing Point checkbox controls both markers. The Text checkbox
labels the pair once at its start. During editing both markers are available.

The `plane:end` datum follows the second profile's oriented Sketch plane and
passes through its centre. It is authored from persisted Sketch coordinates,
not reconstructed from body faces. All three references belong to the transition
feature and are included in the calculated original-reference packet, allowing
Assembly mating to distinguish occurrence paths. Reference planes in feature
groups add no solid geometry. Their oriented corners and identities participate
in calculation fingerprints; the existing native reference serialization stores
their triangles without introducing a new document field.

Changing marker visibility does not change the calculation request. This work
consumes existing placement and Assembly-mate solving; it does not change their
contracts. The tests check endpoint resolution and point/plane coincidence for
two occurrences, including opposite plane sides at zero offset, and native Part
save/reopen/regeneration. All existing UI labels are reused; no new translation
keys are needed.

## End reference display

Both transition types display `axis:start` and `axis:end` with the ordinary datum
point color; hover and selection retain their common colors. The existing
`plane:end` reference now has a screen-sized outline, using the same display and
picking path as a construction plane. Its identity and occurrence path are
unchanged. A localized End plane leaf appears directly below the transition in
the Tree. Plane visibility follows the existing plane visibility control.

The outline is derived from already persisted reference corners. Repeated vertices
in calculated and construction packets are deduplicated for display only. This
neither changes persistent topology nor invokes OCCT during view updates.

Transition end-plane borders use the same base size and camera scaling as
container Origin planes. This changes display/picking only; persisted plane
positions, normals and reference identities remain unchanged.
