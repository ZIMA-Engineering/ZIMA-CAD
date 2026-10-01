# Per-refresh source lookup — 2026-10-01

## Change and boundary

Inputs are the current open documents and requested source identities. The
existing Workspace lookup and session setup resolve each identity once per
source refresh, then a local map reuses that result, including missing entries.
Refresh does not add or remove open documents, so pointers remain valid until
the map is discarded on return. Recursive Family reads consume const Workspace
sources or temporary native documents; they do not insert documents.

This preserves the existing `find()` interceptor setup on first lookup. No
persistent index or new invalidation contract is introduced. Each subsequent
refresh resolves identities again. Ordinary Parts reuse their already resolved
source instead of looking up the identical Family-parent identity a second time;
Assembly processing also reuses its just-resolved open source.

No geometry, source generation, placement, appearance, native-file checking,
cycle rejection, transaction or regeneration behavior is changed. There are no
new user-visible strings; existing cs/en/de/fr/ru translation coverage applies.

## Measured results and limits

Baseline: `da8436ea`, already including the preceding duplicate-root traversal
optimization. The same expanded benchmark was compiled with baseline and changed
`workspace.cpp`, MSVC Release, Qt 6.11, OCCT 8 on this Windows host. Each cell is
the median of three runs, each averaging twenty warm calls. Compilation was not
concurrent with measurement. Fixtures have distinct open subassemblies with eight
calculated cube occurrences each. No user Assembly was available under Projects;
these are synthetic source-refresh measurements, not complete UI/frame timings.

| Part position | Assembly order | Groups / Parts | Before ms | After ms |
| --- | --- | ---: | ---: | ---: |
| First | Children first | 32 / 256 | 1.013 | 1.027 |
| First | Children first | 128 / 1,024 | 4.529 | 4.819 |
| First | Parent first | 32 / 256 | 0.833 | 0.789 |
| First | Parent first | 128 / 1,024 | 3.540 | 3.394 |
| Last | Children first | 32 / 256 | 1.212 | 0.993 |
| Last | Children first | 128 / 1,024 | 5.905 | 4.551 |
| Last | Parent first | 32 / 256 | 0.896 | 0.795 |
| Last | Parent first | 128 / 1,024 | 4.945 | 3.510 |

The strongest measured benefit is the 1,024-occurrence case with the Part opened
last: about 23–29%, or 1.35–1.44 ms per refresh. A first-position Part already has
a cheap lookup, so results are mixed: the children-first 128-group median is
0.29 ms (6.4%) slower, while parent-first is 0.15 ms faster. Allocation, hashing
and remaining scene/metadata work are included; no uniform speedup is claimed.
The bounded implementation is retained for repeated late/missing-source lookup,
without broadening it into a persistent Workspace cache.
[Raw runs](20261001-source-lookup.txt).

## Checks

The benchmark and three-level Workspace regression now exercise all four
combinations of parent/child order and Part first/last. Checks cover unchanged
shared geometry and metadata, current calculated volume after unsaved changes,
source Undo/Redo, and unchanged Assembly revision/dirty/history state. Existing
Workspace tests cover closing/opening sources, missing/reappearing files, relative
paths, unsaved names/appearance, native reopening and cycle rejection.

All eight focused suites passed: Workspace, Workspace publication,
file rename, component properties, Family, translations, Assembly refresh GUI
and scoped presentation refresh. Evidence: `build/source-lookup-tests.log`.
Local GUI/CLI remain available through the root development BAT. No native format,
launcher or release archive changes are required. Linux verification is deferred.
