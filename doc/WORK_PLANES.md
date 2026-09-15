# Work plane selection and offset

Sketch, Holes, Protrusion (Extrusion), Revolve (Revolution), and construction
Plane share the same **Base plane** selection.

- **Automatic** selects the work plane from the first planar reference.
- **XY / XZ / YZ** stores a manual selection in the container's local frame.
- **Plane offset** is measured perpendicular to the selected plane. The
  container itself is still positioned by its references and coordinates.
- Replacing a reference, regenerating, or reopening does not overwrite a manual choice.
- Returning to **Automatic** resumes following the first planar reference.
- The Revolution axis belongs to the Sketch and changes frame with the profile.

The automatic choice is local XZ when the first reference defines FRONT
(the local Y axis). The source plane's world name therefore need not match
its corresponding local plane in the already rotated container.

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
Sketch version is 34, Part 22 (internal 46), and Assembly 21 (internal 30).
Start templates and test documents use the current versions.

The user explicitly approved this shared-contract change on 2026-09-15,
including its extension to construction Planes.

## Verification (2026-09-15)

- Full CTest run: 161/165 initially. The remaining four tests subsequently
  passed after updating automatic-selection expectations and embedded
  Sketches in the ZE-A4/ZE-RAZITKO drawing templates.
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
