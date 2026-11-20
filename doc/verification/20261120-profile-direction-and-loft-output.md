# Profile correspondence direction and Loft output

Status: the native Windows and focused GUI checks below passed, including final
checks on the normal local 2026112002 executable. Remaining gates are listed
below. This is not a published release. Linux execution remains unverified.

## Authored correspondence

The user approved saving the first correspondence point together with the
direction, including verification of factory assets, start templates and
protected Toyota copies. Part native format is now 56 and Part serialized
format is 79. Assembly remains at native 38 / serialized 50.

Closed profiles retain the selected first point when their direction reverses.
Open profiles use the selected endpoint to determine their direction. The
property editor offers first-point selection followed by an adjacent point
in the View. The common viewer candidate list supplies these points, scoped
to the exact feature and occurrence. The command's existing draft scene
supplies their positions; picking does not calculate a solid.

Loft previews show dashed connections between corresponding numbered points
of successive profiles. These visual guides are not persisted reference
geometry. Sweep's ordering editor shows connections to adjacent profiles.
The original per-segment start/end station rows are retained; a junction may
have distinct incoming and outgoing definitions, with inheritance when no
separate profile was authored.

Thin Loft previews additionally reuse the existing planar profile-offset
generator. The two contours indicate thickness and side in each Sketch plane.
They are not claimed to be the intersection of the exact offset Loft surface
with that plane: the calculated wall thickness is normal to the Loft surface.
The localized tooltip makes this distinction explicit.

## Native evidence

- `profile-direction-sweep-native.log`: the complete Sweep contract suite
  passed, including 18 new direction/output cases using a circle with explicit
  correspondence points and a rectangle, Solid/Surface/Thin outputs and all
  Thin sides. It checks actual volume/area, valid Surface references, native
  readback and first-point-preserving reversal.
- `profile-direction-loft-native2.log`: the complete Loft document suite
  passed, including reversed profile pairing and 32 additional open/closed
  Surface/Thin cases. These exercise both directions, all Thin sides, two
  thicknesses, independently expected cylinder/strip areas and volumes,
  valid calculated geometry, planar offset previews, correspondence links,
  native reopening and Undo/Redo. The existing dimension and dragging matrix
  also runs in this suite.

Evidence files are under `build/rip25-20261120/`. The first additional Loft
run exposed a test fixture that saved a pending physical-property cache and
then compared it with an analyzed cache. Saving the analyzed test body fixed
that comparison; the resulting geometry did not require an implementation fix.

## Factory data and protected files

The direction conversion inventory contains 1,072 tracked native assets.
All 1,060 applicable current assets were freshly calculated, compared with
their retained native geometry and authored definitions, saved, reopened and
applied. The other 12 are unsupported legacy fixtures and remain unchanged.
The reports are `direction-factory/all-results.json` and
`direction-factory/applied-results.json` under the evidence directory.

The protected Toyota inventory contains eleven files. Both supported Drawing
copies passed exact native readback. Seven Part files use native format 50;
the Assembly depends on those files, and another file uses the old `.prt`
format. Their affected geometry cannot be verified with the current reader.
No runtime compatibility path was added. All eleven originals and their
recoverable backup copies matched the protected SHA-256 manifest. Original
Toyota documents were not overwritten.

## GUI and localization evidence

`direction-gui/loft` and `direction-gui/loft-light` passed the actual View
mouse interaction in both themes (31.27 s and 30.85 s in the last run). A first
click selects the initial native point; a nonadjacent second click is rejected;
the adjacent second click reverses numbering while retaining the first point.
Cancel restores both fields. The tests verify four exact correspondence links
and that point-order clicks do not republish the unchanged rollback scene.
Thin edits refresh the planar outlines. Surface conversion, its yellow face
classification, hide/show, analytic area, native readback and Undo/Redo pass.
The screenshots were visually inspected, including the shaded Surface and
planar Thin outlines in both themes.

`direction-gui/stations` passed again in 21.70 s after the marker localization
fix. The English View marker and editor were visually inspected. Actual View clicks select both
directions, four links connect the selected profile to its neighbor, and Cancel
restores the draft. The existing Sketch dimensions, repeated drags, station
inheritance and other-profile context checks also pass.

`profile-direction-translations.log` passes source coverage, catalog checks and
translated controls in cs/en/de/fr/ru. A visual check additionally found the
old Czech start marker in an English Sweep preview; it is now translated at
the application presentation boundary, without rewriting native geometry.
`profile-direction-native-documents.log` passes current-format storage,
Unicode paths, templates, native cache/reference readback and failed writes.
`direction-gui/templates` passes actual new-document creation from current
factory templates, active first Part Body and normal Part/Assembly commands.

The separate factory FORM insertion GUI check exposed a no-op dirty-state
change, with no difference in serialized authored data. Its support-derived
frame becomes available after dialog construction. The initial preview now
sets the unchanged-OK baseline after that frame is initialized. The complete
FORM GUI check passes in 80.00 s, including insertion, unchanged OK, internal
dimension editing, independent definition replacement, Cancel, Undo/Redo,
native reopening and thickening the connected factory shell. The standalone
test now enters the Surface workspace before requesting its Thicken action.

`direction-gui/curve` also passes eight actual point-position drafts through
placement offsets, all four existing point editors with OK and Cancel, new-point
Cancel, tangent/rounding changes, two radius drafts, parent Cancel, unchanged OK,
and native reopening. The preceding model retains 6,624 solid triangles. The
reported disappearance has not been reproduced; this is verification evidence,
not a claim to have found and fixed that defect.

`direction-gui/surface` passes Surface/Solid conversion, Surface face
classification, hide/show, Undo/Redo and native reopening for the saved Sweep
example. The current-format `26` candidates retain the original exact cached
geometry, but strict full-history offline conversion cannot compare intermediate
boundaries lacking both physical properties and retained B-Rep. These candidates
are used for isolated GUI context checks only, not presented as fully verified
geometry conversions. Their format-55 source copies remain untouched.

## 3D Sweep View coverage

The new actual mouse fixture exposed a missing Body transform in the 3D Sweep
draft presentation. It now uses the existing `place_body_mesh` path, after
assembling its profile/path geometry and dimensions. The placement solver and
calculated geometry are unchanged. The resulting GUI matrix passes in 42.57 s:
closed first/adjacent clicks in both directions, open endpoints, translated and
rotated feature and Body, exact preview coordinates, adjacent-profile links,
draft Cancel and unchanged document state. Both screenshots were visually
inspected. Evidence is `direction-gui/order3d`.

## Remaining gates

Portable packaging and publication remain pending. The final local executable
passed 3D Sweep point ordering again in 36.75 s and the complete FORM GUI check
in 72.33 s; see [Local Windows build 2026112002](20261120-local-2002.md).
The new point-order mouse fixture
covers a transformed Body inside a Part, not an activated Part occurrence in an
Assembly. Linux execution remains unverified. A source release must include the
exact verified committed factory assets. Cursor-orbit and camera-storage checks
are recorded in [Cursor orbit](20261120-cursor-orbit.md).
