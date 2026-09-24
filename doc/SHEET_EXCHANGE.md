# Sheet from Body and flat-pattern DXF

Both commands belong to the Part's Sheet Metal toolbar. Existing File / Export
commands retain their previous scope and behavior.

## Sheet from Body

Create and activate an empty Body after the source Body in the tree, then choose **Sheet from Body** and a planar
face of another visible Body in the same Part. The source may be a STEP import
or a native solid. The source must precede the destination; the command does not
reorder Bodies or expose later history. The internal properties window offers
the starting face and
material thickness; a matching opposite planar skin supplies a thickness hint.
The hint remains editable. Selection and inspection consume calculated viewer
data and do not run OCCT. Only OK starts conversion; Cancel changes nothing.

The source-face field uses the shared reference controls: a green arrow for an
empty slot, a green outline while accepting a pick, a remove button for a stored
face, and an independent inspection eye. Removing the face reactivates selection.
The form stays at the top when the properties window is resized; extra vertical
space remains below the explanatory note.

Conversion walks the connected tangent planar/cylindrical skin. The initial
wall becomes a native Flat with a numeric position and orientation. Adjacent
cylinders become native Sheet Profiles; subsequent planar walls become native
Flats attached to those new profiles. Terminal bends are supported. The complete
batch is one Undo/Redo transaction. The source Body is unchanged.

The entry and exit generatrices of a cylinder need not have equal lengths.
Conversion measures the exit endpoints in the generated Bend's end-profile
frame and sets its ordinary, editable endpoint extensions. A widening or
narrowing bend can therefore meet the next wall without incorrectly rejecting
the connection as a snapping error. This does not change shared placement or
increase the 0.05 mm joining tolerance. Arbitrary noncylindrical transitions
remain outside this recognition path.

There are no retained references to the source solid. All continuation
references point to newly authored sheet geometry. The new root keeps its
position even when the source moves or is deleted. Source and target Body
placements are accounted for independently, through the existing placement APIs.
The command does not change the common placement contract or add a new persistent
feature type. Generated Flats and Sheet Profiles use their ordinary Properties
windows for later editing.

Straight boundaries remain segments. Circular boundaries become circles or
circular arcs; slots use segments and circular arcs. Exact rational splines are
used for genuinely noncircular boundaries when available; otherwise the calculated
polyline is retained. Multiple curved holes
use the existing shared profile nesting, crossing validation and topology
ancestry implementation, also used by ordinary extrusion and profile previews.

Joining edges may be reconciled within **0.05 mm**, as agreed for sheet
conversion. The newly authored parent's exact boundary is authoritative. This
tolerance is local to conversion and does not widen Sketcher snapping or general
reference tolerances. Material side remains significant for both convex and
concave bends.

This is a basic reconstruction command, not a general inverse sheet-metal
solver. It follows tangent planar/cylindrical transitions; sharp corners without
a cylindrical bend, conical/freeform transitions, and complex corner treatments
may need manual construction or finishing. Unmatched faces on the discovered
skin and their dependent continuations are skipped and counted in the completion
message. Disconnected skins are not inferred from the selected starting face.
The skipped count is not a certification that every surface of the source solid
has been reconstructed.

## DXF

Save the Part, activate its sheet Body, and choose **DXF**. A private calculation
snapshot unfolds all eligible material in that Body. The command finds the
resulting sheet plane and sections the finished material at mid-thickness, so
the export includes holes and cuts and excludes construction geometry, bend
axes and dimensions. Other Bodies, including an original import, are excluded.

The exported drawing is XY at **1:1 in millimetres**, independent of the sheet's
3D orientation. Circular section curves remain DXF circles/arcs; other exact
curves retain their rational spline definition. Export rejects multiple material
thicknesses, failed calculations and material that does not lie in one unfolded
plane. It never commits an Unbend feature or changes the document's saved state.

The DXF writer emits an AutoCAD 2000 (`AC1015`) document with explicit millimetre
units, unique entity handles, Model Space ownership, declared layers/line types,
block records and layouts. The previous minimal header/entities-only stream
omitted the model ownership graph and used undeclared layers. Tolerant readers
could reconstruct these records, while stricter CAD importers could reject the
file. The corrected envelope preserves the exported geometry, rational curves
and existing File / Export scope.

Format references: Autodesk's [common entity group codes](https://help.autodesk.com/cloudhelp/2023/ENU/AutoCAD-DXF/files/GUID-3610039E-27D1-4E23-B6D3-7E60B22BB5BD.htm)
define handles and block-record ownership; [BLOCK_RECORD](https://help.autodesk.com/cloudhelp/2015/ENU/AutoCAD-DXF/files/GUID-A1FD1934-7EF5-4D35-A4B0-F8AE54A9A20A.htm)
defines the associated layout link.

The global `Drawing/DxfDirectory` setting in `config/config.ini` supplies a
folder relative to the Part file, defaulting to `export`. For example:

```text
project/BRACKET.prtz -> project/export/BRACKET.dxf
```

The same command replaces a previous export of that name through the shared
atomic export writer. Failed preparation does not replace the previous file.

## Verification

`zima_cpp_sheet_exchange_tests` covers native slots and circular holes, both
material directions, independently translated/rotated source and target Bodies,
source independence, serialization/regeneration, native cylindrical continuation,
Unbend/Bend Back, through Sheet Cut, and flat-pattern contour/volume checks.
It also rejects a destination Body placed before its source. The GUI contract is
`zima_cpp_sheet_exchange_ui_contract`; `ZIMA_VERIFY_SHEET_EXCHANGE` can select a
private copy of a larger native fixture instead of the synthetic test solid.

### STEP-IMPORT regression, 2026-09-23

The supplied current `STEP-IMPORT.prtz` reproduced **4 created features and
7 skipped faces**. Its three first bends had exit spans wider than their entry
spans; the following wall failed endpoint matching by 3 mm. After copying the
exit spans into native Bend endpoint dimensions, the same starting face produced
**11 features with no skipped faces** in approximately 29.7 seconds, including
native save. This is an explicit calculation measurement, not selection latency.

The source folded volume was 822592.4976508 mm3 and the reconstructed folded
volume was 822592.6393127 mm3 (difference 0.142 mm3). The output uses the
document's existing K-factor for development; the folded volume is not used to
override its bend allowance. The original user document was not overwritten.

The full flat-pattern DXF contains **54 lines and 156 circular arcs**, forming
74 closed contours in XY. Its bounds span approximately 411.780 x 757.212 mm.
An independent ezdxf audit reported no errors or repairs and confirmed millimetre
units, zero Z coordinates and paired contour endpoints. This is independent
format verification, not a claim of a completed interactive VariCAD import.

Native tests cover a following wall, source independence, unfolding/refolding
and cuts. DXF tests check unique handles, resolved object pointers, layer
declarations, unchanged exact curves and deterministic save/reopen output.
The GUI fixture exercises the real source picker, conversion, generated-feature
editing/Cancel, Undo/Redo and the configured DXF toolbar destination. Its larger
fixture mode creates a private empty target when the supplied document already
contains a previous partial conversion. No new user-visible strings were added;
the existing five-language catalog/dialog validation remains required.
