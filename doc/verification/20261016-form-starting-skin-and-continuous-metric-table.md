# FORM starting skin and continuous thread references

## Scope and direction

This supersedes the initial seating convention in
[the preceding investigation](20261016-planar-form-sides-complete-skin-and-measurement.md).
The user's final instruction is to reverse planar tool insertion and also reverse
the thickening side. The authored attachment datum coincides with the selected
sheet face; there is no stock-thickness translation. The planar source frame
reverses its normal while retaining its X axis. This is a rigid half-turn, and
the complete authored skin is retained. Corner tools retain their existing
thickness-side choice, including the inward control fixture.

Internal authored Sketch dimensions are transformed from the embedded definition
through that same frame. Ordinary selection does not expose them; double-click
inspection exposes them read-only, alongside the existing editable placement
dimensions. No persistence format or shared container-placement contract changes.

Existing saved insertion results require explicit Regenerate with the current
runtime. Factory definitions and already embedded definitions remain independent;
the new runtime does not rewrite the user's original working definitions.

## Native geometry verification

The current Windows OCCT 8.0.0 build passed the Dimpled Hole and Bridge Lance
matrix at thicknesses 0.8, 1 and 1.2 mm on both sheet sides. Checks include native
solid validity, replacement-cut classification, the actual formed side, complete
authored skin vertices on the resulting BRep, bridge symmetry, source dimension
values and witness transforms, the existing insertion axis, Unbend/Bend Back,
Undo/Redo, native save/reopen and cold recalculation. Logs:
`build/form-entry-dimple-native.log` and `build/form-entry-bridge-native.log`.

The finished corner and planar Window native cases passed, including the inward
Window preview and persistence checks:
`build/form-entry-factory-native.log`. The geometric direction assertions were
updated for the user's revised direction, rather than retaining the superseded
outside-facing expectation.

Definition, command and ordinary Thicken regression suites also passed after
relinking with the changed native libraries. The first sandbox run of the latter
two encountered temporary-file rename restrictions; rerunning with the existing
writable `build/diagnostic-temp` directory passed. Evidence:
`build/form-entry-adjacent-native.log` (definition),
`build/form-entry-command-native.log` and `build/form-entry-thicken-native.log`.

## Reference entry and GUI inspection

Planar entry measures the next signed offset at the existing first-click
position. The native Body reference frame used by confirmation must also be
used for measuring these offsets: the viewer reference packet can select an
opposite in-plane Euler branch. The existing transient native resolver is now
shared by FORM preview and this measurement probe. It resolves only copied
documents and cached reference packets; it does not invoke OCCT or commit a
document transaction. Shared container-placement equations are unchanged.

All three planar factory cases passed actual Windows GUI interaction, including
successive entry/replacement of point, plane and straight-edge positioning
references in both rows without moving the first-click position. They also
passed the existing rotation and offset edits, independent inspection, reference
clearing/restoration, creation, rollback, unchanged OK, definition replacement,
Cancel, Undo/Redo and save/reopen checks. Actual single and double Tree clicks
verified internal Sketch-dimension exposure, read-only annotations and unchanged
document revision/camera. Logs: `build/form-entry-dimple-gui.log`,
`build/form-entry-bridge-gui.log`, `build/form-entry-window-gui.log`.

The corner GUI case verified the first-face normal immediately after entry and
the complete second-face orientation and seating before a station was entered.
The third reference retained all three oriented axes. It also passed longitudinal
dimension edits, native mouse hover/click face identity, Cancel, unchanged OK,
Pattern/Mirror, the existing centroid axis, Unbend/Bend Back, Undo/Redo and
save/reopen: `build/form-entry-corner-gui.log`.

All five localization catalogs passed the native translation coverage check:
`build/form-entry-translations.log`. The reference-entry changes introduce no
new visible text. Internal-dimension inspection uses one translated read-only
status message in all five languages.

A subsequent mouse-interaction regression verifies dismissal as well as opening:
the ordinary short middle click hides planar FORM dimensions, and a middle
double-click hides internal and placement dimensions for both planar and corner
FORMs. Repeating the Tree double-click exposes them again. All four factory GUI
cases passed without changing the document revision or camera. No product
behavior needed changing: FORM already uses the same shared dismissal callbacks
as ordinary solids. Evidence: `build/form-middle-dimple-gui.log`,
`build/form-middle-bridge-gui.log`, `build/form-middle-window-gui.log` and
`build/form-middle-corner-gui.log`.

## Engineering tables

A subsequent whole-library audit added frozen viewports to 46 further long
references (three general, 36 tolerance and seven thread topics), including
inch and trapezoidal threads. All blocks of at least 16 rows now have an explicit
frozen-header policy. Existing frozen policies and short topics are unchanged.
The renderer's `--only-frozen` option limits regeneration and browser verification
to these relevant references. Across the three affected datasets, 255 localized
pages passed scrolling, fixed-header, sticky-column, viewport and contrast checks:
`build/long-general-render.log`, `build/long-tolerance-render.log` and
`build/long-thread-render.log`. Eighteen Python tests passed, including a new
whole-library long-table invariant: `build/long-reference-tests.log`.
The extended native File Manager matrix also passed inch and trapezoidal
dimensions in all five languages and Light/Dark/Light, with fixed headers during
scrolling: `build/long-references-native-ui.log`. All ten localized inch and
trapezoidal PDFs passed print-bound and selectable-text checks:
`build/inch-frozen-print.log` and `build/trapezoid-frozen-print.log`.

General-purpose ISO metric threads are one continuous 392-row table. All original
row values and order are independently compared with the retired source pages.
Twenty-five original first-choice rows are emphasized. Five range folders and
the intermediate authored metric landing HTML are retired; native navigation
retains General use, Precision and Tolerances. Seventy-three retired files remain
hashed in the hidden retained-source collection, with original paths recorded.

The metric profile is expanded. Precision metric and Whitworth tables also use
fixed headers with scrolling data. The Whitworth radius leader is collinear with
the radius of its analytic root circle; its marker's tip is exactly on the arc.
The R label follows the leader. The SVG is regenerated from its analytic source,
and independent checks verify the arc contact and radial direction.

Verification completed in Czech, English, German, French and Russian:

- Eighteen Python reference-table checks passed, including original-row and
  archive hashes, metric equations, schematic geometry and localized layout.
- Generated thread HTML matches the dataset: 29 directories and 130 pages.
- Edge rendering passed for the metric and Whitworth topics, including vertical
  header retention, horizontal first-column retention, narrow viewports, theme
  contrast and absent network assets. Logs are
  `build/metric-visible-precision-render.log` and
  `build/whitworth-frozen-render.log`.
- The focused native Qt File Manager matrix passed all three affected references
  in five languages and Light/Dark/Light without page reload. All 1660 labels in
  332 factory directories remain readable native Qt strings:
  `build/threads-final-native-ui.log`. This focused run does not claim completion
  of the unrelated later synthetic file-operation suite.
- All fifteen localized PDFs for General, Precision and Whitworth passed
  selectable-text and print-bound checks. Printing removes viewport limits and
  repeats headers. Logs: `build/threads-final-print.log`,
  `build/precision-final-print.log`, `build/whitworth-final-print.log`.

The shared C++ implementation is used on both platforms. Linux build and GUI
verification remain unperformed on this Windows host. This work does not publish
a new release or modify the immutable 2026101601 release.

Driving FORM values subsequently became editable per insertion; see [the instance editing verification](20261016-form-instance-dimension-editing.md). Layout manipulation remains unchanged.
