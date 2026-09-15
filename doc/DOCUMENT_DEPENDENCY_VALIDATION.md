# Shared document dependency validation

Component insertion and external Part reference creation use the same directed
document-graph traversal. A new owner -> source edge is rejected if the source
depends directly or indirectly on the owner.

Traversal includes inserted components and references in all Sketches: root
Sketches, feature-owned profiles, and sections. Open documents take precedence
over saved files. Closed sources are loaded privately from native files, including
sources beneath closed intermediate Assemblies; relative paths resolve against
the actual owner. Loaded document identity must match. An unknown source must
not be treated as independent.

Validation opens no live tabs, changes no activation, history, or calculated
data, and uses no OCCT. Already visited documents are skipped; the active
recursion stack detects cycles, and depth is limited to 256. Native-source
lookup traverses the persisted occurrence hierarchy.

This stage expands the pre-addition external-dependency check, which previously
visited only root Sketches in open Parts. Atomic commit of a new Sketch and all
Assembly dependencies remains follow-up work for context commands. The placement
solver, format, templates, and **209-command** count are unchanged.

## Verification

After fixing incomplete Helical test geometry, the model suite passed **3/3**
(0.83 s), logged in `build/document-dependency-tests.log`. It uses a valid existing
geometry fixture and a chain of closed Parts with an owned Helical-profile
reference. It covers cycles, successful acyclic traversal, an unsaved open source,
missing sources, incorrect identity, insertion cycles, and unchanged live state on rejection.

All programs built (`build/document-dependency-all-build.log`). The subsequent
**9/9** suite (37.09 s), `build/document-dependency-integration-tests.log`, covers
a standalone CLI process, Workspace, references, activation, component insertion
and removal, actual Properties GUI, and profile projections.
