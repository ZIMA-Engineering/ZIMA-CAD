# Individual Sketch Reference entry

Post-release correction after signed Windows 2026102201. Its immutable archive
and source tag are unchanged. Windows: Qt 6.11.0 / OCCT 8.0.0. Native Linux
execution remains unverified.

## Cause and change

In `Projects/2000.prtz`, the Sketch owned by `Vytažení 003` contains one stored
Face reference with four cached paths, referencing the original curved side of
`Vytažení 001`. Ordinary Reference explicitly admitted whole Face candidates,
so selecting that face projected several boundaries instead of one edge.

The user's revised rule selects individual edges/curves, vertices, axes and
datum planes. Whole-face entry belongs to the separate Face × Sketch-plane
intersection command. Existing stored Face definitions remain unchanged.
Part and Assembly now consume one candidate-kind helper, with the same restriction
at acceptance. Reference to outline retains calculated-body Edge selection.
Container placement, projection algorithms, solver equations, topology identity,
native formats and factory assets are unchanged. Interaction reads existing
viewer data; no OCCT calculation is introduced.

## Finite verification

| Area | Coverage | Result |
| --- | --- | --- |
| Candidate policy | Face excluded; individual source kinds retained; body-edge outline mode unchanged; stored Face ancestry and occurrence preserved | Passed |
| Synthetic GUI | Original start/end rim edges of Solid and Thin; actual hover/RMB/LMB; one finite reference, save/reopen and Undo/Redo | Passed, four cases |
| Localization | cs/en/de/fr/ru guidance and names after language changes; light/dark themes; catalog coverage | Passed |
| Mouse trim | Existing straight/circular cutters, repeated gestures, Cancel and source identity | Passed |
| Native manipulation | Full Sketcher contract; external boundary matrix with both directions, finite/infinite supports, exact circle/arc equations, three successive drags, R/D driving/reference and lock states, rejected edits, persistence and Undo/Redo | Passed |
| Reference commands | Individual projection, exact circular support, translated/rotated frames, immutable source endpoints, Part reopening and Save As | Passed |

Eight distinct CTest suites pass. The initial combined run passed six. The
reference-command Save As test encountered sandbox hard-link permissions and
the boundary executable had not been built. After building that target, both
remaining suites passed with filesystem access (0.58 seconds). These were
verification-environment corrections, not product changes.

Evidence: `build/reference-edge-only-build.log`,
`build/reference-edge-only-regression.log`,
`build/reference-edge-only-boundary-build.log` and
`build/reference-edge-only-final-native.log`.

The real-model GUI probe uses an isolated copy of `2000.prtz`. It preserves the
existing four-path Face definition while selecting an individual rim edge.
The right-hand source arc is chosen from the stored reference's parent identity.
Both its start and end rim selections pass actual hover/RMB/LMB, all five
languages and both themes, one-edge insertion, unchanged stored Face data,
unchanged Feature definitions, native reopening and Undo/Redo. Evidence:
`build/2000-reference-fillet-20261022/gui-right-arc-verification.log`.

Fresh nested-Assembly mouse entry is not separately certified here; both scene
paths share the tested policy and existing occurrence handling. This matrix does
not certify every dimension arrangement on every spline or ellipse. Existing
datum-plane/infinite supports remain valid. This does not implement the planned
surface-intersection command.

## Separate Fillet diagnosis

The subsequent [connected-curve correction](20261022-connected-fillet-curves.md)
addresses the cause below. This section preserves the initial diagnosis.

Native queries find 43 input edge identities. Two curved keys created by
`Vytažení 003` each describe two connected segments, with saved endpoint degrees
1, 1, 2. Workspace validation rejects every multiple-segment key before OCCT.
Both keys reject R = 0.5, 1 and 2 mm with `invalid_reference`. Four other keys
accept those three radii; commit success alone does not certify every intended
rounded shape or dimension.

No Fillet production change is included in this entry-filter correction.
The existing kernel already resolves every shape use of a requested original
edge. Further work must distinguish a connected persisted curve from disconnected
or branching ambiguity without inventing IDs or choosing by OCCT enumeration or
screen position. Approximate preview and exact calculation remain separate.

Evidence: `build/2000-reference-fillet-20261022/read-only.jsonl` and
`fillet-probe-results.json`. Original and isolated input remain byte-identical,
SHA-256 `1fe7f4a358c54f87604b8e1073f86bed4a9c99bb9c1db06b0d492527014db72f`.
