# Surface Extrusion and Revolution

Part Extrusion and Revolution share three result types: Solid, Thin and Surface.
Surface sweeps the Sketch curves directly into faces, without closing cap faces
or material volume. A connected open profile and closed profile regions are
supported. Open endpoints are not extended automatically.

The existing Properties window handles creation, editing and conversion. Selecting
Surface forces Add and disables Subtract. Workspace commands, document calculation
and the kernel also reject a subtractive Surface. Assembly profile features remain
solid or Thin cuts. A solid cutter can trim a surface, including a history containing
both solids and sheets; their Boolean calculations use separate dimensional inputs.

## Identity and appearance

Side faces remain children of their source Sketch curves. Longitudinal edges and
rim endpoints retain their Sketch point parents. Existing start/end rim references
remain where the geometry has them; Surface has no start/end cap faces. Changing
dimensions or result type does not introduce enumeration-based topology identity.

Calculated Surface faces and edges use fixed yellow (`#F2D34F`) on both sides.
Hover and confirmed selection retain their normal orange and azure wire colors.
The Tree keeps the original feature icon with a small surface badge. Converting
back to Solid or Thin removes the surface appearance.

The **Surfaces** action above View hides and restores surface geometry and its
reference candidates using calculated viewer data. It does not invoke OCCT or
change document history. Technological thread surfaces and thread wires are not
Surface profile results and are unaffected.

Drawing views omit Surface profile results, including their shading, outlines,
hidden edges, section geometry and measurement targets. Threads remain available.
This rule applies independently of the current model-view visibility toggle.

## Console and persistence

`extrusion.create`, `extrusion.set`, `revolution.create` and `revolution.set` accept
`"result_type": "surface"`. A simultaneous `"combine": "subtract"` is rejected
without changing document state. Thin thickness parameters are inactive for Surface.

Part INI/payload versions are 27/51; Assembly INI/payload versions are 23/35.
The native calculated viewer packets retain Surface flags for faces, edges and
points, including anonymous display triangles. Start templates use these versions.
All required data remains inside native documents; no external cache is required.
Drawing storage is unchanged because Surface geometry is omitted before projection.

## Sketcher analysis-row regression

Model measurement rows and Body measurement rows belong to the model history,
not the Sketcher tree. Previously a scene refresh during Sketcher Undo could append
existing analysis records to that tree. Both analysis refresh paths now leave the
Sketcher tree alone. Leaving Sketcher restores the saved model-history rows.
The regression scenario creates an offset, deletes its curve and undoes deletion
with both analysis types already saved, then verifies persistence and row counts.

## Verification

The dedicated surface geometry, shared profile-command and GUI contract suites
cover area/volume, ancestry, open endpoints, conversion, Boolean trimming, native
reopening, visibility and Drawing/thread separation. The existing Sketch Offset
GUI contract covers the analysis-row Undo regression.

Windows Release verification, 2026-09-16:

- Twelve targeted geometry, command, persistence and GUI suites passed
  in 16.26 s (`build/surface-final-tests.log`).
- Full workspace startup, Body measurement UI and section/Drawing UI passed
  in 128.61 s (`build/surface-startup-tests.log`).
- After avoiding geometry copies for models without Surface results, all four
  affected geometry, Drawing, section and surface GUI suites passed in 24.46 s
  (`build/surface-filter-tests.log`).
- Extrusion and Revolution screenshots were inspected for yellow surface shading,
  the toolbar filter and the feature badge.
