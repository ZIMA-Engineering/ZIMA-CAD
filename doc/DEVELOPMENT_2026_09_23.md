# Drawing responsiveness and reference workflow checkpoint

This development checkpoint combines the agreed Drawing, Sketcher, import and
Assembly improvements. It is a source checkpoint, not a packaged release.
The normal local launcher remains `zima-cad.bat`.

## Implemented behavior

- Ordinary Drawing views reuse calculated viewer geometry. Placement shows a
  bounding frame; projected placement inherits its parent and commits directly.
  Hidden edges remain continuous on the canvas and dashed in vector output.
  Sections, breaks, details and persistent reference geometry remain supported.
  See [Drawing performance](DRAWING_PERFORMANCE.md) for measurements and limits.
- PDF and active-sheet DXF quick export live in the right command toolbar.
  Drawing defaults and relative export directories are edited in Global Settings
  and stored in `config.ini`; `drawing.ini` is removed. Source parameters and
  local title-block values are separated. See [Drawing settings](DRAWING_SETTINGS.md).
- Factory title blocks and engineering symbols use the agreed colors, thin
  strokes and projection-symbol placement. Balloons retain white text with thin
  yellow circles. Tolerance fields belong to the title block, with `ISO 2768-m`
  and `ISO 8015:2011` defaults; see [Title-block tolerances](TITLE_BLOCK_TOLERANCES.md).
- Sketcher intersections can retain both external references. Coincidence takes
  priority over midpoint snapping. Original reference ownership remains explicit;
  see [External profiles](SKETCH_EXTERNAL_PROFILE.md).
- File renaming respects configured name normalization. Reopening an existing
  Windows file with different letter case resolves the same open document.
- Imported-feature Properties retains a wire preview of the pending placement.
  Controlled STEP/IGES export tests preserve translated and rotated geometry;
  the reported coordinate-loss case was not reproduced.
- Axis from cylindrical face is an explicit command. Assembly axis coincidence
  accepts cylindrical faces, including mixed cylinder/axis pairs and references
  inside an inserted subassembly. Existing Axis containers remain available.
  See [Cylinder axes](CYLINDER_AXES.md) for ownership and verification details.
- The Insert Component icon uses the agreed colors. Normal insertion accepts
  a Skeleton and retains the existing Skeleton tree location and restrictions.

## Console fixture maintenance

The console GUI test's `LENGTH` family column now references the actual test
Box through `kind: dimension`, its stable owner ID and `parameter:length`.
The variant supplies its persisted ID. The test verifies GUI OK/Cancel,
unchanged reference and variant identity, and exact native save round-trip.
Missing references remain invalid; production validation was not relaxed.

The component insertion fixture now checks that Cancel removes the pending
component, then repeats insertion with OK before checking persistence.
These fixture updates add no user-visible product strings. The surrounding
feature changes include all five supported localization catalogs.

The drawing fixture explicitly assigns the sheet's selected source and its
title-block/BOM source. Family navigation assertions compare translated labels
instead of assuming Czech while another UI language is active.

## Final verification

The current Windows application builds successfully. The following focused
checks pass after the fixture updates and idle cylinder-axis marker change:

- Family Table core: 2.99 s; construction commands: 0.33 s, including the new
  assertion that idle cylinder axes retain the axis and original point reference
  without adding a display point.
- Family Table GUI: 7.61 s; cylinder-axis GUI: 7.79 s.
- Localization catalog and source coverage: 2.46 s. These final fixture/display
  changes introduce no user-visible strings.

The full console GUI test passes the updated family fixture and continues
through component insertion, drawing title editing and construction tests. It
fails later after 87.95 s with `Sweep Properties did not consume the CLI reference
offset`. This is an unresolved verification failure, not a green full-suite
result; the Sweep placement implementation was not changed in this follow-up.

Evidence is retained locally in `build/family-fixture-core-tests.log`,
`build/axis-point-core-tests.log`, `build/family-fixture-gui-tests.log`,
`build/family-fixture-gui-retest.log`, `build/family-fixture-console-final.log`
and `build/family-fixture-build.log`. Earlier feature-specific checks and
performance measurements are documented in the linked feature notes.
