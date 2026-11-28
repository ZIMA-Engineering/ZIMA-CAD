# Sheet-from-Body source edit preparation

## Change and boundaries

Editing an upstream solid previously evaluated the old generated sheet before
refreshing its source boundary. Source associations then calculated that source
again and could split the same RIP repeatedly while preparing the replacement.

The reference-settling operation now prepares the real source prefix first when
its complete boundary input key has changed. It refreshes associations using
that prefix and finishes the ordinary material/reference calculation afterward.
A failed RIP cannot publish a source-only result. Prefix reuse remains subject
to the kernel's complete input fingerprints. Exact RIP results are shared only
within one operation, keyed by source history and transformed RIP definition.

The existing exact Common/Splitter, boundary reconciliation, branch identity,
material validation and invalid-reference handling remain active. Merely retaining
a RIP identifier or lying on its infinite support plane is insufficient: its
actual intersection and the generated topology must remain compatible. This is
not a new approximate sector test or a guarantee that every source edit is fast.
No persistent format or shared placement solver contract changes.

## Finite Windows measurements

Release-mode MSVC, native OCCT 8.0.1, on the same Windows machine. The isolated
user model has two source extrusions, two Fillets, one RIP, 25 generated sheet
children, five independent branches and an Unbend. The original was left
unchanged. Baseline source was the published 2026112803 implementation
(`de735241c1553f636c561187c2648cfa4ce86803`). Measurements are single samples;
they exclude the separate cold-equivalence oracle and are not universal bounds.

| Actual workspace transaction | Before | After |
| --- | ---: | ---: |
| Lower extrusion length +10 mm | 32.392 s | 11.752 s |
| Upper extrusion length +10 mm | 21.014 s | 8.444 s |

The final matrix also measured lower/upper length -10 mm (11.552/8.489 s),
lower/upper X placement +5 mm (14.027/11.529 s), and each source Fillet radius
+1 mm (14.109/11.843 s). All eight edits changed actual material, preserved
child and correction identities and agreed with a fresh full calculation.
The comparisons include branch solids, two-way material differences, physical
properties and reference samples, not only a nonempty result.

An actual GUI run double-clicked the upper feature's displayed dimension and
entered 40 -> 50 -> 40 mm. Confirmation including scene refresh took
8.907/8.520 s. Both images show the folded model and the corresponding dimension.
The history cursor remained before Unbend. Undo/Redo, unchanged confirmation
and Escape cancellation passed; the last two invoked no geometry calculation.

## Deterministic regression

`sheet_source_edit_contract.inc` is part of the existing required
`zima_cpp_sheet_boundary_skeleton_contract` gate. Its authored 100 x 80 x 40 mm
solid has four R5 bottom Fillets and a finite RIP through y = 8 mm. R7 retains
the intended bottom-face split; R9 removes the RIP from that trimmed skin.
An independent native face classifier checks that finite sector. Returning to
R5 repairs the conversion without replacing authored child or correction IDs.

The matrix covers repeated radius, height and translation changes, exact 2 mm
wall thickness, nonzero corrections, unchanged last-valid child and Sketch
frames after loss, native save/reopen without calculation, fresh regeneration,
Undo/Redo, Unbend with zero branch overlap, Bend Back and Explode. Stable height,
translation and rejected-sector edits have an operation-count bound to catch
recalculation of obsolete sheet branches. Wall-clock time is diagnostic only.

The standalone source matrix passed on Windows before integration into the full
mandatory branch suite. Raw local evidence is under
`build/sheet-source-edits-20261128/` (source-first matrix, source-contract and GUI
reports). Private user model data is not a repository test dependency.

The twelve mandatory native sheet-branch gates then passed together in
2355.18 seconds on the Windows Release build, including the integrated source
matrix. This is the complete test-run duration, not an interactive speed claim.
The final GUI branch gate also passed in 59.26 seconds, with its required
common-parent prerequisite passing in 24.46 seconds. Packaged-release checks
remain a separate requirement.

Localization review: no new or changed product text. New English strings are
test diagnostics. Linux execution remains unverified because no Linux host is
available; both platforms use the same implementation.
