# Repeated dimension edits in converted sheet metal

Baseline source: `be3e87c74` (Windows MSVC Release, Qt 6.11.0, OCCT 8.0.1).
The input is an isolated copy of the previously verified complex RIP box;
the user's source file is never changed. This investigation concerns repeated
edits to an already calculated model, rather than initial conversion speed.

## Cause and bounded reuse

An early Bend correction requires several reference-settling passes to advance
the existing dependency chain. Each pass rebuilt all 25 independent sheet tools
in temporary worker kernels, reconstructed their authored material differences,
and repeated identical RIP preprocessing of the source Body. In the measured
box, six passes were needed for an early correction.

The existing shared placement/reference solver and pass convergence checks are
unchanged. Independent parallel tools now retain their immutable native/viewer
packets in the owning kernel, keyed by the complete standalone operation.
Parallel groups also include their exact input/reference prefix and the existing
index discriminator. They receive no sibling tools. Groups without an input
fingerprint are excluded. The key includes
geometry, frame, owner/material/side identity, tolerance and mesh demand; no
signed-zero normalization is introduced. Worker threads only read the cache;
publication and eviction happen after the workers join. The cache is bounded
to the existing 256-entry live-cache limit.

The authored child-minus-parent material calculation also reuses exact operand
fingerprints, only for independently prepared parallel sheet tools. Retained results are
native archives; reuse restores a separate OCCT shape, so later Boolean or sheet
state operations cannot mutate the retained entry. Branch ownership, source
reference identities and tolerances keep their existing definitions.

Within one explicit reference-settling calculation, identical RIP source results
are reused by the full source-history and transformed-RIP fingerprint. This
temporary cache is discarded after that calculation. A conversion marker alone
is not a sufficient key because it does not describe source geometry or RIPs.

## Measurements and verification

The serial baseline CLI sequence changed the same early Bend endpoint correction
three times (base + 0.25 mm, base + 0.5 mm, base), then repeated the sequence on
the last Bend. Opening, activation and metadata normalization are outside the
reported correction times. The first apparently unchanged CLI `bend.set` updates
an existing Sketch name; only the subsequent unchanged confirmations are true
no-ops. The command host executes the same native modeling transaction used by
the application, but these times do not include GUI event/render latency.

Baseline early edits: 12.3700, 11.6500 and 13.3473 seconds. Baseline last-Bend
edits: 4.3371, 2.9996 and 2.9961 seconds. Actual unchanged confirmations take
0.0026–0.0036 seconds and report no document change. Artifacts are under
`build/bend-first-correction/performance/`; timing files include the full command
responses and per-pass calculation profiles.

Optimized early edits: 8.3133, 7.2081 and 6.5164 seconds. The first edit fills the
new transient tool caches after reopening. The two subsequent edits take
6.5–7.2 seconds versus 11.7–13.3 seconds in the baseline. Last-Bend edits take
3.9527, 2.3253 and 2.4738 seconds. Unchanged confirmations still report no change
and take 0.0026–0.0033 seconds. These are finite measurements, not universal
speedup promises. Final Body assembly and exact reference resolution remain
measurable costs; this change does not make every edit instantaneous.

In a later early-Bend edit, individual settling passes reuse 23–25 of the 25
tools and 18–19 authored material differences. The RIP association phase falls
from about 1.6–1.8 seconds per six-pass edit to about 0.35–0.43 seconds. The
intermediate standalone-only experiment excluded composite Bend tools and did
not provide a reliable improvement; the accepted implementation includes their
complete reference dependencies. Timings from that experiment are not release
evidence.

The final local native Sheet Exchange suite passes in 73.68 seconds. The
common-parent branch gate passes in 27.42 seconds, including new repeated-radius
checks of warm-versus-fresh native definitions, exact material volume and area,
reference point identities/coordinates and face/branch/side identities.
Changed mesh demand and Boolean tolerance must invalidate tool reuse. Existing
overlap, attachment, Undo/Redo, save/reopen and sheet-state checks remain active.
All five localization catalogs pass in 20.90 seconds. The exact committed
Windows candidate must also pass the complete mandatory sheet and packaged GUI
gates before publication. Native Linux execution is not verified.

Localization review: the change introduces no user-visible text.

## Proposed shared boundary graph (not implemented here)

The user proposed a further architectural direction on 2026-11-28: derive an
exact boundary graph from the source Body, including RIP seams, gaps and Bend
limits. Each generated Flat/Bend would derive its zero frame from that common
graph and apply its own corrections, rather than waiting for the preceding
generated solid. Changed independent elements could be calculated concurrently
and collected by authored branch. Spatial contact must still never merge
independent branch ownership.

On release/decomposition, those common-graph references would be converted to
ordinary feature-to-feature attachments without changing geometry, frames,
corrections, side choices or identities. Source topology changes and missing
references must retain the existing repair and last-valid-frame behavior.

Current generated children are ordinary linked Flats/Bends, and the preview
wire contains sampled display curves with preview identities. It cannot serve
as this mathematical graph. The proposal needs exact authored/native curves,
surfaces, explicit branch incidence and stable source identities. It would change
the required reference/persistence model and the current release operation;
implementation therefore needs a concrete format, affected-asset conversion and
verification agreement under AGENTS.md. Shared placement remains protected.
This idea is recorded separately from the measured cache optimization above.
