# Shared Sketch input feasibility audit

Date: 2026-11-26. Status: source-level design review, not an implemented or
end-to-end verified external-Sketch feature. Scope: reuse a standalone native
Sketch from another history feature, with a choice between a linked input and
an internally owned Sketch. Cross-document/occurrence inputs require an explicit
extension of the dependency contract; they are not implied by the existing enum.

## Current user decision

The work-plane field remains a plane selector. It must select the current
container's XY/XZ/YZ datum through the View, consistently with its preset buttons
and with Mirror/Pattern reference entry. External-Sketch selection in this field
was explicitly dropped during the review. Existing explicit placement references
remain separate and must preserve their established behavior.

A separate proposed Sketcher command, **Take geometry**, would select a source
Sketch. The user clarified that locked geometry stays linked to that source;
unlocking produces an independent editable copy owned by the target Sketch.
Moving the consumer must never move the source. The user subsequently deferred
this command explicitly; it is outside the current release and has not been
implemented. Source scope, dependency persistence and the verification matrix
still need to be traced before any future native-contract extension. The inventory
below records the alternatives investigated, not a feature approved for the work-plane field.

## Earlier interaction proposal (not selected)

A reference field can dispatch by selected geometry: a Plane/planar Face locates
an internal Sketch; a standalone Sketch supplies a linked profile. Show the
resolved source name and its role, keep independent inspection, and prevent the
consumer's Sketch editor from changing an external source. Clearing removes the
link only, retains the source document and geometry, and permits authoring a new
empty internal Sketch. It must not silently copy, convert or delete the source.
A previous internal draft should remain recoverable during the same uncommitted
Properties session. Cancel restores the complete prior definition.

The user clarified two distinct linked modes: (1) attach the consumer to the
source Sketch container and inherit its frame, retaining a separate relative
correction; (2) reuse the source's local geometry in the consumer's independently
placed frame. Both retain geometry dependencies; neither means an unlinked copy.
Moving the consumer must never move the source. In the second mode, moving the
source container must not move the consumer. Source shape/dimension edits update
both linked modes on explicit regeneration. An internal Sketch remains a third,
ordinary ownership state. Proposed UI: a source field and an explicit "Inherit
Sketch placement" choice, initially enabled so insertion occurs at the visible
source. This is a design proposal, not a changed placement or persistence contract.

For multiple-input commands, each profile/path has its own source choice. One
ambiguous global field cannot choose all station profiles or both Loft ends.
Display labels and selection rows do not replace stable Sketch/curve/point IDs.

## Command inventory

