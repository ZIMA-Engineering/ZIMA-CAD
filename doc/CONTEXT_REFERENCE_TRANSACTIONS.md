# Shared context-reference transactions

A Sketch in an active Part stores source-document, original-object, and exact
occurrence identities. Dependencies between immediate branches of their common
Assembly are derived summaries of those references. Both changes belong to one
Part commit; an independent Assembly Undo step could separate the summary from
the actually persisted reference.

## Preparation and publication

`commit_part_document` compares dependencies across standalone Sketches, internal
feature profiles, and section Sketches. Unchanged dependencies use ordinary Part
commit. Otherwise it validates exact context and cycle freedom, prepares required
Assembly states and document-list capacity, then commits the Part and publishes
prepared summaries without further allocation. Failed Part validation does not
publish even a newly loaded owner. Preparation solves no mates and calculates no body.

Part Undo/Redo exposes its target document before history movement. Dependencies
are checked first, so a later change in another Part can cause a cyclic Redo to
be rejected without moving history or changing the Assembly.

Removing one reference does not automatically delete the whole branch dependency.
Validation includes other open/closed native Parts in the branch and all their
owned Sketches. Open documents take precedence. A missing/replaced source is not
proof that an existing aggregate dependency is unnecessary; such an edge is retained
conservatively. It may be removed after proving the last reference was removed.
The native loader still reads the entire document; there is no new lightweight
format or external cache.

## GUI and CLI

`sketch.reference.create` accepts the exact source path in active-Part context.
The Part's own occurrence retains the existing earlier-geometry rule. Another
instance of the same source Part cannot create a self-dependency. A Part maintains
one consistent context across internal profiles as well.

GUI selection uses the same preparation. A pending owned profile keeps its reference
in a temporary draft; selection does not change the Assembly summary. Only the
owning feature's OK commits both sides. Detaching through `sketch.reference.delete`
retains the native projected curve. Only existing prtz/asmz/drwz files are persisted.

## Verification and stage boundaries

- The original command rejected an exact context source (`invalid_reference_source`).
- Four basic regressions passed in 0.80 s. Expanded coverage includes failed Part
  validation, privately prepared closed owners, repeated occurrences, cyclic Redo,
  a closed sibling's shared dependency, and retention of an unverifiable dependency
  when a file is missing.
- A deliberate additional test showed that a missing first sibling could hide a
  candidate's new dependency. New branch pairs are now prepared separately, and
  traversal continues through other available siblings. It retains only small
  reference-identity sets, not complete calculated native Parts.
- An actual CLI process creates/saves a reference and common-Assembly summary,
  then a new process detaches and saves them. It checks 257 points of the retained
  rational quarter circle against the analytical equation.
- Six GUI scenarios cover ordinary Parts and Parts in Assemblies: reference,
  projected curve with Cancel, and reference with OK in each. Draft/Cancel must
  not change live Assembly state; OK stores exact context and aggregate dependency.
- GUI regression also exposed the Assembly rollback branch omitting the active
  Sketch from the scene. It now uses the existing input mesh and Sketch-draft
  renderer, including correct body/occurrence placement. The test checks the
  profile in the scene before reference selection.

After correction, the two targeted GUI/actual-CLI tests passed in 38.82 s, followed
by **118/118 in 513.98 s**. A final test directly creates all four context-reference
kinds and compares projection with independently calculated coordinates. Another
regression corrected forwarding an explicitly selected inactive Part to
`history.can_move`; that query preserves activation, revisions, and calculated data.
After this final change, both applications rebuilt and **9/9 targeted tests passed
in 53.72 s**, including GUI, actual CLI, history, and profiles.

This stage covers shared transactions for Sketches, owned profiles, Part history
(including body removal), and section Sketches. Follow-up unification for Assembly
Undo and explicit regeneration is documented in
[ASSEMBLY_REFERENCE_SUMMARIES.md](ASSEMBLY_REFERENCE_SUMMARIES.md).
Detachment with a closed original context does not guess the Assembly path from
its name. Its derived summary is checked when the actual hierarchy becomes available
during explicit regeneration. No format change or sidecars are required.
