# Drill Point Properties opening

Inputs are calculated Part geometry and persisted bottom-face references. The
output is the same rollback input, selected faces, angle and final model. The
Tree previously published a final scene immediately before the command replaced
it with its rollback scene. The existing guarded command-owned preview path now
also covers Drill Point. Its face cleanup uses the stored rollback packet; face
labels, picking filters, highlighting and the command's own refresh remain intact.
No shared placement contract, geometry calculation or persistence path changes.

## Evidence

Windows Release/Fusion, baseline `19e69db2` plus the opt-in GUI probe; synthetic
40 mm block with two blind holes (radii 5 and 3 mm), then eight additional 1.2 mm
holes. Each run opens six times: three unchanged OK and three Cancel. A warmup
establishes the existing camera framing. Means exclude the first measured opening.
These are GUI preparation times, not OCCT calculation times.

| Holes | Before ms | After ms | Reduction | Scene publications |
| --- | ---: | ---: | ---: | --- |
| 2 | 43.942 | 40.260 | 8.4% | 2 to 1 |
| 10 | 85.089 | 59.028 | 30.6% | 2 to 1 |

All twelve preview hashes match. One restored frame in the first candidate run
had a transient pixel difference; the repeat matched all twelve preview and all
twelve restored RGB hashes exactly. The repeat overlapped regression work and is
used only as functional evidence, not a timing sample. Exploratory isometric
captures showed 1–8 differing pixels around small holes already on the baseline;
fixed Top view provides the recorded comparison. No product renderer change was
made. Image settling/capture is outside the timed opening interval.

Each opening verifies exact viewer-packet and camera restoration, unchanged
document status and Undo/Redo availability. Interaction checks remove and re-pick
a bottom through the common View picker, discard pending changes, commit 120°,
Undo/Redo and save/reload. Saved volume matches the independent block-minus-hole-
minus-cone formula. The dedicated CTest regression requires one scene publication.

Six dependent contracts passed in 89.07 seconds: Drill Point commands, drill
references, edge-treatment GUI, scoped refresh, selection filters and five-language
catalogs. The final dedicated Drill opening GUI contract passed in 12.89 seconds.
No user-visible text changed; localization review is complete. The development
launcher is unchanged. This follow-up is after the immutable Windows 2026100101
release and is not included in that archive. Linux remains unverified here.

Raw comparison: [probe logs](20261001-drill-properties-opening.txt).
Generated logs: `build/drill-opening-regression.log`,
`build/drill-opening-final-test.log` and `build/drill-opening-final-build.log`.
