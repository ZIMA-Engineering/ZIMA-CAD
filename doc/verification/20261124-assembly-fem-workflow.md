# Assembly reference authoring and FEM workflow verification

## Scope

The user confirmed that Assemblies retain Point, Axis, Plane, Sketch, 3D Curve,
Mirror and Pattern authoring for layout and future mechanisms. New Extrusion
and Revolution material operations belong to Parts. Existing Assembly cut data,
editing and explicit regeneration remain available; no native format changes
are introduced.

FEM Body properties move immediately below Selection in the right toolbar.
Mesh settings, Generate mesh, Show mesh, analysis calculation and result display
use individual shared command buttons. Display toggles retain checked states and
the shared azure appearance. Results retain their existing submenu. Existing
translation keys are reused in Czech, English, German, French and Russian.

The approved FEM-specific placement filter accepts the Part Origin as well as
the source Body Origin. Ordinary Body feature filtering is unchanged. The
shared placement equations, reference identities and file formats are unchanged.

## Reordering across FEM

Baseline product commit: `8a909e6b82ac046cfabc98ac22a6e2c315d0c879`.
Measurements use Windows/MSVC, Qt 6.11.0 and OCCT 8.0.1. The fixture is a private
copy of the user's saved `50.prtz`; original files are untouched.

| Action | Before | After |
| --- | ---: | ---: |
| CLI Mirror Body move across FEM | 15.82 s | 0.617 s |
| Real Tree mouse drag after the change | — | 0.784 s |
| CAD calculations during the changed GUI move | — | 0 |

These are individual local observations, not universal latency promises.
The query remained approximately 2 ms. The optimization applies only when the
order excluding FEM rows, every Body definition, operation count and exact
current geometry fingerprints remain unchanged. Dependency validation precedes
the metadata commit. Other material reorders retain full calculation.

Native history tests cover both move directions, source-order rejection,
unchanged order, revision count, shared source snapshot, Undo/Redo and native
save/reopen. An independent fresh calculation compares reference keys and
volume with the reused result. The real GUI drag verifies stored order,
Undo/Redo and zero CAD calculations.

## Checks completed

- Assembly profile, cut-history, history-command, history-cursor and Sketch
  properties suites pass. Existing authored cuts are seeded as native test
  fixtures; their editing and persistence checks remain active.
- Profile/Sketch and construction-occurrence reference suites pass.
- The standalone CLI process suite passes, including rejection of new Assembly
  material profiles and continued editing of existing cuts.
- The Assembly GUI gate verifies all four reference presets, disabled material
  creation, retained Curve/Mirror/Pattern commands and restored Part commands.
- FEM GUI Tree clicks accept XY as the second reference from either the Part
  or source Body Origin for Constraint and Force. Replacement, Cancel, OK,
  native reopen and Undo/Redo pass without CAD calculation.
- The general UI contract suite passes with native Windows OpenGL. The
  offscreen Qt platform cannot provide the framebuffer required by that suite.

## Repaired FEM placement regeneration

The current saved `50.prtz` contains three conditions with
`placement.reference_valid = false`. Each condition resolves successfully against
`fem_placement_geometry`, with every referenced face present. Full regeneration
marks all three unresolved again. This reproduces the user's new diagnostic
before meshing begins; it is separate from the previously fixed Gmsh edge
recovery failure.

The final material Body moves the reference packet into its local calculation
carrier before the subsequent analysis-frame pass. The FEM pass then lacks
material references. The user separately approved correcting this protected
preparation path on 2026-11-24. The packet is now retained when FEM, Render or CAM
frames still need it. The placement solver and its equations are unchanged.

The targeted native regression passes for FRONT/BACK face placement with the
source as the last or an earlier material Body, three successive regenerations,
and native save/reopen followed by regeneration. Existing placement commands,
Body reference commands, Render operations and CAM operations pass as well.

The private `50.prtz` GUI reproduction now passes explicit regeneration, native
reopen, mesh generation and linear statics. All three condition placements are
valid. The unchanged authored analysis reports maximum displacement
`0.0285864 mm` and maximum element-average von Mises stress `49.5398 MPa`.
These are software regression observations, not engineering validation of the
user's loading/support assumptions. Mesh and solve do not modify the document
revision or recalculate CAD. The complete local GUI reproduction took 54.5 s.

The full FEM GUI suite passes, including direct sidebar commands, real button
clicks, checked state, azure framebuffer pixels in dark/light themes, five
languages, structural/thermal/coupled calculation, progress, region modes,
condition editing, Undo/Redo and native persistence.

Windows 2026112403 publication passed the committed-source package, fourteen
additional packaged GUI cases, signature and public-download acceptance gates.
The accepted binaries are installed behind the unchanged development BAT.
Native Linux execution has not been verified. No user model or personal
configuration has been overwritten. See the
[publication evidence](20261124-windows-2403-publication.json).
