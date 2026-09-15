# Construction geometry in console and CLI

`construction.list` and `construction.get` read the current committed model of the
same open Part/Assembly used by construction Properties. They trigger no OCCT,
reference solving, dependency loading or regeneration, and change no history,
activation, selection or calculated geometry. They work with editing dialogs open,
returning committed data rather than pending dialog drafts.

## Commands

```text
construction.list
construction.list point
construction.get <construction-ID>
```

Use JSON for optional named arguments:

```json
{"command":"construction.list","arguments":{"parent":"<curve-ID>","offset":0,"limit":100}}
{"command":"construction.get","arguments":{"construction":"<point-ID>","document":"<open-document-ID>"}}
```

`list` accepts `kind` (`point`, `axis`, `plane`, `curve3d`), `parent`, `document`,
`offset`, `limit`. Unfiltered, it includes document constructions and owned points.
`parent` selects immediate children: Body IDs return its constructions, 3D Curve IDs
its points. Ordering is saved construction order, with each curve's points immediately
after their owner in path order, not sorted by name or projected from tree widgets.

Items contain stable `construction`, `entity`, `entity_parent`, `origin`, `kind`,
`name`, `parent`, `body`, `parent_construction`, `reference_valid`, `suppressed`,
`parent_suppressed`, `child_count`. `parent_suppressed` means parent-construction
suppression, not Body/occurrence hiding. Listing does not copy coordinates, references
or every curve point. `total`, `more`, `next_offset` support pagination.

`get` accepts construction-container or nested-point ID, not entity/Origin ID.
It additionally returns saved coordinates, rotation, locks, definition and exact
references including occurrence paths, keys, offsets and locks. Axes report direction;
planes report base plane, work offset and separate planar-entity placement. Curves
report type, rounding toggle and point IDs; nested points report radius/tangent controls.
Invalid references retain last-saved state without repair attempts.

Both commands default `limit` to 500, range 1–5000. For `get` it independently limits
references/child IDs with `references_truncated` and `children_truncated`. Retrieve
all children through paginated `list parent`. List `offset`: 0–100000000. If `revision`
changes between pages, reload the list. Sizes/offsets must be integers.

## Coordinates and ownership

`_mm` values are millimetres, `_degrees` degrees, independently of display units.
References use existing native `offset` in mm. `coordinate_system` and
`coordinate_owner` distinguish:

| System | Owner |
| --- | --- |
| `document` | Document, such as a root Assembly |
| `body` | Owning Part Body |
| `parent_construction` | Parent 3D Curve; points use its local frame |

Queries do not transform saved coordinates into View space. Read Body placement
with `body.get`, original reference geometry with `reference.get`. Names are not
identity; equally named constructions have distinct IDs.

Explicit `document` may target another open source without activation. Queries
neither traverse inserted-component constructions nor open sources. Read nested Parts
through their open source document; repeated occurrences do not separately own
constructions.

## Scope and query verification

Basic queries cover `document.constructions` and owned 3D Curve points. Later additions
include embedded paths and construction deletion. Creation/Properties and standalone
3D Curves are described below; references are linked at the end. Numeric placement
also uses [placement.get/set](PLACEMENT_COMMANDS.md). Native schema/templates are
unchanged. Construction commands share Properties transactions and native reference solving.

Model tests cover all four kinds, distinct container/entity IDs, Body/point ownership,
exact local frames in moved/rotated Bodies, invalid references retaining placement,
locks, output limits, errors, inactive documents and native Part/Assembly roundtrip.
Revision, generation and calculated-cache identity checks enforce read-only behavior.
Real CLI reads Part via JSON and Assembly via text stdin; GUI console reads identical
native geometry and verifies unchanged documents.

Final suites **6/6** (26.86 s), `build/construction-query-final-tests.log`: construction
queries, host catalog, real CLI, original references, translations and GUI console.
The previous 5/6 run failed because GUI tests sent empty arguments as JSON `null`
instead of an object. Only that test message changed before the complete affected rerun.

Initial verification used an alternate GUI executable while user CAD ran. After it
closed, normal GUI/CLI rebuilt and passed final startup/console checks **3/3**; see
[PLACEMENT_COMMANDS.md](PLACEMENT_COMMANDS.md). This was not a distribution package.

## Creation and property editing

`construction.create` creates absolute `point`, `axis`, `plane`, `curve3d`.
`construction.set` edits a saved construction by ID, preserving identity, parent and
references. Both share the GUI Properties commit transaction. Part creation inserts
at the active Body's current history position; Assembly creation belongs to its document.

