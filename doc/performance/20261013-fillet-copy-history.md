# Fillet edits after a copied feature

## Scope and fixture

The Windows Release investigation uses the private user model `01.prtz`,
74,561,473 bytes, SHA-256
`7cd1be76bf7dba76f80fea880dbc0a1bb75d127efcce708da18b98f9f9e146df`.
The frozen investigation copy preserves that measured version. The user has
subsequently saved further edits to the original; timings refer to the frozen
version above. The model is not part of Git, factory data or a distribution. All agent edits and saves used
isolated copies under `build/fillet-01-investigation`.

The baseline is the published `ZIMA-CAD-2026101209` native runtime
(`fc06b8f3168f388ad4107d3d41bf513309d2ecd3`); the investigation started from
`d321d63a`. Both use the same Windows machine, Release configuration,
Qt 6.11.0 and pinned OCCT 8.0.0. Baseline and modified measurements ran serially,
without a concurrent build or regression suite. `ZIMA_CPP_PROFILE_COMMAND=1`
and `ZIMA_CPP_FORM_PROFILE=1` separated kernel phases from loading and saving.

The model includes two FORM operations, an Extrusion, an in-Body Pattern and
two Fillets. The measured command changes the final radius from 1 to 1.01 mm,
then to 1 mm and back to 1.01 mm. A fourth, identical set verifies no change
and no revision increment. The first changed command is a cold explicit
calculation; the next two use the live kernel.

## Cause and implementation

The fillet builder itself takes approximately 3–7 ms on this fixture. Most
latency is result preparation: volume/inertia integration over trimmed rational
FORM surfaces, original-reference publication and repeated preceding history.

Any requested standalone copy operand previously disabled prefix reuse. Editing
the last Fillet therefore recalculated the unchanged Pattern source, FORM
history and first Fillet too. Reuse now retains the actual standalone operand,
its complete source result and FORM material/tool instances under the existing
full source-prefix fingerprint, including upstream/external dependencies.
Through All and Up To are calculated at their original input boundary. A
complete Body is never substituted for its copied feature's operand.

Exact original-reference snapshots restore the chosen input boundary. Filtering
the final packet by owner is insufficient: a later operation can synthesize or
alter edges owned by an earlier source. Eight recent requested-copy reference
snapshots are retained, with the reused snapshot refreshed in the eviction
order. Missing snapshots, live ancestry or operands fall back to an earlier
calculation. Standalone-source entries use the existing 256-entry kernel limit.
These are disposable, in-memory caches; native format 46, model storage,
reference/side identities and factory assets are unchanged.

Fillet/Chamfer results retaining FORM geometry also use the existing FORM
parallel volume-property path. Full inertia and centroid integration retains
OCCT's `1e-12` tolerance. Independent span-aware volume contributions run in
parallel with the same reference point, integrator and tolerance; reduction
keeps the original face order. No accuracy, validation or properties are omitted.

## Measured results

Kernel calculation time excludes document loading and saving:

| Changed final Fillet | Published baseline | Modified runtime |
| --- | ---: | ---: |
| First change, cold | 26,424 ms | 23,789 ms |
| Second change, warm | 25,578 ms | 4,680 ms |
| Third change, warm | 26,774 ms | 4,572 ms |

The two warm observations improve by approximately 5.5–5.9 times. This is
fixture-specific evidence, not a promise for arbitrary fillets or hardware.
The warm trace reuses eight of ten compiled history operations and retains one
copy source. Only the last Fillet invokes the treatment builder; the preceding
Fillet and unchanged FORM/Pattern history no longer run. The additional compiled
history entry still follows its ordinary processing path.

Full result volume/inertia preparation for the final treatment takes about
1.7–1.8 seconds with parallel integration. Reference publication/cache still
takes about 1.1–1.2 seconds, and area/face/edge preparation remains required.
The first changed calculation after a cold open still reconstructs live kernel
ancestry. Unchanged OK and the repeated identical command calculate nothing.

