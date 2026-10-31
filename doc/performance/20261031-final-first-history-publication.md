# Final-first history publication and rollback materialization

## Scope and representative data

This investigation addresses three reported slow workflows: editing an early
history parameter, moving **Insert Here** in history and creating or changing a
late Feature on a geometrically complex Part. The representative private input
is `ZE0176-0101-0000_SKELETON.prtz`. Every destructive or edited measurement used
an isolated copy; the original project document was not modified or packaged.

Measurements use the Windows x64 Release build, Qt 6.11.0 and the shared OCCT
8.0.1 pin. Wall-clock values are observations from one workstation, not portable
acceptance thresholds. Geometry, complete stable-reference identity sets, side
choices, physical properties and native reopen behavior are the correctness
gates.

## Diagnostic A/B results

The original Skeleton deliberately contains rounded Sketch geometry, Thin
features, body edge treatments and a demanding downstream Boolean chain. A test
copy with the applicable rounded geometry removed and ordinary closed solids in
place of suitable complex features isolated the cost of that authored geometry.

| Operation | Original Skeleton | Simplified test copy |
| --- | ---: | ---: |
| Complete calculation | 28.9 s | 16.93 s |
| Early parameter edit | 40.7 s | 12.41 s |
| Last-feature creation | 5.23 s | 1.447 s |

The simplified copy is a performance diagnostic, not a replacement model.
Changing Thin to Solid is not generally valid for the open profiles and sided
material choices in this Part. Thin/Solid identity inheritance therefore remains
separate design work; no authored source or native format was changed here.

Changing the modeling tolerance from 0.001 to 0.01 did not produce a material
speedup and was rejected. Removing calculated history cache increased work and
was also rejected. Precision and exact geometry remain unchanged.

A synchronous attempt to precompute the complete rollback prefix while opening
Properties moved latency instead of removing it. Properties opening increased
from approximately 1.4 seconds to 14.231 seconds, while changed OK still took
27.463 seconds; a repeated opening/OK pair measured 1.023/27.059 seconds. That
route was removed.

## Implemented design

An edit of an existing history container still evaluates every native OCCT
operation and its Boolean/topology dependencies in authored order. The evaluator
now separates that required shape work from publication of complete intermediate
viewer and persistence packets:

- the complete final Body result is published synchronously;
- Fillet and Chamfer owner packets remain complete;
- Sketch body-edge projections, local exact intersections and Drill Point retain
  the exact preceding dependency boundary they consume;
- other large intermediate packets may be intentionally deferred;
- opening Properties materializes the real input body on demand before rollback;
- the low-priority background queue completes remaining disposable history
  packets from private snapshots after requested physical analysis; and
- stale background work is discarded unless runtime identity, document revision
  and calculated-data generation still match.

Publishing a completed cache boundary does not replace or move-assign the active
`PartDocument`. This is important because a Properties command can hold
short-lived pointers to its edited container. The first implementation exposed
that pointer-lifetime defect: materializing rollback geometry copied the document,
invalidated the pointer and made a repeated dialog lose its name. The cache-only
publication path now preserves the exact document and container addresses while
advancing only calculated-data generation. It creates no Undo transaction.

The final body is not rebuilt merely to complete a deferred boundary. No scene is
republished because the visible final result is unchanged. Properties opening
also preserves the exact current camera instead of fitting or recentring the
rollback scene.

The Feature dialog layout moves the existing **Origin axis** and **Centroid
axis** checkboxes next to **Text**. They are no longer mixed with **Symmetric**,
**Direction** and **Swap sides**. This is a layout-only change using existing
localized text and persisted fields. Individual-profile centroid axes are not
introduced by this change.

## Exact-output gate

The isolated final-first calculation measured 20.7268 seconds versus the
approximately 40.7-second early-edit baseline. Two-way Boolean difference was
zero in both directions. Volume, surface area, viewer mesh, stable reference
identities, occurrence paths and side identities matched. Materializing the
required operation-19 rollback body separately measured approximately 2.28
seconds.

This isolated result was not reported as the complete GUI interaction. Reference
settling and the remaining suffix still run on explicit changed OK, and the
complete production route must include them.

## Final automated GUI observation

The final test opened an isolated copy of the native Skeleton, activated the
authored Body, changed the same early Thin Feature twice, verified draft
isolation, confirmed both edits, executed Undo/Redo, saved and reopened the file,
and compared its authored Sketch and history counts. It also compared the exact
camera before and after each Properties opening.

| Interaction | First edit | Repeated edit |
| --- | ---: | ---: |
| Open Properties | 1.379 s | 1.860 s |
| Draft value update | 0.061 s | 0.067 s |
| Changed OK | 31.437 s | 25.181 s |

Undo measured 0.515 seconds, Redo 0.568 seconds and Save 24.546 seconds. The
second changed OK is faster, but the result does **not** meet the desired
approximately three-second early-history target. The remaining foreground cost
is dominated by required OCCT topology/Boolean work and reference-settling
passes, not by the deferred viewer packets alone. Further reduction is separate
work and must retain the same exact-output gate.

The user's earlier live observation on the preceding development build measured
approximately 2--3 seconds for moving **Insert Here**, 3 seconds for adding the
last element and 4 seconds for subtracting it. The final automated matrix focuses
on the harder early-parameter edit and does not replace those interactive
observations.

## Verification

The final Windows Release build passes the directly affected tests for model
calculation and deferred rollback materialization, metadata without geometry
mutation, the Feature dialog/layout, Solid State history/document/Fillet,
multi-Body history, cache storage, history deletion/recovery, external profile
targets and Sketch intersections, derived copies, Thin profiles, Fillet/Chamfer
commands and queries, tangent edge routes, Shell identity, Drill Point,
workspace activity and document-session transactions. The native Skeleton GUI
scenario passes camera retention, repeated edits, Undo/Redo and save/reopen.

The five-language translation contract passes. No user-visible string or catalog
key changed; existing Czech, English, German, French and Russian labels are
reused.

Two unrelated baseline gaps remain explicit. The current Linux parity fixture no
longer contains the runtime fingerprint expected by
`zima_cpp_native_documents_tests`. The Feature-axis GUI source also contains an
older construction-point image assertion that conflicts with its current rule
that authored points in the active Sketch are visible. A separate imported-body
frame fixture fails its independent signed work-plane equation before its
dependent datum command runs. None of those source paths changed in this work.

The implementation has no platform-specific modeling branch. Native Linux
compilation, execution and timing require the Linux host and are not established
by this Windows verification.
