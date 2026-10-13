# Axis extents

## Ordinary Feature Axis

The Part and Assembly toolbars use the shared Feature Axis described in
[Unified Feature types](UNIFIED_FEATURE_TYPES.md). Each side offers Length,
Up to or None; symmetry uses Side 1 on both sides. No default-plane section is
shown. None stores `axis_enabled=false` while retaining the authored numerical
length, target and lock. Its effective endpoint stays at the origin with its
original Start/End identity. The common profile-target resolver supplies the
native plane or captured original-face geometry without an OCCT query.

`feature.get/create/set` use the same native Feature definitions in both
documents. Assembly material operations remain subtraction-only. Ordinary Axis
creation, editing and Sketcher return consume the same internal Properties
window and shared placement contract as Part features.

## Viewer helper-point visibility

The Axes visibility switch hides the associated finite endpoints and defining
point markers, including generated profile/sweep/helix axis endpoints. The
Planes switch similarly hides a native Plane's defining-point marker. Ordinary
hidden helper points are removed from the common candidate offer; independent
Point containers remain visible. Explicit point/axis/plane reference entry and
reference inspection retain their existing exceptions. Origin visibility keeps
its separate established rules.

These switches filter already calculated viewer data. They do not remove native
points, change identities, calculate geometry or move the camera. The shared
MeshView behavior applies to Parts and Assembly occurrences.
`zima_model_preview_ui` verifies native Windows picking, reference entry and
inspection, visibility restoration, unchanged camera and an exact framebuffer
comparison against a fresh packet without the hidden helper geometry.
It additionally uses the production Feature Axis packet and its real Start/End
point ancestry in a nested occurrence, including an Assembly packet whose hidden
Axis line has already been omitted. Original reference ownership remains available
to hide its center and endpoint markers together. Explicit point entry can still
select those same persisted identities.

## Construction-reference data

The construction API also supplies native datum data used by existing reference
and curve workflows. Its extent representation below is separate from the
ordinary Feature Axis parameter object. Release 2026101108 extended this
construction path; the ordinary Feature controls were completed subsequently.

The Axis container uses the existing placement and direction controls. Its finite
geometry offers three extent modes:

- One side: from the container origin to the specified length in its direction.
- Two sides: an independent reverse length and forward length about the origin.
- Symmetric: half the specified total length on each side of the origin.

Properties creation and editing share the same dialog, transient preview and
OK/Cancel transaction. The second length is visible only for Two sides. Both
lengths support numeric locks and inline dimensions. Changing an extent does not
change the container placement, orientation references or axis direction.
Existing placement shortcuts that derive display length from two bounding planes
continue to derive that length. New face-based axes use Feature > Axis with the
[On axis reference mode](SURFACE_PLACEMENT.md); existing face-derived construction
records retain their automatically calculated span and point visibility.

The finite endpoints use the persisted Axis entity as their parent and the
semantic roles `axis:point:start` and `axis:point:end`. The native construction
record stores their parent and roles explicitly. Coordinates and lengths do not
form the identity. Both points are available in the original-reference packet;
downstream containers can attach through the existing placement contract. Start
is the negative-side boundary (the origin in One side); end is the positive-side
boundary. Switching extent mode moves these boundaries without swapping roles.

The infinite analytical axis keeps its existing reference origin and identity.
The visible finite axis is centered between its two boundaries. Ordinary endpoint
markers are brown. No OCCT calculation is needed for this geometry.

Native Part and Assembly construction records store `axis_extent_mode` and
`axis_reverse_length`. CLI `construction.create` and `construction.set` expose
`extent_mode` (`one_side`, `two_sides`, `symmetric`) and `reverse_length_mm`;
`display_size_mm` remains the forward/total length.

Verification: `zima_cpp_axis_extents` covers geometric extent, endpoint identity,
downstream point attachment, native serialization and Undo/Redo.
`zima_cpp_translations_contract` covers the Properties modes, second-length
visibility, OK, reopening, Cancel and all five UI languages.

## Up-to termination

Each side supports Length, Up to or None; Axis never offers Through all.
None disables only that direction. At least one direction must retain a
nonzero span. Selecting None for the forward direction activates Two sides so
the reverse direction remains independently editable. Re-enabling a numeric
direction supplies a positive initial length. A disabled direction stores zero
in the existing length field and has no Up-to target; no extra persistent field
is needed. Its endpoint stays at the origin and retains its semantic identity.
Two sides have independent targets. In Symmetric mode the forward target sets
half the total span and the other end is mirrored about the container origin.
Switching back to Length restores the entered numeric value.

Targets use original persisted points, planes or faces and the same reference
cell styling and common Viewer candidate list as other commands. A point defines
the plane through that point perpendicular to the axis. Plane intersection is
exact; non-planar faces use the stored target triangulation, so their accuracy is
limited by that representation. No OCCT work occurs during target picking or
preview. A parallel, missing, self-referencing or wrong-side target cannot be
committed. Regeneration retains the last valid finite span when a target is lost
and reports the invalid reference. Up-to endpoints retain the same semantic IDs.

The native record stores two `axis_ends` with target occurrence/owner/semantic
identity, end mode and last resolved distance. CLI `forward_end` and `reverse_end`
accept `condition` (`length`, `up_to` or `none`); Up to also requires `owner_id` and
`semantic_key`, with an optional `instance_path`. End targets participate in
history dependency and construction deletion checks independently of placement.

Verified on Windows (2026-09-25): `zima_cpp_axis_extents`,
`zima_cpp_contract_tests`, `zima_cpp_translations_contract`,
`zima_cpp_axis_up_to_ui_contract`, `zima_cpp_template_ui_contract` and
`zima_cpp_new_document_options_ui_contract` passed. The Axis GUI test exercises
the common hover candidate list, actual mouse confirmation, OK and Undo/Redo.
Factory templates were regenerated with the current native build. The normal
repository launcher still starts `build/cpp-windows-release/zima-cad-cpp.exe`.

The 2026-10-11 native Windows extent regression additionally verifies forward-only
and reverse-only axes, disabled-end identity, serialization, Undo/Redo and rejection
of both directions being disabled. Linux execution remains unverified.
The Windows five-language dialog contract also covers selecting
None, committing a reverse-only axis, reopening it, and committing a forward-only
axis. Existing Up To GUI selection and reference inspection passed separately.