| Consumer | Candidate linked input | Current implementation and necessary work |
| --- | --- | --- |
| Feature: Extrusion, Revolution, Twist, thin or surface result | Profile; Revolution also requires its axis | `FeatureParameters` contains `sketch_id` and `ProfileSource`. Creation currently forces Internal. The old profile-pick callback is not connected by the workspace. Verify ownership, frame and regeneration before exposing it. |
| Extrusion/Revolution dedicated commands | Profile | Same stored source enum, but current editing prepares owned drafts. Reuse the shared source-selection implementation, including both sides and end references. |
| Planar surface | Closed profile, including holes | Geometrically suitable. Current validation explicitly requires Internal. This guard cannot simply be removed without shared ownership/frame checks. |
| Project/Wrap curve, surface or relief | Source profile | Suitable in principle. Both validators currently require an owned planar Sketch. Keep exact source geometry and the independent target Face references. |
| 2D Sweep | Planar path and each station profile | `Sweep2DParameters` embeds `path_sketch` and `Sweep3DProfile` embeds serialized station Sketches. Requires persistent source references for each role and stable station correspondence after source edits. |
| 3D Sweep | Each station profile; optionally an appropriate curve path | Profiles are embedded; the path is an embedded `ConstructionObject`, not a planar Sketch. Do not treat a Sketch as a universal replacement for its spatial path. |
| Helical Sweep | Orbit, radial evolution and normal section separately | Three embedded Sketch roles. Orbit/start-point/guide-point identities and section frames must remain valid. An arbitrary Sketch is not valid for every role. |
| Loft | Each profile and guide | Owned `GeneralSurfaceBoundary` definitions plus profile/guide IDs. Linked inputs need explicit role, order and correspondence handling. |
| General Surface | Boundary Sketches or curves | Boundaries currently embed Sketch/Curve3D definitions. Linked curves are feasible, subject to the existing surface boundary/continuity requirements. |
| Boundary Surface | Selected curves/edges | Already uses source owner/curve references. This is the nearest established model for reading source geometry without taking ownership; whole-Sketch selection still needs chain interpretation. |
| Holes | Position layout | Stores a Sketch ID. Candidate reuse must preserve the command's point/layout interpretation and avoid reparenting the source. |
| Flat | Closed profile | Stores a Sketch ID. Independent Flat is a suitable candidate; sheet attachment imposes a support frame and needs separate verification. |
| Sheet Profile | Finished contour | Stores a Sketch ID and stable source curve bindings for generated Flat/Bend children. Linked edits must preserve generated child identity and material side. |
| Bend | Start profile; potentially auxiliary path/end profile | Start Sketch ID plus two embedded auxiliary Sketches. Generated/attached profiles have special support and thickness rules. Not a simple ownership toggle. |
| Line Bend | Selected line/segment | Stores Sketch and segment IDs. Source segment must satisfy the planar sheet operation and retain its fixed side. |
| Sheet Cut | Closed cutting profile | Suitable in principle, but validator explicitly requires Internal today. Preserve projection through the sheet and clearance behavior. |
| Sheet Revolution | Profile | Validator requires Internal; attachment, wall side and thickness contracts need dedicated verification. |
| Sheet Transition | Start and end sections | Two embedded Sketches and independently defined end frame. Linked inputs are feasible only with role/section validity and unfolding checks. |
| Fillet, Chamfer, Shell, Thread, ordinary Hole, Mirror, Pattern | No general Sketch input | These commands consume edges, faces, axes, numerical definitions or existing features. Adding this field globally would be misleading. Their explicit reference inputs remain appropriate. |
| Surface sewing/thickening/intersection/trimming | Faces or trimming tools | These already have operation-specific source references. Offer a Sketch only where the actual trimming-tool contract supports its geometry. |

## Shared requirements before implementation

- Resolve exact authored Sketch geometry and native transforms, never display
  tessellation. Preserve source identity, coordinate handedness and contact side.
- Do not change source ownership, move it into the consuming Body or open it as
  the consumer's internal editor. Reference visibility and suppression follow
  ordinary history rules.
- Reject self-reference and dependency cycles. Missing sources retain the last
  valid frame/result for repair and block new material calculation.
- Explicit Regenerate updates dependents in order. Tab switches, source inspection
  and unchanged Properties OK must not calculate solids or create Undo steps.
- Persist required links and authored definitions in native documents; no required
  sidecars. Extending embedded-only inputs is a format/contract change requiring
  the established scope agreement, template/factory verification and protected
  project backup policy before editing serialization.
- Verify source edits, changed topology, multiple consumers, disconnect/reconnect,
  creation versus later Properties, Cancel, no-op OK, Undo/Redo and save/reopen.
  Test independent and attached frames, Body transforms and permitted Assembly
  occurrences. Keep material authoring in Parts under the existing Assembly rule.
- Localize source labels, clear/replace tooltips and validation in all five languages.

## Read paths supporting this assessment

- `cpp/modules/document_core/include/zima/document/feature_parameters.hpp`
- `cpp/modules/document_core/include/zima/document/profile_parameters.hpp`
- `cpp/modules/document_core/include/zima/document/part_document.hpp`
- `cpp/modules/workspace/src/profile_operations.cpp` (`validate_profile_definition`)
- `cpp/app/workspace/primitive_properties.cpp` (draft ownership and forced Internal)
- `cpp/app/primitive_properties_dialog.cpp` (`set_profile_pick_request`)
- `cpp/app/workspace/sweep_properties.cpp` (embedded Sketch editing and frames)
- `cpp/app/sweep2d_dialog.hpp`, `cpp/app/loft_dialog.hpp`, `cpp/app/general_surface_dialog.hpp`

No serialization or new linked-Sketch behavior was introduced by this audit.
