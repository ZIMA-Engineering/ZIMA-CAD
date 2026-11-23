# Curved-face picking for owned 3D Points

## Scope and lifecycle

The first face reference of a new Point owned by a 3D Curve or Sweep captures
the actual View hit on an analytic curved support. The hit is converted from
the displayed occurrence through the owning Body and path frames, then resolved
with the existing placement solver. The shared reference-entry control consumes
its established transient picked-position input. No shared placement or native
serialization contract changes.

This applies only to View entry into the empty first position row. Tree entry,
replacement, planar supports, existing numeric locks and general Surface-owned
Points retain their established behavior. Cylinder, Cone and Torus supports use
their calculated analytic metadata; arbitrary spline faces are outside this
change. No geometry calculation is performed while picking or editing drafts.
The ordinary parent command owns calculation and the single commit.

## Kernel failure found during verification

Committing a three-point Sweep exposed an access violation in analytic support
recognition. OCCT 8.0.1's adaptor recognizes a rectangularly trimmed spline as a
spline, but its converter then casts the wrapper itself to a spline before
segmentation. See the [upstream conversion implementation](https://github.com/Open-Cascade-SAS/OCCT/blob/V8_0_1/src/ModelingData/TKGeomBase/GeomConvert/GeomConvert_SurfToAnaSurf.cxx#L817-L837).

Both existing callers now unwrap rectangular surface wrappers before conversion,
while passing the same exact face UV bounds and tolerance. The calculated BRep,
trimming wires, orientation and reference identity are unchanged. This is shared
C++ code with the existing pinned OCCT version. A native reproduction using the
GUI-created Curve failed with access violation before this change and converted
successfully afterward.

## Verification matrix

`zima_cpp_curved_face_point_ui_contract`, selected with
`ZIMA_VERIFY_CURVED_FACE_POINT_ONLY=1`, creates four independent documents:
Curve/Sweep crossed with identity/translated-and-rotated Body and path frames.

For each case the GUI performs three actual View clicks at different Cylinder
heights. An independent ray/Cylinder intersection and Euler frame calculation
checks the captured coordinates. The resolved radial equation must hold within
1e-7; the initial display hit is allowed the existing 0.2 model-unit mesh bound.
The test also checks an off-Origin planar-cap click against ordinary Origin
projection, child Cancel, unchanged draft calculation count, parent OK, native
save/reopen and Undo/Redo. The Sweep section is created through its existing
Sketch action. This is a Point/reference-entry test, not a Sketch solver change.

All four cases passed on Windows with OCCT 8.0.1. The previously crashing
Sweep commits also pass the complete Save and Undo/Redo sequence.

The existing native Curve placement, mesh surface placement, Sweep Point
reference and construction-curve reference suites passed. After the converter
fix, the eight open/closed spline section cases and two saved-support side cases
passed again, as did all eight Sheet Cut skin-to-skin variants (both sides,
clearance on/off, tilted/parallel). The existing GUI Body Curve/Sweep reference
suite and its four interior-curve/solid-edge picking cases also passed.

Logs are under `build/render-followup-20261123/gui-curved-face.out`,
`build/render-followup-20261123/gui-body-curve-reference.out`, and
`build/sweep-subtraction-20261123/` (`native-final.log`,
`sheet-cut-analytic-final.log` and the focused reference logs). An initial
reference-run wrapper mistakenly selected the unrelated Console contract;
the corrected dedicated flag selected and passed the intended reference suite.

No product UI text is added or changed. Translation coverage and catalog
validation passed for Czech, English, German, French and Russian. Cone/Torus
mouse input, nested Assembly mouse input and Linux execution have not been
verified by this matrix. All four new cases and the existing Body/curve GUI
cases passed in the packaged Windows 2026112303 runtime.

## Windows package acceptance

The same focused GUI cases passed in the committed Windows 2026112303 package.
See the [publication evidence](20261123-windows-2303-publication.json).
