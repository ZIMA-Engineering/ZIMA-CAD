# Next Work

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
