# Assembly history insertion cursor

The Assembly insertion marker uses one ordered history containing immediate
components, standalone Sketch containers, Assembly cuts and construction
objects. Owned Sketches and nested Curve points remain children of their owning
history entry. Mates retain their existing component ownership.

The marker selects an authoring boundary. Moving it uses already calculated
data; it does not solve mates, replay cuts or calculate source geometry. Later
entries remain in the Tree with an inactive appearance. New entries are inserted
at the cursor and advance it. Cursor movement and insertion use ordinary
document Undo/Redo.

A transient history view restores the component inputs retained before the
first inactive cut. It hides later immediate components and local history
objects, while the document retains its complete calculated source data.
Part/Assembly source ownership and explicit dependency regeneration remain
unchanged. An active nested Assembly keeps the top-level context and owns only
its immediate entries.

Explicit Measurement consumes the displayed input boundary too. Requested face
areas and component scalar properties use that exact cached input, while a
different passive occurrence of the same Assembly source retains its complete
result. Measurement does not change the source, parent or Undo history.

Reordering an existing list moves the actual entry within the unified history;
it does not exchange IDs in fixed type-specific slots. Existing destination
scope restrictions remain. Dependency checks reject references and cut targets
placed before their sources. Reorder queries read metadata without serializing
or calculating model geometry.

## Persistence

The current Assembly format stores typed `history_order` entries and a numeric
`history_cursor` in its native `.asmz` root. The JSON version is 48 and the INI
version is 36. Required information remains in the native file. There is no
required history sidecar and no legacy-format execution path.

Both start Assembly templates are generated with the native implementation,
preserving their authored parameters, language labels, units and document IDs.
The New Document GUI check must confirm the initial Assembly editing context
and command availability after each template-format change.

## Verification status

The dedicated native cursor fixture verifies cached component visibility,
insertion in the middle, identity, Undo/Redo, unchanged-cursor behavior, strict
native identity validation and save/reopen. The existing Assembly history
command matrix covers references crossing list types, ordering queries, nested
ownership and real cut ordering. The dedicated GUI fixture sends actual drag
events and checks the Tree, cached View, command API and saved cursor.

The mixed native matrix passes Component, Point, Axis, Plane, 3D Curve and
Sketch-to-cut conversion in the middle of history. It verifies the exact cached
uncut native boundary, unchanged authoritative final data and atomic rejection
of a cut targeting a later component. The nested GUI matrix rewinds and restores
the exact active occurrence while retaining a separate passive parent component.
Native Assembly, metadata, ordering, profile/reference, Undo/Redo and persistence
contracts pass. Both regenerated Assembly templates pass the New Document GUI
context/command check; the five-language catalog/coverage contract passes.

Evidence is recorded in `build/02-history-repaired-final-regressions.log` and
`build/02-followup-final-regressions.log` (the latter retains the initially
rejected cases, subsequently repaired and rerun). Existing profile reference
fixtures now insert their new reference components before the consuming cut,
as required by the unified dependency order. Linux execution remains a
platform-specific verification gap.
