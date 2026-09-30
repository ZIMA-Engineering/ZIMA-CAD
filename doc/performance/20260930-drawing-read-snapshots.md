# A10 — current model snapshots for Drawing projection

Drawing projection previously copied each live Part/Assembly session, including
all Undo/Redo states, into its private source workspace. It now requests an
explicit current-state copy. The copy preserves the complete current document,
calculated boundaries, generation, revision and dirty-state bookkeeping, while
omitting old edit states. Part source geometry/generation, source path, runtime
identity and library/import metadata remain intact. Drawing sessions are still
excluded. Source edits and exact native-byte invalidation are unchanged.

Ordinary session copy construction and assignment still preserve full history.
The subsequent [A7 follow-up](20260930-history-sharing.md) shares inactive states
while retaining isolation, instead of deep-copying every history record.
Atomic relation regeneration and Drawing-driven edits still stage their full
Workspace and publish only successful transactions. The new method is confined
to private read/calculation snapshots; it must never replace a live edit session.
Thus this addresses the read-only portion of A7/A10, not general transaction
copy-on-write or dependency-specific cache invalidation.

## Verification and measurement

The Drawing view command contract exercises snapshots with both Undo and Redo,
checks exact serialized current documents and retained calculated B-Rep, compares
revision/generation/dirty state, and mutates private copies to prove live state
is isolated. Existing projection tests verify exact/deferred output equivalence,
unsaved source changes, nested dependencies, same-timestamp file changes,
signed-zero geometry, source availability, native persistence and annotation
editing. Full session transaction and Workspace publication checks remain gates.

Windows/MSVC Release, five samples of twenty copies each, forty previous edit
states. The Part fixture contains a calculated 20 x 10 x 6 mm body; the Assembly
contains 64 occurrences of it. Timings compare the existing full session copy and
the new read snapshot in the same executable. No timing threshold is asserted.

| Fixture | Full history copy (ms) | Current snapshot (ms) |
| --- | ---: | ---: |
| Part, twenty copies | 56.1292 | 0.49084 |
| Assembly, twenty copies | 113.126 | 1.05054 |

These are synthetic copy timings, not overall Drawing speedup percentages or
peak-memory measurements. The read copy contains one current state instead of
forty history records plus current state. The benchmark is in
`zima_cpp_drawing_view_command_tests`; [raw output](20260930-drawing-read-snapshots.txt).

No persisted format, native templates, localized UI text, numerical precision,
reference identities or protected placement behavior changes.
