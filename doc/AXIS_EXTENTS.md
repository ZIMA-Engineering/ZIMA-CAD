# Axis container extents

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

Each active side supports Length or Up to; Axis never offers Through all.
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
accept `condition` (`length` or `up_to`); Up to also requires `owner_id` and
`semantic_key`, with an optional `instance_path`. End targets participate in
history dependency and construction deletion checks independently of placement.

Verified on Windows (2026-09-25): `zima_cpp_axis_extents`,
`zima_cpp_contract_tests`, `zima_cpp_translations_contract`,
`zima_cpp_axis_up_to_ui_contract`, `zima_cpp_template_ui_contract` and
`zima_cpp_new_document_options_ui_contract` passed. The Axis GUI test exercises
the common hover candidate list, actual mouse confirmation, OK and Undo/Redo.
Factory templates were regenerated with the current native build. The normal
repository launcher still starts `build/cpp-windows-release/zima-cad-cpp.exe`.
