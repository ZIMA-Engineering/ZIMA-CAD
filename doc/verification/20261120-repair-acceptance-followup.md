# Thin Loft, Shell and repair acceptance

The shared repair is committed and pushed as
`39bcecb4b` (Windows build `2026112003`). Its product verification matrix below
supersedes the earlier investigation checkpoints. The immutable Windows
candidate passed its gates and its executables are installed locally; see the
[release record](../releases/2026112003.md) and
[machine-readable acceptance](20261120-windows-2003-acceptance.json).

## Committed Windows checkpoint

The Windows candidate from commit
`9099dd97586d17cfda9ce195de50664e04ffabcf`, version `2026112002`, passed
the repository packaging checks, twelve mandatory sheet-branch tests and all
1,185 packaged native factory-asset loads. The additional acceptance run passed
ten native contracts and fifteen GUI scenarios, including connected Flat/Twisted
Sheet fixtures in both directions and both material-side variants.

The GUI scenarios cover General Surface, Loft, profile point order, text,
templates, Twist centroid endpoints, Twisted Sheet dimensions, Sheet Transition,
cursor orbit, ordinary inspection/history, FORM and four attached-Flat axis
documents. The finite scope and earlier matrices remain documented in
[the reference and sheet follow-up](20261120-sketch-reference-text-followup.md).
These checks do not establish a successful Thin rectangle/circle/slot Loft.

The first additional GUI run in the package's Unicode extraction directory
found a test-helper defect: conversion through a narrow filesystem string
corrupted the localization path. A second fixture issue assumed the startup
menu was Czech after changing only the translator used by subsequently created
dialogs. The immutable checkpoint was verified using its identical original
ASCII assembly and a Czech startup fixture for that test. The five-language
dialog checks passed. This is not a claim that the original Unicode helper
passed. The helpers now use Qt Unicode paths and check the menu against its
actual startup language. The rebuilt helper GUI passes the complete Loft
scenario from a Unicode working directory with English startup (28.588 s),
five-language FORM settings (3.090 s), template editing and images (11.690 s)
and the five-language Default Origin scenario (7.920 s). The template test
requires repository test fixtures, so it runs from a separate Unicode test
directory exposing those fixtures instead of the portable runtime alone.
Localization coverage and catalog validation pass in 17.82 s. These edits
change verification helpers only, not modeling or application translations.

With the user's permission, the normal local GUI, CLI and updater were replaced
by the accepted checkpoint executables. `zima-cad.bat` remains unchanged.
The preceding three executables are retained in
`build/sheet-preview-20261120/before-release`. All 56 existing common runtime
DLLs matched; ten missing native runtime DLLs were added from the package.
User settings, projects and original model files were not replaced. After the
helper rebuild/tests, the accepted checkpoint GUI was restored under the normal
launcher; the separately verified helper executable is retained as
`build/thin-loft-repair-20261120/unicode-verified-gui.exe`. Its identity and test
results are in `helper-verification.json`. The disposable package assembly was
removed only after verifying the retained candidate archive's SHA-256.

Evidence: `build/sheet-preview-20261120/release-acceptance.json`,
`release-checks/`, `local-installation.json`, and the candidate validation report
under `.dist-output/2026112002-candidate/`. This is a local test delivery, not a
new signed/public release. At that checkpoint, the next commit and push were
pending completion of the requested repairs.

## Thin Loft investigation baseline

Investigation uses the isolated copy
`build/loft31-followup-20261120/latest31.prtz`; the original is read only.
The ordinary three-profile carrier is a valid non-self-interfering solid with
volume approximately 1,437,240 mm³. Its four lateral B-spline faces have changing
convexity along shared longitudinal edges. Independent oriented-normal checks
find a common sign change at parameter approximately 0.438681 and further
changes between 0.872314 and 0.880961.

The diagnostic algorithms must pass the actual solid validity, interference,
thickness and reference-ancestry contracts. In particular, a closed offset's
`IsDone()` and a repaired valid B-Rep are insufficient: several diagnostic
variants returned the original full volume instead of a thin wall. Those
results were rejected. Simple offsets increase joint tolerances and fail
self-interference checks. Exact surface subdivision and offset-join variants
remain investigation, not a product repair or an accepted geometry change.

