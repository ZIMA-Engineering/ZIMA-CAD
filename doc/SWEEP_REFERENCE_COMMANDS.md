# Original Sweep placement references through CLI

`sweep2d.reference.set`, `sweep3d.reference.set`, and `helical.reference.set` assign
original references to existing 2D, 3D, and helical Sweeps in the active Part.
`placement.reference.set` exposes the same operation with `object` instead of `container`.

```json
{"command":"sweep2d.reference.set","arguments":{"container":"SWEEP-ID","index":0,"reference":{"owner":"PART-ID:origin","key":"origin:plane:yz"},"offset_mm":3}}
{"command":"sweep3d.reference.set","arguments":{"container":"SWEEP-ID","index":0,"reference":{"owner":"PART-ID:origin","key":"origin:plane:yz"},"offset_mm":3}}
{"command":"helical.reference.set","arguments":{"container":"SWEEP-ID","index":0,"reference":{"owner":"PART-ID:origin","key":"origin:plane:yz"},"offset_mm":3}}
{"command":"placement.get","arguments":{"object":"SWEEP-ID"}}
```

## Contract

Arguments match [shared assignment](PLACEMENT_REFERENCE_COMMANDS.md): `index` 0–2
identifies position fields, 3 FRONT, and 4 TOP. `reference` contains `owner`, `key`,
and optional empty `instance_path`. `offset_mm`, `flip`, `derive_orientation`, and
guard `document` are optional. Command, argument, and error names stay English;
user-facing messages are localized.

Sweeps accept local original Part references: Part Origin, available Body Origins,
and preceding geometry. Own/later sources, invalid paths, duplicate references,
and invalid indexes are rejected. Inactive and derived Bodies use the same protections
as other features. FRONT/TOP remain separate from position fields. Assignment
preserves a locked distance.

The adapter uses existing `prepare_part_feature_reference` and `commit_sweep` in
Replace mode, as does GUI OK. It adds no placement solver, changes no path rules,
and adopts no new Sketch. Container, path, owned Sketch, and profile-station identities
are preserved. The 2D path-plane reference, helical base-Sketch offset, and individual
3D-path point references remain separate properties.

Specific command results match `.get` plus `changed`. The general entry returns
`placement.get` data including full persisted placement. Identical assignment
returns `changed:false` without calculation or Undo. Actual changes use one calculation
and transaction. An active GUI editor blocks mutation; Cancel preserves the document
and OK commits the full proposal.

Persistence remains in `.prtz`; formats and start templates are unchanged.

## Verification

Model tests measure a 3 mm shift in both extreme X coordinates of the result body
with Body at the origin. Straight Sweeps of an R = 2 mm circle along 20 mm retain
80π mm³ volume. A helix with R = 10 mm, pitch 5 mm, height 10 mm, and profile
R = 0.5 mm is checked against cross-section area times helix length. Tests cover
all three specific commands and the general entry, original identities, no-ops,
locks, invalid inputs, active editing, inactive Body, Undo/Redo, and native saving
with calculated bodies.

The actual CLI process operates on native documents, assigns a reference, performs
Undo/Redo, saves the Part, and checks position and volume. GUI opens each Sweep type's
Properties, changes offset 3 → 4 mm, and checks Cancel/OK, reads of the uncommitted
model, command rejection during editing, Undo/Redo, and saved bodies.

GUI regression exposed and fixed lost position references during 3D Sweep editing:
the adapter displayed container references but adopted Absolute mode from the local
owned path. The editor now passes all container placement references and retains
the original path definition on return. Shared solver and ContainerPlacementSection
are unchanged. A separate dialog test checks 0, 1, and 3 position references, offset
editing, confirmation/cancellation, and original local-path identities and points.

Both applications and all test targets built successfully. Targeted Windows Release
regression passed **9/9 in 228.05 s**: three geometric Sweep tests, UI contracts,
translations, actual CLI process, Sweep commands, catalog, and GUI console
(`build/sweep-reference-final-tests.log`). The dialog test additionally checks
preservation of 0, 1, and 3 position references and the local path. Before the adapter
repair it caught reference loss on GUI OK; afterward, 3 → 4 mm editing passed with
saving and Undo/Redo. The full suite was not repeated at this stage; the last full
result is 150/150 in `build/assembly-profile-reference-full-tests.log`.
