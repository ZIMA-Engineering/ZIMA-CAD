# FEM Sketch regions, drawing dimensions and Sketch exit controls

## Scope

FEM Point conditions hide the region table. Curve and Surface conditions own
an ordinary editable Sketch on an offset plane. The explicit projection
checkbox enables selection of target faces; Body surface selects faces directly.
Switching the projection mode preserves the authored Sketch. Native FEM Sketch
definitions persist the projection mode and the owned Sketch in the Part file.

The Sketch toolbar places large OK and Cancel controls at the top right,
between two separators. Ordinary Sketch Properties now uses the same transient
editor as owned profiles. Sketch Cancel restores its entry state; Sketch OK
returns the draft to Properties, whose Cancel still discards pending changes.
English is the first-launch default. Explicit saved language settings remain
authoritative; the distribution builder writes English into factory defaults.

Drawing Show/Erase includes solid extrusion length parameters. A model diameter
viewed exactly edge-on uses a sheet-plane linear presentation with the diameter
symbol and witness lines. Its source remains the original driving diameter.
Front and oblique views retain the ordinary diameter presentation.

Straighten/Restore replays referenced subtractive openings against their own
resolved inputs. Exactly planar spline faces inherited from a Twist expose
certified plane metadata and support Up to limits for both bore and thread.
The shared placement solver and signed-side contract are unchanged.

## Windows verification

Tests use the shared C++ implementation, OCCT 8.0.1 and the official Qt 6.11
MSVC kit. Disposable fixtures, logs and GUI captures are under
`build/fem-region-ui-20261122`; user originals are never test outputs.
The checks below passed on the local candidate. The root `zima-cad.bat` remains
the development entry point. Portable release checks are recorded separately.

| Area | Executed coverage |
| --- | --- |
| FEM native Sketch editing | 64 combinations: planar/projected, curve/surface, radius/diameter, driving/reference, locked/unlocked and fixed/free center. Three successive dimension edits and drags per case, independent radius and movement checks, exact region validation, Undo/Redo and native reopen. |
| FEM exact geometry | Native edges, circular regions and holes; external current-edge projection with source identity and read-only source checks; empty/outside regions rejected without a transaction. |
| FEM meshing | Boundary planes on both sides and an internal plane; curves and surfaces, linear/quadratic tetrahedra, mixed projected and direct-face groups, plus the existing cantilever solution checks. |
| FEM GUI | Point/Curve/Surface/Body surface controls, projection toggle, drawing and editing owned line/rectangle geometry, current-edge projection including endpoint/midpoint references, independent inspection, Cancel, no-op OK, middle-button confirmation, Undo/Redo and native reopen. |
| Ordinary Sketch GUI | 12 combinations of XY/XZ/YZ, radius/diameter and fixed/free center. Three actual value edits and mouse drags, draft and document Undo/Redo, Sketch Cancel, Properties Cancel, confirmed save/reopen and a 1920 x 1080 capture. |
| Extrusion dimensions | Native one-sided, two-sided and symmetric dimensions in both directions; GUI Show/Erase, three value edits and reopening. |
| Edge-on model diameter | Both normal signs, horizontal/vertical orientation, three scales, three grips and successive offsets; source updates and native layout reopen. GUI dragging all three grips and editing the driving diameter through three values. |
| Existing dimension layout | 1,344 angular landing variants, actual text drags, light/dark/light rendering, label clearance, properties and middle-button confirmation. |
| Opening Up to | Certified planar spline supports in both directions, separate bore/thread limits, native reopen, no-op and Undo/Redo; slightly bowed supports rejected. Both Up to fields also exercised on an isolated copy of the reported model after Regenerate. |
| Straighten/Restore | Twist attachment matrix at 30/90/120 degrees in both directions, successive state changes, no-op, Undo/Redo and cold native reopening. Reported model copy also checked through Straighten/Restore and cold regeneration, including volume, area, centroid and reference identity. |
| Localization and defaults | Five-language catalog validation; FEM dialogs in all five languages and both themes; GUI/CLI first-launch defaults and explicit user overrides; distribution tests for English factory defaults. |

No new solver equations or constraint types are introduced. Selection-order,
tangency-contact and trimming combinations were not exhaustively repeated for
all Sketch geometry. The GUI exit matrix uses driving unlocked circular
dimensions; the wider driving/reference and lock combinations are covered by
the native FEM matrix. Other Sketch entity families and nested Assembly editing
have not received the complete exit-control matrix in this change.

## Native assets and verification limits

