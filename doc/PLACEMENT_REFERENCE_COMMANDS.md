# Assigning original placement references through CLI

`placement.reference.set` provides a shared entry for Body, standalone construction
containers, six primitives (Box, Cylinder, Sphere, Cone, Pyramid, Wedge),
Extrusion/Revolution profiles, Hole/Opening, and all three Sweep kinds in Part.
Bodies and constructions delegate to existing operations; model features share GUI
OK transactions. Profiles commit their owned Sketch too. Details:
[PROFILE_REFERENCE_COMMANDS.md](PROFILE_REFERENCE_COMMANDS.md),
[DRILL_REFERENCE_COMMANDS.md](DRILL_REFERENCE_COMMANDS.md),
[SWEEP_REFERENCE_COMMANDS.md](SWEEP_REFERENCE_COMMANDS.md).
The command changes neither shared solver nor dialogs.

```json
{"command":"placement.reference.set","arguments":{"object":"CONTAINER-ID","index":0,"reference":{"owner":"DOCUMENT-ID:origin","key":"origin:plane:xy"},"offset_mm":7}}
{"command":"placement.get","arguments":{"object":"CONTAINER-ID"}}
{"command":"placement.set","arguments":{"object":"CONTAINER-ID","values":{"reference_offset:0":13}}}
```

`object` is an actual body/container ID. Position indexes are 0–2, FRONT 3, and TOP 4.
Reference sources have `owner`, `key`, and optional `instance_path`, which must be
empty in Part. Assembly constructions preserve exact occurrence paths. IDs come
from persisted original geometry, not result topology or triangulation order.

`offset_mm` is the signed offset of a plane position reference; other references
accept only zero. Replacing a source with locked distance preserves the actual
measured distance. `flip` carries orientation; `derive_orientation` (default true)
allows automatic FRONT/TOP assignment under shared rules. Transient measured values
are not persisted.

Repeated primitive source assignment retains that reference's existing orientation
role. Assigning the same plane again creates neither parallel FRONT/TOP nor a new
transaction. Independent orientation fields remain independent under the shared contract.

A primitive may reference Part Origin, its own or earlier Body Origin, and original
geometry preceding it in history. Own/later objects are rejected; another Body can
be a source only if it precedes the owning Body. References are transformed into
the owning Body's local frame before solving. Existing rules protect inactive and
derived Bodies.

The proposal is validated before commit. Primitive changes use one calculation and
Undo/Redo transaction; identical assignment does not recalculate. Invalid input
leaves no partial changes. Optional `document` guards the active tab; pending GUI
commands block mutation.

Results match `placement.get` plus `changed`. Formats and Part/Assembly start
templates are unchanged. Reference state and calculated data remain in native files.

At this stage, Assembly profile cuts use `extrusion/revolution.reference.set` and
sections use `section.reference.set`; the general entry does not yet include them.
Further adapters, particularly imported geometry, remain. Curve-point references
are prepared on local branch `codex/curve-reference-pending` with a stale-position
reproduction. The shared reference-solving repair and shared reference-removal
extraction await separate explicit user approvals at this historical stage.

## Original primitive-stage verification

Integration passed 11/11 in 109.47 s, covering all six primitives, locks, repeated
no-op assignment, rejected input, an original face in another Body with different
coordinate frames, and source-Body changes. Standalone CLI and GUI check Cancel/OK,
Undo/Redo, actual files, and preserved volume. The catalog has 263 commands and the
suite 144 tests at this stage.

Full Windows Release regression passed **144/144 in 574.23 s**, including all GUI,
model, process, drawing, and Sketch tests. Log: `build/primitive-reference-full-tests.log`.

## Imported bodies

`placement.reference.set` also supports existing STEP/IGES Part features through
the same operation as `import.reference.set`. Source geometry and identities remain
persisted natively and immutable; placement belongs to the container.
See [IMPORTED_FEATURE_COMMANDS.md](IMPORTED_FEATURE_COMMANDS.md).