Do not postpone required physical properties by publishing stale or incomplete
results: Body Properties, centroid datums, relations, measurements and native
history queries consume the calculated snapshots without invoking OCCT from
inspection. This change removes repeated work and overlaps independent work
inside explicit calculation; it does not introduce background incomplete bodies.

## Verification

### Deletion and regeneration follow-up

Deleting history now passes the existing calculated boundaries to the same
dependency-aware evaluator used by feature edits. Removing the final Fillet
retains its unchanged input prefix; references to genuinely removed geometry
still go through the existing deletion and dependency-repair rules.

Explicit regeneration fully recalculates loaded native snapshots at least once
in the current kernel instance. A nonserialized shared provenance token marks
only results calculated entirely in that instance or derived from previously
proven results. Native loading and results from a different kernel have no valid
token. A mixed evaluation starting from unproven native boundaries remains
unproven, so a later Regenerate cannot accidentally bless old cached geometry.
Failed results also require a fresh calculation.

After this initial verification, Regenerate uses the existing incremental path.
Relations, refreshed source links, placement and external references continue
through their ordinary input resolution and fingerprint checks; changed inputs
recalculate the affected history. The token is disposable runtime state and
changes neither native format 46 nor factory assets, saved geometry, precision
or identity. It does not add required sidecars or persisted cache data.

Radius inspection now reads the shared, Body-specific operation-input boundary
instead of counting entries in a flat document history. The owning Body's frame
places the resulting annotation. Ordinary Fillet/Chamfer single-click selection
selects the owning feature in the Tree without exposing parameter dimensions;
double-click exposes the dimensions through the shared inspection path. Neither
interaction calculates geometry or commits a model change.

The follow-up measurements use the same frozen model and run two successive
Regenerates, delete its final Fillet, then Undo/Redo/Undo and save the restored
model in an isolated file. The pre-follow-up build includes the earlier copy
optimization; these numbers measure the additional deletion/regeneration fix.

| Operation | Before follow-up | After follow-up |
| --- | ---: | ---: |
| Repeated Regenerate | 17,989 ms | 226 ms |
| Delete final Fillet | 14,372 ms | 339 ms |

The first full Regenerate after native loading remains expensive: the current
run takes 21,375 ms. The repeated command reuses all ten compiled operations,
calculating none. Deletion reuses eight of nine remaining compiled operations
and retains the required copy source; the remaining processing takes its normal
path. These are kernel/preparation timings, not complete GUI latency promises.

Ordinary picking additionally gives an actually visible Fillet/Chamfer surface
priority over a preceding Sketch within the wire picking tolerance. It uses the
already calculated display-face hit, preserves explicit datum markers and keeps
all candidates available for cycling. Face/curve reference-entry ordering and
identities remain separate; a treatment behind a foreground face gains no
priority. The focused native viewer test covers both treatment types, foreground
occlusion, explicit point selection and unchanged face/Sketch reference offers.

The user's newer saved model was independently copied for verification
(SHA-256 `3b4f07a63c0810ba47b4af4bd86a2f6b3f16eb6b9606ee6f34f37e928847fd5e`).
It contains nine compiled operations and the remaining R25 Fillet. Its first
Regenerate takes 15,596 ms, repeated Regenerate 166 ms, and deletion of that
Fillet 265 ms. The original user file is not overwritten.

The restored quick-run model and a separate fresh-kernel full calculation were
decoded and compared across 1,452,006 numeric values. All values match exactly,
including signed zero. Native identities, reference order, triangle indices,
centroid/inertia and kernel-binding references also match. Only opaque BRep
archives and their internal locator strings are excluded from this comparison.

