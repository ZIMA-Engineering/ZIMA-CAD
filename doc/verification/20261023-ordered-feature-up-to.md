# Ordered Feature Up To: finite Windows acceptance

Status: native implementation, factory/dependency refresh and focused editor
verification complete; portable-release acceptance remains in progress. Scope was
explicitly approved on 2026-10-23 in
[the native-contract plan](../FEATURE_ORDERED_UP_TO_PLAN.md). This does not claim
native Linux execution or universal convergence for arbitrary surface chains.

## Shared implementation and persistence

Extrusion and Revolution consume an ordered chain of consecutive target
intersections. Each patch is delimited by its immediate neighbours, preserving
both convex and concave results. This is not a global intersection of every
target half-space. The per-side native definition stores directed retained-patch
choices, independent linear/angular corrections and the primary cap source.
Display rows do not identify geometry. All required data remain in native
documents; resolution receipts and borrowed occurrence snapshots are transient.

The change reuses the existing Feature, source-reference reader, native Boolean
history, Part/Assembly ownership and transactions. Assembly cuts still subtract
only from selected immediate Part occurrences. Container-placement solving,
native coordinate conventions, OCCT 8.0.0 and platform-independent modeling
remain unchanged. The source reader extraction also passes the existing Sketch
intersection regressions; Windows/Linux do not have separate algorithms.

## Geometry and editing matrix

| Scope | Verified cases |
| --- | --- |
| Native builder/kernel | 776 checks: independently integrated convex/concave volumes, source orientations/order, exact plane and sphere seams, small radii, signed zero and positive/negative correction, mirrored/rigid placement, Solid/Thin/Surface, circular profiles/holes, Solid/Surface draft, Extrusion/Revolution, native occurrence inputs and cold bindings, source changes, immutable sources and accepted-request reuse |
| Angular limits | Both directed axes and reflected sources, invalid correction through zero/360 degrees including partial profile crossings, valid contacts near zero/360 degrees, corrections beyond half a turn, hollow/Surface profiles, both original cap identities, nonzero authored start plus rigid placement; independent volume and wall-area integrals |
| Native definition | 31 checks: optional neutral block, separate quantities, reversal/symmetry, negative zero, native round-trip, adjacency identity under reorder/removal and atomic invalid data rejection |
| Native Workspace | 71 checks: independent sides/symmetry, repeated signed corrections, full creation/edit Undo/Redo, unchanged OK, original cap ownership, invalid order/missing source atomic rejection with rows, native save/cold regeneration, Part/Assembly Relations and locks, real Assembly cut from source caps and occurrence motion without source mutation |
| Main editor | Actual three-face mouse entry, independent eye/input state, short middle-button termination, Cancel, accepted correction and exact volume, unchanged OK/revision, save, label-only Delta inspection, Undo/Redo and original own-operand wire |
| Shared dialog | Append/replace/remove, duplicate rejection, one ordering checkbox, Up/Down identity, retained choices, independent inspection, Axis single-target transition, both correction quantities, precision/negative zero, two themes, five languages, live new-control translation, tall/short windows and reference-cell colors |
| Existing dependent paths | Original single-target Extrusion/Revolution, draft, side reversal, Sketch face/plane intersections and references; five related existing Feature/Sketch GUI suites |
| Localization/units | All five complete catalogs and placeholder coverage; expression/unit input, shared reference-entry tables and numeric locks |

The narrow angular end window removes only excess near the corrected contact.
A global half-space can cut an earlier orbit segment once the sweep exceeds
half a turn. The window excludes the initial ray near a complete turn and uses
the actual rotated profile ray. Oriented planar half-space bounds include all
extended supports so rotated UV rectangles cannot become accidental cut limits.
These are exact native calculations, not mesh approximations or relaxed tolerances.

## Interaction and performance boundary

Opening Properties uses its existing rollback scene. Entry, inspection, ordering
and draft values consume stored data and overlays. Extended limits show a profile
draft and a localized notice; target eyes inspect exact sources. Changed OK owns
calculation and commits once. Unchanged OK retains the document revision and
calculated storage and adds no Undo step. Cancel restores the original document.
Accepted ordinary selection uses the Feature's stored original operand wire,
through the model, without new solid fill or a thicker line.

