# Work plane selection and offset

Sketch, Holes, Bend, Flat, Protrusion (Extrusion), Revolve (Revolution), and
construction Plane expose **Base plane** selection.

- **Automatic** follows the first directional reference (FRONT) for feature
  containers. A planar reference contributes its normal; a curve contributes
  its tangent at the attachment. An anchoring point does not consume FRONT.
- **XY / XZ / YZ** stores a manual selection in the container's local frame.
- **Plane offset** is measured perpendicular to the selected plane. The
  container itself is still positioned by its references and coordinates.
- Replacing a reference, regenerating, or reopening does not overwrite a manual choice.
- Returning to **Automatic** resumes following FRONT.
- The Revolution axis belongs to the Sketch and changes frame with the profile.

The automatic choice is local XZ when the first reference defines FRONT
(the local Y axis). The source plane's world name therefore need not match
its corresponding local plane in the already rotated container.

Bend has a feature-local attachment profile: for a straight outer edge followed
by its narrow containing face, Automatic selects local XY and aligns the profile
segment with the edge. Material points into the face. FRONT/TOP and the container
origin still follow the common solver. See [Bend placement](SHEET_METAL.md#bend--unbend).

The next independent direction establishes TOP after perpendicular projection.
It does not replace FRONT or switch the work plane. Directions within 0.01 degrees
of parallel or antiparallel leave one rotation free. The shared ordered-frame
contract and verification are documented in
[CONTAINER_PLACEMENT_ANALYSIS.md](CONTAINER_PLACEMENT_ANALYSIS.md).
Construction-specific definitions, such as a plane through three points, retain
their own geometric meaning.

During placement reference entry, clicking the whole **Body Origin** in the
Tree fills the remaining position rows with its datum planes, ordered XZ, XY,
YZ. This also works when the Origin row was already selected before reference
entry; confirmation must not depend on a change of Tree selection.
`zima_cpp_sketch_origin_pick_ui_contract` covers actual Tree clicks for new and
existing Sketch, Holes, Flat and Bend, including partial input, an already
selected Origin, Sketcher return, OK and native save/reopen.

## CLI

- `sketch.create`: omitting `plane` selects Automatic; explicit values are
  `AUTO`, `XY`, `XZ`, and `YZ`.
- `sketch.set`: the same `plane` values, including a Sketch owned by Holes.
- `extrusion.create/set`, `revolution.create/set`: `profile_plane` accepts
  the same values; the offset remains `profile_offset_mm`.
- `construction.create/set`: `base_plane` = `auto`, `xy`, `xz`, or `yz`.
- Queries return the resolved plane and the `plane_auto`,
  `profile_plane_auto`, or `base_plane_auto` flag.

## Native data

The choice belongs to the Sketch (`plane_auto`) or construction container
(`base_plane_auto`). Extrusion and Revolution do not duplicate it in their parameters.
The flags live in the native document data and tracked start templates. The
ordered FRONT/TOP repair reuses these fields and does not change the schema.

The user explicitly approved this shared-contract change on 2026-09-15,
including its extension to construction Planes.

## Verification (2026-09-15)

- Full CTest run: 161/165 initially. The remaining four tests subsequently
  passed after updating automatic-selection expectations and embedded
  Sketches in the ZE-A4/ZE-TITLE-BLOCK-CS drawing templates.
- Model tests cover automatic/manual planes, perpendicular offsets, reference
  replacement, Undo/Redo, save/regenerate, and analytical body volumes.
- The GUI test passed for Sketch, Holes, Extrusion, Revolution, and Plane:
  Cancel, OK, reopen, return to Automatic, and selection retention when
  entering an owned Sketch. The planar reference remains persisted.
- The Assembly cut test also checks volume changes when switching between
  manual XY and the automatic reference plane.
- The development application was rebuilt; `git diff --check` passed.
- Additional live-preview checks verify that XY/XZ/YZ immediately moves and
  rotates the actual plane border in View for all five dialogs. With a 3 mm
  offset, the test checks the center distance, perpendicularity to both plane
  edges, and exact border restoration on AUTO before OK. All scenarios passed;
  no further production preview changes were needed.
