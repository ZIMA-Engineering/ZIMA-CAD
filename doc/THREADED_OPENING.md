# Opening with optional internal thread

**Opening** uses one Properties dialog for creating/editing one container. Types
are plain hole, ISO metric thread, Whitworth BSW, and cylindrical G pipe thread.
Plain holes use numerical diameter; threads use catalog size and derived or manually
specified pilot-bore diameter. This command is internal-only; external threads are separate.

- Hole depth defines the cylindrical end. The hole may also end Up To a target or
  extend Through All.
- Thread length runs from the local opening Origin to the thread cylinder end.
  Runout of a specified pitch multiple continues beyond it.
- Optional chamfer has axial depth and included angle (default 90°). It trims the
  thread surface without shifting the measurement origin or thread-length end.
- Optional drill tip extends beyond blind cylindrical depth and is omitted for
  Up To/Through All.

OK calculation subtracts the extruded bore profile, optionally subtracts the revolved
tip, creates the technological thread surface, then subtracts the revolved entry
chamfer. The final subtraction clips solid and technological surfaces. Everything
forms one history boundary. Profile Sketches/identities persist; preview and dimensions
use ZIMA data without OCCT. Cancel discards pending changes.

Opening owns persistent reference axis `axis:primary`, visible outside Properties
and saved with calculated data. It extends from local Origin to the cylindrical
hole end; tip/chamfer do not change its length. The old separate Hole command is
no longer in the toolbar menu.

New openings initially enable tip and chamfer. Analytic wire preview uses circles
and one connector per cylinder/cone. Container highlighting uses the persisted outline
of the combined cutting body, excluding vanished circles between drilling/chamfering steps.

All surface connectors share a longitudinal half-plane defined by opening axis and
local radial direction. Analytic preview and the circular profile used during explicit
OCCT calculation share it.

Catalog changes send complete dimensions to preview. Switching to a thread extends
fixed-depth holes if needed to fit thread length plus runout. New holes default to
20 mm depth. Plain holes use no catalog; switching back retains the selected thread size.

Drawing sections can use `FaceReference::is_thread_surface()` on displayed/original
triangle references. It derives from persisted role `thread:surface:nominal`, surviving
trimming and save/load. Runout, pilot wall, chamfer, and tip lack the flag. Reading
it requires no OCCT or duplicate metadata detached from face semantics.

Dimensions inside open Properties edit only pending parameters: depth edits the hole;
thread length edits the thread cylinder measured from Origin. Pilot diameter is actual
numerical diameter anchored on the cylinder beyond chamfer. For threads it is informative
(`driving=false`, role `measurement:bore_diameter`); for plain holes it is editable.
A designation such as M10 anchors to the nominal thread cylinder; editing invokes
existing catalog selection in Properties. Only OK writes changes to the model.

Enabled chamfer offers depth/included angle; enabled blind tip offers included angle.
Angular dimensions use existing Sketcher `AngleSymmetric` (A–B–A) and shared rendering.
A temporary axial section contains one generator and the axis; the symmetric dimension
mirrors the other side. This is neither another editable Sketch nor another parameter
source. Existing Properties controls edit values; display performs no OCCT. Dimensions
use shared ViewerDimension; transfer of Opening dimensions into Drawing is subsequent work.

Catalog invocation from a dimension waits for release of the double-click's LMB so
the release does not immediately close it. Informative pilot diameter uses Sketcher
measured-dimension color; its leader points opposite the thread designation, preventing
overlap even in axial views.

Double-clicking thread designation in Edit mode opens a standalone inline catalog.
Selecting an item explicitly recalculates/commits and returns to dimensions; closing
the list changes nothing. Properties does not open. If already open, the catalog
edits pending Properties values until OK. Both entry points share catalog data.

Informative diameter carries ⌀ and remains inspectable; double-click does not edit.
For threads it anchors below the entry to distinguish it from chamfer dimensions.
Integration checks brown pixels in the actual 3D image, return to dimensions after
M12 selection, and cancellation of another selection without mutation.

Opening Properties has no Dimension Text field or custom designation override.
Thread dimensions use catalog designation; hole diameters use actual values.

Up To takes over reference input using Extrusion's target handler, disabling the
active placement row and automatic next-row advancement while preserving placement
values. After target selection or returning to Length, placement input stays inactive
until explicitly clicked. Depth to an inclined plane uses the axis/plane intersection.
Integration tests select actual faces, confirm OK, persist references, and check volume.

Up To, like Through All, both omits tip calculation and unchecks/disables tip in
Properties. Returning to fixed length allows re-enabling it. Helper extrusion to an
inclined plane covers the whole circular profile before trimming, not just its seam vertex.

Thread length has independent Length/Up To termination and a separate plane/planar-face
reference. Up To clips the nominal thread surface even against an inclined plane,
creates no runout, and hides runout settings and numerical thread length. Pilot depth/
termination stays independent. Preview uses analytic intersection; OCCT clipping runs
only during calculation.

Subsequent chamfers/fillets preserve technological thread surfaces at every history
boundary, subtracting only volume removed by that edge treatment. Owner and nominal-
thread flag remain. Through-hole regression also checks the thread-surface end at
the exit chamfer.

Tree expands Opening into base Opening and enabled Thread, Chamfer, and Tip. Clicking
highlights only existing edges of that child in azure, using persisted profile parents
and exact occurrence paths. Context Edit shows only that child's dimensions; Properties
opens the shared Opening dialog. Delete exists only for optional children, disabling
the parameter with calculation/Undo. Removing thread switches to plain bore while
preserving pilot diameter; Tree uses the original cylindrical-hole icon. Properties
can re-enable disabled children.

Chamfer/Fillet use the same Tree selection mechanism: containers hold routes with
collapsed segments. Azure wire comes from persisted pre-operation input. Delete removes
edge selection, not source-body geometry. Split connected portions form separate routes;
R1 starts derive from persisted original-route endpoints. Deleting the final route
removes its container through standard undoable deletion.
