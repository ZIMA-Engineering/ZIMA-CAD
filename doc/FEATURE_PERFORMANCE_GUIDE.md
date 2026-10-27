# Performance by design for modeling features

This guide applies to new commands and requested extensions. It complements
AGENTS.md; it does not authorize unrelated rewrites or changes to protected
placement, topology, persistence or geometry-side contracts.

## Design the lifecycle before writing the command

Inputs are calculated native geometry, stable references and the pending command
definition. Outputs are a correct interactive preview and, after explicit
confirmation, an undoable model result. Means are the existing document session,
shared properties controls, viewer packets and explicit kernel operations.

Trace creation, Properties opening, reference entry, parameter editing, unchanged
OK, changed OK, Cancel, regeneration and save/reopen. Record which state each step
reads, changes and displays. Include dependent and nested occurrences when the
command supports them. Reuse the same dialog and shared operation for creation
and editing.

## Own each scene update

- Install the correct editing/rollback boundary before publishing the command's
  input scene. Picking must use that actual geometry.
- Prepare each required base scene once per transition. Do not first publish a
  full-result scene that will immediately be replaced and never consumed.
- Overlay-only actions should update their overlay when the base inputs remain
  unchanged. Keep Tree state, command filters and reference inspection correct.
- A synchronous callback may inspect geometry, choose a semantic route endpoint
  or initialize reference entry. Trace those effects before deferring or removing
  it. Similar-looking refreshes are not automatically redundant.
- Leave the shared container-placement contract intact. A command-local
  optimization must not change reference solving or general placement behavior.

## Keep interaction inexpensive

Read persisted native/viewer data for labels, inspection, picking and previews.
Do not invoke OCCT from opening, hover, selection, tab switching or repaint.
Calculate bodies only for explicitly requested model calculations. Compare the
pending definition and annotation changes with the existing values before commit:
unchanged OK creates neither a calculation nor an Undo step; a genuine
annotation-only edit still commits its intended change. Cancel restores the
original model and normal full-history presentation.

The accepted drawing-view rules in AGENTS.md also apply: interactive views must
not wait for precise vector hidden-line preparation intended for export.

## Avoid unnecessary copying and allocation

- Move locally owned temporary geometry after its last read. Never move from a
  shared cached result or leave another consumer with an emptied packet.
- Reuse calculations only while every actual input remains valid. Source changes,
  occurrence transforms, reference/side choices and relevant display settings
  must invalidate dependent reuse. Prefer a bounded operation-local reuse when
  that avoids long-lived invalidation complexity.
- Choose data structures from their consumers' requirements. Preserve ordering,
  identities and floating-point accumulation order when those are observable.
- Intern repeated exact strings only with proven storage lifetime. Views must
  never outlive their owner. Do not normalize semantic keys or signed zero to
  increase cache hits.
- Retain precision, supported cases and model properties. A smaller result caused
  by dropped geometry or skipped required validation is not an optimization.

## Verify both the cost and the result

Record the baseline commit, fixture, build configuration, platform and measured
operation. Use representative user data when available, plus focused small and
larger fixtures; clearly label synthetic evidence. Run before/after serially on
the same machine, separate setup from the measured interaction, and state whether
cold or warm samples are reported. Repeat noisy measurements as needed.

Count unnecessary work directly where possible, such as base-scene publications.
Use deterministic counts and correctness checks for regression gates; wall-clock
milliseconds are observations, not hardware-independent pass/fail thresholds.

Compare geometry, stable reference owners/occurrences and candidate ordering;
compare exact viewer packets and controlled framebuffer hashes when applicable.
Check reference selection/replacement/inspection, camera restoration, unchanged
and changed OK, Cancel, Undo/Redo and native save/reopen as affected. Matching
pictures alone do not prove identity or persistence. Hash comparisons require
identical rendering conditions and are not cross-platform golden images.

Document the measured scope and remaining uncertainty. Review localization in all
five supported languages; no-text changes still require that review. Follow the
Windows runtime rules for builds and releases; Linux verification belongs on Linux.

## Reuse derived structure separately from actual geometry

Prepare an operation-local reference source lazily when a known dimension or
construction consumer requests it. Retain the complete established builder and
all its consumers; avoid replacing a needed source with a filtered approximation.

