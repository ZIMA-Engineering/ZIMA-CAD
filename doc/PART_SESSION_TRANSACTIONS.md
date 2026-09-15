# Atomic Part commits and history

The shared Part session completes physical-relation validation, dimension-identifier
validation, and memory preparation before publishing a document change. Rejected
`commit`, `replace`, and `update_calculated_boundaries` calls must preserve revision,
generation, save state, available Undo/Redo, and calculated geometry.

## State ownership

The current state and history entries use unique ownership through `std::unique_ptr`.
Commit moves the prepared state; history-vector growth moves only pointers. This
also matters on Windows: moving a complete `PartDocument` need not be noexcept,
so a vector of values could copy older calculated boundaries when growing.

Explicitly copying a complete `DocumentSession` still creates independent document
and history data. Session moves are noexcept. Native format and history semantics
are unchanged. Imported B-Rep remains shared through the existing `shared_ptr`;
validating new physical values does not copy existing calculated boundaries.

Undo/Redo first prepares retention of allocated dimension identifiers, then moves
state ownership. Rejected edits consume no revision number and preserve existing
Redo. Regeneration changes generation and the unsaved-calculation flag, without
creating a separate editing step.

## Verification

`zima_cpp_document_session_transaction_tests` uses a 1000 mm³ Box and a change to
2000 mm³ with a relation that rejects the latter through division by zero. It
checks physical-relation, unit, and identifier errors; original geometry; save
state; counters; and continued Undo/Redo. Across 24 additional steps it compares
actual calculated-boundary addresses through history and verifies independent
explicit session copies.

The original implementation failed in 0.13 s: a rejected transaction changed
generation. After the fix, 3/3 targeted tests passed in 0.48 s, including dimension
identifiers and document persistence. After expanding the test, both applications
built and **115/115 regressions passed in 502.04 s**, including an actual CLI process,
console, GUI startup, internal profiles, exact splines, offsets, and native documents.
Logs: `build/part-transaction-all-build.log` and `build/part-transaction-full-tests.log`.