The actual GUI verifies click and double-click on visible rounded surfaces in
both user-model variants (R1 and R25), checks Tree selection and the absence of
the radius annotation after one click, then its presence after double-click,
and confirms no revision change. The test waits for the 850 ms camera animation
to finish before projecting and clicking a surface. The runtime-provenance test
requires a full first native regeneration, zero calculated operations on repeat,
prefix reuse on deletion, correct source-change invalidation, native reopen and
Undo/Redo.

The corrected ordinary selection contract is recorded in `AGENTS.md`. Both
private R25/R1 GUI probes pass (7.3/6.6 seconds), as does the full edge-treatment
GUI matrix (64.7 seconds), including dimension grips, value changes, Properties,
Cancel and Undo/Redo. Viewer candidates and all five language catalogs pass
separately. No user-visible text changes. Dimension inspection restores the Tree
selection with scoped signal blocking, preserving free dimension hover/editing
instead of re-confirming the feature in the View.

The saved serial and optimized models were decoded and compared across all
history packets, including original-reference packets and Body inputs/outputs.
All structural fields, ordered reference identities and triangle index buffers
match. The comparison checks 1,452,006 numeric values, including decoded binary
vertex/edge-point coordinates; maximum relative difference is
`1.4765423535417921e-16` with a `1e-10` comparison threshold. Opaque OCCT archives
and their private addresses are excluded. Native geometry precision is unchanged.

`zima_cpp_derived_copy_contract_tests` compares incremental and fresh packets,
including geometry, volume, area, centroid, inertia and reference ordering. Its
deterministic count gate requires three unchanged operations reused and just the
last operation calculated, with one retained copy source, through twelve
successive final edits. It also covers native packet round-trip, cold rebuild,
changed source inputs, Through All stock changes, Up To targets, Pattern spacing
and count, Mirror conversion and a separate Body requesting a feature operand.

The affected validation set additionally covers history recovery, multibody and
extrusion targets, Body Properties, edge-treatment commands and GUI transactions,
FORM copies before/after Unbend, ordinary copy batches and factory outward-window
calculation. File Manager and datum-marker GUI checks accompany the related UI
fixes. Five-language catalog/coverage validation is required; no new user-visible
text was introduced by these changes.

The final affected set passes all 22 tests in 436 seconds; results are recorded
in `build/fillet-01-investigation/final-contracts.log`. This includes catalog
coverage/placeholder validation and affected controls in cs/en/de/fr/ru. After
reopening the optimized user-model copy, `body_properties.create` reads its
volume, mass, centroid and full inertia with `body_calculated=false`; Undo and
Redo restore the record and revisions. The enabled kernel trace remains empty
through that sequence. No save was performed by the inspection check.

The deletion/regeneration and surface-selection follow-up passes all 26 affected
checks in 367 seconds (`followup-final-contracts.log`), including the native
cache, runtime provenance, history recovery/deletion, multibody, copied operands,
viewer picking, Body Properties, five-language catalogs, File Manager workspace,
edge-treatment GUI, FORM copies before/after Unbend, factory outward window and
all eight ordinary copy batches. The two private-model surface GUI probes pass
separately. The user's original current model retains its recorded SHA-256.

## First full regeneration follow-up

The first explicit regeneration after native reopen still calculates the real
geometry in the current kernel. Persisted display caches do not acquire runtime
provenance merely because a document opens successfully.

The remaining dominant property work was adaptive integration of the rational
FORM patches. The existing parallel, span-aware GK volume integration now also
serves later operations in a FORM history, including cuts and copies. The full
centroid/inertia integration uses independent OCCT `BRepGProp_Vinert` face
calculations concurrently, with the same vertex-mean reference point, `1e-12`
tolerance, orientation and original face reduction order as the pinned OCCT
8.0.0 adaptive `VolumeProperties` implementation. Each integrator and domain is
local to its worker. This changes scheduling, not integration precision.

Repeated solid instances, mesh-only faces, shapes without vertices and small
single-worker cases retain OCCT's existing full-property path. A surface-only
history boundary has no volume solid and retains its established empty-volume
calculation. Area, topology validation, meshing, references, persistence and
Undo/Redo paths are unchanged by this follow-up. No native format change or new
user-visible text is introduced.

