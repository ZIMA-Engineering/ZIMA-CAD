# Fillet regeneration and unchanged Drawing dimension edits

## Representative input and scope

The user requested a substantial speedup of regeneration after changing the
three Sketch radii of `Tažení 02` in `50.prtz`. This follow-up uses the latest
eight-operation file, including the additional `Zaoblení 04`, rather than the
seven-operation fixture in the earlier radius-regeneration report.

The original remains untouched. The isolated input is
`build/fillet-perf-followup-20261123/50-saved.prtz`, 3,130,968 bytes, SHA-256
`62ed2ba2150232db917780563928a29b462215f18afafdd9051619379355f555`.
The baseline is the implementation at `c693de344`. Measurements use the same
Windows host and OCCT 8.0.1, without a concurrent build or another test workload.

## Calculation changes

Exact topology and final self-intersection/curve-on-surface validation now use
the kernel's parallel execution support. All intermediate and final checks,
their exactness settings, tolerances and acceptance criteria remain in place.

Independent toroidal rolling envelopes are prepared with at most four workers.
Each worker receives private copies of its route edges and support faces,
including their original orientations. Copying the complete Body is unnecessary.
Results and failures are consumed in authored route order. Material replacement,
ancestry composition and final cleanup retain their previous sequential order.
There is no cross-edit geometry cache or precision reduction.

The two test overrides `ZIMA_CPP_TEST_SERIAL_FILLET_ROUTES` and
`ZIMA_CPP_TEST_SERIAL_FILLET_VALIDATION` allow isolated serial comparisons.
They do not alter authored model data or the normal UI.

## Measured native results

| Fresh evaluation of all eight history operations | History evaluation | Complete test process |
| --- | ---: | ---: |
| Baseline | 18.631 s | 20.631 s |
| Parallel validation only | 11.935 s | 13.725 s |
| Parallel validation and envelope preparation | 9.559 s | 11.454 s |
| Repeated parallel evaluation | 9.494 s | 11.331 s |

The complete final BRep is byte-identical to the baseline, SHA-256
`e7cf9ecb6588c57cdf14d150db95be25231af3b7e20134227030fe1e2d4b7e40`.
The final volume is 360409.929356543 mm³. Comparison of persisted calculated
packets across all history boundaries found only the expected source-build
fingerprint change; geometry, presentation packets and reference ancestry match.

Four successive native profile commits use radius factors 1.01, 1, 0.99 and 1:

| Radius factor | Baseline commit | Optimized commit |
| --- | ---: | ---: |
| 1.01 | 17.466 s | 9.960 s |
| 1 | 17.427 s | 9.735 s |
| 0.99 | 19.244 s | 11.079 s |
| 1 | 18.762 s | 10.745 s |

The four volumes agree between baseline and optimized runs. Each commit uses
one Body calculation. Unchanged OK creates no revision or calculation. Undo/Redo
restores the calculated snapshots without calculation. Native save/reopen and
fresh regeneration pass, with all dependent treatments valid.

The complete native planar Fillet matrix passes, including convex/concave
routes, mixed CUT/FUSE order, multi-branch nodes, curved supports, terminal
trimming and rejection checks. The added two-envelope fixture verifies exact
solid validity, source ancestry, expected combined volume, byte-identical input
preservation and repeatable output. Existing toroidal cases retain multiple
radii, both route orientations, tangent contacts, save/reopen and downstream
Hole checks.

## GUI verification

The same latest file passes the real Feature Properties → Sketch → mouse
double-click radius editor → OK workflow for all four changes. GUI OK times are
10.380 / 9.923 / 11.956 / 11.217 seconds, with exactly one Body calculation per
commit. Sketch entry, the three draft radius edits and return to Properties
calculate no Body. The test also verifies dependent treatments, unchanged OK,
Undo/Redo and close/reopen. The first Properties opening materializes its input
boundary in 389 ms; subsequent openings take 200 / 205 / 226 ms with no Body
calculation. This existing rollback behavior is separate from regeneration.

The verified candidate executable has SHA-256
`fd192739063610fe0cf7406da63ff349b79829e6a26bef68221b5f86cc1bf3b9`.
The stable repository-root `zima-cad.bat` remains unchanged. The currently loaded
Qt DLLs match the pinned SDK copies byte-for-byte; harmless applocal copy warnings
from their file locks do not represent missing or changed dependencies.

## Drawing dimension no-op behavior

The shared Drawing dimension operation compares the requested value with the
current authoritative source value before calculation, projection or commit.
A stale Drawing label cannot suppress a genuine source edit. Reference and lock
validation still runs, including occurrence-path validation. Equal values retain
the sign of zero. A closed source inspected for comparison remains closed in
the live Workspace when there is no change.

The inline editor retains the original exact value when the user confirms its
formatted display value, including equivalent numeric formatting or expressions.
This avoids replacing hidden decimal precision with the rounded label. A no-op
does not refresh the Drawing, mark a source edited, create an Undo transaction,
discard Redo or replace calculated geometry.

Native tests cover ordinary and unified Extrusion lengths, forward/reverse,
one-sided/two-sided/symmetric, with three values per configuration. GUI tests
cover untouched, retyped and equivalent-expression confirmation for ordinary
and unified lengths, Sketch width and axial diameter. They include hidden
precision, three successive real length/diameter edits, three diameter-layout
grip drags, stale labels, closed sources, unrelated broken views, Undo/Redo and
native persistence. They check actual source equations and calculated-object
identity as applicable. The existing rejection checks remain enabled.

The native Drawing annotation command suite, Show/Erase GUI suite and five-language
translation contract all pass. No user-visible strings, native file format,
placement solver, Sketch constraints or side conventions are changed.

## Verification limits

These measurements describe this user model on the Windows host; they are not
a universal speedup promise. Linux uses the same C++ implementation but has not
been built or measured in this follow-up. The broader Sketch solver/drag matrix
and nested Assembly dimension editing are not claimed as exhaustively retested.
The known positive-residual toroidal contact-trim case remains outside this
optimization; its acceptance criteria have not been relaxed.
