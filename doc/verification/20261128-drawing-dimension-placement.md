# Drawing dimension placement and projected-view icon

Status: Windows development candidate verified; publication awaits the user's
requested sheet-mechanism review. Native Linux execution is not verified.

The ordinary Dimension command now opens in Automatic mode. The existing native
classifier chooses Diameter for a full circle, Radius for an arc, Angular for
nonparallel straight supports, and Linear for the remaining supported pairs.
Explicit type selection, repeated manual commands and editing an existing
dimension retain their selected kind. Automatic placement keeps the same dialog
and resets its references for the next dimension.

While the final placement click is pending, the drawing canvas handles a right
click with the same `cycle_dimension_presentation` implementation used by the
Sketcher. Ordinary dimensions alternate arrow direction; Radius cycles normal,
reversed and shortened leader presentation. This changes only the pending
layout. Movement retains that choice, Cancel discards it, and confirmation saves
it in the existing dimension transaction. Reference-entry candidate cycling and
middle/right-button view navigation retain their established roles.

The projected-view QAction now owns a dedicated SVG icon, shared by its drawing
menu and canvas context menu. There are no drawing-format changes. The two
changed placement messages are translated in all five catalogs; no Sketcher
solver, measurement equation or shared placement solver was modified.

## Windows verification

The native Windows/Fusion harness passed the measurement-dimension, drawing and
repeated-dimension GUI contracts. The expanded repeated-command case exercises
three actual mouse creations for each of Linear, Radius, Diameter, Angular and
Chain dimensions across two views. It cycles presentation repeatedly before
placement, verifies independently expected measured values and unchanged
references/document revision, moves the draft and exercises view navigation.
It also covers five automatically classified cases, explicit override, Cancel,
one transaction per committed dimension, Undo/Redo and native save/reopen.

The broader measurement contract checks the placement hint after switching each
of `cs`, `en`, `de`, `fr` and `ru`. Catalog coverage also passes. Evidence logs are
under `build/sheet-skeleton-first/drawing-contracts.log` and
`build/sheet-skeleton-first/drawing-translations.log`; native screenshots are
written under `build/drawing-radius-placement-*.png` and
`build/drawing-projected-view-menu.png`. These finite checks do not claim an
exhaustive matrix for unrelated Sketcher constraints or drawing annotations.

The source-picker contract additionally checks the same projected-view icon
through Light -> Dark -> Light: neutral outlines follow the palette, the cyan
projection arrow stays present, and the context QAction owns the nonempty icon.
The drawing and source-picker contracts passed together in 21.86 seconds after
adding those assertions (`drawing-icon-themes.log`).
