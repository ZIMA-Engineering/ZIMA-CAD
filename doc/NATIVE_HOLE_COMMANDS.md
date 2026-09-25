# Native Hole in GUI and CLI

`hole.create/get/set` operates on `FeatureKind::Hole`. `opening.*` separately manages
the current Opening, `FeatureKind::Thread`, with catalog sizes and thread surfaces.
This does not change file formats.

## Command contract

```json
{"command":"hole.create","arguments":{"diameter_mm":10,"bore_length_mm":10,"placement":{"z":-20}}}
{"command":"hole.set","arguments":{"container":"<id>","diameter_mm":8,"entrance_chamfer_mm":1}}
{"command":"hole.get","arguments":{"container":"<id>"}}
```

- `type`: `plain`, `metric`, `pipe`, `whitworth`. Threaded types enable thread wire;
  plain type may also explicitly set `thread_enabled`, matching GUI.
- Dimensions in mm: `diameter_mm`, `bore_length_mm`, `entrance_chamfer_mm`,
  `exit_chamfer_mm`, `thread_diameter_mm`, `thread_pitch_mm`, `thread_length_mm`.
  Tip angle `drill_point_angle_degrees` is in degrees.
- Flags: `drill_point_enabled`, `exit_chamfer_enabled`, `thread_enabled`, `left_handed`.
  Tip disables exit chamfer as in the dialog.
- `bore_end`: `length`, `through_all`, `up_to`. `thread_end`: `length`, `through_all`;
  the latter copies persisted nominal cylindrical length, rather than automatically
  terminating at the through-bore exit face.
- `name`, `placement`, `document`: shared naming, editable-placement, and active-document
  contracts. Numerical fields are JSON numbers.

Results contain parameters, container/feature/owning-Body IDs, identities of three
internal Sketches and the circle, revision, and locks. Mutations add `changed`.
`get` reads persisted target references without body calculation, revision changes,
or calculated-data allocations.

`set` requires a mutation parameter; identical values do not recalculate. Locks use
existing keys, including `pitch` (alias `thread_pitch`). Targets must be active writable
Part Bodies. GUI templates, Assembly, Drawing, wrong feature types, and inactive Bodies
are rejected.

## Owned Sketches and transactions

`document::update_hole_profiles` edits the existing circle and both axial profiles'
points without replacing identities or traversing OCCT. Dialog preparation and
`workspace::commit_hole` share it. Profile preparation uses a private copy. Its owned
axial frame has positive depth along drilling; ordinary XZ Sketches use −Z as positive
second axis, and blindly using that frame previously sent chamfer/tip against drilling.
Shared placement solver and general Sketch frames are unchanged.

Commit validates profile ownership, circle, points, and construction axes, preserves
identities, applies existing FRONT normalization, and explicitly calculates the Part.
Publication uses `commit_part_document`, including external-reference summaries.
Invalid proposals preserve live model/Undo. Creation/editing share one transaction;
untouched numbers retain full persisted precision. Cancel does not commit; unchanged
OK adds no history.

## Stage limits

Bore `up_to` is supported under the contract below. Native thread wire still accepts
only `length`/`through_all`; no separate thread-length target is introduced. Tip and
exit chamfer require fixed bore length; through/target combinations are rejected
because those profiles need actual end depth. Thread wire removes no additional volume.
Catalogs and thread surfaces belong to `opening.*` / `shaft_thread.*`.

`.prtz`, `.asmz`, `.drwz` extensions and field structures are unchanged, without
external geometry or required caches. Start templates are unchanged; all new text
translations are in tracked `config`.

## Verification

Inputs: 40 × 40 × 40 mm box and Hole parameters. Means: existing Extrusion/Revolution
combined into one subtraction. Output: valid body, stable identities, exact volume.
Independent removed-volume checks:

- Cylindrical bore: `pi * r^2 * L`.
- 45° entry/exit chamfer of width w adds `pi * (r*w^2 + w^3/3)`.
- Tip with included angle a adds `pi * r^2 * (r/tan(a/2)) / 3`.
- Through-bore normal to a box uses box thickness as length.
- Thread wire changes no volume.

