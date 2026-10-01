# Optimization review status — 2026-10-01

This reconciles the original A1–A15 audit with implemented, measured follow-ups.
It records the current review pass; it does not claim that no further speedup is
possible. Detailed records retain their own baseline, fixtures and test limits.
Windows 2026100102 packages all listed changes through `8ec6daa4`; its
[release record](../releases/2026100102.md) contains acceptance and remaining limits.

| Original area | Accepted work and evidence | Retained boundary |
| --- | --- | --- |
| A1 Sketch solver | [Independent equation blocks](20260930-rectilinear-blocks.md) | Do not change solution branches or precision for speed. The withdrawn older-file corner issue did not produce a Sketcher change. |
| A2/A3 Assembly scenes and picking | [Scene/picker changes](20260930-assembly-scene-picking.md), [container highlighting](20261001-container-highlight.md) | Common ordered candidates, occurrence identity and hit tolerances remain mandatory. |
| A4 refresh scope | [Presentation refresh](20260930-presentation-refresh.md), [command opening audit](20261001-command-properties-audit.md), [Drill Point](20261001-drill-properties-opening.md) | Command callbacks with required picking, camera or reference side effects remain. |
| A5 source refresh | [Repeated source reuse](20260930-source-refresh.md), [source lookup](20261001-source-lookup.md), [open roots](20261001-open-assembly-roots.md) | Open source documents remain authoritative; display refresh never solves mates. |
| A6 View preparation | [Body-context transfer](20261001-body-context-transfer.md), [shading](20261001-shading-adjacency.md), [silhouette storage](20261001-silhouette-pair-storage.md), [buffer reuse](20261001-silhouette-buffer.md), [end-plane scene preparation](20261001-end-plane-scene.md) | No precision, tessellation or display-quality reduction. A general persistent GPU/scene redesign remains unproven. |
| A7 Workspace staging | [History sharing](20260930-history-sharing.md) | Keep current mutable state isolated and preserve complete Undo/Redo. |
| A8 fingerprints | [Exact batching](20260930-fingerprint-batch.md) | Preserve all real inputs, exact semantic keys and signed zero. |
| A9 native data | [Typed Sketch packets](20260930-native-sketch-packets.md) | Assembly scene/reference validation remains functional work. |
| A10/A11 Drawing sources/output | [Read snapshots](20260930-drawing-read-snapshots.md), [bounded reuse](20260930-drawing-reuse.md) | Retain exact output geometry and conservative nested Assembly invalidation. |
| A12 embedded definitions | [Symbol definition cache](20260930-symbol-definition-cache.md) | Variants, user text and actual geometry still evaluate. |
| A13 icons | [Cache lookup](20260930-icon-cache.md) | Palette and DPI remain cache inputs. |
| A14 measurement | [Primitive allocation](20261001-measurement-allocation.md), [repeated face metadata](20261001-measurement-face-metadata.md), [partition centres](20261001-measurement-partition-centres.md) | No unproven cross-operation index reuse; changed geometry must be revalidated. |
| A15 developer build work | [Compiled fingerprint encoder](20260930-fingerprint-batch.md) | Integrated verification remains reachable and packaged; broad compilation-unit separation is not a runtime optimization. |

The [Sweep audit](20261001-sweep-audit.md) and linked H-Sweep records additionally
cover explicit calculation/no-op paths. A user's accepted overlapping-coil policy
is documented separately; it is not a general license to delete geometry checks.

## Disposition of remaining hypotheses

The current review found no additional proven-dead product code suitable for
safe deletion. Registrations, test entry points, research modules and active
validation remain functionality, even where names or old prose suggest otherwise.
User projects, backups and images were not cleanup candidates.

A persistent measurement cache, finer nested-Assembly Drawing invalidation and a
complete GPU scene/transform redesign need new evidence and complete dependency
invalidation tests. Current measured allocation/refresh fixes do not establish
those broader contracts. STEP/IGES now has measured local inputs; see the follow-up below.
Do not substitute a synthetic speed claim or remove validation to mark those
hypotheses complete. Shared placement remains protected; Linux verification is
assigned to the Linux host. None of these retained hypotheses is presented as an
implemented optimization or a current measured regression.

Localization review: this status update contains no product text. All linked
implementation records describe the applicable five-language checks. Documentation
is English. The released archive is immutable; later source changes do not modify
it. Future optimization should begin from a measured operation and retain the
[performance-by-design rules](../FEATURE_PERFORMANCE_GUIDE.md).

## Autonomous follow-up and release preparation

The [remaining-area investigation](20261001-remaining-investigation.md) records
source tracing, real import probes, rejected experiments and unresolved boundaries.
The end-plane scene optimization is accepted. Read-only import diagnostic modes
remain available for reproducing the measured bottleneck. No additional dead
product code was proven removable. Windows 2026100102 packages these changes
together with Drill opening and all three measurement improvements. It is signed,
published and verified; acceptance is recorded in its separate release record.

A [subsequent STEP/IGES pass](20261002-step-iges-projection.md) investigates
operation-local projection and locator preparation. Its measured scope and
remaining large-import limits are recorded separately from this release.
