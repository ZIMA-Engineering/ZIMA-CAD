# Accepted Helical overlap behavior and reference-side optimization

## Inputs, means and output

The user explicitly accepted overlapping turns after reviewing the disabled-check
experiment, then requested further speed improvement without reducing precision.
The representative input is unchanged `Projects/02.prtz`, evaluated by a fresh
Windows Release kernel. The required output is unchanged calculated geometry and
reference data for the accepted example, with the requested new overlap policy.

## Changes

The transported Helical path no longer runs the global
`BOPAlgo_ArgumentAnalyzer` self-intersection pass. Basic pipe validity, hollow
Boolean validity and ordinary result Boolean validity checks remain. This does
not merge overlapping turns. It also removes the old check-triggered retry at
0.001 mm; construction uses the configured tolerance. No tolerance or sampling
precision is reduced. Overlapping BRep integrals are not verified union-material
properties, and later operations may reject such overlapping geometry.

Profiling the remaining time found two reference-packet passes spending about
10 seconds each calculating inward directions along a single-face seam. Those
values were subsequently deduplicated and discarded because a treatment edge
requires exactly two distinct face identities. The new preflight detects fewer
than two distinct valid persisted identities before sampling. It skips only work
whose output would be discarded. Edges with two or more distinct identities use
the unchanged sampling, sorting, ambiguity handling and paired-side validation.
No side identity, signed zero, per-edge sampling point, surface precision or
shared container placement behavior is changed.

This is not a cache. Actual changed geometry is still recalculated on explicit
OK/regeneration, and final results retain complete geometry and physical data.
No file schema, runtime dependency, UI control or user-visible text changed.

## Initial phase measurements

Without self-intersection analysis, the original packet preparation took
21,538.4 ms overall. The Helical operand/result packet direction sampling took
10,249.0 / 10,028.5 ms. With the distinct-side preflight the complete calculation
took 1,880.4 ms and those phases took 344.6 / 318.2 ms. Other real edges continue
to compute their sides. Temporary phase timers were removed before final build.

The complete native output before and after the preflight matches byte for byte,
and matches the original check-enabled calculation. SHA-256:
`93eea1d01c56faed2d09f56d45789369921c78ab7c3223402b04a7d9b49d9348`.
This confirms unchanged persisted geometry, physical properties, side data and
references on this input, rather than only matching volume or a screenshot.

## Final uninstrumented measurements

Three serial fresh-process/fresh-kernel runs of the complete user document took
1,919.63 / 1,833.36 / 1,902.68 ms (mean 1,885.22 ms). All three complete native
outputs have the same SHA-256 recorded above. The Helical-only variant took
1,518.23 ms. Loading and saving are excluded; no other calculation or compilation
overlapped these runs. The earlier check-enabled complete evaluation was
86,392.7 ms; this example therefore improves by approximately 45 times, not a
promise about every spring or total GUI interaction time.

No user model was overwritten. The final local GUI and CLI were rebuilt with
both accepted changes; the normal repository launcher remains unchanged.

## Verification

Windows Release GUI/CLI and affected test targets built successfully. All 12
selected contracts passed in 173.86 seconds: core geometry, 2D/3D/Helical models,
Surface/Thin profiles, Sweep commands, edge-treatment commands and queries,
H-Sweep GUI, edge-treatment GUI and five-language localization. The core tests
include opposite treatment sides and oblique Boolean intersection directions;
the H-Sweep tests explicitly verify empty seam-side data and complete paired
cap-rim directions for both handednesses. The crossing fixture now verifies
accepted calculation and unchanged shape/properties/pitch on native reopen.
Existing Sweep command tests cover transactions and Undo/Redo.

Logs: `build/helical-fast-final-build.log`, `build/helical-fast-regression.log`,
and the final timing records in the adjacent text file. No localized UI strings
changed. Linux and portable packaging were not performed in this change.
