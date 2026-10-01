# Helical calculation profiling and reference-packet properties

Inputs: the user-provided native `Projects/02.prtz`, containing an Extrusion and
Helical Sweep, with its authored geometry and precision unchanged. Means: the
existing explicit OCCT evaluation and native persistence. Required output:
identical geometry, physical properties and reference packets with less redundant
work. The user file is read only; benchmark outputs go to `build/`.

## Findings and scope

Temporary phase timers on Windows Release/OCCT 8.0 measured about 0.15 seconds
for pipe construction plus validity checking and about 65 seconds for the
transported sweep self-intersection check. Total evaluation was about 87 seconds.
A user-started calculation was still running during that initial investigation;
these figures identify the expensive phase, not an uncontended speedup result.
The user authorized terminating their blocked GUI process before clean timing.

Enabling the existing parallel checker produced an identical native output but
no improvement in this experiment (about 69 seconds for the check). That trial
was removed. Self-intersection detection, its retry at the established tolerance,
and invalid-solid rejection remain unchanged. No reduced precision or bypassed
validation is used.

The accepted change skips aggregate volume, area, centroid and inertia integration
only when building the ordinary operand reference mesh whose aggregate result is
immediately discarded. Per-face areas, edge lengths, reference geometry, meshing
and identities are still computed. Final Body results and retained copy solids
continue to compute their complete physical properties with the existing methods
and tolerances. This is local work elimination, not a new cache or file format.
The removed integration took approximately 0.27 seconds in the diagnostic run;
this small saving does not resolve the dominant self-intersection cost.

## Reproduction

The existing Helical contract executable accepts:

```text
zima_cpp_helical_sweep_contract_tests --benchmark-native INPUT.prtz NEW_OUTPUT.prtz [helical-only]
```

It times request preparation and fresh-kernel evaluation separately, then saves
the calculated document. Output must not already exist. Optional `helical-only`
removes other history features in the in-memory document before calculation and
requires exactly one Helical feature; it does not alter the input document.
This comparison is suitable for a self-contained Helical feature without external
feature dependencies. The benchmark is a test tool, not a product command.

Temporary internal phase instrumentation was removed from product code.
No UI text changed. Localization review therefore requires the existing
five-language catalog contract, not new translation keys.

## Clean comparison

After the GUI calculation stopped, serial runs of the same document measured
88,040.3 ms before and 86,392.7 ms after, with fresh kernels and identical
settings. These single runs do not establish a statistically significant 1.9%
speedup; the directly removed integration is only about 0.27 seconds.
Request preparation took 2.02 / 1.50 ms. Loading and saving are outside the
reported evaluation interval. No other test or compilation overlapped these runs.

Both complete saved native documents have SHA-256:
`93eea1d01c56faed2d09f56d45789369921c78ab7c3223402b04a7d9b49d9348`.
Thus persisted shapes, meshes, references and physical properties matched byte
for byte on this input. This is stronger than comparing volume alone, but is not
a claim of coverage of every possible model.

The same Helical feature alone, with the preceding Extrusion removed in memory
through the document's history-removal operation, took 84,563.4 ms. The complete
optimized document took 86,392.7 ms. This single-pair comparison suggests only
about 1.8 seconds is avoided by that removal for this particular input; it does
not remove the expensive transported-sweep self-intersection check. The rest of
the evaluation time must not be attributed entirely to the Boolean operation.

Logs are retained locally under `build/helical-perf-*.log`. A compact timing
record is stored alongside this note. The source of the baseline is commit
`57b4e191`; clean baseline evaluation explicitly retained aggregate integration,
while the candidate changed only the discarded operand-properties branch.

## Verification

Windows Release GUI/CLI and affected test targets built successfully. Nine
regression entries passed in 185.38 seconds: H-Sweep GUI; 2D, 3D and Helical
model contracts; Surface and Thin profiles; Sweep and Profile command contracts;
and five-language translations. The Helical model test retains self-intersection
rejection, native round-trip and regenerated reference checks. Command contracts
exercise transactions and Undo/Redo. Log: `build/helical-perf-regression.log`.
No shared placement code, model schema or localized UI strings changed. The local
launcher still selects the current development build. No portable release was
published and Linux was not tested.