Connectivity reuse requires exact checks of every classification input, including
current coordinate equivalence, topology and ordered identities. Recompute actual
positions and normals, and restore the established publication order after a
rotation. A cache hit is useful only if its validation, cold construction and
invalidated cases do not impose disproportionate work elsewhere. Measure those
cases separately on real data; do not relax precision to improve hit rates.

Forced-fresh GPU comparisons are correctness oracles. Their extra uploads and
repaints do not represent ordinary command timing. Run ordinary A/B interactions
separately from those checks. The finite large-import evidence is in the
[STEP and viewer investigation](performance/20261010-step-gauss-shared-source.md).

## Measured examples to reuse

- [Properties refresh audit](performance/20261001-command-properties-audit.md):
  command-by-command dependencies; why a blanket refresh removal is unsafe.
- [Profile opening](performance/20261001-profile-properties-opening.md):
  deferred initial preview with preserved rollback, fit and extent inputs.
- [Boundary Surface opening](performance/20261001-boundary-properties-opening.md):
  retain the command-owned refresh consumed by reference inspection and picking.
- [Fillet and Chamfer opening](performance/20261001-treatment-properties-opening.md):
  remove the unused Tree publication while keeping route/dimension setup.
- [Body context transfer](performance/20261001-body-context-transfer.md):
  eliminate copies only after establishing local ownership and last use.
- [Shading adjacency](performance/20261001-shading-adjacency.md) and
  [silhouette preparation](performance/20261001-silhouette-pair-storage.md):
  reduce lookup/allocation costs while proving exact output equivalence.

- [End-plane scene preparation](performance/20261001-end-plane-scene.md):
  consume an existing local identity map in one traversal instead of rescanning
  the whole scene for every feature; verify repeated occurrence identities.

- [Import projection preparation](performance/20261002-step-iges-projection.md):
  memoize exact locators within one immutable operation; compare complete large
  packets, and discard promising experiments when controlled timings show no gain.

- [Imported-model interaction](performance/20261010-import-interaction-investigation.md):
  index the common picker while preserving the complete ordered candidate list,
  retain complete Body geometry in placement previews, and invalidate bounded
  reference/face caches from their actual inputs.
- [STEP loading and Extrusion](performance/20261010-step-load-and-extrusion.md):
  archive imported source geometry before one final history preparation, avoid
  unused measurement/section reference indexes, and retain GPU preparation only
  when its exact geometric inputs remain unchanged.
- [Dense-import placement validation](performance/20261011-placement-validation-cache.md):
  size an entry-scoped cache for its actual finite candidate set. Clearing all
  entries at a tiny fixed count can make every warm pass repeat the same solves.
  Verify complete candidate ordering and fresh-versus-cached results after a
  scene revision, and distinguish picker time from scene publication.

- [Single-write native Part save](performance/20261014-native-save-and-trimmed-circle.md):
  write the validated current representation once, check stream closure before
  replacement, and keep complete model readback in development tests. Avoid
  automatically reloading the full saved history during ordinary Save.

- [Whole-Origin input and native edge packets](verification/20261025-sketch-junction-selection-performance.md):
  retain ordered reference initialization while publishing its completed scene
  once; bound native face-classification preparation to immutable packet inputs
  and compare every classification state and full reference packet.

- [Sketch face-reference confirmation](performance/20261025-sketch-reference-entry.md):
  finish the command's work-plane update before publishing its preview, and
  consume an already published complete preview rather than publishing it again.
- [Native history continuation](performance/20261025-native-history-continuation.md):
  validate an unchanged native prefix against all actual inputs, and retain
  independent fresh-calculation equivalence checks.
- [Adaptive native storage compression](performance/20261025-native-storage-compression.md):
  keep exact data, validate the complete stream and measure opening as well as size.
- [Skeleton history editing](performance/20261026-skeleton-history-edit.md):
  retain the exact native prefix, skip reference work only after comparing all
  actual projection and dependency inputs, and measure cold invalidation,
  repeated confirmation, draft interaction, Undo/Redo and native reopen.
- [Native viewer cache and redesign gate](performance/20261027-native-cache-close-and-redesign-gate.md):
  compact exact display packets, detach large closed documents before destruction,
  and keep a proposed semantic/topology redesign behind a measured tenfold gate.

These are patterns to evaluate, not feature lists to copy blindly. Their reported
percentages apply only to the recorded operations and fixtures.
