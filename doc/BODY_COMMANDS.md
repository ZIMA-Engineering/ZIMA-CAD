# Bodies and Booleans in the shared command layer

`cpp/modules/workspace/body_operations` shares body/Boolean Properties commit,
body activation, and cursor movement across existing dialogs, tree, console, and CLI.
Dialogs remain internal with OK/Cancel and existing reference interaction.

## Operation contract

`prepare_body_edit` and `prepare_body_boolean_edit` create small transient history-graph
snapshots without calculated geometry. New objects receive identity before calculation.
Bodies use existing `create_origin_bound_body`: three position and two orientation
references to Part Origin. No additional placement solver is introduced.

Commit compares original/current graphs, validates edited-object identity, calculates
a working copy, and commits the session only on success. Stale proposals cannot
overwrite newer edits to other Bodies. Cancel affects only preview. Unchanged values
create no Undo/calculation. Body Properties retains its final pass for exact normalized-
parameter fingerprints; Boolean retains its reference-solving pass.

Activation clears confirmed selection as tree activation does. Activation/cursors
are pure document transactions without OCCT, adopting calculated results. Branch
cursors move only within the active Body. Save/Undo/Redo use existing sessions; parent
Assemblies are not automatically recalculated. Formats/start templates are unchanged.

## Commands

| Command | Text arguments | Purpose |
| --- | --- | --- |
| `body.list` | optional `document` | Bodies/Booleans in history order, active Body, cursor, available results |
| `body.get` | `body`, optional `document` | Persisted properties, original references, branch contents/cursor |
| `body.create` | `name`, optional `visible active document` | New Part-Origin Body, visible/active by default |
| `body.set` | `body`, optional `name visible active document` | Patch at least one name/visibility/activation property |
| `body.activate` | optional `body document` | Activate Body; omitted body ends activation |
| `body.cursor` | `index`, optional `body document` | Without body, end activation and set main cursor; with body, move active branch cursor |
| `body.boolean.get` | `boolean`, optional `document` | Operation properties and input-result IDs |
| `body.boolean.create` | `operation target tool`, optional `name document` | Calculate from two distinct available preceding results |
| `body.boolean.set` | `boolean`, optional `operation target tool name document` | Edit supplied properties and calculate |

`operation` is `add`, `subtract`, or `intersect`. `index` is a zero-based nonnegative
integer; `visible`/`active` are booleans; other arguments are strings. IDs come from
list/get/creation. Use JSON to skip optional positional arguments:

```json
{"command":"body.set","arguments":{"body":"BODY-ID","visible":false}}
{"command":"body.cursor","arguments":{"index":0,"body":"BODY-ID"}}
{"command":"body.boolean.set","arguments":{"boolean":"OPERATION-ID","operation":"intersect"}}
```

Successful mutations return `changed`; queries neither activate nor calculate models.
Query `placement` is persisted data: mm lengths, degree rotations, and references
with stable owner/semantic key. Placement, ordering, branch deletion, and derived
copies use separate commands. `body.set` accepts neither arbitrary graph JSON nor
unvalidated document serialization.

## Verification

Qt-free model tests use 10 mm and 4 mm cubes. Independent volumes: difference 936 mm³,
intersection 64 mm³, union 1000 mm³. Moving the tool 20 mm through an original plane
reference gives union 1064 mm³. Tests also cover feature ownership, stable IDs, cursors,
correct-branch insertion, invalid inputs, stale proposals, Undo/Redo, and real save/load.

Integration creates Bodies/Booleans through console, edits through actual Properties,
and checks saved volume. Actual CLI performs the same creation. Six focused regressions
passed (11.58 s, `build/body-commands-integration-tests.log`), plus model tests
(0.26 s, `build/body-command-model-tests.log`).

Full Windows Release passed **60/60 in 383.35 s**, `build/body-commands-full-tests.log`.
After matching confirmed-selection clearing on activation, **6/6** affected tests
passed again in 12.28 s, `build/body-commands-final-tests.log`. GUI/CLI built from final
source (`build/body-commands-final-build.log`).

