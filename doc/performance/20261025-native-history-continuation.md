# Native history continuation and repeated edge guides

Status: implementation under verification, not yet released. Windows evidence
below is finite; Linux verification and packaged-runtime acceptance are pending.

## Cause and implementation

A loaded Part already contained its calculated boundary BReps and viewer data,
but not all runtime ancestry and standalone operands required to resume history.
The existing ancestry guard therefore correctly replayed the complete history
on the first edit after reopening. Removing that guard would have risked losing
original face identities and copy sources.

The optional continuation graph now retains those exact native inputs inside
the document's existing `CachedBodies` data. It stores original topology layers,
append-only reference deltas, needed standalone operands and hidden-edge archive
addresses. Existing boundary archives are reused rather than duplicated. Only
the final boundary owns the graph; reference deltas are linear in history size.
No required sidecar or external cache is introduced.

Loading only parses data. An explicit calculation validates the graph's shared
modeling-code hash, ordered input fingerprints and native archive bindings before
restoring a prefix. Missing, stale or malformed optional data takes the existing
full calculation path. It never restores an ephemeral live-calculation proof.
The first explicit Regenerate retains its existing fresh-calculation behavior;
an already verified unchanged live result can subsequently be reused.

The initial continuation scope excludes FORM, thread-surface and sheet-state
histories. They retain their existing evaluation paths. Original-edge ancestry
requirements still control whether a restored prefix is sufficient. These
exclusions are performance limitations, not alternate geometry algorithms.

Edge treatment guide preparation was also repeated between calculated packets.
A bounded cache now shares the numerical directions within one explicit flat
history calculation. Its keys contain complete current native edge and trimmed
face archives, including orientation/location, and bit-exact ordered sample
parameters. It does not use TShape identity alone, rounded coordinates or face
reference names as a substitute for geometric inputs. Immutable signature bytes
are interned once. The estimated storage budget is 64 MiB; misses or budget
exhaustion calculate normally. The cache publishes each caller's current face
references and never changes precision, side selection or sampling.

`ZIMA_CPP_VERIFY_EDGE_GUIDE_REUSE` recalculates every hit through the original
sampling function and compares every vector component bit for bit. Enabling
the existing classifier oracle also forces these recalculations, so guide reuse
cannot bypass the face-classification checks.

## Serial measurements on the user's saved Skeleton

Baseline: committed `fa498bbcdac4f494f885e761258feb6a4b579fcf`, native Release,
OCCT 8.0.1 and Qt 6.11.0 on the same Windows machine. Both processes read isolated
copies of the same 20-operation native file. Compilation, other verification
processes and cleanup were excluded from measured calculation runs.

Changing the seventh Extrude's profile-plane offset from 0 to 1 mm and then
2 mm exercises a real geometric change, including the downstream Loft:

| Calculation | Baseline | Changed implementation | Work |
| --- | ---: | ---: | --- |
| First edit after reopening | 59,680.777 ms | 11,644.980 ms | 20 calculated becomes 16 reused + 4 calculated |
| Repeated edit in the same process | 14,690.180 ms | 12,687.421 ms | 16 reused + 4 calculated in both |

The observed first-edit improvement is approximately 5.1 times; repeated
calculation improves by about 14%. These are kernel/workspace calculation
timings, not complete GUI response times or universal speedup promises.

The final changed model has volume `17669492.188050855 mm^3` and surface area
`7859221.724830113 mm^2` in both implementations. All 20 original-reference
geometry packets compare exactly with the established coordinate-contract
comparison. The source volume was `17670348.999618083 mm^3`, independently
confirming that this measurement changed actual geometry. Save/reopen completed
for both isolated outputs.

An earlier diagnostic changed the stored length while the feature used Up To;
that value was inactive. Its 47.2-to-12.0-second result measures invalidated
history processing only. It is not the evidence for a changed geometric result.
GUI verification must edit an enabled driving value or Up To correction.

The unedited 20-operation source grows from 87,896,981 to 92,832,893 bytes when
the continuation data is calculated and saved (approximately 5.6%). Native
readback, authored history and Sketch definitions remain unchanged in the
regeneration report. Physical integration differences against the earlier
source were within ordinary integration roundoff; the serial changed-model
comparison above has exact volume and area equality.

## Verification status

### Longer current Skeleton and reference-packet eviction

The later saved source contains 26 operations. A serial Windows GUI probe
changed the same Up To correction twice (`+5`, then `+10`), preserving draft
isolation, Undo/Redo and native value readback. Before the follow-up fix, OK
took 47,552 ms and 82,606 ms. The second edit replayed all 26 operations.

The native shape cache and bounded eight-entry reference-packet cache have
independent lifetimes. A ten-operation suffix evicted its input reference
packet while retaining the native shape. The continuation fast path had
incorrectly treated that shape alone as proof that all reuse inputs existed.
It now verifies the required reference packet and copy operands, restoring
missing data from validated continuation frames. Both cache limits remain
unchanged. Restoring an existing key also refreshes its retention order.

