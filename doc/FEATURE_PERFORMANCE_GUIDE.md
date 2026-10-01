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

These are patterns to evaluate, not feature lists to copy blindly. Their reported
percentages apply only to the recorded operations and fixtures.