| Argument | Creation | Editing and meaning |
| --- | --- | --- |
| `kind` | Required: `point`, `axis`, `plane`, `curve3d` | Existing kind cannot change |
| `construction` | Model assigns ID | Required existing-container ID |
| `name` | Required nonempty name | Optional rename |
| `values` | Optional numeric object | Same keys, units and restrictions as `placement.set` |
| `direction_axis` | Axis: `x`, `y`, `z`, default `y` | Selected local axis |
| `display_size_mm` | Axis: default 100 mm | Display length 0.001–1000000 mm |
| `base_plane` | Plane: `xy`, `xz`, `yz`, initial default `yz` | Local Origin plane; current automatic/manual selection follows [WORK_PLANES.md](WORK_PLANES.md) |
| `offset_mm` | Plane: default 0 mm | Entity offset along normal, ±1000000 mm |
| `document` | Optional active-document ID | Inactive documents are rejected |

Send extended arguments as JSON. Text uses positional arguments, for example
`construction.create point "Measurement point"`; optional arguments do not use
`key=value` syntax.

```json
{"command":"construction.create","arguments":{"kind":"plane","name":"Mounting plane","base_plane":"xy","offset_mm":12.5,"values":{"x":10,"rotation_x":90}}}
{"command":"construction.set","arguments":{"construction":"ID_FROM_PREVIOUS_RESULT","name":"Mounting plane 2","offset_mm":15}}
```

Results match `construction.get` plus `changed`. Multiple fields commit as one Undo
transaction. Invalid parameters, inappropriate properties, locked values or unresolvable
references reject the whole draft. Identical values return `changed:false` without
history/cache changes. Properties length/offset locks and placement constraints are
respected, never unlocked by commands. `values` includes placement in the same transaction.

Properties use the returned local frame. Plane offset moves its entity, not container
Origin. The same pure GUI/command function prepares axis direction in X, Y, Z rotation
order before the native solver resolves geometric references. Existing `placement.set`
for standalone axes uses it too.

Editing requires the active owning Body; derived Bodies are protected. Pending dialogs
block console mutations. Constructions resolve without OCCT body calculation, preserving
last-calculated geometry as Properties does. Dependent bodies/mates update on explicit
regeneration. Native formats and Part/Assembly templates are unchanged.

Standalone 3D Curves and points also support name/placement editing; geometry/full point
lists are below. Reference replacement was a subsequent stage. `construction.delete`
below handles root constructions.

Native construction reads also restore derived plane-entity placement from saved
Origin, normal and offset; the deserializer previously left it zero. Restoration
neither solves references, rewrites diagnostics nor calls OCCT. It also preserves
last placement for missing-reference planes. File format is unchanged.

Editing an existing 3D Curve point validates its entire owning path before commit,
using the same native check as the dialog. Collapsing onto a neighbor or other invalid
paths leaves no partial changes through either `construction.set` or shared `placement.set`.

## Creation/Properties verification

Full Windows Release **88/88** (389.12 s), `build/construction-edit-full-tests.log`.
Builds: `build/construction-edit-full-build.log`,
`build/construction-edit-final-build.log`. Normal GUI/CLI executables were used; no
alternate test EXE was needed. Production code did not change after the successful run.

Model checks independently verify axis directions after Y/Z rotation, offset-plane
position after local-plane/rotation changes, and a point in a 90°-rotated Body. They
check calculated-body identity, one Undo, no-op, argument types/ranges, locks, valid/lost
references, active Body/document, pending edits and 3D Curve neighbor-collapse rejection.
Native Part/Assembly roundtrips check identity, parameters and last plane placement
even with invalid references.

Real CLI creates/saves/opens a plane, edits with JSON Undo/Redo and creates an axis via
text stdin. GUI console creates a plane, opens the same Properties, checks Cancel, OK,
concurrent-mutation guards, Undo and later display of command-set values. Full suites
also include original modeling, Drawing, Sketcher, interchange, translations and dialogs.

## Standalone 3D Curves and points

`construction.create kind:"curve3d"` requires `points`: 2–5000 point objects in path
order. `construction.set` accepts the same curve parameters. This extension covers
standalone Part/Assembly constructions; feature-owned paths were added separately.

| Argument | Meaning |
| --- | --- |
| `curve_type` | `polyline` (default) or `interpolating_spline` |
| `rounding_enabled` | Polyline rounding; not editable for splines |
| `points` | Complete replacement list; omission retains the original |
| `radius_mm` | Internal rounded-polyline point radius, 0–1000000000 mm |
| `tangent` | `automatic`, `+x`, `-x`, `+y`, `-y`, `+z`, `-z` in point-local axes |
| `tangent_enabled` | Enables control; disabling preserves chosen axis/sign |

