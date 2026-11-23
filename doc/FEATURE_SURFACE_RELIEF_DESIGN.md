# Feature relief from a target surface and cylindrical wrapping

Status: behavior and the native storage scope below were explicitly approved
on 2026-11-23. From target surface is implemented in the shared C++ development
build; see the [finite Windows verification](verification/20261123-feature-surface-relief.md).
Cylindrical wrapping remains a design scope, without an implementation claim.

## From target surface

Add **From target surface** beside Correction for a solid Extrusion side ending
Up To. It creates only the material between the uncorrected target boundary and
the corrected boundary, within the extruded Sketch profile. It does not create
the intervening volume back to the Sketch plane. The Feature's Add/Subtract
choice remains authoritative. Assembly Features retain their subtract-only rule.

Correction keeps its existing signed, path-direction meaning. The present
kernel translates the resolved target by `unit_direction * path_correction` in
`occt_kernel.cpp`. It is not a surface-normal offset. For example, a profile
outside a cylinder, directed toward it, can use a negative Correction to create
a raised patch on the near surface. Preserve reversed sides, explicit target
side identities, signed zero and target ordering. Do not infer material side
from Add/Subtract or silently take the absolute value of Correction.

The ordinary and editing wire preview must show both limits and their connecting
profile walls in the shared cyan editing presentation. Reuse the established
Feature preview generator and exact-preview command. Numeric draft changes must
not calculate an OCCT solid. The original/corrected boundaries must share the
same target resolution, so the preview cannot jump to a different cylinder side.
Hover, selection and double-click inspection consume the same cached authored
preview, with the established colors and dimension contract.

At explicit calculation, reuse the existing Up To target resolution and the
same profile side walls, including draft. Build the bounded layer between the
two limits, preserving source/cap ancestry. A difference of the ordinary and
corrected extents is a candidate implementation, subject to geometry and timing
checks; it must not trigger two complete Body regenerations. Zero thickness,
missing targets or an unresolvable layer are explicit failures. Retain the last
valid frame and the editable definition through the existing unresolved-feature
workflow. The unchecked case keeps its established geometry and performance.

Use the existing Feature dialog for creation and editing, with OK/Cancel and
the common middle-button double-click confirmation. Unchanged OK creates no
calculation and no Undo transaction. No shared container-placement solver or
reference-entry behavior is changed.

## Cylindrical wrapping within Feature

Wrapping is a further Feature type using the same Sketch, placement,
Add/Subtract and Properties structure. The first scope is an exact cylindrical
target. A planar Sketch in the cylinder's tangent frame maps axial distance
unchanged and circumferential distance to arc length. Persist an explicit anchor
and orientation; keep the original Sketch editable. Relief height/depth follows
the target surface for this operation and is distinct from path-direction
Correction above.

The first scope must define seam crossing, repeated turns, holes in text,
reversed cylinder axes and inner/outer faces before implementation. A cone is
a later developable target. General doubly curved surfaces are deferred because
an arbitrary planar profile cannot preserve all lengths on them; their distortion
mapping requires an explicit, separately agreed behavior.

## Native storage and asset plan

For the first feature, persist the enabled From target surface state inside its
Feature side's existing `up_to` definition. Keep the ordinary disabled state's
existing encoding. This is a current optional behavior field, not a migration
adapter or a second legacy execution path. Store all required state exclusively
inside `.prtz` and `.asmz`, including Family definitions and Assembly-owned
Features. No sidecar data is required. Wrapping will need its own concrete native
schema and approval after its seam/anchor behavior is specified; it is not part
of this first storage change.

A read-only inventory of 1,210 tracked native assets found no persisted Feature
side `up_to` records in those assets. The private inventory is
`build/trim-contact-20261123/up-to-assets.json`. Recheck this inventory against
the implementation commit. Regenerate the six Part/Skeleton/Assembly start
templates under `config/templates` with the changed native implementation and
verify new-document activation and available commands in the GUI. Recalculate,
save and reopen every factory asset actually affected if that inventory changes;
verify the release packages contain the exact committed assets. Preserve all
authored history, role Bodies and reference/side identities.

Private projects, including ZE0176-TOYOTA-G92K1X, are checked only on verified
recoverable copies. Never replace an original user project or custom library.
Verify unchanged ordinary documents and newly enabled relief documents through
Undo/Redo, save/reopen, regeneration and source-reference loss/repair. A stored
pending/unresolved definition must retain its new mode as well.

## Acceptance

- Planar and cylindrical single limits, ordered multiple target patches, both
  correction signs, both side directions, symmetric sides, draft and profile
  holes/multiple islands, including a raised and recessed text-like fixture.
- Independent volume, boundary position and source/cap ancestry checks. Reject
  invalid/zero-thickness results atomically. Preserve source geometry.
- Feature creation/editing, immediate cyan wire feedback, independent reference
  inspection, exact preview, Cancel, unchanged OK, Undo/Redo, dimensions, Family
  changes and native reopen. Preserve ordinary unchecked Up To behavior.
- Check output equivalence and calculation counts, plus measured timings on
  representative curved targets. Preview movement must not rebuild a Body.
- All labels/tooltips/errors in Czech, English, German, French and Russian.
  Shared C++ implementation; report Windows and Linux execution independently.