The inventory of 1,210 repository-owned native library/template assets found no
FEM condition definitions requiring conversion. Six start Part/Assembly
templates were recalculated or rewritten and reopened with the changed native
implementation; their bytes remained identical. Template GUI creation checks
verify active editing context and normal commands.

Current-edge projection from an ordinary calculated Body is covered. Boolean
result packets can contain unnamed current edges; projecting those edges into
a Sketch remains an existing limitation and is not replaced by untrimmed
original edges. Boolean source selection, direct face regions and independently
drawn FEM Sketch regions are covered separately.

The affected code is shared between platforms. Linux build and GUI execution
have not been verified in this Windows session.

## Shared FEM plane entry and pressure regions

The Feature and FEM dialogs consume `ProfilePlaneControls`: the same XY/XZ/YZ
presets, View reference entry, plane offset and Sketch button. FEM consumes the
existing placement picker with automatic progression through position references,
the existing first-support/TOP policy, whole-Origin placement and middle-click
termination. Entering its owned Sketch ends placement entry. Plane selection and
the offset lock use existing Sketch and Placement fields; this follow-up adds no
serialized fields or asset conversion.

Pressure accepts either direct Body faces or a bounded owned Surface Sketch,
with optional projection onto selected faces. Planar pressure arrows follow the
actual Sketch plane and reversal; projected arrows use the selected region and
source surface normal. These changes cover condition authoring, persistence,
presentation and mesh regions. The existing linear statics solver still rejects
Pressure conditions; solving pressure loads is not implemented by this change.

Follow-up verification passed: three orders of actual source-Body plane clicks
and local XY alignment, View replacement, Sketch Cancel, explicit XY/XZ/YZ
planes and offset locks, pressure Sketch persistence/no-op/Undo/Redo, shared
Feature controls and Sketcher return, meshing, and all five languages in both
themes. The placement test uses the source Body Origin, consistent with the
ordinary Feature contract. Fixed-pixel picking waits for the existing Sketch
camera animation to finish. Native FEM tests and the distribution unit suite
passed (14 unit cases passed; two platform-specific cases skipped).

## Detail dimensions, independent frames and inherited breaks

New details retain model dimension sources for Show/Erase but start with those
dimensions hidden and without the parent's paper placements. Refreshing a detail
updates model values and geometry while retaining its own dimension visibility,
handles and view layout. Parent annotations remain unchanged. Existing authored
detail dimensions remain visible according to their saved state.

Each detail has its own dimension frame, using the existing per-view guide
count, offset, spacing and visibility fields. Properties reuses the ordinary
view's guide controls. Circular and elliptical crop bounds are analytic; spline
bounds use the extrema of the same periodic cubic boundary as the renderer.
Nested details intersect the inherited crop bounding rectangles; this remains
a conservative frame for overlapping curved boundaries. It does not change
the rendered crop intersection. Model dimensions use the detail frame and may
extend outside its boundary with their witnesses, including in print output.

Projected views offer **Inherit breaks from parent** above the break preview.
Only a model axis shared by parent and child carries a break; reversed axes
reverse the interval correctly. Parent edits refresh linked descendants.
Disabling inheritance keeps the current breaks as editable local definitions.
The approved `inherit_parent_breaks` Drawing field is stored only when true.
Existing drawings default to their saved local breaks. Source, scale and
projection conventions retain the existing shared contracts.

The break editor uses the existing expanding-content policy. Its preview starts
at the A4 landscape aspect within the owning application's available area, and
receives surplus height on resize. The reference table keeps fixed-height rows
above OK/Cancel. Detail insertion guidance now uses the existing assistant/status
channel, without a large instruction overlay on the sheet.

Native verification passed 96 parent/child camera, projection and break-axis
combinations with three successive parent edits, detachment, parent removal,
Undo/Redo and native reopen. Eighteen detail-frame combinations cover all three
crop shapes, three scales and ordinary/broken geometry. GUI checks cover exact
crop bounds, picking and three actual dimension drags outside the crop, printed
strokes, independent Show/Erase, guide Properties, Cancel, Undo/Redo and reopen.
Existing ordinary view controls, model-dimension editing and projected child
refresh checks passed. Break dialog resizing, inheritance and localization
passed in all five languages and light/dark/light themes. Shared guide labels
reuse existing translations; catalog validation passed.

The native asset check reopened and round-tripped 138 tracked factory Drawings
and two protected Drawing copies with both baseline and changed runtimes.
Their saved bytes agreed and source hashes remained unchanged. No factory
Drawing conversion was necessary. Protected originals were not overwritten.
