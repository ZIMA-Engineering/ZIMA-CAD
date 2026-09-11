# Assembly and Drawing geometry sharing

## Verified baseline, 2026-09-11

The current Windows Release passes all 44 CTest contracts. Completed changes:
- Deferred dimension drag until the pointer crosses the drag threshold;
  pending Assembly angle edits use the component-properties transaction.
- Cached surface batches and selected-reference outlines between camera moves.
  On step-pokus.asmz (506,314 triangles), the isolated 1400 x 900 diagnostic
  measured the shaded-with-edges paint function at about 287 ms before and
  87 ms after. These are paint timings, not interactive FPS. The user confirmed
  smoother navigation; geometry precision and history caches were preserved.
- STEP product relationships distinguish real Assemblies from compound Part
  representations. ZE0026-0101-9001 retains one solid (26 faces) and two auxiliary
  faces in one Part. The source has 85 unique Parts and 11 real Assemblies under
  this classification, previously 121 and 47. Existing imports are not rewritten.
- Ordinary occurrence picking includes visible triangles without a source-face
  identity. It never promotes them into topology/placement references. Hover,
  RMB cycling and LMB confirmation share one ordered list.
- Nested-source Open resolves relative paths through unopened Assemblies without
  activating or regenerating them. Select Parent advances one level through the
  displayed root, highlights the exact subtree and synchronizes the Tree.

## Approved next work

First audit original STEP face/edge/point reference completeness. The ordinary
hover fix does not repair missing topology references. Never invent persistent
identities from triangle indices or OCCT traversal order.

Then share immutable calculated source geometry between repeated occurrences.
Assemblies own occurrences, placements, mates and their own geometry changes
(such as cuts), while unchanged source geometry is shared. Preserve pinned
last-calculated source revisions: editing/saving sources or switching tabs must
not update parents. Explicit Regenerate refreshes the nested dependency chain,
using open documents as the authoritative sources.

A mate persists occurrence path, source owner ID and semantic key, and resolves
original source geometry. Storage addresses must not replace topology identity.
If a source face disappears, report a missing reference instead of rebinding.

Drawing views retain calculated projections for fast display. Share common 3D
measuring data between views of the same source revision. Current views store
projected edges/triangles with depth and 3D measuring curves/points, not a full
source B-Rep or Part history. Current Assemblies still embed BodyResult per
occurrence and recursive child outputs. File-local interning reduces stored
bytes but does not establish shared runtime ownership.

## Formats and durable storage

Keep .asmz, .drwz and .prtz extensions. Advance internal Assembly and Drawing
versions when their persisted models change. Removing copies from Assemblies
and Drawings alone need not change Part format. Assess any separately necessary
source-reference change explicitly. No legacy compatibility paths are required.

A shared external revision store must be durable and relocatable with the
project. Persist revision data before publishing its referencing document.
Missing/corrupt revisions must fail explicitly. A disposable machine-local cache
cannot be the only copy needed to reconstruct a saved Assembly.

## Verification gates

- Complete reference capture on representative STEP solids and sheets.
- Stable mates after supported Part edits and explicit regeneration.
- Shared geometry with distinct repeated/nested occurrence paths.
- No implicit dependency refresh after source editing or tab activation.
- Save/reopen, project relocation and missing-revision diagnostics.
- Drawing projection and dimension references retain their meaning.
- Compare RAM, loading/saving and interaction on the same STEP source.
- Use repository Windows build scripts and relevant regression contracts.
