# Sweep Properties scene publication — 2026-10-01

## Cause and scope

Inputs are the already calculated Part and its selected Sweep; the required
output is the existing rollback scene, editable dimensions, preview and reference
selection, followed by the unchanged full display after OK or Cancel. The change
reuses the existing synchronous scene builder without geometry regeneration.

Opening a 2D/H Sweep through the Tree published three meshes: the final body with
parameter dimensions, the rollback scene before showing Properties, then the
rollback scene with its actual preview. The first two were replaced immediately.
On the user's `Projects/02.prtz`, temporary instrumentation measured the initial
rollback refresh at about 72–86 ms, versus about 1–3 ms for preview path generation.
All temporary timing instrumentation was removed from production code.

The Tree now omits its preliminary full-body refresh only when a valid 2D/H Sweep
editor will follow (editable context, existing history entry and resolved occurrence).
That editor shows the dialog and publishes its complete preview once. Creation
also avoids the redundant initial rollback refresh. The existing changed callback
still prepares origins, dimensions, reference state and preview geometry. No
shared placement solver, selection contract, geometry cache or precision changed.
Sheet Transition and 3D Sweep retain their previous paths. Closing is unchanged.

## Measurement

Serial Windows Release GUI runs, no concurrent build/test work. The existing
native-document probe opens each Properties window six times: three unchanged
OK confirmations, then three Cancel closures. Times include pending GUI events;
frame hashing and packet comparisons are outside the measured intervals. Warm
means use trials 1–5; first-use overhead is excluded. Baseline production code is
commit `135364c6`, with temporary timing diagnostics and the expanded probe.

| Input | Warm open before | Warm open after | Warm close before | Warm close after |
| --- | ---: | ---: | ---: | ---: |
| User `02.prtz` H Sweep | 371.939 ms | 135.517 ms | 233.137 ms | 238.722 ms |
| Circular 2D Sweep | 54.523 ms | 40.298 ms | 25.965 ms | 24.734 ms |
| Rounded 3D Sweep (unchanged control) | 59.888 ms | 61.231 ms | 10.336 ms | 10.228 ms |

H Properties opening is approximately 2.7 times faster in this case, a 64% time
reduction. This is not an OCCT calculation speedup or a universal application
ratio. Mesh publication falls from three to one for 2D/H; 3D stays at three.
Closing has no demonstrated improvement and remains a separate candidate for
future profiling. Raw evidence: [GUI logs](20261001-sweep-properties-refresh.txt).

## Equivalence and verification

All eighteen corresponding preview framebuffer hashes (six per command) match
before and after. Restored-frame hashes already vary between trials in the
baseline, so they are not used as proof of pixel equivalence after closing.
The final probe instead additionally asserts exact serialized viewer-packet
restoration after every OK/Cancel, covering geometry, dimensions and original
reference packets. It also asserts unchanged camera, document state and Undo/Redo
availability, and identical repeated preview images. All three final probes pass.
The probe never overwrites the input file and does not impose timing thresholds.

Localization review: no product UI text or translation keys changed. New probe
diagnostics and documentation are English. The development launcher is unchanged;
no native format, Windows portable release or Linux changes are included.

All six selected regression gates passed in 63.86 seconds: 2D Sweep GUI,
Helical Sweep GUI (including owned Sketch context and persisted placement),
unresolved owned Sketch recovery, selection-filter GUI, Sweep commands and
five-language catalogs. The Sweep command tests include unchanged edits and
retained Redo branches. Logs: `build/properties-regression.log` and
`build/properties-final-build.log`. The local GUI build is ready through the
repository-root `zima-cad.bat`.

Follow-up: [other command Properties audit](20261001-command-properties-audit.md)
includes the measured 3D Sweep optimization and retained command-specific work.
