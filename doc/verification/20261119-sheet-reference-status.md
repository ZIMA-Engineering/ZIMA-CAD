# Sheet attachment reference status and stable regeneration

## Reproduction and scope

The saved user model `17.prtz` displayed Bend 05 and Flat 06 in red although
their calculated geometry and persisted placement validity were present.
The inspected copy has SHA-256
`fd3974b8d43262e1fc1cd72bd455845046b1a5dc2f14f317bb6f77c2e408cfa3`.
It contains 18 calculated history boundaries, including later attached features.
All investigations use isolated copies; the source document is preserved.

Two separate defects were reproduced:

- Tree validation searched the final authored reference packet. A cut edge
  consumed by a later attachment can exist only at that attachment's exact
  input boundary. A generated edge midpoint has the same boundary requirement.
- Explicit regeneration with the saved reference baseline rounded Bend 05's
  derived Y rotation from approximately -1.54e-12 degrees to zero. This changed
  the downstream fingerprint, preventing reuse of the unchanged exact input
  packet and breaking Flat 06 and Bend 06.

## Implementation boundaries

Tree validation for attached Flat, Bend and Sheet Profile now reads the same
requested input identities and Body-local boundary packets as reference
resolution. It performs no OCCT calculation. An absent requested identity does
not fall back to stale final geometry. Other native reference checks retain
their existing behavior.

History resolution retains the last valid derived frame within floating-point
roundoff only when the evaluator proves that the complete placement input,
source boundary and consumer definition match their saved fingerprints.
The proof is transient and is not serialized. Actual input/parameter changes
receive no such proof. Authored offsets, correction angles, reference identities,
side flags and signed zeros remain unchanged. The shared placement solver itself
is unchanged. This implements the previously authorized narrow stabilization.

Helical Curve uses the exact Helical Sweep SVG path with the shared neutral
foreground color. It appears light in the dark theme and follows the existing
readable foreground in the light theme. Both its command and Tree entry use
the new icon. There is no new user-visible text or native format change.

## Verification

The isolated saved model passed three cold calculations using its saved native
reference baseline, with native save/reopen between calculations. All 18 history
entries had valid placement and no Tree reference issue or calculation error.
The physical volume remained exactly `224947.72013905013` in each cycle. The
complete serialized document definition was identical before and after the
three cycles. The original and input copy still have the hash recorded above.

The Flat, Bend, placement-command, reference-assignment, reference-removal and
construction-Tree native suites passed. Flat's cut-edge matrix includes both
fragments, all three anchor choices, all three attached feature types, and
default/transformed Body frames. It additionally checks that a missing exact
input remains invalid even if a stale final packet contains that identity.

The icon renderer passed Light/Dark/Light checks for matching Helical Curve and
Sweep alpha masks and their neutral/azure colors. Translation coverage, catalog
keys and placeholders, and translated dialogs passed for cs/en/de/fr/ru. The
icon-only change introduces no new localized text.

An exploratory one-ULP point movement did not produce bit-exact coordinates
through the existing solver path, where frame retention is disabled. The focused
regression instead checks that an unproven input receives no frame retention,
and checks an independent 1e-6 source movement. One-ULP coordinate edit fidelity
is not claimed, and the solver's tolerances have not been changed.

The model-calculation suite passed after that test correction. Its new frame
matrix covers eight combinations of Body/no Body, both orientation sides and
positive/negative zero offsets; four repeated resolutions and native reopenings,
the absence of reuse without proof, source motion, correction edits and reference
loss. All seven selected native suites and the two icon/localization suites pass.

The hidden Qt GUI cut-edge matrix passed all 12 cases: two surviving fragments,
Flat/Bend/Profile, endpoint/midpoint. It checks the actual Properties Sketch
entry, edits, Undo/Redo, Cancel, Tree status, native persistence and cold
regeneration. No desktop control or screen capture was used.

The saved-model hidden GUI check passed: all 13 attached Tree rows are valid on
open, after each of five editable attachment Properties/Cancel cycles, after
explicit Regenerate, and after save/reopen. The eight generated children of
Sheet from Body remain controlled by that source operation; their independent
Properties are intentionally read-only and were excluded from the editing loop,
but included in every Tree-state check. All final history rows were checked on
reopen. The local GUI and CLI build completed as 2026111902, and the normal
repository `zima-cad.bat` entry point remains unchanged.

Native Linux execution remains a separate host gate; this change uses shared
C++ code. These results do not establish regeneration after removing the
document's persisted reference ancestry.