The same 26-operation GUI probe now takes 38,686 ms and 39,450 ms. Both edits
reuse 16 operations and calculate the ten affected successors. The repeated
edit is approximately 2.09 times faster in this measured pair. These are single
observations on the same Windows machine, not universal latency promises;
the affected Boolean/edge-treatment suffix still requires substantial work.
This later model is distinct from the 20-operation serial CLI comparison above.

A focused 12-operation regression deliberately exceeds the reference-packet
limit, then performs three successive changes. Cold and repeated edits retain
their two-operation prefix; independent volumes and complete original source
packets match fresh kernel calculations. The history deletion/recovery suite
passes with that check. Private evidence:
`history-reference-eviction-native.log`,
`history-recovery-final-gui-candidate.log` and
`history-reference-eviction-fixed-gui-candidate.log`.

### Isolated thickness edit of the last Thin feature

The 26-operation source ends with Thin `Tažení 009`
(`01a1732114437a0d8722f8a9b8155161`). A separate actual Windows GUI probe changes
its thickness from 4 to 4.2 and then 4.4 mm. It must not be confused with the
seventh Extrude's Up To correction and ten-operation suffix measured above.
This probe changes the visible `featureThickness` control, confirms OK, and
checks draft isolation, Undo/Redo, native readback and unchanged authored Sketch.
Both edits reuse 25 operations and calculate exactly one.

With stage profiling enabled, OK takes 16,644 and 17,833 ms. The single kernel
operation takes 7,452 and 8,313 ms; complete settling calculation takes 9,746 and
10,352 ms. The final whole-body result's edge packet alone takes 4,407 and
4,861 ms, whereas its triangulation takes 323 and 130 ms. Opening Properties
is about 3.2 seconds and the draft update about 1.6 seconds, measured separately.
Saving subsequently integrates the current solid's physical properties; its
cost is outside the reported OK times. These are development measurements,
not acceptable final latency targets or uninstrumented timing guarantees.

The guide reuse scope starts empty for each evaluation. This isolated edit
records 6,008 edge/face cache misses and zero hits: unchanged parts of the whole
result also have their guide data prepared again. The established resolver
projects each sampled edge point onto its face, projects two neighboring probe
points, and classifies their trimmed-face positions. Those are geometric
searches, not merely arithmetic on the thickness value.

A follow-up candidate reuses the existing p-curve resolver for native planar
faces. It requires a valid same-parameter curve, an unambiguous oriented edge
use and agreement between surface and edge sample positions; unsupported
cases retain the established resolver. FORM/STEP retain their existing
normalization. The native planar path preserves the previous component
normalization order. A diagnostic recomputes the old projected guides and
rejects any direction deviation exceeding `1e-12` or a different sample count.
The signed/rotated/nested/curved/split Skeleton source matrix, profile command
suite and cold-history/recovery suite pass with that oracle enabled. Full
Skeleton verification and serial before/after latency are still pending.

Private evidence: `thin-last-feature-stage-profile-candidate.log`,
`native-planar-guides-source-oracle.log`,
`native-planar-guides-profile-oracle.log` and
`native-planar-guides-history-oracle.log`. New commit-phase diagnostics separate
placement preflight, settled calculation, reference refresh and transaction
publication. No modeling tolerance or tessellation precision was changed.

### Rejected edge-guide eviction experiment

The operation-local guide cache can fill its 64 MiB budget before the last
history operation. An LRU eviction experiment retained exact results: its
focused budget/lifetime tests passed, and the real 26-operation source passed
41,679 bit-exact reuse oracle checks with unchanged authored history and native
readback. However, correctness and more cache hits did not prove better latency.
The serial GUI probe took 48,060 ms for the first edit and reached its 300-second
process timeout during the second edit. It did not reproduce the accepted
38,686/39,450 ms result above. This experiment was rejected and its code removed;
the established bounded admission policy is retained. The timeout does not
isolate its internal cause, so no particular stage is blamed without another
profile. Private evidence: `edge-guide-lru-unit.log`,
`history-performance/lru-oracle.log` and
`history-edge-guide-lru-gui-candidate.log`.

- The real Skeleton's fresh generation passed both direction and original
  face-classifier oracles. The bounded guide cache recorded 27,025 checked hits.
- The focused history suite passed first/repeated tail edits, fresh geometry
  and original identities, native reopen, read-only input preservation,
  missing/stale optional data and malformed metadata handling in earlier builds.
- Final relinking and rerunning of affected native suites is required after
  the remaining command and GUI corrections.
- Complete GUI driving edits, unchanged OK, Cancel, Undo/Redo, native reopening,
  template/factory acceptance and packaged-runtime verification are pending.
- No claim is made here for Linux, every feature family, or every applicable interaction variant. The placed-Sketch reference issue
  subsequently passed its focused GUI check and 384-case native matrix; those
  checks are documented separately.

Private evidence is retained under `build/loft-audit/history-performance/`:
`offset-serial-benchmark.json`, `offset-equivalence.json`,
`current-loft-guide-cache.json` and the associated native/oracle logs. User
models and private reports are not release source assets.
