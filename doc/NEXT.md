# Next Work

## In-View Dimension and Parameter Representation

- Refine the `Edit` mode so 3D dimensions remain clear and readable for every
  viewing direction and model size.
- Review the placement of dimension lines, plane-like end rectangles and
  editable value fields.
- Prevent projected value fields from overlapping each other or obscuring
  important model geometry.
- Define consistent spacing and automatic offsets for solid dimensions and
  object-position parameters.
- Refine the representation of zero values so they remain visible and
  editable without suggesting a false non-zero distance.
- Finalize the color system:
  - blue for sketches shown in the model;
  - yellow for active parametric/system geometry and selection;
  - cyan for dimensions;
  - orange for the active dimension;
  - red for invalid or conflicting dimensions.
- Test legibility during rotate, pan and zoom, including dark and light
  backgrounds.
- Design angular arc dimensions for object rotation (`RX`, `RY`, `RZ`).
- Later extend the same visual language from solids and object placement to
  sketches, points, axes and planes.

## Viewer Pan and Zoom Diagnostics

- Investigate jerky pan and zoom while rotation remains smooth.
- First run an isolated input test:
  - pan using only `V3d_View::Pan()`;
  - zoom using exactly one OCCT zoom operation;
  - temporarily disable selection, hover detection, `FitAll()` and auxiliary
    overlay updates.
- Log each relevant input event:
  - event type;
  - current and previous pointer coordinates;
  - calculated `dx` and `dy`;
  - number of Pan, Zoom and Redraw calls caused by the event.
- Verify that Qt and OCCT camera controls are not both processing one event.
- Verify that pan uses the delta from the previous position and updates that
  position after every step.
- Check logical versus device-pixel coordinates and avoid unnecessary integer
  rounding.
- Check whether the fixed wheel factors (`1.25` / `0.8`) cause discrete zoom
  jumps, especially for touchpads.
- Test the cursor-anchor correction separately. One wheel event currently
  performs `ZoomFactor()` followed by a corrective `Pan()`, making this the
  primary suspected cause.
- Confirm that pan and zoom do not invoke `FitAll()`, `ZFitAll()`, `Reset()` or
  multiple redraws per event.

## Custom Objects

- Continue with custom objects in the part tree.
- Each Object may contain exactly one user entity (Point, Axis, Sketch or Solid)
  in addition to its mandatory system Origin. Creation commands and `.prtz`
  validation enforce this invariant.
- Add editable object position:
  - X
  - Y
  - Z
- Add editable object rotation:
  - RX
  - RY
  - RZ
- Initial RX/RY/RZ support is implemented in object properties, `.prtz` storage,
  solid rebuild and selected-object local coordinate-system display.
- Object rotation must rotate its local coordinate system:
  - point
  - axes
  - planes
- Child solids and sketches now follow the complete parent object transform.
- Continue extending local-coordinate evaluation to future geometry types.
- Initial plane-on-face attachment is implemented for Object XY/YZ/XZ planes
  and semantic Box faces (`x_min` through `z_max`). Attachments project the
  global Origin onto the target plane, use two perpendicular global reference
  axes with a 45-degree switch, persist in `.prtz`, and preserve the last valid
  transform when the target disappears.
- Extend attachment references beyond Box faces and add dependency-cycle
  detection before supporting general feature chains.
- Wedge is available as a parameterized solid (`length`, `width`, `height`,
  `top_offset`) for testing sloped planar faces. Its semantic face roles include
  `slope`, and plane-on-face attachment supports all six Wedge faces.