Hidden table rows are created lazily. After that change, the existing full units
GUI matrix completed in 25.75-28.20 seconds and the standalone Feature dialog in
16.47 seconds on this Windows host. These are scoped observations under varying
concurrent verification load, not an A/B speedup claim. The final nine-suite
native/main-window regression completed in 84.38 seconds; its kernel suite took
12.21 seconds and the 71-check Workspace suite 78.14 seconds. Unchanged acceptance
is checked by revision, native storage and the existing no-change commit path.

## Factory and template gate

The committed-source inventory contains 1,185 native assets, including 137
Drawings. Exactly 1,016 Parts are affected, including BridgeLance, CornerGusset90,
DimpledHole and VentilationWindow. The repository-owned verification drivers
operate on a marked isolated stage and preserve original/private working bytes:

- `tools/verify-feature-up-to-factory.py`: fresh native calculation/save/readback,
  exact authored history and Sketches, original-reference packets, existing
  dimension numbers, allowed new correction slots, source/side bindings,
  physical values, metadata, runtime hash, elapsed time, memory and file sizes.
- `tools/verify-feature-up-to-dependencies.py`: native load of all assets,
  density/published mass at authored precision, six refreshed templates, and
  Drawing regeneration/native reopen compared with accepted Windows 2201.
- `tools/install-feature-up-to-factory.py`: read-only validation by default,
  explicit installation/staging only after all reports pass. It preserves the
  private M10 working Part and stages its converted committed baseline directly.

The final runtime passes all 1,016 fresh conversion/save/reopen checks,
1,185 read-only native loads and 137 Drawing comparisons with native reopen.
All six refreshed templates have exactly the bytes used by the completed final
GUI creation matrix in mm/in and all five languages. Mass verification
separates 1,013 solids with positive density/published mass from three intentional
non-solid/reference-only definitions. Authored publication precision is retained;
unavailable analysis is not represented as verified zero.

Git normalizes physical CRLF line separators in the four FORM INI files and
six templates. Installation verifies that no other byte changes, reopens all
ten exact indexed copies with the native CLI without source calculation, and
repeats actual new-document GUI creation from those six indexed templates.
The published file-hash summary records the exact committed bytes. Engineering
library assets retain their byte-preserving Git policy; the private M10 working
file is untouched and its verified public baseline is staged without filters.

The three-worker factory run took 488.15 seconds under concurrent regression
load. File sizes, largest per-process peak working set, native runtime hashes,
all converted file hashes and tested implementation blob identities are recorded
in [the machine-readable summary](20261023-ordered-feature-up-to.json).
These measured values are not a claimed speedup or a cross-platform benchmark.

All four final-runtime FORM insertion probes pass. BridgeLance and DimpledHole
cover three thicknesses and both stock faces, insertion, symmetry/precut or
starting-skin checks, internal dimension editing, Unbend/Bend Back, native
save/cold regeneration and Undo/Redo. CornerGusset90 and VentilationWindow also
pass their dedicated native outward insertion/dependent geometry probes.
The standalone FORM test needed the application's existing eight-megabyte MSVC
stack budget: its former one-megabyte stack overflowed during nested readback
(native status `0xC00000FD`), while independent CLI readback succeeded. With the
correct test budget the complete BridgeLance matrix passes.

## Explicit limits and remaining gates

The full generic physical-cursor hover test remains a runner limitation:
`QApplication::widgetAt(QCursor::pos())` returns null. Its original expectation
was retained. Dedicated reference-delegate color, shared-table and actual
main-window entry checks pass; the complete unrelated hover suite is not claimed
as accepted. Existing old dialog labels are not all live-retranslated by this
change; new controls are, and startup language catalogs pass.

Arbitrary mixed curved/draft chains and a complete nested-Assembly GUI matrix
remain unverified. Ambiguous/disconnected limits are rejected rather than
silently bridged or rebound. New Sketch Projection and view-relative H/V are
separate queued work. Committed-source portable build, exact packaged factory
files, archive integrity, native smoke and GitHub publication remain pending
acceptance gates.
