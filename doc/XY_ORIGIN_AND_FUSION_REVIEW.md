# XY Origin and Fusion properties verification

User authorization: 2026-10-08. The requested shared placement change aligns
new free container Origins with Default and uses XY as the initial drawing
plane. Existing reference equations, datum ordering, side choices, native
identities and stored work planes remain authoritative.

## Changes

- A complete Origin means three distinct positional datum planes from one
  owner and occurrence. Orientation-only twins do not create another frame.
  Partial references and mixed occurrences do not qualify.
- Explicit whole-Origin entry selects automatic XY in Sketch, owned profile
  and construction Plane dialogs. Manual plane choices survive replacement.
- Sketch/profile previews recover the actual container basis from the resolved
  XY, XZ or YZ frame instead of rotating an XZ display carrier. Calculation
  and persisted geometry continue to use the native resolver.
- Whole-Origin Sketch conversion to 2D Sweep preserves the source plane and
  frame; authored XZ quarter turns retain their original normal axis.
- Properties subwindows stay inside their owner when it is resized, using
  the existing shared sizing and positioning helpers.
- View Properties initially requests its natural content height. The owner
  bounds it on smaller screens; scrolling remains available for longer forms.
- Entering an ordinary component Sketch preserves Assembly zoom/pan while
  aligning its actual world plane. Standalone Part entry retains Sketch fitting;
  explicit normal-view fitting is unchanged.
- Ordinary Sketch Properties uses the same persisted rollback boundary as
  other history containers. Entry retains it through Sketcher; closing without
  entry restores the normal scene. Downstream containers in the edited Body
  are suppressed only in the active occurrence; passive occurrences retain
  their current source geometry.
- Drawing Break Editor validates its data directly on OK. Reading effective
  button enablement inside submission was wrong because shared confirmation
  temporarily disables the button box.
- Drawing Origin is a display-only pair of 40-pixel X/Y arrows at the actual
  lower-right sheet origin. Positive X runs left and positive Y up. It does
  not create a pick candidate, history transaction or printed/exported ink.
- The empty application workspace paints a subdued ZC brand mark. It returns
  after the last document tab closes, follows the application palette and is
  absent while documents are open. ZC is a product identifier, not a new
  translated UI label.

## Verification matrix

| Area | Coverage |
| --- | --- |
| Native frames | XY/XZ/YZ, automatic/manual, both reference sides at zero offset, numeric correction, perpendicular offset, all quarter turns and native roundtrip |
| Actual Default GUI | Initial and confirmed Sketch preview axes; all five translated tooltips; Tree Origin and Default equivalence; repeated entry; OK/reopen, Cancel, Undo/Redo and save |
| Dialog capabilities | Every feature using the shared placement predicate; Point, Axis, Plane and Curve; manual plane retained after reference replacement |
| Work planes | Sketch, Holes, Extrusion and Revolution: plane changes, offsets, reference replacement, regeneration, Undo/Redo, save/reopen and independent analytical volume checks |
| Profile GUI | Extrusion/Revolution, existing/new, all three first-Origin entry orders and eight orientations, offset, normal/Origin preview, solid bounds, inline editing, OK/Cancel and other Sketch hosts |
| Sketch GUI | 48 Sketch/Holes/Flat/Bend return-frame cases, external-constraint GUI matrix, native dimension suite; rotated nested Part/Assembly point dragging, exact occurrence selection, local equations, active-only rollback, passive context, saved movement and Trim Cancel |
| FORM | All 24 Body permutations, copied native role IDs, real reordered insertion, BRep validity, volume and reference identities |
| Sketch to Sweep | 72 plane/reference/side/quarter-turn combinations, transformed owning Bodies, source identities and constraints, geometry, successive edits, save/reopen and Undo/Redo |
| FORM authoring plane | Native settling calculation; equal shell area; every old/new tessellation vertex independently tested against the opposite native shell after a rigid 90-degree rotation; stable IDs and R15 corners after reopening |
| Fusion Part dialogs | 38 dialog configurations × five languages × two application sizes = 380 combinations; field/cell containment, top anchors, table growth, confirmation visibility and application bounds |
| Drawing View Properties | All five languages, 1366×768 and 1920×1080 owners, default/resized properties, horizontal containment, every input reachable vertically, always visible OK/Cancel; screenshots inspected |
| Drawing Origin | Two zooms × two pan positions × screen/print; color, actual origin, direction, constant screen extent and absence from print independently checked |
| Drawing transactions | Rotation and guides, projected children, preview, Cancel, OK, Undo/Redo and isolated Break Editor confirmation |

The layout matrix includes profile features, openings, shell/surfaces from solid,
surface operations, Form, Fillet/Chamfer, construction containers, three sweep
types, thread, Body/Boolean, revolved sheet, section, mirror/patterns,
Sketch/Holes/Bend/Flat and Unbend/Bend Back. This is not an audit of every
application dialog, every populated table size or every DPI/theme combination.

These changes do not alter Sketch constraint equations. They are not evidence
that every solver dimension/contact/drag permutation has been retested. Native
profile and external-reference regressions cover the affected placement inputs;
the GUI cases above add actual mouse entry, editing and drag evidence, but do
not cover every constraint kind, support orientation or contact combination.
Linux must run the same shared implementation and its own build/GUI checks;
Windows verification does not establish Linux acceptance.

## Agreed next startup page

The user owns zima-cad.com and zima-cad.cz (2026-10-08). The intended future
startup experience is a closable internal HTML tab named ZIMA-CAD loading
https://zima-cad.com, alongside ordinary document tabs. It is a startup page,
not a modeling document or history owner. Closing all tabs returns to the
empty-workspace ZC background. Website loading should be asynchronous and
must not block CAD startup. This change implements the background only; the
web page and embedded browser integration still need to be prepared.
