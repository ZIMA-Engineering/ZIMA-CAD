# Broken Drawing views

A break is a Drawing-view presentation property. It hides an interval in the
original projection and translates the retained fragments to leave a paper-mm
gap. It does not cut the Part/Assembly, calculate a new body, change measurement
geometry, or replace source references with invented boundary points.

## Editing

Open View Properties and choose **Edit breaks** (`Editovat přerušení…`). The
internal properties window contains a separate copy of the entire view and its
annotations. The actual sheet is not expanded or edited during this interaction.

The table always ends with an empty input row, marked by the shared green
reference arrow. Choose horizontal or vertical shortening in that row, click
**First position** or **Second position**, then place that boundary in the view.
A dashed line and a point follow the cursor while position entry is active.
Only the active position cell has a green outline. Either position may be entered
first. A short middle-button click or Escape ends entry without discarding an
already picked position. Both positions are required before the row is complete.
The completed row replaces its green arrow with the shared red remove button
and adds a new empty row (up to 32 breaks).

A segment joins the endpoints; perpendicular construction lines mark
the boundaries. The tinted strip is the region to omit. Drag either endpoint to
change the interval, or drag the segment to move both boundaries together.

Each table row specifies:

- Start position from the source origin projected into the view, in model mm.
- Omitted length, in model mm.
- Gap between retained fragments, in paper mm; new breaks default to **2 mm**.
- Boundary mark: none, straight gray line, or gray zigzag.

The position and length are also shown as helper dimensions. Double-click their
values to edit them with the shared inline numeric field (Enter confirms the
number; Escape discards it). The table accepts numeric expressions and shows
three decimal places. Wheel zoom and middle-button pan operate in the editor.
The former Add break, Remove, whole-view/result selector and Fit controls are
replaced by the table workflow. There are no background instruction messages.
The source geometry remains visible while editing; the accepted result uses
the shared Drawing renderer, including existing dimensions.

Editor OK transfers the pending breaks to View Properties; View Properties OK
commits the view. Cancel at either level does not commit that level's pending
changes. The shared properties-window middle-button double-click confirmation
contract applies. Editing an existing break uses the same window as creation.

## Scope and placement

One view may contain up to 32 nonoverlapping intervals in one direction.
Horizontal shortening uses vertical boundaries; vertical shortening uses
horizontal boundaries. Different views may use different directions. Derived
projected views have independent breaks, rather than inheriting another view's
intervals across a different projection direction.

Positions are measured along the view's horizontal/vertical basis from the
projected source Part/Assembly origin. Moving the view on the sheet does not
change them. Changing scale leaves model intervals unchanged and preserves the
paper gap. Rotating the view changes the interpretation of these view-axis
coordinates; review the result after changing orientation. Confirmed orientation
changes remove Drawing-created dimensions in affected views; model dimensions
are retained. Cancel keeps the old orientation and dimensions, and Undo restores
them together. Moving the view on the sheet preserves both kinds of dimensions.
Attaching break
boundaries to model details is not implemented in this version.

The negative-side retained fragment stays at its original position. Each later
fragment is translated by the accumulated removed length minus the paper gap.
No surviving fragment is stretched. Annotation positions inside an omitted
interval use a continuous interpolation into the gap; this is a presentation
mapping, never a replacement geometric reference.

## Dimensions and selection

Native measurement geometry and Show/Erase model dimensions remain intact.
Length values are calculated from the original source positions, while witness
lines, arrows, text and grips follow the displayed fragments. A 1000 mm rod
still measures 1000 mm after a middle interval is hidden. Existing annotation
layout values remain stored; editing breaks does not run automatic text layout
or discard manual offsets. Large changes may still require manual spacing of
labels; inspect the result preview.

Source attachment points inside an omitted interval are not offered for picking.
A dimension attached to a hidden point is hidden without deleting its references
or user layout. The Drawing tree identifies dimensions hidden by breaks. Removing
or moving the break restores their presentation. Balloon anchors in omitted
intervals are also hidden; balloon records are retained.

One shared source-to-display map and its inverse support drawing, grips,
reference highlights and dragging. Source polylines are split at each boundary,
and shaded triangles are clipped into retained fragments. Retained edges keep
their original source identity. Visible construction markers in the editor are
not exported. PDF, DXF and raster exports consume the common sheet renderer and
include only the selected break marks.

## Persistence and command interface

The `.drwz` view stores `breaks`: an array of records with `id`, `vertical`,
`start`, `length`, `gap` and `mark` (0 none, 1 straight, 2 zigzag). No external
cache or sidecar is required. Part and Assembly formats and their start templates
are unaffected. Empty breaks produce the ordinary view.

`drawing.view.create`, `drawing.view.set` and `drawing.view.get` use the same
`breaks` array. Validation rejects invalid numbers, duplicate IDs, mixed
directions, overlapping/touching intervals and more than 32 breaks. View editing
uses the existing Drawing transaction and Undo/Redo machinery.

## Verification (2026-09-20)

The resumed implementation is built into the local native application. The
registered `zima_cpp_drawing_breaks_ui_contract` and
`zima_cpp_drawing_view_controls_ui_contract` pass with offscreen Qt. They cover
isolated editing, endpoint and segment drags, expressions, cursor-centred zoom,
result preview, inner/outer commit ownership, Cancel and shared middle-button
confirmation. The measurement-dimension GUI contract additionally drags an
already dimensioned broken view and verifies both model and Drawing-created
grips, unchanged values/references/layout, and native save/reopen. Native command
tests cover multiple intervals, scale-independent paper gaps, hidden references,
clipping, invalid input, persistence and Undo/Redo. PDF and DXF exports pass.
See [Drawing acceptance and broader test limits](DRAWINGS.md#drawing-projection-and-break-acceptance-2026-09-20).


The 2026-09-20 visual follow-up sharpens both zigzag corners to an included
angle of 20 degrees. The rise is `amplitude * tan(10 degrees)`; the paper
amplitude, gap and dimension values remain unchanged. Both orientations and
multiple scales are covered by the Drawing view command contract.
