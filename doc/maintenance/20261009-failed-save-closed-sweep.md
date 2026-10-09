# Failed-feature persistence and closed 2D Sweep

## Behavior

Native Part saving preserves recoverable failed history features instead of
requiring their geometry to calculate successfully. Existing input errors and
cached calculation diagnostics select the recoverable path. Container hierarchy,
Sketch ownership, identities, calculated-boundary fingerprints and actual write
failures remain checked. Saving uses the existing snapshot/atomic-write path;
it neither calculates OCCT geometry nor creates an Undo step. Loading retains
failed Helical/3D Sweep definitions whose incomplete geometry cannot yet supply
their derived frames. Existing native fields carry all data; the schema remains 70.

2D Sweep now accepts a single connected closed chain of supported curves. Open
chains retain their Origin-start contract. A closed chain whose Origin is not a
vertex starts at the first stored native curve endpoint. Geometry, rather than
UUID ordering or OCCT enumeration, validates connectivity. A closed chain has
no terminal attachment points, and an unspecified closing section returns to
the first profile. Properties displays this inheritance consistently.

Capsule paths have parallel endpoint tangents even on their semicircles. The
kernel therefore also uses the exact middle tangent to establish the route
plane, while retaining the existing exact planarity checks. Circular section
transport uses that plane at a half-turn. Rigid profiles on planar circular spans
reuse the existing exact revolution implementation and retain native analytic
section geometry. The ordinary straight/sharp 3D Sweep path remains unchanged.
An initial broader analytic-section selection failed the full endpoint-circle
regression; narrowing it to routes with planar circular spans repaired that
regression. No tolerances are relaxed and no topology names come from OCCT order.

## Verification

- Persistence matrix: 28 feature kinds, using structurally valid empty definitions,
  missing supports or failed calculation. Extrusion, Revolution, Feature, 2D/3D/
  Helical Sweep, Fillet, Chamfer, Shell, Hole, Shaft Thread, Flat, Bend, Form,
  Surface Thicken, Boundary Surface, Surface Sewing, General Surface, Surface
  Intersection, Surface Trim, Holes, Twisted Sheet, Unbend, Bend Back, Derived
  Copy, Sheet Transition, Straighten and Restore Shape retain their serialized
  definitions, error maps and preceding valid volume through save/reopen.
  Malformed container hierarchy is still rejected. The existing snapshot suite
  checks unchanged Save, failed I/O, newer edits/calculations, reference identity,
  dirty state, background completion and Undo/Redo.
- Closed capsule matrix: Origin at a vertex or inside the chain, ordinary or
  rotated plane, Solid, all three Thin sides, open/closed Surface profiles,
  independent analytic volume/area equations and native save/reopen ancestry.
  Four placements times six result/profile variants give 24 calculated cases.
  The actual extracted KAPSA Sketch additionally supplies a closed R3 lower-face
  lead-in: inward material removal and the opposite side agree with independent
  quarter-circle volume equations.
- Existing 2D U-route side, Loft, hole, Thin, spline, own-plane and Surface cases
  pass. The complete 3D Sweep contract passes, including original full circular
  endpoints. Helical Sweep and affected Sketcher suites pass.
- GUI checks cover closed-path Properties, station availability, calculation,
  Save/reopen and Undo/Redo; failed Fillet and unfinished 2D Sweep creation,
  red Tree state, Save, repair and repair Undo/Redo; unresolved embedded Sketch
  entry; and the capsule dimension mouse/edit/drag matrix described in
  [the solver record](20261009-capsule-dimension-solver.md).
- Fourteen focused native/GUI/localization suites pass across the recorded runs.
  The final seven-suite geometry/persistence/GUI run passes in 53.91 seconds.
  All five catalogs pass JSON, unique/equal keys and placeholder checks.
  Actual invalid-path dialog text follows cs/en/de/fr/ru language changes.

## Limits

Periodic single circles, ellipses and closed splines are still unsupported as
path curves; use connected arcs/other supported curves. Closed variable-profile
Lofts, arbitrary sharp closed corners and self-intersecting paths are not covered
by this acceptance matrix. Imported STEP and the Hole's subordinate Thread/Drill
Point states are not independent cases in the 28-kind matrix. The separate
redundant radius-driver limitation in the capsule solver record remains open.
No shared placement, side convention, dependency version or native schema is
changed. Windows is verified here; Linux execution remains a separate gate.

The user's current KAPSA file is preserved, with a separate diagnostic backup;
tests use an extracted native Sketch or isolated temporary Parts. Personal
configuration and unrelated untracked assets are excluded from the commit and
distribution.