`radius_mm`, `tangent`, `tangent_enabled` belong to points, not the curve root.
Set them directly by point ID or inside `points` objects. Points also accept `name`
and `values`, sharing construction locks, units and placement rules.

Point objects with `construction` select existing points of that curve, retaining IDs,
original entities, references and omitted properties. Without it, they create native
points/Origins owned by the curve. Coordinates are local to the curve even when it or
its Body is moved/rotated. Names are not identity.

`points` **replaces the entire list**, like Properties confirmation: array order is
path order, omitted old points are deleted. Repeated IDs, foreign points and unknown
properties are rejected. Read IDs with `construction.get/list` and include every point
to retain. Deleting a point never substitutes another point in dependent references.
Further dependent geometry updates on explicit regeneration.

```json
{"command":"construction.create","arguments":{"kind":"curve3d","name":"Rounded path","curve_type":"polyline","rounding_enabled":true,"points":[{"name":"Start","values":{"x":0,"y":0,"z":0}},{"name":"Corner","values":{"x":10,"y":0},"radius_mm":2},{"name":"End","values":{"x":10,"y":10}}]}}
{"command":"construction.set","arguments":{"construction":"CORNER_POINT_ID","radius_mm":3}}
{"command":"construction.set","arguments":{"construction":"CURVE_ID","curve_type":"interpolating_spline"}}
{"command":"construction.set","arguments":{"construction":"FIRST_POINT_ID","tangent":"+x","values":{"rotation_z":90}}}
```

Splines use exact cubic spans and existing native interpolation. `automatic` disables
controlled tangent; signed axes enable it. Explicit `tangent_enabled` applies afterward;
enabling a previously automatic point uses `+x`, as GUI does. Rotating a point rotates
its controlled tangent. Switching curve type retains radii/tangents for later return;
inactive values do not affect current geometry.

Root and multiple-point edits form one transaction. Native `curve3d_route` also checks
adjacent segments: point collapse, rounded 180° reversal and oversized neighboring
radii reject the entire draft. Replacing lists with the same IDs cannot bypass radius/
placement locks. Identical lists/values are no-ops. Exact-curve calculation uses no OCCT
and preserves body caches. Native schema/templates are unchanged.

## 3D Curve verification

Full Windows Release **89/89** (417.79 s), `build/construction-curve-full-tests.log`.
Extra regression then found combined editing checked referenced-point coordinate
editability in the old frame when the curve rotated simultaneously. Commands now
prepare the new frame with existing native functions and use one reference packet
for the whole list, without creating/calculating a temporary body.

After correction, **8/8** affected regressions passed (38.76 s),
`build/construction-curve-final-tests.log`: curves, general constructions, placement,
catalog, real CLI, GUI console, translations and 3D Sweep. Final GUI/CLI build:
`build/construction-curve-final-build.log`. A later test-only own-Origin-plane check
passed **1/1**, `build/construction-curve-own-frame-tests.log`; production code was unchanged.

Model tests independently check exact quarter-circle midpoint, cubic-spline endpoints,
tangent after local-axis rotation, all signed controlled axes, tangent disable/restore,
point insertion/reordering/deletion, locks, atomic errors, no-op, Undo/Redo and native
Part/Assembly roundtrip. Combined edits use foreign-plane and own-Origin references,
including new points in the same transaction. Solving must neither silently overwrite
requested coordinates nor reject them because of an old frame.

Process tests create splines in Part/Assembly, save, convert to rounded polylines in
new CLI processes, Undo/Redo and inspect native files. GUI opens CLI curves in the same
Properties and checks radius, Cancel, OK, Undo and changed-type display. Initial row-count
expectations were corrected: the table includes **New point…**, which is not geometry.

## Deleting standalone constructions (2026-09-13)

`construction.delete <construction-ID>` shares tree-menu removal. Part uses existing
history deletion and active-Body checks. Assembly also protects references to owned
points, entities and Origins, including section sketches and embedded profiles in
open Parts. Edit owned points through their parent curve list and embedded paths
through their owning Sweep.

Shared Undo/Redo and native saving apply. Catalog at this milestone: 210 commands.
Exact scope, errors, calculation behavior and tests:
[CONSTRUCTION_REMOVAL.md](CONSTRUCTION_REMOVAL.md).

## Original references of independent constructions

`construction.reference.set` shares positional/orientation-field assignment and the
Properties construction transaction. IDs, locks and scope:
[CONSTRUCTION_REFERENCE_COMMANDS.md](CONSTRUCTION_REFERENCE_COMMANDS.md).