An experiment batching history fingerprints produced no measured improvement
on this model and was removed. The final change is limited to property
integration scheduling. Timing and validation evidence is recorded separately
under `build/fillet-01-investigation/first-full-*`.

Two serial samples per fixture and executable use new CLI processes, isolated
copies, and the same native dependencies. The calculation trace measures the
first full regeneration separately from load/save and the repeated regeneration.

| Fixture | Before, first full | After, first full | Reduction of sample mean |
| --- | ---: | ---: | ---: |
| Current nine-operation model, R25 | 18,486 / 18,868 ms | 11,902 / 11,789 ms | 36.6% |
| Frozen ten-operation model, R25 and R1 | 22,502 / 22,884 ms | 15,052 / 14,846 ms | 34.1% |

Repeated regeneration remains 177–214 ms in these paired runs and reuses every
operation with zero geometry calculations. Earlier samples above were taken at
different times and should not be substituted for this paired comparison.
The final solid-instance lookup uses a hash set instead of a quadratic scan;
it does not change the integration inputs on these one-solid fixtures.

Each paired native output was decoded and checked, including signed zero and
reference/triangle order. Both current-model samples match across 1,221,146
numeric values each; both frozen-model samples match across 1,452,006 values
each, with zero numeric or structural differences. Only the opaque OCCT archive
and private locator fields are excluded as in the preceding comparison.

The final executable is checked again on the current fixture after replacing
the shared-solid detection scan with the hash set: first regeneration takes
11,533 ms, repeated regeneration 193 ms, and Fillet deletion 322 ms. Undo/Redo,
restoration and native save succeed. The source fixture retains its SHA-256.
The final restored output again matches 1,221,146 numeric values and the native
reference structure exactly against the pre-change full calculation.

All 20 affected native checks pass in 261 seconds (`first-full-contracts.log`):
FORM geometry and copy matrices before/after Unbend; both factory FORM assets;
corner round-trip/copy and selected tight/range cases; independent Body
Properties volume/tensor checks and centroid-driven dependent placement; native
cache, history deletion/recovery, multibody and derived-copy invalidation;
relations, edge-treatment commands and five-language catalog coverage. The
boundary-surface regression also checks that a suppressed FORM in a rational
surface-only history preserves zero volume, area/centroid and exact persisted
reference geometry. No localized product text changes in this follow-up.
Both native GUI contracts also pass in 73 seconds
(`first-full-ui-contracts.log`): edge-treatment creation/editing, confirmation,
Cancel and Undo/Redo, plus history deletion and restoration. The local Windows
GUI and CLI use the verified Qt 6.11.0 runtime; the root development launcher
continues to select the newly built executable. This follow-up is a local build,
not a new published release.

Evidence logs and generated comparisons live under the ignored investigation
directory. The large-model timing is measured through the shared CLI/Part
calculation path; GUI edge-treatment transaction coverage uses synthetic fixtures.
Windows is the verified platform. The same C++ implementation serves Linux,
but a Linux build and these measurements have not been run on the Linux host.

## Calculation demand audit before implementation

The opt-in `ZIMA_CPP_CALC_PROFILE` trace separates result packets, aggregate
properties, individual face measurements, source-material retention and total
operation time. It changes diagnostics only. Two isolated cold calculations of
the current nine-operation fixture take 12.321 and 13.403 seconds. These samples
are a phase breakdown, not a paired comparison against the preceding scheduling
optimization; machine load differs between runs. The traced native output
matches the pre-change output across 1,221,146 numeric values with zero numeric
or structural differences, including signed zero and ordered references.

