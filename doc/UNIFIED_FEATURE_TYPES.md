# Unified Feature types

## Agreed behavior

One Modeling command opens one Feature Properties dialog. Its top-level type
selector offers Point, Axis, Plane, Sketch and Extrusion. The last
choice retains the independent operation on each side, symmetry, Boolean
operation, solid/surface/thin result and optional origin/centroid paths.

Changing type preserves the container identity, placement, owned Sketch,
inactive side settings and their references. Only the active type determines
the available controls and the published result. A type switch must never
silently discard authored geometry or reset its numerical values.

The common placement/reference section uses the existing shared contract.
Plane selection and signed offset form a separate Plane section. The Sketch
button has its own compact section to the right of Plane, on the same row,
and is present only for Sketch and modeling results.
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
| Extrusion | Independent Extrusion/Revolution side controls | The selected result and optional authored paths |

The Axis is always the origin axis normal to the selected work plane. Its
origin lies on the offset plane; lengths are measured from this point. The
origin-axis checkbox is unnecessary for this type and the centroid option is
absent. Plane results show only the offset plane. Labels are anchored at the
origin on that plane, with a readable screen-space gap. The Axis uses the same
finite dash-dot rendering path as the other native axes; neither hidden optional
modeling-axis checkbox controls its visibility. Hovering or selecting a Plane
highlights its border as well as its origin, and coincident unselected planes
cannot cover that highlight. Selecting an Axis highlights its
line and its three defining markers, without adding a fourth midpoint marker.
The **Point** and **Text** checkboxes control the idle origin marker and name.
While Feature Properties is open, the complete Origin is exposed for every
type, including Point. The same presentation controls are available for other
meaningful placed modeling and sheet containers; see [surface placement and
Origin display](SURFACE_PLACEMENT.md).

## Automatic names and Tree icons

New standalone Features receive a localized type name followed by a three-digit
number, for example `Bod 001`, `Osa 001`, `Rovina 001`, `Skica 001` or `Vytažení 001`
in Czech. The Extrusion type uses that name even when one or both sides rotate.
Allocation uses the
first available number for that prefix and checks existing history and
construction names in the Part. Numbers can exceed three digits.

The Tree and View use the same stored name. Changing type updates an automatic
name and the Tree icon; a user-authored name is retained. Renaming never changes
container, feature, Sketch or reference identities. Undo/Redo, Cancel and
Sketcher return preserve the pending or committed naming state with the Feature.
Generated prefixes use the current UI language; stored document names are not
translated simply because the application language changes.

The Feature definition stores `automatic_name`, the last name assigned by the
application. An empty value, including an omitted optional value, denotes a
user-authored name. A name differing from that marker is also user-authored
(for example after a Tree rename). This presentation metadata does not affect
geometry or reference resolution. Existing names are not guessed from their
spelling or overwritten merely because they look like a generated name.

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

Point, Axis, Plane, Sketch, Extrusion and Revolution toolbar entries are shortcuts
to the common Feature command. Revolution initializes the Extrusion feature
with both side operation modes set to rotation. Feature sits immediately below
Select, with its green radial icon and a green separator below it. The separate
Cylinder Surface Axis command is retired; select the cylinder's Axis reference
interpretation in Feature placement instead.

Tree rows follow the actual Feature type. Point, Axis and Plane have no visible
owned Sketch row. Sketch exposes its profile once. Extrusion exposes its owned
Sketch and active side operations, with extrusion/revolution icons matching the
actual operations (both icons for mixed sides). New pending Features stay
collapsed. Opening an owned profile from the Tree enters the owning operation's
Sketcher transaction; Finish returns to its Properties and Cancel restores the
original definition. Hidden rows never delete retained Sketch data.

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

## Contact between modeling Features

The profile command regression covers two 10 × 10 × 10 mm blocks extruded to
opposite sides of the same XY plane. The second profile shares a face, only an
edge, or only a point with the first result. It exercises the real unified
Feature transaction, expected total volume of 2000 mm³, Undo/Redo, native save,
reopen and explicit regeneration. Edge/point contact must not be repaired by
moving geometry or increasing tolerance; it can legitimately retain multiple
solids in the Body result. All three cases pass without changing the kernel.
The reported rejection was not reproduced by these cases; a failing native
model/profile is still needed to diagnose that particular failure.

## Verification

The dedicated Qt contract checks the horizontal Plane/Sketch layout, automatic
and custom names, actual OpenGL axis/plane pixels, whole-Feature highlighting
and the idle modeling-origin marker. The translation contract checks generated
names in all five languages; the GUI lifecycle checks Tree names and icons.

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

### Feature presentation follow-up (2026-09-25)

The complete Windows Release build passed, together with 22 targeted contracts:
12 native tests for Feature parameters, picking, axes, work planes and profile
operations; and 10 GUI tests for Feature lifecycle/layout/rendering, translations,
New Document, owned points, profile frames, Sketcher return, axis end targets,
work-plane editing and cylindrical-face axes. The overlap rendering check uses
an additional coincident plane to catch highlights being painted over.

The work-plane GUI fixture now finds its edited Sketch by ID. Its stock block
also owns a Sketch, so checking the first Sketch in the document incorrectly
reported that accepting a manual plane had failed. All five editor paths now
check the intended object through Cancel, OK, save, reopen and AUTO restoration.

Part, Skeleton and Assembly start templates were resaved by the current native
serializer and are byte-identical to the tracked templates; their GUI creation
checks passed in all five languages. No packaged release is created by this
source/build verification. The previously recorded unrelated failures below
are outside this follow-up test set.

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
