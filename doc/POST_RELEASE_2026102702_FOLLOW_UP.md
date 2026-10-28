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

### Large automotive STEP scalability

Use the user-provided local 175 MB automotive sheet STEP as a representative
performance workload without copying, committing or redistributing that data.
The current native importer completes functionally with a 10 mm display mesh
deflection, but the first measured run took approximately ten minutes and saved
a 283 MiB Part. Inserting that Part into the experimental Assembly, saving and
reopening succeeded; the Assembly remained a 67 kB source reference instead of
duplicating the Part data. Reopening the Assembly took approximately 24 seconds.
Windows temporarily reported the application as not responding during the
blocking load, while interactive navigation was usable after loading completed.
Direct user evaluation after loading rated ordinary rotation as excellent
(grade 1) and interactive dragging through the pre-confirmation phase as good
(grade 2). Confirming with OK then blocked for approximately 30 seconds before
completing. Body Measurement on the same occurrence also became a visibly long
operation, but it eventually completed and allowed the centroid record to be
saved. A command-line check of the same native Part showed that its aggregate
properties were already cached (`body_calculated=false`); opening the Part took
about 12.5 seconds and the Body Properties command added less than one second.
The long GUI delay therefore belongs to preparing and publishing the measurement
scene rather than recomputing the exact mass integrals. Remove only redundant
scene copies/publications and verify identical values and unchanged document
revision.

Closing the loaded 283 MiB Part tab also temporarily produced a Windows
not-responding state before the tab eventually closed and the application
continued. The close path already retires the document state and principal View
geometry on worker threads, so profile remaining synchronous View indexes,
derived presentation caches and the replacement-scene publication instead of
assuming native-document destruction is still the cause. Explicit Assembly
regeneration requested after saving the centroid again produced a
not-responding state lasting minutes. The saved Body Properties origin is
intentionally a source-Part annotation and is not copied into occurrence scenes,
so its absence in the Assembly is correct and must not be treated as a display
defect. The long regeneration remains a valid independent performance defect.

Provide cooperative Escape cancellation for long work. Escape must request a
safe stop, discard only private candidate data, leave the live document and Undo
history unchanged, and never publish a partial native save. Existing atomic
temporary-file publication is the required basis for save cancellation. Do not
terminate a calculation thread or the application process; OCCT and persistence
work must observe cancellation at explicit safe checkpoints, with unsupported
monolithic calls reported as still finishing their current safe step.

Treat the complete workflow as the performance contract: import, native save,
native reopen, insertion, Assembly save/reopen and later interactive navigation.
Keep exact B-Rep geometry and source sharing. Do not trade geometry precision,
reference identity or supported data for speed. Long operations must publish
progress and keep the application responsive rather than presenting an apparent
hang. Measure time, peak memory, persisted size and source-data duplication at
each stage independently.
