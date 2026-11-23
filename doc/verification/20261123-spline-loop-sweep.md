# Spatial spline sweep subtraction

## Defect and implementation

The reported closed spatial spline had four tangent-connected segments and one
inherited circular section. Its subtractive tool sometimes contained only one
quarter of the route, despite OCCT accepting that incomplete tool as a valid BRep.
Consequently, checking validity or the Boolean return status alone did not detect
the missing channel.

Converting the inherited circles into rational NURBS before building the pipes
allowed transported section seams to collapse individual pieces. Keep a single
centered, inherited circular section analytic on spline paths. Explicit or varying
sections, fixed frames, smooth lofts and separate sharp segments retain their
existing construction. This is shared C++ code; it does not change persistence,
placement, tolerances, reference identifiers or command publication.

The private input was preserved as an isolated copy with SHA-256
`ce9dc0d32ec74c4c160d3eb526822427331a128423a8d1980cf85477221d8775`.
No private document is included in the repository. The committed fixture is a
synthetic four-segment spatial loop around a cylinder.

The successful subtraction also exposed a separate invalid-reference state in
the Tree. Regeneration replaced freshly calculated analytic Cylinder references
with an unchanged saved boundary containing only display triangles. Matching
reference identities were insufficient to establish that the saved boundary
provided the same placement support. Baseline reuse now also requires the
analytic surface kinds needed by the requested references. Incomplete display
data cannot replace the freshly calculated support. This changes cache
eligibility, not the placement solver, native format or retained side identity.

## Verification

Windows native OCCT 8.0.1:

| Case | Result |
| --- | --- |
| Open and closed spline, circle radii 5 and 14.48 | Passed |
| Open and closed spline, 16 by 10 rectangle | Passed |
| Open and closed spline, ellipse semiaxes 8 and 5 | Passed |
| Existing 3D Curve/Sweep contracts, including sharp station cap references and variable profiles | Passed |
| Existing 2D Sweep/Loft contracts | Passed |
| Existing Helical Sweep contracts | Passed |
| Saved Cylinder supports, both Flip states and signed zero, Regenerate / section edit / Regenerate | Passed |
| Private input in GUI: regeneration, valid Tree state, Cancel, unchanged OK, edited section, Undo/Redo, native save/reopen | Passed |
| History reference views and Straighten/Restore dependent-reference lifecycle | Passed |
| Curve and mesh placement, owned Sweep Point and Curve reference contracts | Passed |

Each of the eight new cases checks swept volume against independently integrated
cubic arc length times section area, with relative tolerance 0.0005. It also checks
BRep validity and tests actual solid membership at three interior samples on
every segment, before and after subtraction from the cylinder. Native save/reopen
preserves the exact serialized definition and calculated reference packet; a
fresh calculation reproduces the final volume within 0.000001 cubic model units.

The native test is `zima_cpp_spline_loop_sweep_tests`. The GUI test is
`zima_cpp_spline_loop_sweep_ui_contract`, selected directly with
`ZIMA_VERIFY_SPLINE_LOOP_SWEEP_ONLY=1`. An optional
`ZIMA_VERIFY_SPLINE_LOOP_SWEEP_DOCUMENT` input is always copied before editing.

The GUI check also asserts that draft changes, Cancel and unchanged OK perform
no Body calculation. All five Point supports remain valid after the private
input's regeneration and section edit, and the final Tree row has no unresolved
reference state. The original private file retains the SHA-256 recorded above.

No new or changed product UI text is introduced. Translation coverage and
catalog validation passed for Czech, English, German, French and Russian.
Linux execution remains pending. Packaged Windows execution passed for 2026112303. This finite matrix does not establish every arbitrary
profile, self-intersecting route or closed-loop frame configuration.

## Windows package acceptance

The same focused GUI cases passed in the committed Windows 2026112303 package.
See the [publication evidence](20261123-windows-2303-publication.json).
