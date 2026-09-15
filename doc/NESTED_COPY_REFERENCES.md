# Original references in nested Mirror and Pattern copies

An independent test exposed inconsistent analytical surfaces in nested copies.
The source plane matched its triangulation vertices within approximately
4.7 x 10^-14 mm, but after mirroring the deviation reached **51.5766 mm**.
Pattern also rejected a persisted path through a virtual occurrence.

## Coordinate-frame correction

Assembly-packet samples already contain nested placements, while an analytical
surface remains in its original occurrence frame. Mirror/Pattern calculation
transforms the whole packet at once. Before that explicit calculation, only
analytical data is therefore converted into the packet frame; the calculated copy
returns it to the frame of its exact persisted hierarchy. Existing transformation
functions are reused.

This additional conversion changes no samples, spline poles, topology identities,
mate solving, or placement values. Triangles of each face share analytical data.
Display and original-reference packets share separately so their material-side
information is not mixed. A missing exact source path is rejected.

Shared persisted-path reading now accepts Pattern nodes. Their virtual occurrences
belong to the surrounding real Assembly. An internal Part of an inserted subassembly
still belongs to that source subassembly. Activating a derived occurrence selects
its exact original source while keeping the top-level Assembly displayed. Queries
and projections do not calculate copies.

Extensions, native structure, and templates are unchanged. Previously calculated
copies receive corrected analytical data at the next explicit recalculation;
reads and tab switches do not recalculate automatically. The catalog remains at
**209 commands**.

## Verification

The original reproduction failed **0/1** (0.15 s),
`build/nested-copy-reference-baseline-tests.log`. After correction, the first suite
passed **3/3** (1.17 s), `build/nested-copy-reference-tests.log`; Mirror/Pattern
deviation fell to approximately 2.5 x 10^-14 mm.

Expanded regression passed **1/1** (1.07 s),
`build/nested-copy-reference-expanded-tests.log`. Independent checks cover plane
and cylinder equations at triangulation vertices, a unit axis, volume
10 x 12 x 14 + pi x 3^2 x 18 mm³, and Mirror, double-Mirror, and linear-Pattern
bounds. Cases include two bodies, composed spatial rotations, all three principal
mirror planes, circular/linear patterns, Mirror of Pattern, and Pattern of Pattern.
The same checks run after transferring references into another Part and after native save/reload.

After adding virtual-node ownership/activation, five translations, and building
all programs, the wider suite passed **21/21** (142.57 s),
`build/nested-copy-reference-integration-tests.log`. It covers full GUI, copy
Properties/creation, original references, context refresh, Workspace, native/cache
tests, source documents, actual CLI, and translations.
Build log: `build/nested-copy-reference-all-build.log`.
