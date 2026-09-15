# Shared placement in GUI and commands

The user explicitly approved extracting shared placement editing and calculation
transactions from GUI into the model layer. Native reference-solving rules remain
unchanged. Commands and View inline editing share numerical assignment; construction
Properties and inline editing share commit transactions too.

## Commands

```text
placement.get <object-ID>
```

```json
{"command":"placement.set","arguments":{"object":"<feature-ID>","values":{"x":12.5,"y":-3,"rotation_z":90}}}
{"command":"placement.set","arguments":{"object":"<body-ID>","values":{"reference_offset:2":25}}}
```

`get` accepts `object` and optional `document`, reading current persisted placement
without OCCT, reference solving, dependency loading, or activation changes. Results
contain `kind`, owning `body`, `coordinate_system`, `coordinate_owner`, `placement`,
`revision`, and mm/degrees units. `placement` uses native names: `x/y/z`,
`rotation_x/y/z`, `absolute_rotation_x/y/z`, `rotation_offset_x/y/z`, references,
locks, and validity. Construction placement locks drop the `placement:` prefix to
match Body/feature keys.

`set` accepts `object`, nonempty `values`, and optional `document`. Values must be
finite JSON numbers:

| Key | Meaning and units |
| --- | --- |
| `x`, `y`, `z` | Free coordinate in local frame, mm |
| `rotation_x`, `rotation_y`, `rotation_z` | Corresponding GUI angle field, degrees |
| `reference_offset:N` | Offset of Nth populated position reference, mm |

Reference indexes are zero-based among populated position rows, excluding empty and
orientation-only rows. This matches existing View dimension parameter addressing,
not OCCT order. Exact identity persists as owner, key, and occurrence path.

Angles follow Properties: orientation-driven axes edit corrections; free axes edit
absolute angles. FRONT alone, for example, drives RX/RZ while RY remains absolute.
Commands cannot overwrite reference-driven coordinates or locked values, including
independent active-correction locks. Edit the corresponding unlocked reference offset
instead. Unlocking/adding/replacing references are outside this original stage.

Default Body has three Part-Origin plane references: offsets 0/1/2 correspond to
Z/Y/X. Independent FRONT/TOP controls orientation, so direct `x/y/z` are not free.
GUI follows identical rules.

## Transactions and frames

One `set` edits all values together. Invalid keys, locks, driven coordinates, or
failed calculation leave no partial change. Success creates one Undo transaction;
identical values return `changed: false` without calculation/history. The document
must be active without pending dialogs. Part features/constructions require their
owning Body active. Derived Bodies are definition-driven and reject direct editing.

Body coordinates are in Part, feature/construction coordinates in Body, and 3D-curve
point coordinates in curve. Shared reference geometry expresses sources in the edited
object's frame. For construction points this aligns inline free-axis checks with
existing displayed references, including rotated Bodies/curves.

Body/feature changes explicitly calculate Part through existing reference solving,
preserving owned-profile offset transfer and exact ShaftThread input checks.
Construction changes retain Properties transactions: solve construction references,
refresh external Sketch references, preserve last calculated bodies. Reads, activation,
and tab switches never secretly calculate dependent bodies or parent-Assembly mates.

This stage supports Part Bodies/model containers, standalone Part/Assembly constructions,
and nested points. Embedded feature paths, Assembly components, sections, and reference
editing are subsequent stages. Native formats/start templates are unchanged.

## Verification

Models independently check box bounds after 90° rotation, feature translation, and
owning-Body translation/rotation; volume remains 6000 mm³. They cover one Undo for
multiple values, atomic rejection when a later value is locked, invalid numbers/indexes,
missing references, active Body, and preserved calculated shape on construction edits.
FRONT-only tests distinguish absolute RY from correction RX/RZ and correction locks.
In a rotated curve, Body XZ must fix local X=-60 mm and leave local Y free. Native
Part/Assembly, actual CLI, and bidirectional console/Properties check identical data.

First full Windows Release passed **87/88** (382.41 s), `build/placement-full-tests.log`.
The new console test searched a generic coordinate-field name despite dialog renaming.
A later **8/9** run (66.87 s), `build/placement-final-tests.log`, confirmed other paths
after removing redundant document copying. X-field tests now use actual semantic
lock `valueLock:placement:x`. Added correction-lock protection passed **1/1** (0.17 s),
`build/placement-lock-tests.log`.

Final related tests passed **9/9** (81.10 s), `build/placement-verified-tests.log`:
placement, construction queries, Bodies, catalog, actual CLI, GUI console, inline
dimensions, Assembly updates, and owned profiles. Builds:
`build/placement-verified-build.log`, `build/placement-verified-gui-link.log`.
Production code did not change afterward.

GUI adapters no longer copy entire Parts merely to propose one construction edit;
the document copy occurs inside shared commit. Construction reference geometry is
not prepared for unrelated parameter edits passing through that branch.

After the user closed CAD, ordinary Windows Release `zima-cad-cpp.exe` and
`zima-cad-cli.exe` built (`build/placement-normal-build.log`). Normal executable checks
passed **3/3** (29.51 s): standalone-instance startup, GUI console, actual CLI
(`build/placement-normal-tests.log`). Alternative test executables are no longer needed.
This is a local development build, not a distribution package.

## Explicit approval for shared reference entry (2026-09-13)

The user explicitly approved moving the data portion of
`ContainerPlacementSection::set_reference` into a shared GUI/CLI function, preserving
position/FRONT/TOP separation, duplicate checks, measured locked distance, and automatic
orientation assignment. Approval applies to every dialog using the section and requires
model/GUI regressions. Earlier automatic review rejected the extraction; no code changed
before approval. Placement solver, equations, and persistence contract are unchanged.
Push remained deferred under a separate user instruction.

## Shared reference-field assignment (2026-09-13)

Document-layer `assign_placement_reference` extracts only the data portion of
`ContainerPlacementSection::set_reference`. GUI retains labels, translations,
highlights, and change notifications. Shared code edits proposed position/orientation
rows and locks without creating geometry/history. It preserves exact instance paths,
independent FRONT/TOP, optional automatic plane-to-orientation transfer, and one-time
locked-distance capture.

New tests exposed a preexisting repeat-assignment defect: auto-orientation checks
compared source strings after moving them into the target row, allowing duplicate
TOP assignment. Checks now retain the original path/owner/key tuple before moving.
Source replacement adds no duplicate automatic copy. Both data and real-widget tests
cover this defect.

Baseline reproduced it (**0/1 in 0.13 s**); repair passed **1/1 in 0.12 s**. Both apps/
tests built and broader regression passed **11/11 in 90.45 s**, including Properties,
Windows locks, model placement, constructions, console, and embedded-profile references.
Logs: `build/placement-reference-assignment-tests.log`,
`build/placement-reference-assignment-fixed-tests.log`,
`build/placement-reference-assignment-integration-build.log`,
`build/placement-reference-assignment-integration-tests.log`.

This stage prepares shared data flow without yet adding a standalone assignment
command. Native format is unchanged.

## Reference assignment

`placement.reference.set` adds shared entry for Bodies, standalone constructions,
and primitives. Exact scope, source rules, and examples:
[PLACEMENT_REFERENCE_COMMANDS.md](PLACEMENT_REFERENCE_COMMANDS.md).
