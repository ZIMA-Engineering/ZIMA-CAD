# Skeleton history editing and reference reuse

## Reported workflow and fixture

The user reported that creating or editing a Feature becomes progressively slow
as a Skeleton grows, although the same command is responsive in an empty Part.
The representative private document is `ZE0176-0101-0000_SKELETON.prtz`. All
tests used isolated copies; the original project file was neither edited nor
packaged.

The measured Part has one Body and 26 calculated operations. Its last operation
is a Thin Modeling Feature with an owned Sketch, two Up To sides and many local
external references. Editing only its thickness is a useful finite case: the
first 25 operation definitions, Sketch projection frame, placement and reference
choices remain unchanged, while the final solid must genuinely be recalculated.

Measurements use the native Windows x64 Release build, Qt 6.11.0 and the shared
OCCT 8.0.1 pin. Wall-clock timings are observations from this machine, not
hardware-independent acceptance thresholds.

## Findings

The previously added native history cache is useful. On a warm edit the kernel
reports 26 operations, restores 25 matching boundaries and calculates exactly
one. Removing that cache would force the whole Skeleton history to replay. The
cache is stored losslessly inside the native document and is accepted only when
its exact input and kernel-history identities match; it is not an external
loading buffer.

The changed final operation remained expensive for separate reasons. Diagnostic
samples attributed most of its native time to the OCCT Boolean, identity-safe
Unify and complete result/reference packet construction. The final scene also
prepares and publishes a large viewer packet. Protecting stable ancestry during
Unify is required: in the measured result 104 of approximately 2,780 candidate
edges needed protection. A speculative parallel Unify experiment did not improve
the controlled sample and was removed.

Before this follow-up, a settled core calculation was approximately 9.4 seconds
in the recorded local sample. Rebuilding the same kernel-operation vector and
re-resolving unchanged reference state were measurable avoidable costs; OCCT's
real changed geometry was not.

## Implemented changes

- Complete source-reference packet copies use the existing 256-entry live cache
  limit instead of an unrelated eight-entry limit. The cache still validates its
  fingerprint and can evict any entry.
- Kernel operations prepared for the ordinary group/solid-state decision are
  passed to the selected path instead of being built a second time.
- Sketch-reference refresh returns immediately when no Sketch has external
  references, skips individual Sketches without references and avoids Drill
  Point preparation when the document has no Drill Point Feature.
- A final Body reference packet is moved after its last consumer instead of
  being deeply copied twice.
- The changed-last-Feature path can retain its already resolved local Sketch
  frame and reference state only for a proven pure thickness edit. It compares
  the complete projection state, placement, profile source, Sketch, offset,
  dependency graph, Body structure and every preceding calculated source
  fingerprint. Multiple Bodies, Body Booleans, saved Body properties, Sections,
  external documents and downstream consumers retain the complete resolver.
- Properties draft preparation registers the pending owned profile before one
  required reference resolution. Length, thickness or operation-only changes
  reuse the unchanged frame; placement or profile-frame changes still run the
  established resolver.
- Native Part saves use the final self-contained Body result as the single cache
  root when its complete local Body histories can reconstruct every document
  boundary. The loader restores those exact immutable packets, their document
  prefix fingerprints and the evaluator's non-final reference compaction without
  running OCCT. Unsupported layouts retain the previous complete boundary array.
- Cache references expand in place. Ordinary scalar, array and object nodes keep
  their existing allocations instead of being self-move-assigned while traversing
  approximately 1.5 million nodes in this file. Reference order, depth checks,
  exact sharing and malformed-input rejection are unchanged.

No calculation precision, authored model data, topology identity, reference side
or Undo transaction rule changes. The native file's disposable calculated-cache
encoding gained a compact variant; the existing complete-array encoding remains
readable and is still the fallback. The shared Windows/Linux C++ implementation
is unchanged by platform branches.

## Final measured interaction

The complete automated GUI scenario made two successive thickness edits, checked
that draft changes did not modify the document, confirmed each edit, performed
Undo and Redo, saved and reopened the native file, and compared the authored
Sketch and history counts.

| Interaction | Windows Release observation |
| --- | ---: |
| Open Properties, first sample | 1.461 s |
| Open Properties, repeated sample | 1.610 s |
| Draft thickness update | 0.134--0.139 s |
| First OK after kernel-history identity changed | 32.028 s |
| Repeated changed OK with 25/26 prefix reuse | 4.627 s |

