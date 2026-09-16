# Removing placement references through GUI and CLI

`placement.reference.remove` uses the same row-data operation as the cross button
in shared Container Placement. The object's existing transaction commits the change,
without bypassing document validation.

```json
{"command":"placement.reference.remove","arguments":{"object":"OBJECT-ID","index":0}}
```

`object` is a stable ID in the active document. Optional `document` guards against
active-document changes. Indexes 0–2 are position references, 3 FRONT, and 4 TOP.
Invalid indexes are rejected; an already empty field is a no-op. Results contain
`document`, `object`, `index`, `changed`, `body_calculated`, and new `revision`.
Command names remain English; help and errors are localized.

## Supported objects

| Object | Command ID | Commit |
| --- | --- | --- |
| Part body | Body ID | Body Properties and calculation |
| Point, axis, plane, or standalone 3D curve in Part/Assembly | Construction ID | Construction Properties, without body calculation |
| Standalone 3D-curve point | Owned point ID | Construction transaction in the parent's local frame |
| Box and other primitives | Container ID | Corresponding Properties and calculation |
| Part Extrusion and Revolution | Container ID | Profile transaction including owned Sketch |
| Bend, Flat and Holes | Container ID | Feature transaction including owned Sketch |
| Assembly profile cut | Cut ID | Calculate the owned cut while preserving source Parts |
| Sweep2D, Sweep3D, and helical Sweep | Container ID | Shared Sweep transaction |
| Embedded Sweep3D path point | Owned point ID | One parent-Sweep commit |
| Hole and current Opening | Container ID | Corresponding hole/opening transaction |
| Imported body | Container ID | Import Properties with preserved persisted source |
| Part/Assembly section | Section ID | Section transaction without body calculation |

Inserted Assembly components manage mates separately. For embedded paths, edit the
whole Sweep's placement through its container ID, not the owned path root ID.
Ownership, active-body checks, and derived-body protection remain in domain transactions.

## Rows and orientation

Deleting a position row in pending GUI state leaves a gap without moving later
rows. Its lock is released. If an orientation row references the same geometry,
that paired orientation is removed too. Pairing includes owner, geometric key,
and full occurrence path.

After paired-orientation removal, remaining orientations are relabeled FRONT/TOP
under the existing GUI rule. Inspection highlight stays on the same surviving
reference. Direct FRONT/TOP removal neither shifts another orientation nor removes
the corresponding position reference.

On commit, empty position rows are filtered as before. A later CLI index therefore
addresses current persisted position references. This introduces neither permanent
empty slots nor a format change.

## Transactions and missing sources

Removal does not require resolving the deleted source geometry, allowing broken
references to be repaired. Remaining references and results must pass normal
validation; invalid proposals change neither document nor Undo history.

One actual change is one Undo/Redo step. Empty rows add no history. Open Properties
blocks concurrent console writes. GUI Cancel discards the proposal; for a curve-owned
point, child OK updates only the parent's proposal and parent OK commits the document.

All data remains in `.prtz`, `.asmz`, `.drwz`, and their ordinary native dependencies.
Formats and start templates are unchanged.

## Verification

Targeted domain tests passed **8/8 in 8.64 s**: six primitives, Part/Assembly
constructions, a body with measured geometry displacement, all Sweeps, missing-source
repair for an embedded path point, Hole/Opening variants, imports, Part profiles,
Assembly profile cuts, and sections in both document types. Checks include actual
volumes, section areas, source-Part preservation, atomic errors, Undo/Redo, and native
saving. Log: `build/reference-removal-domain-tests.log`.

Subsequent tests passed **6/6 in 169.70 s**, including actual CLI, catalog, general
GUI contracts, Windows locks, and console with Properties windows. GUI Cancel/OK,
child/parent confirmation, and Undo/Redo are verified. Equivalent GUI/CLI removal
saves identical native construction and primitive definitions.
Log: `build/reference-removal-gui-tests.log`.

Callback review added forwarding of changed inspection highlights to the View after
preview refresh and before resuming reference entry. Final full regression passed
**156/156 in 662.55 s**, without errors. Both applications and all test programs built.
Logs: `build/reference-removal-final-build.log`, `build/reference-removal-full-tests.log`.
The catalog contains **289 commands** at this stage.
