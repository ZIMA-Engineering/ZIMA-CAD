# Sheet from Body: independent exact attachment boundaries

Status: local native and complex-box GUI verification passed. Windows candidate,
packaged GUI and publication checks remain separate release gates; native Linux
execution is unavailable on this host.

## Scope and native data

The user authorized a native format change on 2026-11-28 to make converted sheet
corrections faster while retaining unfolding and ordinary attachments after
Explode. The change is confined to the Sheet from Body definition, version 2;
document extensions and the shared container-placement solver remain unchanged.
There is no legacy Sheet from Body loader or required external cache.

Initial conversion still uses the established exact conversion algorithm. After
its nominal Flats and Bends have been calculated, the conversion retains their
exact finite attachment boundaries, analytic supporting surfaces and native
endpoint identities. Subsequent local corrections consume that common native
reference source instead of following another corrected child's calculated
solid. The initial expensive conversion is deliberately outside the edit-time
optimization.

The native conversion owns:

- An exact reference packet in target-Body coordinates. Straight support is
  certified from native spline poles or analytic plane/plane or plane/cylinder
  intersections, with explicit tolerances; display samples are not its proof.
- A source-input record: exact source history fingerprint, target frame,
  effective sheet settings and authored RIP definitions/frames. Packet ordering
  and tessellation do not constitute a source edit.
- Each generated child's logical parent and ordinary attachment references.
  Logical branch ancestry still drives independent material ownership,
  dependency closure and Unbend/Bend Back.

Source changes invalidate reuse. Missing sources keep the established unresolved
state and last valid frames. Reconstruction retains the first Bend section's
native endpoint and segment identities by their authored roles. This mapping is
also used by explicit conversion Properties edits.

## Release to ordinary features

Explode privately requests the current exact parent boundaries. Its reference
request plan must have the identical material-operation fingerprint to the
current document. It restores ordinary attachments through the existing solver,
re-expresses native points in the resulting frame, and rebases endpoint
corrections to the current parent. It rejects any non-coplanar reprojection,
constraint failure or point movement beyond the declared tolerance.

Partial release includes the selected logical branch. Full release preserves
already released branches and freezes only independent roots numerically. Both
retain one ordinary transaction. The source prefix remains suppressed after a
complete same-Body release, so source material cannot reappear.

## Asset and production-project scope

The pre-change read-only inventory inspected 1,259 repository/config native
assets (933.9 MB) and ten protected Toyota project documents (1.466 GB). None
contains a Sheet from Body definition. The production documents and hashes were
backed up and their originals have not been modified. Inventory and recoverable
backup details stay in local verification storage, outside published artifacts.

The complex converted user box is verified on isolated copies. Its source-only
native definition retains the five source features; current converted copies
are calculated and written with the changed native implementation. Merely
changing a saved format number is not accepted as conversion evidence.

All six start templates were regenerated and reopened with the changed native
implementation; their bytes remain identical. Normal New Document GUI creation
passed for metric and inch templates (40.030 and 39.324 seconds), including active
editing context and commands. Exact packaged factory verification remains a
release requirement even though the factory inventory contains no affected
conversion definitions.

## Regression and timing evidence

The existing native Sheet Exchange cases remain active, including source-radius
changes, read-only references, missing-source repair, same- and cross-Body
conversion, RIPs, STEP sources, partial/full release, sheet states and DXF.
The focused bent-source entry point runs that same test body, not a reduced
replacement. New cases exercise both correction Sketches, both ends, repeated
positive/negative/zero edits, independent neighboring Sketches, native reopen
without calculation, cold regeneration and Undo/Redo. Corrected partial release
followed by complete release checks material preservation and unfolding.

The focused native test additionally checks a maximum of one body calculation
per local correction, exact equations, unchanged neighboring Sketch definitions
and graph, combined corrections, DXF, partial-then-complete release and ordinary
radius editing after release. Bound source-owned radius/angle or attachment
changes are rejected without document mutation; they become editable after
release. The full twelve mandatory sheet gates passed in 194.65 seconds. The
final focused test passed after adding this ownership guard.

The complex-box GUI passed in 100.866 seconds, including conversion, all five
languages, Cancel, middle-button OK, actual Flat and first-Bend correction clicks,
endpoint equations, partial/full release, Undo/Redo and DXF. The separate
selection regression passed in 46.409 seconds: three mode/hover/click cycles,
both Bend Sketch correction editors, persistence and endpoint-face contact.
Its finite support cube now follows the tested endpoint instead of a fixed
location that missed the larger box. Flat mouse inspection waits for the camera
animation, as the existing Bend test does; neither test weakens its assertions.

The earlier [first-Sketch matrix](20261128-converted-bend-start.md) remains in the
native suite: driving/reference and locked/unlocked drags, signed zero,
source-radius reconstruction and exact read-only endpoint equations. The twelve
sheet gates retain the broader attachment, both-side and end-constraint matrix.
This change does not claim exhaustive coverage of arbitrary Sketch constraints,
source topology or all possible combinations of released branches.

Serial complete CLI transaction measurements on the same complex RIP box are:

| Correction sequence | Original | Tool cache only | Exact boundary graph |
| --- | --- | --- | --- |
| Early Bend, first edit | 12.3700 s | 8.3133 s | 1.9187 s |
| Early Bend, second edit | 11.6500 s | 7.2081 s | 0.8437 s |
| Early Bend, return | 13.3473 s | 6.5164 s | 0.9275 s |
| Last Bend, first edit | 4.3371 s | 3.9527 s | 0.9664 s |
| Last Bend, second edit | 2.9996 s | 2.3253 s | 0.8887 s |
| Last Bend, return | 2.9961 s | 2.4738 s | 0.7527 s |

True unchanged confirmations took 0.0021-0.0028 seconds without document changes;
explicit regeneration took 1.2337 seconds. These measurements cover the shared
native command transaction, excluding GUI event/render latency. They are finite
measurements on this model, not universal speedup promises. Initial source
conversion remains expensive. Independent correction intentionally keeps the
neighbor's nominal frame unchanged; it is not claimed to reproduce the old
propagation of one child's correction into every following child. Native tests
compare cold and reused results under the new contract.

Local evidence is under `build/sheet-boundary-graph/`: `boundary-current.json`,
the matching calculation profile, `bent-final.log`, `box-current.log`,
`sheet-branch-gates.log`, `start-templates.log` and `catalog-and-ui.log`.

Localization review: three new boundary/release errors are translated in Czech,
English, German, French and Russian. Catalog validation and the five-language
synthetic Sheet Exchange GUI passed (2/2 checks, 34.09 seconds). No
platform-specific modeling algorithm was introduced. Native Linux execution
remains unverified.
