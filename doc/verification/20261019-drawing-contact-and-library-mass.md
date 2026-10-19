# Drawing contact silhouettes and engineering-library mass audit

## Reported contact artifact

The recorded presentation's Assembly contains two occurrences of one native
120 x 60 x 12 mm plate, in exact face contact, two M12 screws and two nuts.
Its isometric Drawing showed internal triangular lines along the plate contact.

`project_drawing_edges` collected facing adjacency by quantized three-dimensional
endpoints. Ordinary solid triangles from every Assembly occurrence shared one
bucket. Oppositely wound contact faces could therefore mark the diagonal of
their triangulation as both front-facing and back-facing, creating a silhouette.

Adjacency is now scoped to the exact persisted occurrence path. Different
feature owners within one occurrence still share adjacency; symbolic threads
retain their existing additional owner scope. Depth occlusion still considers
all components. This changes neither coordinates, topology identities, material
sides, native schemas nor modeling/placement behavior. The shared C++ path is
used by interactive projection and precise vector output on both platforms.

## Verification

- A new native regression uses two oppositely wound, coincident contact faces
  from repeated source Parts. It checks direct/nested occurrence paths, both
  insertion orders, interactive preparation and vector projection. It requires
  all eight real face boundaries and rejects the internal diagonal. The test
  failed before the fix and passed after it.
- Drawing contracts, thread Drawing, PDF commands, Drawing view commands,
  solid-state Drawing, Drawing GUI and all-five-language UI/catalog contracts
  passed on Windows. Initial tests in the sandbox's redirected system temporary
  folder failed on file creation/rename. Re-running those tests with a writable
  repository-local TEMP/TMP passed; no test assertions were relaxed.
- The actual demo Assembly was projected through the updated native command
  host, saved, reopened and exported as an isolated isometric Drawing. Both
  visible-only and hidden-edge views were visually checked in the product GUI
  and native PDF output. Contact triangulation lines are absent; the intended
  plate outlines and component edges remain. The ordinary demo views, their
  dimensions, axes, frame and title block were retained.
- No product UI text changed. Localization review found no new translation keys.
  Linux execution has not been performed on this Windows host. Independent
  touching Bodies within one Part are outside this occurrence-scoped repair.

Local evidence: `Projects/test/drawing-contact-20261019/`, including
`demo-mass-verification.json`, the isometric native Drawing/PDF and command logs.

## Demo mass diagnosis and correction

All three authored demo documents already contained `mass = model.mass`, and
both Parts contained a valid density of 7.85e-6 kg/mm^3. Their physical model
inputs were nonzero, while the last published parameter was the initial zero.
Explicit Regenerate published the correct relation outputs. This agrees with
the existing [Relations](../RELATIONS.md) and
[physical-properties](../PHYSICAL_PROPERTIES.md) contracts: Drawing regeneration,
opening, saving and tab changes do not evaluate source-model relations.

The performance optimization still defers exact aggregate analysis during
ordinary modeling. Explicit Part regeneration requests final physical analysis
before publishing relation parameters; Assembly regeneration completes required
source analysis and publishes the Assembly's own relation outputs. Those paths
were not changed. Missing density is not substituted with zero.

The demo source Parts were regenerated and saved, followed by the Assembly and
both Drawings. Updated values are:

| Model | Physical mass (kg) | Published parameter (kg) |
| --- | ---: | ---: |
| One plate | 0.6532332366366904 | 0.653 |
| One screw | 0.05299400693885462 | 0.053 |
| Complete six-component Assembly | 1.445756937851467 | 1.446 |

The Assembly total was independently compared with two of each Part's unrounded
physical mass, including the native nut. Volumes remained unchanged through
regeneration. Updated native Drawings/PDFs and the downloadable native project
ZIP were saved. The existing presentation MP4 remains the original recording;
its earlier zero-mass displays were not silently replaced.

## Complete engineering-tables inventory

All **910** `.prtz` models present below
`config/lib/03-ENGINEERING-TABLES` were opened and queried through the native CLI.
Every model had a mass relation, available density, nonzero physical mass and a
stored `mass` agreeing with `model.mass` within its configured decimal precision.
No factory model needed a mass correction. No native file was saved by the audit.

This is a complete saved-mass audit of that finite inventory, not a fresh
regeneration, independent BRep validation or Family/edit matrix for all 910
models. It compares the published parameter with the available persisted
physical calculation; no model required the audit's suspect-only regeneration.
The inventory includes local representative reference Parts as well as factory
files. The initial parallel trial encountered the CLI working-directory lock;
all affected entries were rechecked serially and the final result has zero
errors. Full per-file data is in
`Projects/test/engineering-library-mass-audit-20261019/summary.json`.

The mandatory authoring sequence is recorded in
[AGENTS.md](../../AGENTS.md#engineering-library-mass-publication-mandatory):
Regenerate and verify Parts, then Assemblies, then refresh Drawing/BOM output.
