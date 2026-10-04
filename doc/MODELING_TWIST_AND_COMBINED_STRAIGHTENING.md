# Modeling Twist and combined profile straightening

## Engineering definition

Inputs are one owned, closed profile Sketch, a positive axial length, twist
angle, handedness and transition law. The output is an undoable solid history
Feature. The existing profile editor, placement controls, native history and
explicit OCCT loft calculation provide the means.

The axis passes through the area centroid of the filled section, including
holes, and follows the Sketch normal. This is an axial geometric deformation;
it does not estimate elastic springback or manufacturing strain. An asymmetric
L section provides an independent centroid check against its analytical area
moments. A straightened section has volume equal to section area times corrected
length.

Modeling **Twist** uses the ordinary profile Feature dialog and its placement
contract. Length, angle, right/left direction and optional smooth transitions
define one solid side. Disconnected filled regions in the owned Sketch share
the combined area-centroid axis, including the negative area of holes. Each
region retains its source topology ancestry. Ordinary Sweep remains available
for different profiles at multiple stations. The existing sheet-metal Twist command
retains its own material and thickness behavior.

## Command lifecycle

Creation and editing use the same internal properties dialog. Opening Properties
uses the ordinary rollback boundary. Pending values update a native wire preview
around the section centroid without invoking OCCT. Cancel restores the input;
unchanged OK creates no transaction. Changed OK calculates and commits through
the existing profile operation. The native Feature stores the twist law and
owned Sketch. Sweep station samples are calculation details, not persisted
topology identities.

**Straighten** accepts the Modeling Twist and combined solid profile Features
with a Revolution side. It preserves an ordinary Extrusion side and straightens
the curved side. **Restore shape** replays the authored definition. Dependent
mixed datum/face placements and limit-driven cuts use an isolated document with
the existing reference solver during explicit calculation. Authored references
and placements are not rewritten. Subsequent fingerprint checks consume the
calculated reference packet without invoking OCCT.

Twist publishes the enabled centroid axis and the origin trajectory with their
semantic endpoint identities. The centroid axis stays straight; an off-axis
origin follows the same twist law as the section. Both are retained in the
calculated native viewer packet and follow repeated Straighten/Restore states.

Dependencies referencing a preceding solid state's face, edge, point or axis
resolve that stored identity against the current source geometry during an
explicit state calculation. Mixed global-datum and state-face constraints still
use the existing placement solver. The common picker offers current state
geometry and excludes source faces and older state snapshots for that exact
source occurrence. Authored snapshots remain available for inspection.

## Authored geometry inspection

Straighten and Unbend calculate state geometry without moving or duplicating
the authored container Origin. Parameter inspection by double-click shows the
entire authored feature as an azure wire together with its original dimensions
in its original location, regardless of the current solid or sheet state.
Properties and Sketch editing retain their ordinary history rollback boundary.

The inspection wire uses the existing native profile previews and stored
original contribution edges. Through-all previews use the calculated input at
the feature's creation boundary. Body and active occurrence transforms apply
once at the display boundary. The overlay is not reference geometry and does
not change the calculated body, placement, references, history or Undo state.
Escape retires the inspection wire. No OCCT calculation is requested by
parameter inspection.

Ordinary View selection of straightened geometry follows the persisted source
ancestry to the authored feature, including already calculated native packets.
Click and double-click use the common picker and retain the exact occurrence
path when synchronizing an active Part inside an Assembly.

Circular Sheet Cut axes are calculated separately for the current sheet state
and its rigid material carriers. Unbend removes the original axis from display
and picking and publishes a state-owned axis in the unfolded position. Bend
Back restores its folded position. The source axis and Origin remain immutable
for authored inspection and editing. A footprint unrolled from a curved carrier
has no single straight cylinder axis. Existing cached results with the old axis
require explicit Regenerate; opening a document never silently calculates it.

## Imperial drawing tolerance presentation

Drawing annotation formatting uses a decimal point for inches and omits the
leading zero below one inch. Stacked deviations preserve signed zero and align
their decimal points, for example diameter `.475"`, upper `+.001`, lower
`-.000`. Literal text overrides retain the user's spelling. The drawing uses
the same text layout for display and output; ordinary model annotations retain
their existing formatting.

The comparison reference is the linear-tolerance illustration in Xometry's
[technical drawing guide](https://d27ze05algd7ka.cloudfront.net/resources/design-guides/technical-drawing-best-practices/).
The automated rendering proof is `build/inch-tolerance-layout.png`. This checks
deviation presentation, not comprehensive ASME compliance or a new limit-size
dimension mode.

## Verification

Combined Feature straightening resolves the rotation direction with the same
Sketch-normal and authored-side rule as the existing solid calculation. The
construction axis's drawing order must not swap Start and End. An unchanged
Extrusion side, including its draft, remains unchanged.

The kernel regression matrix covers all five curved combinations of None,
Extrusion and Revolution across both sides, both Sketch-normal directions,
both axis directions and profiles on either side of the axis. Profile cases
include an asymmetric L section, a circle, an eccentric hole, a thin circular
section, an unchanged drafted Extrusion and full/symmetric rotations. Expected
straight solids are constructed independently; endpoint positions, side/source
identities, volume, surface area and restoration are checked.
Explicit calculation rebuilds combined solid states from their valid original
Feature prefix instead of accepting a persisted final-state snapshot. Native
fingerprints remain stable, so saved documents can still open without computing
geometry. Batch and single prefix fingerprints are checked for the complete
variant matrix. Unchanged OK remains outside the calculation path.

- Kernel tests cover both twist directions, linear and smooth laws, asymmetric
  centroid, corrected straight volume, semantic state references and restoration.
- The solid-state GUI test covers Twist command initialization, editing, native
  preview, Cancel, unchanged OK, combined Features, state selection, rollback,
  Undo/Redo, confirmation and native save/reopen.
- Inspection tests compare complete source wires before Straighten/Unbend and
  after state changes and restoration, including a rotated and translated Body
  and an active Assembly occurrence. Optional user-model GUI checks use
  `ZIMA_VERIFY_AUTHORED_MODELS` with semicolon-separated native Part paths;
  they save only test copies and verify unchanged feature/Sketch definitions.
- The user-model test accepts `--user-model Projects/02.prtz` and verifies the
  complete combined Feature, following extrusion and through-all cut, including
  cold regeneration. It does not write to the source file.
- `--state-alias-model` verifies the state-attached Twist in the `03.prtz`
  definition: movement, restoration, unchanged authored references, Undo/Redo
  and cold native reopening. Use an explicitly regenerated test copy when its
  stored calculation predates the current geometry inputs. GUI checks cover
  its Properties boundary and current face candidates.
- Drawing dimension tests cover imperial conversion, decimal alignment, signed
  zero, literal overrides and native annotation persistence.
- Localization coverage checks all five supported catalogs. Start templates are
  rewritten with the current serializer and tested through real GUI creation.