Regression covers 60°, 118°, 150°, FRONT references, diameter/depth changes, both
chamfer kinds, three thread kinds, all eight locks, inactive Bodies, invalid inputs,
Undo/Redo, native saving, and fresh calculation. Actual CLI creates/edits Hole; GUI
checks tree, shared Properties, OK/Cancel, exact untouched depth, and saved volume.

Baseline failed on `unknown_command` (0/1, 0.10 s). Initial implementation passed
2/3; volume regression exposed reversed axial frame. After repair, model passed
1/1 in 0.34 s. Both applications/all tests built. Extended integration passed
**11/11 in 179.53 s**, including GUI console (51.20 s), CLI (22.32 s), startup/
translations (95.23 s). Owned-reference refresh passed **1/1 in 0.22 s**. The suite
has 121 tests at this stage; only the stated subset ran. Catalog: **213 commands**.
Logs: `build/native-hole-integration-build.log`, `build/native-hole-integration-tests.log`,
`build/native-hole-owned-reference-tests.log`.

## Bore termination at an original face or plane

```json
{"command":"hole.set","arguments":{"container":"<hole-id>","bore_end":"up_to","bore_targets":[{"owner":"<original-owner-id>","key":"<original-semantic-key>","label":"Target face"}]}}
```

`bore_targets` works in create/set, with at most one reference; active `up_to` requires
exactly one. Required strings are `owner/key`; optional fields are `kind` (`face`/`plane`),
`label`, and empty `instance_path`. Coordinates, substitute triangles, and Part result
topology are not accepted. Input parsing is shared with `opening.*`.

GUI/CLI preparation validates original geometry and owner. Sources may be preceding
objects and available Origins in the same Part, including another Body's original
object. Bodies keep independent local frames; existing placement contracts transform
target references. Self/later/missing sources, points, nonempty occurrence paths,
multiple targets, or supplied derived coordinates are rejected without history.

Native calculation restores active datum planes from original ZIMA data. Original
planar solid faces remain references to that solid; explicit OCCT calculation locates
them in the current original body. Planarity must not convert them into independent
datums. Source-body changes therefore affect fresh native calculation and cached
calculation alike. Missing active targets cannot use old coordinates as valid substitutes.

Target refresh is shared by Opening/Hole. Unavailable target coordinates/identities
are retained for repair, but active calculation fails. Inactive stored targets do
not affect fixed/through drilling. Reassigning identical normalized references neither
calculates nor adds history.

This stage changes only bore termination. Native Hole thread wire retains nominal
length with no new target property. Opening thread-surface termination is documented
in [OPENING_COMMANDS.md](OPENING_COMMANDS.md). Tip/exit chamfer still require fixed
length. Formats, extensions, and start templates are unchanged.

Baseline failed on missing `bore_targets` (**0/1 in 0.14 s**,
`build/hole-target-baseline-tests.log`). After implementation, a model-edit fixture was
corrected to use its required dimension representation. Expanded geometry passed **1/1 in
0.49 s** (`build/hole-target-expanded-tests.log`), covering independent volumes,
plane/source-solid changes, identities, invalid inputs, Undo/Redo, saving, differently
placed Bodies, and fresh incremental calculation. Existing Hole/Opening-target tests passed too.

Both applications/all tests built. Full Windows Release finished **124/127 in
489.43 s**. Three failures were invalid fixtures: two used `origin:plane:XY` instead
of `origin:plane:xy`; an older geometry test declared nonexistent `datum:test-plane`.
Using an actual Origin plane preserved the independently checked volume. After fixture
repairs/rebuild, all **7/7 affected tests passed in 68.55 s**, including actual CLI,
GUI console, Hole, and original Opening targets. Production code was unchanged.

GUI verifies CLI → Hole Properties → OK preserves target plane, owned Sketch identity,
and revision on unchanged confirmation. Models also test lost original planes, later
targets, and inactive persisted references. Logs: `build/hole-target-full-tests.log`,
`build/hole-target-fixtures-build.log`, `build/hole-target-fixtures-tests.log`.
Catalog remains **226 commands**, full suite **127 tests**.

Queries and separate optional-component removal:
[OPENING_COMPONENT_COMMANDS.md](OPENING_COMPONENT_COMMANDS.md).