## Assigning an original Body reference

`body.reference.set` assigns an original placement reference to one field of an existing
Part Body. At this stage, feature/construction/section references and removal were
subsequent work.

```json
{"command":"body.reference.set","arguments":{"body":"BODY-ID","index":0,"reference":{"owner":"SOURCE-ID","key":"ORIGINAL-KEY"},"offset_mm":2}}
```

- `index`: position 0–2, FRONT 3, TOP 4.
- `reference`: required strings `owner`/`key`, optional local empty `instance_path`.
  Other fields are rejected.
- `offset_mm`: plane distance, default 0. Locked position fields adopt measured current
  distance and retain lock. Nonzero offsets for point/axis/orientation fields are rejected.
- `flip`: reference-direction reversal, default false.
- `derive_orientation`: existing automatic orientation assignment from position-plane
  selection, default true.
- `document`: optional open Part ID under shared targeting rules.

Sources may be Part Origin or original geometry of earlier Bodies in graph order,
matching Body Properties. Own geometry, later Bodies, unknown sources, and foreign
paths are rejected. Derived Bodies are not directly editable. Lookup uses persisted
original data and Origin geometry; clients cannot spoof source kind.

Shared field assignment and existing `prepare_body_edit` / `commit_body_edit` match
Body Properties. Changes explicitly recalculate in one Undo transaction, preserving
active Body. Identical assignment calculates nothing/adds no history. Errors leave
no partial graph/geometry. Results match `body.get` plus `changed`; subsequent
`body.get` / `placement.get` reads do not recalculate.

Model tests verify actual 13 mm displacement with 24 mm³ volume, earlier-Body dependency,
locked-distance adoption, source changes, FRONT, atomic errors, Undo/Redo, and native
Part. The original fixture mistakenly used a global operation count for a local Body
boundary; corrected to one target-Body operation, it passed **1/1 in 0.17 s**
(`build/body-reference-model-fixed-tests.log`). After both apps built, integration
passed **10/10 in 105.00 s**, including CLI, bidirectional GUI, translations, locks,
and history. Added original top-face (Z=33 mm), point, and ineffective-offset rejection
checks passed **1/1 in 0.17 s** (`build/body-reference-topology-final-tests.log`).
The fixture also needed to retain face-query results throughout iteration and account
for centered native boxes; production face sources did not change. Final apps/tests
build and full regression passed **135/135 in 551.56 s**:
`build/body-reference-full-build.log`, `build/body-reference-full-tests.log`.

## Shared visibility and main cursor (2026-09-15)

`body.set` with only `visible` uses `set_part_body_visibility`, shared with body-menu
Hide/Show. It changes only visibility, without Properties graph preparation, body/mate
calculation, or changes to placement/results. Creation/full Properties retain existing calculation.

`body.cursor` without `body` sets main cursor and ends activation in one transaction,
even if the cursor index already matches. With `body`, it changes only the active
branch cursor. Invalid indexes fail before ending activation or changing data. Matching
index and activation creates no Undo.

Context Insert Before/After and the tree cursor marker share `set_body_history_cursor`
with CLI. Visibility/cursor-only commands return `body_calculated=false`. Undo/Redo
and saving use ordinary sessions; formats/templates are unchanged.

Model tests check geometry, fingerprints, other Body properties, original references,
one Undo, no-ops, invalid indexes, and activated-Part ownership inside Assembly. GUI
invokes real context menus/cursor callbacks and compares full saved files with equivalent CLI.

Initial model passed **1/1 in 0.32 s**. Both apps/all tests built; related regression
passed **10/10 in 231.89 s**, covering history, multibody Part, Body original references,
Assembly-cut history, host, console, and full workspace walkthrough.
Logs: `build/body-display-model-tests.log`, `build/body-display-verified-build.log`,
`build/body-display-verified-tests.log`.
