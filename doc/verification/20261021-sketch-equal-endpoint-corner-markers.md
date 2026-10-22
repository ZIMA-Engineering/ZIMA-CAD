# Segment endpoint equality and rounded-corner markers, 2026-10-21

## Causes and correction

During the second segment endpoint, the existing equality inference was cleared
when the common picker supplied an axis contact. Point-alignment H/V variants
also preceded equivalent direction-plus-equality variants. Preview and commit
now retain the selected equality when the accepted start and endpoint lie on
the same axis: radial length adjustment then preserves the picked support.
Compatible point-alignment variants also offer equality without changing their
H/V equation. Other picked supports retain their exact position. Incompatible
inferences remain separate candidates through the established RMB cycle.

Corner radius evaluation correctly kept source constraints persisted, but its
display copy omitted contacts at the authored corner and equal design lengths.
The viewer now restores missing source constraint annotations after transient
profile evaluation, using their existing IDs and authored points. Those
constraints do not solve or distort the shortened profile. A set of published
IDs keeps the annotation merge linear; inspection does not change the document.

## Verification matrix

| Scope | Variants and independent checks | Result |
| --- | --- | --- |
| Native corner annotation | Horizontal/vertical, both contact sides, native origin/external point contact, R/D, driving/reference, locked/unlocked | 48 supported combinations passed; 16 locked-reference combinations rejected atomically |
| Repeated native edits | Three radius edits and actual endpoint movements per supported combination, equality lengths, origin equation, exact circle endpoints, read-only external reference, serialization/reopen and solve | Passed |
| Fully fixed source | Rejected endpoint drag leaves the complete serialized Sketch unchanged; source identities and markers remain present | Passed |
| Actual mouse creation | Four axis cases and four off-axis cases, perpendicular shared-point segments, both directions and signs; exact 40 mm preview with H/V and equality | Eight cases passed |
| Alternative offers | Preserve the valid higher-priority midpoint offer when it requires another length; RMB reaches equality and stationary LMB commits that exact offer | Passed |
| Actual corner creation | Click both legs and confirm shared radius dialog; equality and original origin-contact markers remain visible | Eight cases passed |
| Actual mouse manipulation | Three successive endpoint drags per case, movement observed, origin contact retained and equality marker still visible | 24 drags passed |
| Native document | Finish Sketch, commit, save/reopen, equality/rounded topology retained, Undo/Redo restores identical serialized Sketch | Eight cases passed |
| Localization | Existing symbols and translation keys reused; production source coverage and catalog validation in cs/en/de/fr/ru | Passed |

The complete native Sketcher suite passes in 12.37 seconds, including its
existing trim/dimension/drag studies. The new GUI suite passes in 22.36 seconds.
Localization/catalog verification passes in 18.28 seconds. Times are host
observations, not portable performance guarantees.

Existing GUI regressions also pass: chained corners (3.89 seconds), four equal
corner radius edits (11.48 seconds), and the broader endpoint-priority suite
(70.58 seconds). Its former original-face-only assertion was updated to the
user-approved visible Body-face contract. One intermediate run stopped at the
axis-overlap hover assertion; the diagnostic rerun passed all eight endpoint
cases and the external-reference scenarios without a production-code change.
This intermittent hover result remains a test-stability uncertainty; the new
eight-case equality/corner suite passed independently.

Evidence: `build/sketch-equal-corner-native-tests.log`,
`build/sketch-equal-corner-gui-tests.log`,
`build/sketch-equal-corner-gui-detail.log`, and
`build/sketch-equal-corner-localization-tests.log`. Screenshots and native copies
are under `Projects/test/equal-*`.
Broader GUI evidence is in `build/sketch-equal-corner-existing-gui-tests.log`,
`build/sketch-equal-corner-endpoint-regression-tests.log` and its `-detail.log`.

No persistent schema, factory asset, shared placement contract or source model
is changed. The new automatic length offer targets native finite segments;
external finite-segment equality inference, every non-axis picked-support
combination, and every distance/angle dimension arrangement are not certified by
this finite matrix. Native R/D value changes are covered above; actual GUI
radius-value editing belongs to the existing equal-corner regression suite.
Linux execution remains unverified.
