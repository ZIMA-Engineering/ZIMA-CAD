# Opening the last calculated state

## Request and audit scope (2026-09-14)

The user proposes displaying the last saved calculated state on opening and reevaluating
history only through Regenerate. This is an audit and measurement proposal, not an
announcement of completed loading optimization.

Inputs are native documents with parameters, history, original references, and
calculated data. Required output is a model quickly available for View and editing.
Means are existing persisted results and shared source snapshots. Reading tree data
is not the same as executing its model operations again.

## Verified paths

- Host `open` in `cpp/modules/command_host/src/host.cpp` uses `read_native_document`
  and `insert_native_document`, without `evaluate_history` or regeneration. Workspace
  insertion adopts loaded results.
- `PartDocument::load` in `cpp/modules/document_core/src/part_document.cpp` still
  constructs `kernel_operations(false, true)` for the whole history, restores all
  persisted boundaries, and validates fingerprints. Operation preparation processes
  source profiles among other work; it does not create new OCCT bodies.
- Every boundary calls `history_fingerprint(operations, index + 1)`. Its implementation
  in `cpp/modules/kernel_api/include/zima/kernel/geometry_kernel.hpp` traverses the
  whole prefix again each time. For n operations this visits at least n(n + 1)/2
  items: 500,500 for 1000 operations. Actual cost depends on parameter sizes. This
  independently checks traversal counts, not measured duration.
- Restoring `original_references_mode=append` copies accumulated reference geometry
  and appends each boundary's increment, followed by persisted-boundary reference
  validation. This work may also grow with history length.
- `AssemblyDocument::load` restores persisted source snapshots, exact occurrences,
  placements, and owned operations. It finally builds and discards `build_scene()`
  for validation. This uses calculated data without creating new OCCT bodies.
- Session creation restores derived physical values and synchronizes dimension IDs;
  this is not full geometry regeneration.
- GUI `refresh_scene` calls `Workspace::refresh_source_geometry`. Open source Parts
  are authoritative; closed sources load from native files with cached repeated reads.
  Nested Assemblies are traversed for current sources and display preparation.
  Assembly-owned cuts and derived copies retain calculated results until explicit regeneration.

## Recommended approach

First separately measure unpacking/decoding, boundary restoration, operation and
fingerprint preparation, validation, Assembly source refresh, and View/picker preparation.
Static inspection cannot determine their shares for a particular slow file.

The target is one path: opening reads structure and last calculated state, preparing
older intermediate results as editing needs them. Body calculation, mates, and
Assembly-owned operations belong to explicit regeneration or model-change commit.
Required intermediate results and original references must remain available for
editing older features.

Preserve structural, identity, and loaded-data safety checks. Expensive checks may
be reorganized to avoid repeated traversal; corrupt files must not be accepted for
speed. Changes to fingerprint persistence or intermediate-result indexing require
corresponding current schemas and updated config start templates.

Assembly must not pin historical Part revisions against current rules: current
calculated source-Part data must appear without Assembly regeneration. Assembly
mate/cut calculation remains explicit. Stale or missing results must be identifiable;
a retained last-valid preview must not masquerade as a current calculated result.

All required data stays in `.prtz`, `.asmz`, and `.drwz`, with no required external
cache or historical-revision storage.
