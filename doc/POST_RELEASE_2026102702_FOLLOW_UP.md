# Post-release 2026102702 follow-up

Recorded on 2026-10-28 from direct user testing of the published Windows
`2026102702` build. These findings are not claimed fixed by that release.

## Immediate correction batch

1. **Skeleton appearance in an Assembly**
   - A Skeleton occurrence displayed in an Assembly does not retain the authored
     colour of its source Part.
   - The Assembly occurrence must display the current source appearance without
     requiring Assembly regeneration only to refresh presentation.
   - Verify source edits, open source documents, repeated occurrences,
     save/reopen and explicit Assembly regeneration.

2. **Flat attached directly to a Flat edge**
   - The sheet-metal Flat command does not attach correctly to an edge of an
     existing Flat. The reported attempt produces an incorrectly rotated Sketch
     instead of the same endpoint/rectangle workflow used at a Bend edge.
   - Support both sides of every applicable Flat perimeter edge while preserving
     the selected side and intended Sketch orientation.
   - Verify the reported `ZE0176-0102-0001.prtz` document and protect the existing
     Bend, first-Flat and Sheet-from-Body workflows from regression.

3. **Assembly Mirror content-selection boundary**
   - Assembly Mirror must accept only features and geometry authored by the
     Assembly itself as mirrored content.
   - It must not mirror Part or subassembly occurrences, and edges or faces must
     not become mirrored content. A whole mirrored Part is authored in the Part
     workflow instead.
   - Keep the mirror plane/reference entry separate from content selection.
   - Verify View and Tree selection, invalid-selection rejection without a
     transaction, Undo/Redo and native save/reopen.

4. **View context menu for a Skeleton activated inside an Assembly**
   - Activating a Skeleton occurrence inside the Assembly currently leaves the
     View context menu unusable.
   - The complete top-level Assembly must remain visible as passive context while
     View picking, hover and the context menu follow the active Skeleton
     occurrence and expose its Part commands.
   - Verify nested activation, inactive occurrence protection and returning the
     editing context to the owning Assembly.

5. **Blend/Transition command placement and default plane**
   - Identify the exact affected Blend/Transition command. On command opening,
     its existing shared container-placement entry is not active and its default
     Sketch plane is not XY.
   - Consume the existing shared placement contract used by Feature; do not
     specialize or change that protected common contract for this repair.
   - New creation must start with active placement entry and the XY default.
     Verify later Properties editing, Origin/orientation references, Cancel,
     Undo/Redo and native save/reopen.

## Continuation after the correction batch

Resume measured performance work on the representative Skeleton without hiding
the unresolved late-history calculation cost. Profile command opening,
incremental reference entry, changed and unchanged confirmation, Insert Here,
save, open and close separately. Preserve geometry precision, reference and side
identity, calculated output, persistence and Undo/Redo.
