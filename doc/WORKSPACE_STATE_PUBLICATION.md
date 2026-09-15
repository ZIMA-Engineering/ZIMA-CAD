# Opening documents without copying calculated geometry

On Windows, growth of the open-document list copied calculated boundaries
and history of already open Parts. Fixing Part history alone was insufficient:
the shared `DocumentState` also contains Assembly and Drawing states whose
potentially throwing moves caused the vector to copy all existing documents.

AssemblySession and DrawingState now use uniquely owned current and historical
states, like DocumentSession. All three types have non-throwing moves. Explicit
session or Workspace copies still create independent editable data and history.
Existing immutable B-Rep/BodySnapshot sharing remains intact. Native document
structure is unchanged.

Assembly commit, replacement, and calculated-dependency updates finish validation
and data preparation before changing live state. Rejection preserves revision,
generation, Undo/Redo, save state, and geometry. A display-only source refresh
preserves the current document, history, and save state without executing physical
relations, mate solving, or body calculations.

## Verification

`zima_cpp_workspace_publication_tests` opens 48 mixed documents and compares
addresses of calculated Part boundaries and their history. The original
implementation failed (0/1 in 0.11 s) because list growth copied geometry.
The corrected behavior is verified without a timing benchmark; the compiler
also checks that the entire `DocumentState` has a non-throwing move.

The test covers 24 Assembly and Drawing history steps, session copy/assignment,
independent explicit Workspace copies, rejected Assembly physical relations and
units, invalid Drawing identity, and dimension-number conflicts. Source volume
changes from 1000 to 2000 mm³: display refresh follows its existing contract,
while an explicit physical update rejects division by zero.

The targeted suite passed **5/5 in 0.84 s**, including native persistence,
dimension identifiers, component properties, and Drawing commands. After the
final check that display refresh preserves the current Assembly object, both
applications built and **117/117 tests passed in 501.22 s**. Logs:
`build/workspace-publication-all-build.log` and
`build/workspace-publication-full-tests.log`.
