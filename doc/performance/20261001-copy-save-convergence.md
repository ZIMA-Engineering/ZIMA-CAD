# Profile frame convergence and derived-copy saving — 2026-10-01

## Failure and cause

The broad derived-copy GUI test exposed a save failure after a circular Pattern
was inserted into the first of two Bodies, following a translated subtractive
Extrusion. A shorter GUI fixture and a CLI `pattern.create` / `save` sequence
reproduced the failure. Mirror calculation produced the same inconsistency.

The save validator correctly rejected the document with
`Calculated history boundary does not match its parameters`. The broad test then
dereferenced a missing Pattern in the previously saved file, causing Windows
access violation `0xC0000005`. The access violation was in test code; it was not
proof of a production application crash during the copy operation.

Calculation first consumed the existing Sketch frames, then construction
resolution updated the owned Sketch frames. The convergence loop compared
history containers, constructions, Body data and attached Flat profiles, but
missed changes confined to ordinary Sketch frames. It could therefore return
geometry calculated from an old profile frame alongside the newly resolved
Sketch. The reduced case differed by a real 2 mm translation, not hash rounding
or signed-zero normalization. Both batched and single-prefix fingerprints
agreed on the current inputs.

## Correction

Inputs are the existing history, its owned profiles and calculated reference
geometry. The required output is a converged document whose saved parameters
and every calculated boundary agree. The existing bounded calculation loop now
also tracks each Sketch's resolved origin, X/Y axes and normal. A changed frame
requires another calculation pass before external-reference projection or
successful completion.

The comparison retains all floating-point bits and never rewrites values. Its
snapshot contains twelve numbers per Sketch; it does not serialize or duplicate
profile geometry. This adds a small linear check to explicit calculation only.
No performance improvement is claimed for this correctness fix. Existing
placement solving, selection, signed sides, fingerprint encoding, native file
format and strict save validation remain unchanged.

The broad GUI test now checks for a missing saved feature before dereferencing
it. A separate short GUI test covers in-Body Mirror creation, Properties Cancel,
Undo, circular Pattern creation and GUI Save/reload. Both tests use isolated
working directories under the build tree. The broad test's allowance increases
from 120 to 300 seconds because measured baseline runs already needed about
148–167 seconds to reach the old failure; no assertions were removed.

## Verification

The model calculation regression covers eight combinations: one or two Bodies,
X displacement of -2 or +2 mm, and 0 or 90 degree rotation. It checks every
boundary fingerprint, independently expected cut coordinates and volume,
native serialization/reopening, original face identities, unchanged regeneration
without a new Undo step, and Undo/Redo of a frame-only update.

Thirteen focused and dependent suites passed:

- Model calculation, including the eight new frame cases: 1.69 seconds.
- Short derived-copy Save GUI regression: 2.66 seconds.
- Complete derived-copy GUI contract: 173.16 seconds, including the formerly
  failing saved circular Pattern and the subsequent subtractive Pattern.
- Profile-frame GUI contract: 82.75 seconds.
- Five-language translation coverage/catalog validation: 4.84 seconds.
- Arc-only Bend contract, including attachment/history/persistence: 4.45 seconds.
- Seven dependent suites: history fingerprints, derived-copy geometry,
  derived-copy commands, Assembly contracts, document-session transactions,
  Body properties and profile-reference commands.

The full Bend command suite reached an unrelated obsolete fixture:
`cpp/tests/fixtures/sheet/box-cross-branch.prtz` declares INI format 43, while the
current loader requires 46. It fails at `verify_cross_branch_box()` before that
fixture can be evaluated. This change neither migrates the fixture nor adds
legacy-format support; full Bend coverage remains incomplete.

Evidence logs: `build/copy-save-model-tests.log`,
`build/copy-save-gui-fixed.log`, `build/copy-save-regressions.log`, and
`build/copy-save-final-checks.log`.

Localization review: no user-visible labels or messages change. Added text is
limited to English test diagnostics and project documentation. The existing
five-language translation coverage and catalog validation passed, including
language switching and placeholder checks.

Local GUI and CLI executables are rebuilt for the existing `zima-cad.bat`
launcher. This follow-up does not create a Windows release archive.