The diagnostics and logs are under `build/thin-loft-repair-20261120/`. At this
investigation checkpoint, no kernel, placement or native-format change had been
accepted from these experiments.
Linux execution remains unverified. The test-helper edits introduce no
user-visible application text; localization coverage passed as recorded above.

An independent, document-free reproducer is retained as `mixed-reproducer.cpp`
in that diagnostic directory. It constructs a 180 × 70 mm rectangle at Z=0,
a radius-55 mm circle with four matching points at Z=65, and a 154 × 24 mm
slot at Z=153 using native lines and three-point arcs. Its carrier is valid
and non-self-interfering (approximately 1,380,020 mm³). All nine combinations
of 0.1 / 1 / 3 mm and First / Second / Symmetric failed the baseline native
thickening contract. `mixed-matrix.log` records the complete matrix. The failure
therefore does not depend on damaged serialization or the user's private file.

Exact B-spline subdivision, independently capped subintervals, arc joins and
general volume reconstruction were also rejected: the first intervals retained
invalid intersections, open boundaries or the unchanged carrier volume. One
short final interval passes, which does not establish a valid complete wall.
At that investigation checkpoint the defect remained open; none of those
experimental offset results was installed or committed.

## Shell reproduction and exact-offset fallback

The user explicitly requested Shell on the same mixed Loft. The baseline fails
all twelve combinations of 0.1 / 1 / 3 mm and no / first / last / both removed
end caps on both the independent fixture and the isolated private document.
The failure is `OCCT Shell inner offset failed`; the ordinary carrier remains
valid. A straight rectangular Shell control passes its independent wall-volume
equation. The corresponding straight Thin Loft control also exposes an existing
failure and is now part of the permanent regression matrix.

A separate exact-normal-offset prototype passes all twelve signed-offset cases
(±0.1, ±1 and ±3 mm on both fixtures), including native exact B-Rep validity,
self-interference checks and Boolean construction of a positive-volume wall.
The lateral supports are exact `Geom_OffsetSurface` objects. Their shared trim
curves are solved in the mean longitudinal surface parameter and fitted to a
physical residual below 1e-8 mm before the native B-Rep checks at 1e-7 mm.
Marching in fixed world planes loses the intersection branch near tangent
junctions; exact Bezier polynomial extension also avoids independently fitted
end extensions breaking those junctions. No display-polyline approximation,
relaxed document precision or unchanged full carrier is accepted.

Evidence: `parametric-assembled-matrix.json`, `shell-matrix.log` and
`shell-private-matrix.log` in the diagnostic directory. At this prototype
checkpoint, product integration still needed to preserve successful ordinary
offset paths, use one shared C++ fallback for Thin Loft and Shell, and derive
output references from native source ancestry. The accepted implementation and
its finite verification follow below. No native document-format or shared
placement change is required. The new code introduces no user-visible
application text.

The shared product fallback now retains these exact offset supports and native
face/edge/vertex ancestry. Successful ordinary OCCT offset paths remain first.
For an opened planar end, the cavity is trimmed at the original cap plane;
closed caps move inward by the wall thickness. This avoids repeatedly building
temporary opening tools. The change does not alter the shared placement solver,
the native format, ordinary preview generation or source Sketch definitions.

### Product verification

| Check | Result |
| --- | --- |
| Independent rectangle/circle/slot Thin, 0.1/1/3 mm × First/Second/Symmetric, plus straight rectangular control | 10/10 passed; 112.89 s |
| Same carrier Shell, 0.1/1/3 mm × no/first/last/both open caps, plus straight rectangular control | 13/13 passed; 47.36 s |
| Isolated private mixed Loft: nine Thin and twelve Shell combinations | 21/21 passed |
| Six Thin and eight placed/rotated Shell document variants | 14/14 passed; 245.71 s |
| Native references from those fourteen reopened documents | 148 face captures and 336 two-face edge supports passed |
| Existing Loft, Loft document, Surface Thicken, Shell identity, Shell command and five-language translation contracts | 6/6 passed; 51.01 s |

