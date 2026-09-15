# Assembly components through shared commands

The initial component-command stage provides persisted-hierarchy queries and insertion
of an open Part or Assembly. GUI insertion uses the same operation. At this stage
the catalog has 150 commands; mate/placement editing, removal, and insertion into
nested active contexts are subsequent stages documented separately.

| Command | Arguments | Purpose |
| --- | --- | --- |
| `component.list` | `[recursive:Boolean]`, `[limit:Integer]`, `[document]` | Persisted occurrences, default limit 2000 |
| `component.get` | `instance_path`, `[document]` | One exact persisted occurrence |
| `component.dependencies` | `instance_path`, `[document]` | Dependencies blocking immediate-component removal |
| `component.open` | `instance_path`, `[document]` | Open the exact occurrence's source in its own tab |
| `component.insert` | `source`, `[name]`, `[document]` | Insert an open document into the active standalone Assembly |

`source` is an open Part/Assembly ID; `document` identifies the owning Assembly.
Initial insertion requires that Assembly active in its own tab. An active nested
Part/Assembly is rejected to prevent insertion into the displayed parent. At this
stage nested Assemblies can be edited in their own tabs; exact nested activation
is the next stage, now documented in [COMPONENT_ACTIVATION_COMMANDS.md](COMPONENT_ACTIVATION_COMMANDS.md).

```json
{"command":"component.insert","arguments":{"source":"OPEN-PART-ID","name":"Bolt 1"}}
{"command":"component.list","arguments":{"recursive":true,"limit":1000}}
```

Insertion returns new `occurrence`, exact `instance_path`, `source_document`,
`document`, `revision`, and `changed`. Use the returned path, constructed by native
`InstancePath`; it is neither a Part name nor an index. Repeated insertion creates
new occurrence IDs/paths. Part uses a shared calculated snapshot without recalculation.
Subassembly insertion retains native behavior: refresh its available dependency chain
and sections on a private proposal, without committing to the source subassembly
or target document's parents.

Without `recursive`, listing returns immediate components; recursive listing includes
nested occurrences and exact paths. `total` counts all found entries; `items` respects
a limit of 1–10000. `component.get` returns fields including `parent_path`,
`owning_document`, `source_document`, `name`, `kind`, `direct`, `visible`,
`effective_visible`, `suppressed`, `effective_suppressed`, `grounded`, `derived`,
`placement`, and child count. Placement uses mm/degrees in the immediate owner's local frame.

Immediate occurrences additionally expose persisted source path, mate references,
locks, saved volume in mm³, and area in mm². Mate `offset` uses mm, except `plane_angle`
in degrees. Nested snapshots do not contain complete original documents and cannot
return absent data.

At the initial stage, queries read the target Assembly's calculated/persisted snapshot,
without reading newer open sources, opening files, or invoking OCCT. They work after
sources close or their files disappear. Source updates originally reached existing
occurrences through explicit Assembly regeneration; current source-display sharing
is described in [ASSEMBLY_GEOMETRY_SHARING.md](ASSEMBLY_GEOMETRY_SHARING.md).
Ancestor visibility/suppression affects only its occurrence branch, not other
occurrences of the same source.

Before insertion, cycle checks traverse subassemblies and external Sketch references,
including embedded profiles/sections. Open documents take precedence over files.
Closed dependencies use persisted source paths and validated IDs; helper documents
do not open in the user Workspace. If an external source lacks a path, it must be
open before insertion so dependencies can be checked. Traversal depth is limited
to 256 documents; helper documents are released after each branch check.

Successful insertion creates one Undo step. Physical relations are validated on the
private proposal before commit. Failure must preserve revision, data generation,
and open-document list. Names are single-line, trimmed, and 1–256 bytes. Saving is
explicit through `save`.

## Verification

Model tests cover repeated occurrences and snapshot sharing, hierarchy/visibility,
Undo/Redo, native saving, queries without source files, parent isolation, cycles through
closed subassemblies/external references, and division by zero during insertion.
Workspace/import regressions passed **3/3** (1.39 s). Actual CLI saves/reopens repeated-Part
Assemblies; GUI uses original insertion menu/Properties. Integration passed **4/4**
(17.30 s), `build/component-integration-tests.log`.
Full Windows Release passed **77/77** (402.70 s), `build/component-full-tests.log`.

## Opening a source

`component.open` accepts an exact path from `component.list/get`, defaulting to the
displayed Assembly as owner. It opens only the selected source, not intermediate
Assemblies. Derived copies resolve to their original source. Results contain
`document`, `path`, redirected `source_instance_path`, and `opened` (newly loaded).
The source activates in its own tab.

Already open sources retain current unsaved state without rereading files. Closed
sources are validated by ID and document kind; a different Part at the same path is
rejected. Owner changes, closing/reopening, or context switches during reading invalidate
the result. Reading uses persisted data without source/parent regeneration. GUI Open
shares the operation; initial CLI retained the nested-edit guard described above.

Source-opening regressions passed **5/5** (27.35 s),
`build/component-source-integration-tests.log`: actual CLI and nested-Part GUI menu,
unsaved-source preservation, wrong file identity, copy redirection, and Workspace
changes during reading.

## Dependencies before removal

`component.dependencies` accepts an immediate component `instance_path` in its owning
Assembly. For nested Parts, query that owner through `document` with a local path;
parents cannot assume ownership of internal Parts. Source names/IDs are not occurrence identity.

Results contain `blocked` and three lists of stable IDs:

- `placement_components`: components whose persisted mate rows use the occurrence
  on either side.
- `dependent_components`: other components with persisted dependencies on it.
- `sketches`: owning-Assembly Sketches with external references to it.

Referencing subassembly internals counts as using that subassembly. Results deduplicate
uses; repeated source occurrences do not share dependencies. Checks use the existing
GUI deletion function. `blocked: false` means these obstacles are absent, not an
instruction to delete or a guarantee of subsequent section calculation. Existing
deletion, mate solving, and section calculation remain unchanged. Queries open no
files, invoke no OCCT, solve no mates, and change no history.

```json
{"command":"component.dependencies","arguments":{"document":"OWNING-ASSEMBLY","instance_path":"PATH-FROM-COMPONENT-LIST"}}
```

Verification: **4/4** model/process/catalog/translation tests (7.76 s),
`build/component-dependencies-tests.log`, and **2/2** GUI-console/Assembly-update tests
(18.48 s), `build/component-dependencies-gui-tests.log`. Coverage includes all three
obstacle kinds, exact repeated occurrences, duplicates, owning document, rejected
nested/missing paths, unchanged revisions, and queries without source files.
Because the user's CAD was running, GUI validation used separately linked
`build/cpp-windows-release/zima-cad-component-validation.exe` from the same current
CMake objects/libraries. The ordinary executable was not overwritten.
