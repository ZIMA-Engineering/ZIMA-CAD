# FEM projected regions and bounded extrusion subtraction

## FEM definition and mesh

Force and Constraint can own an ordinary editable Sketch on an offset plane
in the condition's placement frame. With Sketch projection enabled, the
geometry table selects native material faces. A surface row selects a patch
with an interior seed and retains its boundary ancestry. A curve row selects
the face that receives the projected curve. Point conditions still use their
ordinary placed origin or an explicit native vertex.

Condition Sketch edits are transient until Properties OK. The native Part
stores the Sketch with its condition. The exact projection stage is shared
with the modeling kernel. Mesh preparation partitions a private material
shape for area conditions and embeds projected line elements in the material
faces. Edge and face elements must be members of the tetrahedral mesh.
Generation leaves the source Body, its placement and its CAD history intact.

The Windows verification uses a 20 by 10 by 5 mm solid. A 4 by 2 mm projected
Sketch produces an independently measured 8 mm² load patch. An open 4 mm
Sketch produces the independently measured edge group. The mixed job also
covers line and area conditions on the same original face. The Qt verification
opens the ordinary Sketch editor from Force Properties, returns to the same
dialog, commits the Sketch, saves and reopens the Part, then checks Undo and
Redo. A native test checks offset and seed round-trip. Existing FEM origin,
mesh order, source sharing, menu visibility, translation and placement checks
remain part of the Windows suite.

The changed native contract is conditional on a condition with an owned
Sketch. The tracked factory library contains no such FEM definitions. All six
tracked Part, Skeleton and Assembly start templates were recalculated and
saved in isolation. Their SHA-256 values match the tracked originals. The
Windows GUI check creates and activates the normal native template context.

The finite matrix covers planar interior targets and mixed curve/surface
conditions. It does not establish overlapping surface conditions, partial
curves coincident with a pre-existing face boundary, external Sketch references
inside an owned FEM Sketch, exhaustive Sketcher drag/dimension variants or
Linux execution. Surface conditions require unambiguous patch ancestry.
No solver or results UI is included.

## Bounded profile cut

In the reported Part, the exact bounded tool intersects the solid, yet the
ordinary fuzzy OCCT cut returned essentially the unchanged volume. The
bounded native-face cut now retries with kernel entity tolerances when the
ordinary result removes only a tolerance-scale volume and an exact common
operation confirms material overlap. It accepts the retry only if the result
is valid and its volume loss agrees with that overlap. Authored offsets,
selected faces and the shared placement solver remain authoritative.

On an isolated copy of the representative Part, the cut changes the volume
from 5,873,936.802 mm³ to 5,399,196.996 mm³. Regeneration produces the same
volume after native save/reopen. SHA-256 comparison confirms that the original
file was untouched. The existing Windows Ordered Up-To and cylindrical
Up-To GUI checks exercise the normal feature paths.