Geometry checks independently test exact B-Rep validity, self-interference,
one positive-volume solid and a wall rather than the unchanged full carrier.
The permanent matrix checks signed normal distances at nine samples on each
lateral face and both material boundaries. The straight rectangular control
also checks the independently calculated wall-volume equation. The document
matrix checks repeated thickness/side/opening edits, unchanged OK without a
calculation or transaction, Undo/Redo, native save/reopen, cold regeneration and
unchanged native reference identities. Shell's carrier is translated and
rotated about all three axes. Reopened source Sketch definitions remain equal
to the corresponding solid fixture, and dependent inspection leaves source
bindings unchanged.

The GUI matrix passes all three Thin sides, draft preview without an OCCT
calculation, OK/Cancel, unchanged OK, saved hollow geometry and Undo/Redo.
Both Shell end caps are selected by mouse events in the actual View, with
reference-table entry, native identity and zero preview-calculation checks.
The complete GUI scenario passes in 69.83 s. Its initial pointer test inspected
the scene before the camera animation completed; the corrected verifier waits
for the established camera transition before testing the visible cap. The
earlier table-setter-only run is not used as evidence of pointer interaction.

Logs and reproducible private diagnostics are retained under
`build/thin-loft-repair-20261120/`: `acceptance-matrix.log`,
`private-thin-accepted.log`, `private-shell-accepted.log`,
`dependent-references.log`, `existing-regressions.log` and `gui-mixed/`.
The permanent tests are `loft_offset_tests.cpp`,
`loft_offset_document_contract.inc` and `loft_offset_ui_verification.inc`.

The fallback is verified for the reported closed, sectioned Loft with two
planar end caps. It does not establish arbitrary guide networks, non-planar
caps, intersecting offsets, multiple Bezier patches per face or arbitrary
side-wall openings. Unsupported/invalid results retain the ordinary failure
contract. No Linux executable was run; Windows and Linux consume the same C++
implementation and unchanged OCCT 8.0.1 pin. The change reuses existing errors
translated in all five catalogs and introduces no new user-visible text.

### Committed package and final verifier follow-up

The immutable 2026112003 package passes all twelve mandatory sheet-branch
checks (178.25 s), 1,185 native factory loads, portable GUI/CLI startup,
save/reopen volume, PDF/JPEG export and working-directory-independent font
rendering. Its mixed Thin/Shell mouse scenario passes in 45.736 s, including
actual end-cap picks and all three Thin material sides. Factory native assets
are byte-identical to the committed source.

The broader packaged Loft scenario initially failed at its first ordinary
post-Sketch selection. A fresh-directory run of the identical binary passed
the complete scenario in 27.460 s. The verifier queried a face pixel while the
return-from-Sketch camera animation could still run between event flushes.
It now completes that animation before locating the pixel, following the
existing native GUI verifier convention. This changes only verification code.
The corrected separate verifier passes Czech/dark, English/light and
German/dark in 27.414, 27.281 and 28.085 s; each scenario includes the existing
five-language dialog checks. Its first launch from the bare build directory
could not find the localization catalogs and is retained as a fixture failure;
the three accepted runs use the normal repository working directory.

The original packaged failure is retained in
`package-checks/ordinary-loft/output.log`; its successful unchanged-binary
repeat is in `ordinary-loft-recheck/`. Corrected-verifier logs are in
`settled-selection2-*/`. The initial failure is not rewritten as a pass or
used to imply that the immutable package contains the later verifier fix.
The new test code has no user-visible text; the existing five-language
catalog checks pass in the accepted full scenarios.

The accepted GUI, CLI and updater were installed after these checks. Their
identities report 2026112003, the repair commit and an unmodified source build.
Their hashes match the archive; the launcher and user configuration hashes are
unchanged. This is a local candidate delivery, with no Linux or signed/public
release claim. Exact artifact identities, initial attempts and final checks are
in `20261120-windows-2003-acceptance.json`.
