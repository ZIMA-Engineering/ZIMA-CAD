# Optimization review status — 2026-10-01

This reconciles the original A1–A15 audit with implemented, measured follow-ups.
It records the current review pass; it does not claim that no further speedup is
possible. Detailed records retain their own baseline, fixtures and test limits.
Windows 2026100101 packages changes through `2a6c4f51`. Drill opening and measurement
allocation/face metadata below are later source changes, available locally and on main.

| Original area | Accepted work and evidence | Retained boundary |
| --- | --- | --- |
| A1 Sketch solver | [Independent equation blocks](20260930-rectilinear-blocks.md) | Do not change solution branches or precision for speed. The withdrawn older-file corner issue did not produce a Sketcher change. |
| A2/A3 Assembly scenes and picking | [Scene/picker changes](20260930-assembly-scene-picking.md), [container highlighting](20261001-container-highlight.md) | Common ordered candidates, occurrence identity and hit tolerances remain mandatory. |
| A4 refresh scope | [Presentation refresh](20260930-presentation-refresh.md), [command opening audit](20261001-command-properties-audit.md), [Drill Point](20261001-drill-properties-opening.md) | Command callbacks with required picking, camera or reference side effects remain. |
| A5 source refresh | [Repeated source reuse](20260930-source-refresh.md), [source lookup](20261001-source-lookup.md), [open roots](20261001-open-assembly-roots.md) | Open source documents remain authoritative; display refresh never solves mates. |
| A6 View preparation | [Body-context transfer](20261001-body-context-transfer.md), [shading](20261001-shading-adjacency.md), [silhouette storage](20261001-silhouette-pair-storage.md), [buffer reuse](20261001-silhouette-buffer.md) | No precision, tessellation or display-quality reduction. A general persistent GPU/scene redesign remains unproven. |
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
those broader contracts. STEP/IGES kernel work needs a representative slow input.
Do not substitute a synthetic speed claim or remove validation to mark those
hypotheses complete. Shared placement remains protected; Linux verification is
assigned to the Linux host. None of these retained hypotheses is presented as an
implemented optimization or a current measured regression.

Localization review: this status update contains no product text. All linked
implementation records describe the applicable five-language checks. Documentation
is English. The released archive is immutable; later source changes do not modify
it. Future optimization should begin from a measured operation and retain the
[performance-by-design rules](../FEATURE_PERFORMANCE_GUIDE.md).
