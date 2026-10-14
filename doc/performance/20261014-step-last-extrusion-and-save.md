# STEP-backed Feature wire, subtraction and native saving

This Windows working-tree follow-up uses baseline commit
`367b44aad794a31675aaafc0181b87eab01bf1e1`, Qt 6.11.0 and pinned OCCT 8.0.0.
The preceding October 14 STEP and datum fixes are included in both subtraction
samples. No new release or Linux verification is claimed.

## Representative document

The user saved `Projects/02.prtz` at 07:12:49 Europe/Prague on October 14.
Its immutable investigation copy is `build/02-user-20261014-071249.prtz`:

- 646,622,704 bytes;
- SHA-256 `F5B343B2F88BF7225036AF43C7576983A575F5D671F78315C563824C723F4EDE`;
- an imported STEP, an empty Sketch Feature, an Axis Feature and the last
  `Vytažení 001`, container `01a138ce2b5873aebe41c69f33c047c0`;
- a closed four-segment rectangle, solid subtraction, side 1 disabled and
  side 2 extruding 50 mm.

The original user document was neither edited nor saved by these tests.

## Pending Sketch wire

`prepare_owned_profile_preview` extracts the editing Body through
`PartDocument::body_document`. That established extraction excludes objects
whose owners are absent from committed history. A new pending Feature is
deliberately absent, so its newly drawn owned Sketch disappeared during this
extraction. The subsequent preview either had no Sketch or recreated an empty
one. The committed calculation still received the authored draft.

Restore the exact matching owned Sketch draft after Body extraction, before
the existing profile resolution. This changes only profile-preview preparation;
it does not change the shared placement panel, solver, dimensions or references.

The actual mouse-driven rectangle/Sketcher-return GUI tests now require the
complete 16-edge preview, repeated length updates and independent geometric
equations. The STEP fixture has a translated and rotated Body. Its actual
authored Sketcher points must remain in the selected support plane and appear
in the returned wire; snapping is allowed to change raw click coordinates.
Cancel must leave the committed document unchanged.

`zima_cpp_feature_modeling_wire_ui_contract` also covers native OK, save/reopen,
Undo/Redo, draft edits, whole-Origin placement and Cancel through the existing
Feature verification. `zima_cpp_import_drawn_feature_wire_ui_contract` covers
new drafting on an imported face. A separate GUI probe of the user's saved last
Feature confirms all 16 wire edges in its persisted Body frame, successful
Properties opening and unchanged Cancel (`build/02-stored-feature-gui.*.log`).
No Sketcher constraint algorithm was modified. These preview tests do not claim
an exhaustive Sketcher dimension/drag matrix on every imported support.

## Subtraction

The cold calculation must rebuild actual STEP topology/ancestry before a real
material operation; the reference-only reuse exception is not extended to cuts.
A 50-to-51 mm edit showed about 98 seconds overall, including 47.44 seconds
preparing result edge packets. Boolean geometry was only part of that work.

The established fallback inward-face resolver constructed three independent
surface projectors per edge sample: the edge point and its two classification
probes. OCCT 8.0.0 provides a many-point API that retains surface preparation
while `Perform` repeats the same extrema search. Prepare that projector once
per edge/face, keeping the original surface bounds, algorithm, tolerance,
sample order, oriented normals and classifier/tie-break rules. Reuse never
crosses an edge/face call or survives a changed geometry input.

The exact saved last 50 mm operation was then calculated cold before and after:

| Measurement | Original projectors | Reused surface preparation |
| --- | ---: | ---: |
| Last-operation calculation | 96,104 ms | 53,340 ms |
| Final result edge packets | approximately 47 s | 11.53 s |
| Complete final local Body packet SHA-256 | `17b2f42f62f5250fe5af24903a2b191bed2592236f1ebf051432281c235d21b7` | identical |

The packet includes geometry, reference identities, side directions, persisted
B-Rep and the existing physical-property state. The probe uses ordinary editing's
deferred analysis policy; it does not claim a newly integrated inertia tensor.
Persisted document definitions and side choices are also compared unchanged.
Logs: `build/02-cut-native-baseline.*.log`,
`build/02-cut-native-reused.*.log`, and `build/02-cut-probe.*.log`.

## Native saving

The initial native round trip measured 114,833 ms for saving. Disk writing took
1,243 ms; cache packing took 31,427 ms and validation reload took 55,470 ms.
The full expanded serialized history and the encoded INI text remained alive
alongside the independent validation reload.

