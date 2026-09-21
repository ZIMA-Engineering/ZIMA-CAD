# Component properties in GUI and CLI

## Properties header and portable sources (2026-09-21)

The first GUI row is Source: a read-only relative path with an embedded Open
action. Selecting a native Part or Assembly updates the second row containing
its filename, icon and Family Table variant dropdown. The source can be a
different file, not only a variant of the original family. Source and variant
changes are confirmed with the same OK transaction as placement; Cancel leaves
the original occurrence intact. See [Family Table](FAMILY_TABLE.md).

Assembly files store component paths relative to the owning `.asmz` file,
including evaluated Assembly family packets. Loading resolves those paths to
absolute runtime locations. Save As rebases them against the new owner; rename
staging uses the final destination rather than its temporary directory. Moving
a project directory therefore preserves its internal dependencies. Sources on
different Windows drives retain absolute paths because a relative path cannot
represent that relationship. No format fields or version numbers change.

Verification covers actual GUI insertion, pending variant/source selection,
Cancel, middle-button confirmation, native-file browsing, one-step Undo,
missing-reference retention, nested placement, project-directory relocation,
file renaming and Save As. The existing cs/en/de/fr/ru translations cover all
labels in the updated header. The Assembly root menu retains Parameters and
Family Table alongside Insert Skeleton.

`component.set` edits an immediate component of the active Assembly. Shared
`workspace/component_properties` also handles GUI Properties OK and visibility,
suppression, and grounding context actions. The catalog has **206 commands** at this
stage. Assembly solving and general container placement are unchanged.

```json
{"command":"component.set","arguments":{"instance_path":"EXACT-PATH","name":"M10 bolt","visible":true}}
{"command":"component.set","arguments":{"instance_path":"EXACT-PATH","grounded":false,"placement":{"x_mm":20,"rotation_z_deg":30}}}
```

Use paths from `component.list/get`, containing one immediate occurrence in the active
Assembly. Parents cannot edit internal subassembly components through this command.
Optional `document` validates the active document. Open editing rejects mutations.
Nested activation is a separate subsequent stage.

Optional properties are `name`, `visible`, `suppressed`, `grounded`, `placement`,
and `placement_references`; at least one is required. Flags are actual JSON booleans.
Names use native-text validation. Results match `component.get` plus `document`,
`revision`, and `changed`.

`placement` accepts JSON numbers `x_mm`, `y_mm`, `z_mm` (±1,000,000 mm), and
`rotation_x_deg`, `rotation_y_deg`, `rotation_z_deg` (±180°). Manual edits use the
same free-coordinate mask and persisted numerical locks as the dialog. Unground a
component first, optionally in the same command. Existing `value_lock.set` edits locks.

## Mate rows

`placement_references` is the complete list of at most three rows; an empty array
removes mate rows. `component.get` returns the same list.

```json
{"command":"component.set","arguments":{"instance_path":"MOVING-PART-PATH","placement_references":[{
  "kind":"plane_coincident",
  "component":{"owner":"ORIGINAL-OBJECT-ID","key":"FACE-KEY","instance_path":"MOVING-PART-PATH"},
  "target":{"owner":"OTHER-OBJECT-ID","key":"FACE-KEY","instance_path":"TARGET-PART-PATH"},
  "offset":7,"flip":false,"locked":true,"lower_limit":2,"upper_limit":9
}]}}
```

Required `kind`, `component`, and `target` define type and exact original references.
Kinds are `plane_coincident`, `plane_angle`, `axis_coincident`, and `point_coincident`.
References contain `owner`, `key`, and explicit `instance_path`. Empty paths identify
Assembly-owned datums. Optional reference `kind` must match the mate (`face`, `axis`, `point`).

The moving side belongs to the positioned occurrence; its target is independent.
References may point inside a subassembly but position its entire immediate occurrence,
without transferring internal-Part ownership to the parent. Changed references are
validated against existing original data, never OCCT face order or result-body owners.
New cyclic mates reject the whole command.

`offset` is plane-mate distance in mm (±1,000,000,000) or `plane_angle` degrees (±180).
Axis/point coincidence accepts zero offset. Their persisted limits must include zero;
as in GUI, they do not constrain remaining axial translation or free rotation.
`flip` and `locked` are booleans. `lower_limit`/`upper_limit` are numbers or `null`
to remove a limit; values must lie within limits. For unchanged mate kind/reference
pair, omitted values inherit the existing row, including its lock. New pairs default
to zero, no Flip, and no limits. Plane/angle mates start unlocked; axis/point coincidence
starts with zero locked, matching GUI selection. Reordering a row cannot bypass its
locked offset. Explicit `locked:false` permits unlocking and editing together as GUI OK does.

## Transactions and calculations

Shared preparation captures revision and only occurrence-owned properties. Commit
never copies stale source geometry, path, identities, or source appearance from an
open dialog. Name/visibility changes do not solve mates and preserve calculated bodies.
Placement, reference, grounding, or suppression changes adopt current shared source
packets as GUI does and run existing `calculate_placement_references` on a candidate.
They calculate no OCCT, derived copies, or Assembly sections; those require explicit
regeneration. Unsaved sources remain authoritative.

OK/CLI creates one Undo step; identical values are no-ops. Cancel changes nothing.
Preparation becomes invalid after revision changes. Mirror/Pattern allows custom
name, visibility, and suppression, but placement uses `mirror.set`/`pattern.set`,
not ordinary component placement.

Existing `.asmz` fields are used; formats and start templates are unchanged.

## Stage verification

Existing model and actual GUI tests passed **2/2** after sharing commit (8.29 s):
`build/component-properties-shared-build.log`, `build/component-properties-shared-tests.log`.
The GUI scenario is separately registered as `zima_cpp_component_properties_ui_contract`.

The new model test checks independently expected coordinates, all four mate kinds,
angle/Flip, original faces, free movement, numerical locks, limits, repeated-occurrence
identity, cycles, invalid inputs, one Undo/Redo, no-ops, native data, and unchanged
source Part. It exposed premature `AssemblySession::data_generation` increments
when physical-relation validation failed, despite unchanged revision/content.
Reproduction: `build/component-properties-rejection-tests.log`, **0/1**.

The counter now changes only after candidate validation. Model tests then passed
**1/1** (0.18 s): `build/component-properties-atomic-build.log`,
`build/component-properties-atomic-tests.log`. GUI also checks command-supplied name
and plane mate in Properties, later OK readable through console, and independent
Undo of both changes. Actual CLI loads `.asmz`, edits components, performs Undo/Redo,
and checks saved 7 mm displacement, limits, lock, and preserved 6000 mm³ volume.

Both programs built and full **107/107** tests passed (496.95 s):
`build/component-properties-all-build.log`, `build/component-properties-full-tests.log`.
Final review compared axis/point limits with GUI: persisted metadata including zero,
not constraints on other free motion. Model and actual GUI tests now use these
explicitly. New coincidences adopt GUI's zero lock. Another model scenario changes
an unsaved source after Properties preparation, checking that a later name commit
preserves new shared geometry of 12000 mm³ without regenerating an old derived copy.

After final additions, both programs rebuilt and all **16/16** affected model,
process, GUI, and translation tests passed (93.45 s):
`build/component-properties-final-build.log`, `build/component-properties-final-tests.log`.
The full 107-test run preceded default-lock/limit changes and the source-change-during-edit test.
