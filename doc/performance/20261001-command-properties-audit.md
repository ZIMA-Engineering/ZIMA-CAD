# Command Properties refresh audit — 2026-10-01

## Scope

The user requested the same redundant-refresh review beyond 2D/H Sweep.
Inputs are calculated native geometry and a command's editing state; outputs
must retain the same rollback context, references, dimensions, Tree and final
model. This is an audit of the opening/preview paths below, not a claim that
all commands have been benchmarked or that every identified opportunity is fixed.

| Reviewed command family | Finding and disposition |
| --- | --- |
| 3D Sweep | Tree published the final scene; the initial preview published it again before rollback was installed; a third refresh then published rollback. Fixed as described below. |
| Extrusion, Revolution and general Feature | Profile previews already avoid base-scene reconstruction when the frame signature is unchanged. Through-all bounds, owned Sketch transitions and operational rollback still have distinct input requirements. Retained; further opening optimization needs dedicated measurements of those branches. |
| Fillet/Chamfer and Shell | Selection setup consumes actual input-body topology and installs command-specific filters/overlays. Refreshes were not deleted based solely on their proximity in the code. |
| Point, Axis, Plane and 3D Curve | Registering the preview callback invokes it immediately and establishes resolved reference geometry. Reference auto-entry follows separately. No global suppression was introduced. |
| Boundary Surface | Scene refresh precedes inspection/picking code that reads the resulting viewer edges. The command's own refresh is required; the Tree pre-refresh is a possible narrower follow-up. |
| Body Properties | The shared finish helper prepares context before showing the dialog and restores it on finish. Retained. |
| Body Scale and Pattern/Mirror | Nested occurrence previews may replace a general refresh with a scoped scene. Candidate for separating context preparation from mesh publication, with occurrence/selection tests required. No change in this pass. |
| Assembly component Properties | Preview can replace the scene after initial context setup. Reference labels and component-origin interaction depend on prepared context. Retained. |
| Body Measurement / centroid | General refresh prepares Tree and command state, then the preview shows the measured input plus centroid. Removing the complete refresh would also remove those side effects. Retained. |
| Section editing | Refresh and preview also manage Sketch context, camera restoration and draft undo. Retained. |

## Accepted 3D Sweep change

The existing rollback context is now installed before registering the synchronous
initial preview callback. That callback publishes the same input-boundary scene
once. The Tree's preliminary full-body refresh is also bypassed for a valid 3D
Sweep editor, using the existing guarded Sweep path. Creation, shared placement
solving, precision, persistence, commit and close handlers are unchanged. No
cached reference geometry is retained across edits and no OCCT work is added.

## Measurement and equivalence

Serial Windows Release native-document probes on the rounded spatial Sweep
fixture, baseline `5451f284`; six openings, three unchanged OK then three Cancel.
Warm means exclude the first opening. Before: 73.863 ms; after: 50.009 ms,
approximately 32% less opening time on this fixture. Mesh publication count
falls from three to one. This is GUI preparation, not kernel calculation speed.

All six corresponding 3D preview framebuffer hashes and all six restored-frame
hashes match before/after. Every probe verifies exact serialized viewer-packet
restoration, unchanged camera, document state and Undo/Redo availability.
2D/H probes also pass with matching preview hashes and retained packet/state
checks. Their production paths were not otherwise changed by this follow-up.
Raw evidence: [probe logs](20261001-command-properties-audit.txt).

Localization review: no product UI text changed. Documentation is English.
No file format, portable release or Linux changes are included. The repository
launcher continues to use the rebuilt local GUI.

Verification: all seven selected Windows contracts passed in 68.72 seconds:
2D/H Sweep GUI, Body-owned Curve/Sweep point references, pending Curve-point GUI,
Sweep commands, selection-filter GUI and five-language catalogs. Logs:
`build/commands-refresh-regression.log` and `build/commands-refresh-build.log`.