Mean aggregate volume/centroid/inertia wall time is 1.930 seconds and aggregate
surface-property time is 1.228 seconds. Deferring both would remove approximately
3.16 seconds of synchronous work from this cold calculation, before accounting
for required consumers. This is an estimate from measured phases, not a measured
implementation of deferred properties. Tensor and scalar-volume integrations
overlap; their individual durations must not be added. Individual face-area
measurements cost another 1.199 seconds within the face-packet phase and are
distinct from aggregate area. Sheet conversion and other geometry consumers
use those face measurements, so they cannot simply be discarded.

The fixture has no saved Body Properties records, but its relation is
`mass = model.mass`. Final volume therefore has a real consumer. A deferred
property policy must distinguish unknown, stale and current results instead of
presenting zero or an old value as current. Centroid-dependent placement, saved
Body Properties, geometry-driving relations and material validation must receive
current values when needed. Ordinary measurement and property display currently
consume calculated packets without hidden geometry calculations. Any explicit
measurement calculation must preserve that ownership distinction. Background
work would require immutable geometry inputs and rejection of results whose
source generation has changed. No such deferred/background policy is implemented
by this audit.

Source-material retention for sheet-state changes is a separate geometry cost.
`retain_sheet_sources` is enabled if the supplied operation sequence contains a
`SheetStateRequest`; the predicate itself does not filter suppressed requests.
At each newly calculated boundary, capture retains authored material changes.
Suppressed operations, state operations themselves and empty sheet regions do
not create ordinary contributions. FORM operations retain their authored cut,
flat and formed operands through a specialized path. Unchanged valid prefixes
reuse retained sources; this work is not repeated on every refresh or unchanged
regeneration. Adding the first sheet-state request to a history without retained
sources currently requires one reconstruction of that history.

For Fillet, Chamfer and Shell, capture extracts removed and added material using
Boolean differences and attributes removed material to its sheet regions using
intersections. The sheet-state operation subsequently transforms and combines
these immutable contributions. This preserves real cuts, authored FORM variants,
material ownership and topology ancestry through unfolding and refolding.

In the two traced samples, all source-material capture phases total 2.501 and
2.923 seconds. The second panel accounts for approximately 1.43/1.81 seconds,
and R25 Fillet for 0.864/0.894 seconds. The later Unbend operation takes
0.105/0.121 seconds with those sources already prepared. These are phase timings
under the existing eager strategy, not evidence that an alternative would make
the complete operation faster. Actual prefix-reference copying at R25 costs only
0.0080/0.0085 seconds; the earlier combined retention timer obscured the expensive
source-material extraction.

A possible demand-based strategy is to retain the exact required inputs cheaply
and extract contributions only when a later sheet-state operation consumes them.
If the same calculation already includes an active downstream Unbend, that work
still belongs to the synchronous calculation and merely moving it will not save
the measured 2.5–2.9 seconds. A benefit is possible when editing a formed model
whose current calculation does not require a sheet-state result. Implementing
this requires bounded input retention, cache invalidation and equivalence checks
for cuts, mixed Fillets, FORM copies, unfolding/refolding and reference identity.
This remains an audited proposal, not a completed optimization.

## Demand-based implementation and paired verification

The preceding audit describes the implementation before this follow-up. Ordinary
modeling now defers aggregate volume, centroid, inertia and total surface area for
expensive adaptive surfaces. Newly calculated cheap analytic results retain their existing immediate
calculation. Required per-face measurements, topology ancestry, side identity,
exact solid geometry and viewer/reference packets remain synchronous and unchanged.
No background calculation or reduced integration precision is introduced.

Explicit Part Regenerate completes the final required body properties. A persisted
Body Properties feature retains its required history-position calculations and
centroid-driven dependencies. Opening a new Body Properties inspector completes
only the selected cached input solids' analysis; it neither regenerates geometry
nor inserts history or an Undo transaction. Its existing Save action owns the
history transaction. Explicit Assembly Regenerate completes pending Part and
nested Assembly source properties without repositioning components or calculating
mates merely to obtain analysis. Open source documents remain authoritative.
Documents with persisted Body Properties records conservatively retain eager
analysis throughout their geometry calculation to preserve all existing dependent
placement consumers. Relation output values remain from the last explicit
regeneration; ordinary modeling does not silently evaluate Relations.