Move ownership of the transient serialized root into the INI writer and release
it after lossless cache/text preparation. Release that text after writing, before
the complete existing validation reload. Large pooled strings use string views
into their immutable, pool-owned allocations as hash-table keys, avoiding a
second retained escaped copy. Pool order, thresholds, exact equality and UTF-8
validation remain unchanged. No format version, geometry, calculation precision,
required native data or validation check is removed.

The first verified modified round trip measured 85,143 ms for saving. Definition
hash `8fbd9befdbba5cf528b4616c4f0f080c43e2d86c3de27a708f1f613deda5baeb`
and complete calculated-packet hash
`76efa13d61c810bb7f29059aed8b608e7a72b6029248605bf48103fdf7d993ba`
match the baseline exactly; the saved file remains 646,622,704 bytes.
Logs: `build/02-io-baseline.*.log` and `build/02-io-fixed.*.log`.

Additional whole-packet memoization was removed after its raw JSON comparisons
increased packing time. A failed disk-full sample is not performance evidence.
Final repeated measurements with disk space available are recorded separately
in `build/02-io-final.*.log`: save 95,472 ms, reopen 33,711 ms, with the same
definition/complete-packet hashes and file size. The observed modified save range
is therefore 85–95 seconds, rather than a guaranteed 85-second result.
The final allocation follow-up consumes only the exclusively owned transient
calculated-boundary JSON tree. The shared cache encoder retains its original
const-input API; its consuming overload reuses the same ordered traversal,
pooling thresholds and exact byte keys. It moves existing object/array
allocations instead of constructing a second tree. Live document geometry and
calculated model packets are never consumed. The original byte format is retained.

On the same immutable input this reduced cache packing to 24,544 ms and releasing
the serialized history to 39 ms (previously 12,823 ms). Complete save took
63,389 ms, with full validation reload taking 25,235 ms. Reopen took 23,266 ms.
Both definition and complete calculated-packet hashes above and the saved file
size remain identical. Evidence: `build/02-io-consuming.*.log`.
A serial repeat measured save 74,351 ms and reopen 27,029 ms, with both complete
hashes and file size identical (`build/02-io-consuming-repeat.*.log`). The final
implementation's observed save range is 63–74 seconds; validation reload and
host memory pressure contribute to the variation.
The encoder contracts compare complete stored bytes with the original copying
oracle, including repeated large escaped Unicode strings after pool growth,
signed zero, nested structures and pooling thresholds. Both input overloads
reject reserved cache keys, invalid UTF-8 and excessive nesting.

Timings are observations on this host, not universal guarantees; saving this
large document still has substantial serialization and full validation costs.

## Verification and cleanup

Six focused cache, edge-treatment, Part/Assembly profile and wire GUI regressions
passed (`build/02-last-regressions.log`, 16.47 seconds). Additional STEP, datum,
Body-properties, construction and translation checks passed. The engineering
metadata test initially used a stale executable expecting `START_PART.prtz`;
rebuilding that target resolved it without a product change. The four final
cache/metadata/wire checks passed in 11.98 seconds
(`build/02-delivery-regressions.log`). No user-visible product text changed;
the five-language translation contract passed.

After the final consuming-encoder change, seven freshly linked scoped contracts
pass in 23.04 seconds: cache bytes and rejection guards, translations,
engineering metadata, native metadata/persistence, Assembly profiles and both
wire GUI workflows (`build/02-consuming-regressions-workspace-temp.log`). The
initial attempt used the sandbox's restricted default temporary directory;
three fixture creation/rename checks failed there. Pointing TEMP/TMP at the
existing workspace test directory resolved those environmental errors without
changing product code or assertions.

The existing profile-frame GUI, Sketch reference commands, dimension command
matrix, profile-Sketch commands and dimension-entry GUI also pass after fresh
native test builds (`build/02-sketch-consumer-regressions.log`, 93.81 seconds).
These cover established dimension value/lock/reference policies, source
references, profile edits, persistence and the exercised GUI interactions.
The new STEP return test still does not claim every dimension/drag/contact-side
combination on an imported support; those untested variants are not silently
treated as exhaustive coverage.

After disk space was exhausted, the user freed space and authorized further
project cleanup. Approximately 19.0 GB of generated test copies and recoverable
vcpkg package/old Qt build caches were removed. Absolute paths and reparse points
were checked before recursive removal. Current `build/cpp-windows-release`, its
installed SDK, OCCT sources, project models/backups, configuration, Git data and
release archives were retained. Cache removal is recorded in
`build/cleanup-20261014-cache-removal.json`; the subsequent build and GUI checks
use the unchanged root `zima-cad.bat` development entry point.
