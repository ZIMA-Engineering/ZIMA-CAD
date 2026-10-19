# Derived opening dimensions in one edit transaction

DIN 1587 candidate verification exposed an ordering defect: changing usable
thread length left the relation-driven bore at its previous depth until a
separate regeneration. Restoring a longer thread could therefore be rejected
against the old, shallower bore even though the complete new definition was
valid.

`commit_opening` now applies existing dimension Relations to its private draft
before construction resolution and geometry validation. It uses current input
physical properties in the same manner as regeneration, calculates the final
properties when Relations require them, and updates derived parameter text from
the resulting geometry before the single commit. The existing placement and
reference resolver is consumed unchanged. No native format changes are needed.

Unchanged confirmation still returns before calculation. A rejected edit does
not publish the draft, replace the live calculation cache or create an Undo
step. Undo/Redo restores the driver and its derived dimensions together.

The native opening regression uses an M10 bore driven by
`min(16, thread_length + pitch)`. It checks four successive thread edits,
immediate dependent bore values, independent cylinder-subtraction volume,
retained profile/placement identities, Undo/Redo, unchanged confirmation,
atomic rejection of an impossible runout and save/reopen. Before the fix this
test failed with an expected 13.2 mm bore still reading 14.5 mm.

The existing opening end-reference test was updated to measure deferred physical
properties on private test copies after suppression and to edit its cold fixture
through the current Default feature's side length. These changes retain the
same geometric assertions and do not force eager product calculations.

The DIN cap-nut GUI regression additionally edits usable thread length through
the actual Opening Properties dialog three times, saves before any explicit
regeneration, verifies the derived bore and profile/placement identities, and
checks Undo/Redo. Existing radius mouse edits remain covered separately.
No user-visible text was added or changed; the existing five-language UI keys
are reused. Windows verification is required; Linux execution is not claimed.
