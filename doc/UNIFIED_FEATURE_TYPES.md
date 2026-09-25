# Unified Feature types

## Agreed behavior

One Modeling command opens one Feature Properties dialog. Its top-level type
selector offers Point, Axis, Plane, Sketch and Extrusion / Revolution. The last
choice retains the independent operation on each side, symmetry, Boolean
operation, solid/surface/thin result and optional origin/centroid paths.

Changing type preserves the container identity, placement, owned Sketch,
inactive side settings and their references. Only the active type determines
the available controls and the published result. A type switch must never
silently discard authored geometry or reset its numerical values.

The common placement/reference section uses the existing shared contract.
Plane selection and signed offset form a separate Plane section. The Sketch
button has its own section and is present only for Sketch and modeling results.
All types use the existing OK/Cancel transaction and Sketcher-return lifecycle.
Confirming unchanged properties preserves the calculated geometry and Undo
boundary. The comparison includes the owned Sketch, so actual Sketch edits
still calculate and commit. Untouched precise dimensions retain their stored
value even when the numeric control displays fewer decimal places.

| Type | Additional controls | Normal View result |
| --- | --- | --- |
| Point | None | Point at the Feature work-frame origin and its name |
| Axis | Work plane, offset, independent lengths and symmetry | Axis, its origin and both endpoints, and the Feature name |
| Plane | Work plane and offset | Offset plane, its origin and the Feature name |
| Sketch | Work plane, offset and Sketcher entry | Sketch geometry, origin and Feature name |
| Extrusion / Revolution | Full existing profile controls | The selected result and optional authored paths |

The Axis is always the origin axis normal to the selected work plane. Its
origin lies on the offset plane; lengths are measured from this point. The
origin-axis checkbox is unnecessary for this type and the centroid option is
absent. Plane results show only the offset plane. Labels are anchored at the
origin on that plane, with a readable screen-space gap.

## Owned geometry in other commands

A Curve or Sweep owns its path points and Sketch definitions. These do not
create separate standalone Point or Sketch history containers. The owned point
factory is independent of the standalone construction command. Both the Point
Feature and the embedded Point editor consume the same `ContainerPlacementSection`
for references, offsets, orientation and value locks; there is no second point
placement solver to maintain. The shared type selector is fixed to Point in
the embedded editor; it has no work-plane offset. Its OK updates only the
parent draft; cancelling the parent drops those changes.

Sweep Properties initializes its reference geometry before binding stored
reference labels. Reopened reference offsets therefore remain editable rather
than being marked missing while the dialog is still being initialized. This
changes dialog initialization only, not the shared placement solver.

Owned profiles enter the same Sketcher as a standalone Feature. The parent
operation owns their frames: Sweep profile frames follow the path station;
its path-plane choice remains a property of the Sweep. Removing a standalone
Point/Sketch action must not remove these lower-level geometric definitions or
the shared editor. The remaining standalone construction data model is not
migrated in this change.

## Removal scope

Standalone Point, Axis, Plane, Sketch, Extrusion and Revolution buttons are
replaced in Part Modeling by the common Feature command. Their shared geometry
and the operations consumed by other commands remain available as needed.

The Box, Sphere, Cylinder, Cone, Pyramid and Wedge modeling types are removed
completely, including GUI/CLI commands, native parameters and serializers,
primitive-specific calculations, assets and obsolete tests/documentation.
There is no migration or compatibility path for documents containing those
removed types. This incompatibility was explicitly accepted on 2026-09-25.
The current native Feature definition also requires its explicit `type` field;
pre-change Feature documents are not migrated, following the repository's
current-format-only policy.

General analytic surfaces and geometric operations needed by retained features
are independent of the removed modeling types and must remain functional.

## Verification

The native parameter contract verifies all five types, original point/axis/plane
references, signed work-plane offsets and retained inactive side settings.
The GUI contract switches every type, saves and reopens it, exercises Undo/Redo
and Cancel, and repeats Properties/Sketcher transitions with both an axis-only
Sketch and a rectangle. Repeated Fit View must preserve their framing.

Owned Point and incomplete owned Sketch recovery have separate GUI contracts.
The profile-frame matrix also checks Extrusion/Revolution placement and the
frames selected by Sweep/Helical Sweep for their internal Sketcher. These
parent-owned frames must not be replaced with the standalone Feature plane.

The removed solid factories have been replaced in downstream regression fixtures
by real editable Sketch/Extrusion/Revolution definitions. Topology assertions
use the authored Sketch identities, with independently checked dimensions,
volumes, placements and downstream reference behavior. No test-only primitive
commands or legacy topology aliases are registered in the application.

Factory Part, Skeleton and Assembly templates were regenerated with the native
serializer; New Document options are exercised through the Windows GUI. The
Windows development launcher remains `zima-cad.bat`. Linux runtime verification
must be performed on a Linux host.

### Existing failures checked against the original revision

The following failures reproduce in a separately exported and rebuilt clean
`e70ee8200940a7f20f2bfee9c6c8a3713853c443`, with the same Windows toolchain and
dependencies. They are not regressions introduced by the Feature redesign:

| Test | Existing failure |
| --- | --- |
| `zima_cpp_material_library_tests` | The test requires at least 62 bundled materials; the tracked library contains 49. |
| `zima_cpp_exact_spline_contract_tests` | A closed exact curved offset reaches `Curved Extrusion profile requires at least two curves`. |
| `zima_cpp_bend_command_tests` | `bend-continuation-side` fails with `Part placement references did not converge during regeneration`. |

These failures remain open. Material definitions were not invented to meet a
count, and the protected shared placement solver was not changed. Reproduce
with CTest's `-R` filter using the test names above.

### Windows verification results

- Full MSVC Release build of the application, CLI and regression targets passed.
- The broad 177-case regression selection has 174 passing cases and the three
  baseline failures listed above. Cursor/focus-dependent widget tests were
  rerun serially; the entry-table fixture now focuses its editor before sending
  Enter. Czech, English, German, French and Russian catalog checks passed.
- Feature type/layout, owned Point, owned Sketch recovery, profile-frame and
  Sketcher-return GUI contracts passed. The Feature lifecycle test includes
  repeated type changes, Cancel, Undo/Redo, native reload and Fit View.
- Standalone CLI process verification passed, including source references,
  native persistence, imports and exports. CLI configuration now honors the
  material-library path used by assembly imports.
- The integrated Windows GUI uses an 8 MiB stack reserve, matching the combined
  native contract; this prevents the large console verification frame and its
  native preview values from exhausting Windows' default 1 MiB reserve.

The integrated GUI console contract passed in 156.96 seconds. It exercises
real profile-based models through Properties, nested Sweep Point/Sketch editing,
reference offsets, assembly cuts, history dragging, native saving and Undo/Redo.
The standalone Sweep2D and Helical Sweep GUI contracts also passed after the
reference-label initialization fix. The final related selection, including
Sheet Transition model and GUI checks, passed all five tests (249.17 seconds).
