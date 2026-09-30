# A7 — isolated sessions with shared inactive history

Ordinary Part/Assembly session copies retain the complete Undo/Redo history but
share inactive states. The current writable state is always copied. Undo/Redo
first makes a shared candidate independent, before publishing either stack;
unshared history keeps its existing geometry allocations. Committing a failed
calculation cannot publish a candidate or consume a revision.

Native file relocation detaches history before collecting writable metadata
addresses. States exposed to a deferred relocation batch are conservatively
copied by later session copies, including a copy made between preparation and
application of the batch. This also protects cancellation and rollback. Such
states remain ineligible for sharing in that session; rename is deliberately
not optimized at the expense of metadata isolation.

No Workspace publication path drops history. Drawing-driven edits and Relations
regeneration still stage a complete Workspace and publish only success. Current
read snapshots still omit history, as before. There is no persistent format,
precision, geometry-side or UI text change.

## Measurement

Windows Release, five samples of twenty copies, forty edit states. Part fixture:
20 x 10 x 6 mm calculated box. Assembly fixture: 64 occurrences of that Part.

| Twenty complete session copies | Before | After |
| --- | ---: | ---: |
| Part | 48.9163 ms | 0.59044 ms |
| Assembly | 105.098 ms | 1.04128 ms |

The benchmark includes allocation/destruction of each copy. It is not an
application-wide speedup or a peak-memory measurement. Current-state read copy
timings remained about 0.56 and 1.02 ms respectively.

Checks cover independent copied history, branch commits, current dirty flags,
revisions/generations, original geometry allocation retention in unshared
history, move/copy assignment and file relocation across current/Undo/Redo.
The Drawing-view fixture compares complete native current-state packets.
