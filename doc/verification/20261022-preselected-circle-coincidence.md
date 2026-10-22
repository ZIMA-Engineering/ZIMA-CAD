# Preselected point to circular support — 2026-10-22

Status: corrected and verified on Windows.

The existing `zima_cpp_sketch_coincident_ui_contract` reproduced
`Coincident candidate missing: circle:...; order=2`: select a native point,
start C, then try to select a circle. This fixture has no external references.

`start_sketch_coincident` initialized the picker without SketchCurve even when
point preselection had already completed the first input. Picking the point
inside the command already admitted that candidate kind. The start path now
admits the same curve supports for a preselected native point. It reuses the
existing acceptance and solver path. Curve-first native C remains outside its
established contract; directional H/V admission is unchanged.

The existing real mouse regression passes (30.25 s): axes in both input orders
and with point preselection; axis-overlapping native segments and native circles
with point-first input and point preselection; independent saved incidence
equations and native save/reopen. This fixture does not newly establish
noncircular preselected curve variants.

No native data, solver equations, placement or user-visible text changed. The
five-language translation/catalog contract passes. Linux execution remains
unverified. This is separate from the correction of artificial crossings on
coincident external reference circles.