The cold first OK is not the normal warm-edit result: the implementation identity
changed during development, so the saved prefix was deliberately rejected and
rebuilt. Saving the current result publishes a cache for later matching edits.
The repeated result is about half the earlier 9.4-second settled observation,
but it does not meet a one-second confirmation target. Remaining time is mainly
the real OCCT operation, complete reference/result packets and scene publication.
Reducing it further without copying less data requires a broader immutable
viewer-packet architecture and must be treated as separate work.

## Verification

The final source passed these focused native contracts:

- `zima_cpp_solid_state_history_tests`
- `zima_cpp_solid_state_document_tests`
- `zima_cpp_history_deletion_tests`
- `zima_cpp_model_calculation_tests`
- `zima_cpp_profile_command_tests`
- `zima_cpp_external_profile_target_tests`
- `zima_cpp_profile_reference_command_tests`
- `zima_cpp_thin_profile_contract_tests`
- `zima_cpp_external_circular_profiles_tests`

`zima_cpp_translations_contract` passed all Czech, English, German, French and
Russian catalogs. This change adds no user-visible source text and therefore no
catalog entries. The complete private-fixture GUI scenario passed draft
isolation, two changed confirmations, Undo/Redo and native save/reopen.

Native Linux compilation, GUI execution and timing remain unverified on this
Windows host. The portable Windows package has its own release gates; this local
measurement alone is not package or publication acceptance.

## Native persistence and repeated-edit follow-up

The same protected Skeleton copy was measured after the cache writer, viewer
packet and native-load changes. A complete verified native round trip observed
approximately 23.1 s load, 37.1 s save and 24.0 s reopen for the 51,025,383-byte
input. Save validation includes reopening and comparing definitions and complete
calculated packets. The source project file remained untouched. These values are
still too high for the intended workflow, but the save path is materially below
the earlier approximately 54 s baseline without reducing precision.

The last-operation probe independently recalculates the 26th operation from its
persisted prefix, then performs two successive valid draft edits and restores the
original value after each. One measured run observed 39.99 s for the deliberately
cold last-operation reconstruction and 5.79/5.64 s then 5.98/6.22 s for the two
warm edit/restore pairs. Each restore retained an identical viewer/reference
packet, reference identity multiset and bounding box.

OCCT initially accumulated obsolete face p-curves in the opaque final BRep:
4,329,238 bytes became 4,526,391 and then 4,723,595 bytes across two edit/restore
cycles. Cleaning only unused p-curves of the definitive persisted result reduced
and stabilized all three archives at 2,710,680 bytes. Applying that cleanup to
intermediate results was rejected because a later history operation still needed
one of those representations. The accepted final-only path completed both warm
cycles with identical geometry and references. The separate save/reopen probe
persisted the current calculated model in 27.1 s as a 43,530,847-byte file and
reopened it with exact definitions, history packets, geometry and references.

An attempted shortcut that skipped a coincident end-plane Boolean was also
rejected: although it reduced work, the representative model changed its original
reference identity set and tessellation. No precision setting was relaxed.

## Compact native cache follow-up

The final cache-root encoding was measured on the same protected 51,025,383-byte
Skeleton copy. Two accepted post-optimization runs observed 18.206--18.464 s
initial load of the old cache, 15.975--16.237 s save and 13.204--13.579 s reopen;
the saved file was 51,019,537 bytes. Compared with the earlier approximately
30.4-second save sample, save time fell by about 47%. In-place cache expansion
also reduced reopen by approximately 17% from the preceding 16.423-second compact
cache observation. Timings remain machine- and cache-state-dependent, so the
structural work counts and exact round-trip checks are the primary acceptance
evidence.

The native I/O oracle hashes the complete authored definition and every expanded
calculated history packet before and after save/reopen. The SHA-256 values matched.
The multi-Body contract additionally compares every reconstructed packet exactly
for ordinary histories, a suppressed last Feature and a Body Boolean. Mirror and
Scale persistence retained their complete reference databases and oriented side
choices. Existing files using the previous cache encoding open directly; saving
them rewrites only the disposable calculated cache. No project conversion is
required for correctness, but resaving with this build enables the faster path.
