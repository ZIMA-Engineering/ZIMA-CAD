# Route Point inspection and Curve/Sweep conversion

## Scope and causes

This follow-up adds an editable station label to Point inspection and exposes
owned Point placement dimensions when inspecting a standalone 3D Curve. It also
repairs Point reference entry and persistence across 3D/Helical Curve/Sweep mode
changes. The shared placement solver, reference-entry controls, geometry sides,
native schema, Qt/OCCT pins and Windows/Linux implementation remain unchanged.

The Point label reads the existing station parameter: `s` in millimetres for
distance, or dimensionless `t` for a fraction. It is not a chord measurement.
Double-click uses the existing reference-offset value editor. Ordinary single
selection does not expose dimensions. Curve inspection reuses the existing Point
dimension generator in the owning Curve frame; editing a child retains the Curve
inspector and Tree selection. Geometry calculation is not added to inspection.

Three conversion defects were demonstrated and repaired:

1. The 3D Sweep resolver retained its owned trajectory, but the public viewer
   reference packet did not publish that trajectory for new Point selection.
   The packet now exposes its original identities without adding a duplicate
   display wire. Scene presentation reuses this packet.
2. Sweep-to-Curve conversion appended the recovered construction to the entity
   table. Native loading reconstructs the table in history order, so a following
   construction caused a definition fingerprint mismatch. Recovery now inserts
   at the original history position; the file format and validation stay intact.
3. A Helical route was incorrectly requested as calculated material topology at
   a downstream Point boundary. That empty/non-trajectory packet masked the
   authored path. Boundary preparation now treats the Helical path as authored
   geometry, as it already does for an owned 3D Sweep trajectory. Solid-face
   dependencies still use their ordinary material boundaries.

## Finite Windows verification

- Nine CTest targets passed in 53.14 seconds: 3D Curve/Sweep geometry, Helical
  geometry, translation catalogs, Point station annotations, common route-pick
  ordering, full Sweep commands, focused route conversion, Sweep Point
  references and Loft document behavior.
- Native station annotation checks cover distance/fraction, zero/end positions,
  five successive values per mode, independently calculated coordinates on a
  two-segment path, an interior anchor, locks, invalid references and JSON reopen.
  A rotated/translated Curve annotation test checks child identity, frame
  coordinates and free zero-coordinate dimensions.
- Native conversion uses a translated/rotated 3D Curve, a downstream Point on
  the whole route and another consumer of an owned endpoint. Both remain valid
  through Curve -> Sweep -> Curve, Undo/Redo and native save/reopen. The 3D Point
  coordinates are checked independently. Helical Curve -> Sweep -> Curve retains
  the exact path, station reference and Point position, including Undo/Redo and
  native reopening. The full existing Sweep command suite also passes.
- Hidden application tests use actual Qt hover, RMB cycling and LMB confirmation
  in Point Properties for both 3D and Helical Sweep fixtures. The whole route
  immediately follows its segment, confirmation retains the original owner/key,
  and Cancel leaves the document unchanged.
- An isolated copy of user `16.prtz` passes Point single/double-click inspection,
  three double-click distance edits, independent arc-length positions, unchanged
  source constructions, native reopening and Undo/Redo.
- A second isolated copy passes Curve Tree single/double-click inspection and
  three View edits of an owned Point's reference offset. Other owned Points stay
  unchanged, the dependent route Point follows the independent length equation,
  parent inspection/Tree selection survives, and native reopening and Undo/Redo
  pass. The rendered dimensions were inspected from the test image.
- The expanded Loft GUI scenario was rerun and passed, including first-plane XY,
  all manual planes, real face picks, five languages, calculation and persistence.

Logs and disposable fixtures are in
`build/test-models/equal-segments-20261118/`. User originals are not edited. The
application tests run with hidden windows and do not capture/control the desktop.
Localization review found no new prose strings: `s`, `t` and `mm` are quantity
notation; all dialogs, editing errors and actions reuse existing translations.

This is finite Windows evidence, not exhaustive coverage of nested Assemblies,
every contact combination or every curve family. Linux execution is outstanding.
Curve inspection inherits the existing generator's omission of zero plane-offset
dimensions; those references remain editable in Properties. The pre-existing
General Surface toolbar assertion is recorded in the
[Loft follow-up](20261118-loft-plane-point-route.md). Packaging/publication evidence
belongs in the release record after verification of the committed-source runtime.

## Agreed future Sketch route scope

The Sketch feature owns geometry; Sketcher edits it. A route should use ordered
incidence through persisted native points, with an explicit start and direction.
Initial support should accept open chains and closed contours, with degree-two
internal junctions. Degree-three branches and mechanism path switching require a
separate design. Mere crossings are not junctions. No mandatory new 2D Curve
subtype is introduced, and Sketcher is not synonymous with the separate 2D path
tool. This agreement does not claim that a new branching route system is shipped
in this change.
