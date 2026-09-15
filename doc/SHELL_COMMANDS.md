# Shell commands

`shell.create/get/set` manages current native Shell. GUI Properties and CLI share
`workspace::commit_shell`, thickness, original face identities, kernel calculation,
and one Undo step.

## Inputs and example

Shell removes material **inward**. Selected faces open the shell; an empty list
creates a closed hollow body. Input must be one connected calculated body. OCCT
geometric requirements are unchanged.

```json
{"command":"shell.faces","arguments":{}}
{"command":"shell.create","arguments":{"thickness_mm":2,"faces":[{"owner":"SOURCE-ID","key":"FACE-KEY"}]}}
{"command":"shell.get","arguments":{"container":"SHELL-ID"}}
{"command":"shell.faces","arguments":{"container":"SHELL-ID"}}
{"command":"shell.set","arguments":{"container":"SHELL-ID","thickness_mm":1}}
{"command":"shell.set","arguments":{"container":"SHELL-ID","faces":[]}}
```

`shell.faces` returns unique identities of the actual input body. Without `container`,
it reads the active Body's history-cursor boundary; with it, input **before** that
Shell. Editing therefore offers faces already opened in the final result. Other
Bodies/later containers are excluded. Identity is `owner`/`key` with empty
`instance_path`; face numbering is only for browsing.

Optional filters: `owner`, `offset` (0–100000000), `limit` (1–5000, default 500),
`document`. Results contain `items`, `total`, `offset`, `limit`, `has_more`, document ID,
optional container ID, and revision. This list is meaningful only for the operation
input, not general placement references.

Boundary reads borrow persisted results without copying full triangulation. Queries
invoke no OCCT, regeneration, or history changes.

## Parameters and commit

- `thickness_mm`: JSON number 0.001–1000000 mm, default 1. Actual calculation rejects
  excessive thickness producing impossible geometry.
- `faces`: complete replacement opening list, optional at creation; omitted during
  editing preserves the list. Empty arrays close all openings while retaining the
  cavity. At most 10000 unique references.
- `name`: optional nonempty name. Mutation `document` must match active Part; editing
  requires the owning Body active.
- `container`: required stable ID for get/set; set also requires a parameter.
  `get` includes feature/Body IDs, locks, and revision.

References contain `owner`, `key`, and optional empty `instance_path`, without substitute
analytic geometry. Faces are validated against calculated input; missing, foreign,
duplicate, or later references leave no partial mutation. Calculation also rejects
ambiguous identities.

Thickness lock uses GUI key `thickness`. Identical patches change neither revision
nor cache. OK calculates/commits; Cancel discards. Existing Properties supports removing
opening faces. Shell exposes neither independent placement nor a new solver.

## Native data and verification

Shell format, document extensions, and start-template structure are unchanged.
Parameters/identities remain in `.prtz`, with no required sidecars.

Models compare hollow boxes and opening combinations with independent wall volumes.
They also cover cylinders/spheres, source changes, history boundaries, Body separation,
empty inputs, invalid references/dimensions, locks, atomic errors, Undo/Redo, saving,
and fresh calculation. Actual CLI/GUI adds creation/editing, OK/Cancel, and opening-face removal.

### Spherical-input defect

New geometry tests and actual CLI reproduced identity collisions for a closed spherical
Shell. Sphere intentionally does not persist OCCT seams/poles as ZIMA edges/points,
but Shell derived identities for them from one adjacent face, producing identical keys.

Shell calculation now recognizes these helper elements of smooth spherical surfaces
and creates no new entities for them. It preserves previous references and still
checks actual faces/edges/vertices for completeness/uniqueness. This is not a general
exception for other singularities, such as cone apexes. Other Booleans are unchanged.
Shell derived-calculation fingerprints have a new version; displaying saved documents
does not trigger regeneration.

First related run passed **10/12** (65.00 s): model tests found the sphere defect;
a process fixture incorrectly converted a Unicode path through the system code page.
Process preparation now passes UTF-8 like other CLI tests. GUI Shell creation/editing
already passed. Logs: `build/shell-command-full-build.log`,
`build/shell-command-related-tests.log`, `build/shell-sphere-probe.log`.

After repair, complete model test passed **1/1** (0.75 s):
`build/shell-command-curved-build.log`, `build/shell-command-curved-tests.log`.
It covers two hollow-sphere radii, exact volume/face identities after saving, Shell
after two vertical fillets with independent volume, and rejection of disconnected
solids. Added open-hemisphere test passed **1/1** (0.78 s):
`build/shell-command-rim-build.log`, `build/shell-command-rim-tests.log`.
Volume matches R10/R9 hemisphere difference, and both actual circular rims remain
referenceable. Production code was unchanged between these runs.

Final build of both programs/all tests passed. Full suite **98/98** succeeded
(463.81 s), without reruns: `build/shell-command-final-build.log`,
`build/shell-command-full-tests.log`. Coverage includes corrected actual CLI in an
accented directory, GUI creation/editing, geometry, body history, Assemblies, drawings,
Sketcher, translations, and native saving. Catalog: **187 commands**. Fillet/Chamfer
is next at this stage; overall coverage still has open rows.
