# Imported-body properties through CLI

These commands edit existing native features imported from STEP or IGES.
`import.step` and `import.iges` still load new files.

| Command | Arguments |
| --- | --- |
| `import.get` | `container`, optional `document` |
| `import.set` | `container`, optional `name`, `combine`, `placement`, `document` |
| `import.reference.set` | `container`, `index`, `reference`, optional `offset_mm`, `flip`, `derive_orientation`, `document` |

`import.get` reads any open Part without calculation. It returns document, container,
feature, and Body IDs; name; add/subtract operation; persisted source and component
path; `mesh_deflection_mm` or null; stored B-Rep byte size; original topology identity
count; placement and references; coordinate frame; reference validity; and revision.
It neither reads original STEP/IGES nor copies B-Rep into JSON.

`import.set` edits the active Part and requires at least one property. `combine`
is `add` or `subtract`; `name` uses shared native-name validation. `placement` is a
numerical patch of x/y/z, rotation_x/y/z, or reference_offset:N, matching other
command properties. Lengths are mm and angles degrees. Constrained/locked values
cannot be overwritten; invalid patches are rejected in full.

`import.reference.set` follows [other placement references](PLACEMENT_REFERENCE_COMMANDS.md):
indexes 0–2 position, 3 FRONT, 4 TOP. `reference` has `owner`, `key`, and optional empty
`instance_path`. Own/later sources, duplicates, invalid indexes, and foreign paths
are rejected. The command uses `prepare_part_feature_reference` and shared imported-
feature commit. `placement.reference.set` exposes the same operation with `object`.

Mutation results match `import.get` plus `changed`. No-ops add no Undo or calculation.
Open GUI Properties blocks commands, while committed-document queries remain available.
Editing requires the active owning Body; derived bodies are read-only.

## Shared GUI/CLI transaction

`commit_imported_feature` extracts the last separate imported-feature commit branch
of `PrimitivePropertiesDialog`. It preserves reference-geometry preparation, solving
before calculation, the original editing boundary, history calculation, external
Sketch-reference refresh, and Part commit. Shared placement solver and
ContainerPlacementSection are unchanged.

Properties cannot overwrite source B-Rep, topology identities, source path, or import
precision. Selected geometry is stored in native `.prtz`; original STEP/IGES is
unnecessary for editing or regeneration. Formats and templates are unchanged.

## Verification

Model tests use STEP and IGES versions of a 10 × 20 × 30 mm box and delete the original
file before editing. They check 6000 mm³ volume, X displacement after assigning YZ,
source identity/immutability, offset lock, no-op, atomic rejection, inactive Body,
Undo/Redo, and native saving. A separate subtraction test inserts a 200 mm cube before
the import and checks 8,000,000 − 6000 mm³ volume, history, and saved results.

The process test runs actual CLI for both STEP and IGES. GUI opens real Properties,
changes offset 3 → 4 mm, and checks Cancel/OK, mutation blocking during editing,
Undo/Redo, and native geometry. New messages are translated into Czech, English,
German, French, and Russian.

## Repair: passing placement to imported bodies

A geometry test exposed an older defect: imported containers stored translation
and rotation, but StepRequest ignored them and calculated the original shape.
The import adapter now passes resolved position/rotation to the kernel and includes
them in calculation fingerprints. The kernel applies the same transform to runtime
shape and original faces, edges, and vertices. Source identities, original B-Rep,
and archive locators remain unchanged.

This consumes existing container placement without changing reference solving,
orientation rules, or shared placement UI. Feature coordinates belong to the owning
Body frame; Body placement composes separately. Tests check actual displacement,
X/Y dimension exchange after 90° rotation, and original identities in the output mesh,
not just saved values.

Add mode still combines original shapes into a compound without fusing intersections.
Tests distinguish summed add-mode volumes from actual subtraction. These rules are unchanged.

Native validation now accepts existing `combine=subtract` for imported features;
previously it rejected that value only during saving. File schema and fields are
unchanged. Subtraction requires a preceding calculated input in the owning Body and
otherwise rejects without mutation. The saved-subtraction test recalculates using
a fresh kernel without source STEP/IGES.

Final Windows Release verification: both applications and all targets built. Full
regression passed **150/150 in 647.59 s**, including model, actual CLI, translations,
and real GUI tests. Logs: `build/import-feature-full-build.log`,
`build/import-feature-full-tests.log`. GUI verifies actual 1 mm X displacement after
3 → 4 mm offset editing and the identical saved-Part result.
