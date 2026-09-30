# Parsed Symbol definition reuse

A12 follow-up, 2026-09-30. Placement validation, glyph rendering and leader
layout repeatedly parsed the same embedded JSON and Sketch definitions.
They now share immutable parsed definitions within each thread, keyed by the
complete authored source bytes. Changed source with the same definition ID is
therefore a different entry. Evaluation still validates and consumes the current
variant, text overrides and font inputs; no evaluated Sketch, text contour or
viewer geometry is cached here. Native document contents are unchanged.

The LRU retains at most 16 definitions and 1 MiB of source text per thread.
Parsed object storage is additional; this is not a 1 MiB process-memory limit.
Larger inputs are parsed without retention. Errors are not cached. Borrowed
shared definitions remain valid after eviction.

On the Windows Release build, the existing deterministic projection-symbol
leader fixture (five samples of 100 renders) measured 93.1902 ms before and
9.82592 ms after. Its exact coordinate/reference/style hash remained
7341180701063428579. This measures repeated leader preparation, not total
application speed or cold parsing.

Validation: Symbol placement, document, integration and Drawing contracts pass.
Additional checks cover exact-source reuse, edited definitions retaining their
ID, invalid input, eviction, retained-reader lifetime and oversized input.
Localization review: no user-visible text changed.