The optional native calculated cache distinguishes completed numeric analysis from
`volume={"pending":true,"solid":...}` with null aggregate area and integrals.
Consumers omit unavailable quantities instead of publishing zero as a measurement.
This does not change authored geometry or introduce required sidecars. Completing
analysis preserves source fingerprints, archives and reference packets. Changed
geometry invalidates the derived analysis. Existing complete cache packets remain
valid; this is a state distinction, not a legacy geometry execution path.
An analysis request cannot take the geometry-only graph shortcut while required
local/input/output analysis is pending. Already complete local history remains
shared instead of being copied. Every pending editable history boundary retains its exact existing solid archive
for later measurement and native reopen, including a cursor ending before a
suppressed downstream operation. This retention performs serialization of the
calculated solid, without repeating its geometric calculation. An empty compound with visible geometry is rejected rather than
reported as a measured zero-volume result.

Sheet-material source preparation now follows actual active downstream sheet-state
consumers at each history boundary. Suppressed Unbend/Refold requests do not cause
preparation. A later newly inserted sheet-state operation reconstructs missing
source contributions once; a changed boundary after the last state cannot claim
that its sources were captured. A required active Unbend still receives the full
authored material data. Exact identical material operands can bypass an empty
Boolean difference; equality includes shape location, orientation and multiplicity,
and unsupported/mixed shapes retain the existing Boolean path.

Two paired cold CLI runs on the unchanged private nine-operation `01.prtz`, with
its active Unbend and `model.mass` relation, compare eager analysis with the new
final-demand policy in the same executable:

| Policy | First full calculation | Repeated calculation |
| --- | ---: | ---: |
| Eager aggregate analysis | 9,520 / 9,502 ms | 162 / 159 ms |
| Final-demand analysis | 7,076 / 7,684 ms | 150 / 218 ms |

The paired first-calculation mean decreases by 2,131 ms (22.4%). This is a
fixture-specific measurement, not a universal speedup. Both samples match across
1,220,874 numeric values with zero differences, including ordered references and
signed zero. Sixteen intermediate cache packets have intentionally pending
aggregate analysis; their geometry and references match exactly. Opaque OCCT
archives and private locators are excluded from structural comparison as before.
The private source is not overwritten.

The final timings include archive retention at every pending history boundary.
Individual first-calculation reductions range from 19.1% to 25.7%; the spread
shows why a single run is insufficient for a general performance claim.

New regressions cover pending cache save/reopen and completion in a fresh kernel,
repeated completion, changed-geometry invalidation, and nested Assembly completion
without source revisions, placement changes or geometry changes. Sheet regressions
cover suppress/unsuppress, a material-changing operation after the last Unbend,
later Refold, fresh-calculation equivalence and native roundtrip. Final scoped
Windows and packaged acceptance are recorded in the release record. Linux uses
the same C++ implementation but has not been built or measured for this change.

## Latest user save: eleven operations

The user subsequently saved two additional Fillets. A separate isolated copy of
that eleven-operation `Projects/01.prtz` was measured with the final executable.
The source SHA-256 is
`420d80e2a6a3cf84fa17b6a37699669a66f10976622f3afa0442a30c37a42849`.
It was never overwritten by these checks. Two paired fresh-process runs measure
eager first calculation at 14.280/14.412 seconds and final-demand calculation at
10.795/10.652 seconds, saving 3.623 seconds (25.3%) on their means. Repeated
final-demand calculations take 0.182/0.176 seconds. Both comparisons match across
1,687,230 numeric values with zero geometric/reference differences under the
same pending-analysis exclusions described above. This is a different fixture
from the frozen nine-operation baseline and is not combined with its timings.
