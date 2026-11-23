# Render spotlight extension

The user approved adding a spotlight on 2026-11-23, alongside the existing
omnidirectional light and directional Sun. The native implementation and Windows
acceptance are recorded [here](verification/20261123-render-spotlight-live-camera.md);
release publication remains pending. The native storage scope below was explicitly
approved on 2026-11-23. The current point-light command will be labelled **Light**; persisted
user-authored object names and the existing `point-light` type remain unchanged.

## Behavior and lifecycle

A spotlight has a position, local +X direction, luminous power, source radius,
full cone angle and edge softness. The existing Render object Properties and
placement controls own creation and editing. New cameras and all light types
start the established reference-entry flow immediately. This consumes the shared
placement contract without modifying its solver or reference semantics.

The ordinary View marker is a yellow cone and direction arrow. Hover, selection,
Tree synchronization, independent reference inspection and broken-reference
feedback retain the existing contracts. Pending parameter changes update only
the marker. Unchanged OK does not calculate or create Undo; changed OK commits
one object edit. Cancel discards the draft.

Rendering uses a private immutable scene snapshot. Compile the spotlight axis,
cone cosines and flux normalization once per snapshot, not once per ray. Clip
direct illumination outside the outer cone and smoothly fade between the inner
and outer cones. Shadow visibility and inverse-square distance attenuation reuse
the existing finite-light path. The cone describes light emitted from the source;
no volumetric fog or visible beam in empty air is implied.

## Approved native storage scope

Add the Render object type `spot-light` and a required `spot` record **only for
that type**, containing `cone_angle_degrees` and `edge_softness` (0 to 1). The full
outer cone angle is in (0, 180) degrees; the inner half-angle is the outer
half-angle multiplied by (1 - softness). The other Render types retain their
exact native representation. All data stays inside `.prtz`; no sidecars or
legacy migration branches are introduced.

Inventory repository native assets for Render definitions before changing the
serializer. Existing camera/point-light/Sun definitions do not require
conversion. Regenerate the Part/Assembly start templates using the changed
native implementation and verify new-document activation and commands in the GUI.
Recalculate/save/reopen any factory asset actually affected by the extended
contract; verify all packaged native assets. Protected production projects,
including ZE0176-TOYOTA-G92K1X, remain read-only; any needed investigation uses
verified recoverable copies. No original user document is converted or replaced.

## Acceptance

- Independent equations for on-axis, cone boundary, outside/backward direction,
  softness extremes, source rotation/translation and inverse-square attenuation;
  integrated emitted flux remains consistent with the authored luminous power.
- Rendered images with sharp/soft cones, shadow occlusion, colour, enabled state,
  multiple lights, deterministic worker counts and cancellation. Existing Sun and
  point-light images remain unchanged.
- Creation with immediately armed reference entry, View/Tree picking, whole
  Origin, replacement, inspection, Cancel, unchanged OK, Undo/Redo, native
  save/reopen and invalid parameter rejection without document changes.
- Actual framebuffer checks for camera/light/Sun/spotlight strokes and labels,
  both themes, ordinary hover/selection and broken-reference colour; no yellow
  colour leakage into ordinary model geometry.
- All five UI languages and compact internal Properties layout; Windows package
  verification. Native Linux execution must be reported separately.
