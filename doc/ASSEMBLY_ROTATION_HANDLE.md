# Assembly rotation handle

The purple radial arm controls an existing component angle mate when that
component also has an axis-to-axis mate with the same rotation axis.
It appears with the component's dimensions (double-click the component).

- The arm starts on the common axis. Its endpoint moves in a plane perpendicular to that axis.
- It is 70 screen pixels long and remains visible through the body.
- Dragging the endpoint changes the angle and repositions dependent components according to their mates.
- The angle's lower/upper limits and existing range of -180 to 180 degrees apply.
  Crossing 180 degrees stores the equivalent signed angle (for example,
  190 degrees becomes -170 degrees); the geometric rotation continues through the same position.
- Releasing LMB stores one revision; Esc restores the original Assembly.
- Grounded components and locked angle values have no active arm.
- An axis-to-axis mate alone does not create an angle mate. The control edits
  an angle mate that the user has already defined.

The arm is derived display information. Its angle remains in the ordinary
component placement reference inside `.asmz`; no document format change is needed.
The GUI uses the same mate calculation as other Assembly edits. Dragging does
not call OCCT or change source Part geometry.

When looking directly along the arm, its projected direction is undefined,
so it is hidden. A view approximately along the rotation axis is convenient for dragging.

## Verification (2026-09-15)

Seven targeted CTest tests passed: Assembly, Viewer, UI, dimension layout,
work planes, Holes, and the new rotation-handle test. The latter was rerun
separately after correcting the input type of its test Box dimensions.

The new GUI test uses real mouse events and checks the common picker,
angle changes, a single Undo/Redo step, Esc, crossing 180 degrees, angle limits,
dependent-component motion, and native persistence. Model tests check the
axis anchor and displayed angle; presentation tests check the 70-pixel length
at three view scales. The development application was built and
`git diff --check` passed.
