# Helical references, Arc curves and the Surface Feature

## Implementation

New H-Curve and H-Sweep definitions start in XY. Existing authored planes remain
unchanged. The H-Sweep section uses the accepted guide start and transported
frame. Incomplete guide input cannot relocate a section to an unrelated frame.
Path endpoints and rotation-axis endpoints have distinct semantic identities
derived from their persisted guide/base-circle parents. H-Curve and H-Sweep
consume the same path definition and reference presentation.

A downstream point may depend on both a helical path endpoint and its cap face.
The reference preparation now retains both demanded native cap geometry and
the authored helical datums. It does not change the shared placement solver.

The new 3D Curve **Arc** type constructs circular segments from the first
reference plane's oriented normal and successive endpoint positions. Subsequent
segments inherit the preceding end tangent. Point XYZ tangent controls and
corner rounding are inactive for this type; their stored values survive type
changes. A collinear endpoint cannot define a finite circular arc and is
rejected atomically. The first-plane normal is persisted with the curve,
including a curve owned by a 3D Sweep. Reference loss retains its last frame.
The viewer and downstream reference packet carry exact rational quadratic
spans, including semicircles and major arcs. Display sampling uses the same
start tangent; it cannot read a rounding station that does not exist for Arc.

The **Surface / Plocha** choice is part of the common Feature type list. It uses
the existing Modeling Surface result with neither side extruded, so it needs
no separate native feature kind. It owns an ordinary Sketch and uses the common
Properties and placement controls for creation and editing. The Surface
workspace offers the shared Feature command; the old standalone planar-surface
command and separate edit dialog route are removed. Closed Sketch regions,
holes and disconnected islands define the planar result.

## Independent checks

Windows Release uses MSVC, Qt 6.11.0 and OCCT 8.0.1. Linux has the same C++
implementation but was not built or executed on this host.

- The helical native contract passed 36 guide variants over XY/XZ/YZ, handedness,
  positive/negative height and pitch, including repeated edits, retained frames
  and native reopening (19.657 s).
- The Sweep command contract passed repeated pitch changes 7/3/6, handedness,
  endpoint/cap dependencies, Undo/Redo and native reopening (10.519 s).
- The H GUI contract passed XY defaults, guide redraw and actual mouse selection
  of the four authored path/axis endpoints (11.490 s).
- The Arc native contract passed six radius/direction combinations. Independent
  circle equations, exact tangents, three spatial segments, source-point
  identities, ignored inactive controls, Sweep volume, save/reopen and cold
  regeneration were checked (9.641 s). The MSVC test stack matches the product's
  8 MiB stack; the default 1 MiB test stack overflowed during native calculation.
- Arc commands passed repeated endpoint changes, atomic rejection of manual
  tangent edits, Undo/Redo and Part/Assembly serialization (0.322 s).
- Exact reference and preview checks additionally cover 90°, 180° and 270°
  arcs, checking the independent circle and the intended arc midpoint. The GUI
  verifies both circular preview segments, inactive direction controls, all
  five languages, unchanged OK, Cancel, Undo/Redo and native reopening.
- Surface GUI checks pass common Feature creation and editing, whole-Origin
  placement, an 82 mm2 hole/island fixture with zero volume, Sketch conversion,
  Cancel, unchanged OK, Undo/Redo, save/reopen, both themes and all five languages.

These are bounded matrices, not a claim that every possible Sketcher constraint
combination has been tested. The Sketcher solver and constraint model are not
changed by these features.

## Reported spring document

The saved `61.prtz` was investigated from a copy with SHA-256
`2a456bf4b158fee0e4ebe7e33307923e3b9da62c91b2ff488599d4fdac6cb075`.
Explicit regeneration repaired the attached 3D Sweep's first point. Saving,
reopening and another regeneration retained all three original endpoint/cap/axis
references and exactly the same frame; its dependency state remained valid
(4.442 s for the reopen check). The user's original file was not overwritten.

## Native assets and publication

The current inventory scans complete native history definitions as well as
entity types. None of 1,212 tracked native assets or ten protected Toyota
documents contain Projection, Wrap, H-Curve, H-Sweep or Arc definitions affected
by these changes. Custom libraries and project originals remain untouched.
No Windows release is authorized by this change; publication follows the user's
separate acceptance of the pushed development result.
