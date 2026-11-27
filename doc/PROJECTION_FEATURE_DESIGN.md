# Projection feature

Status: implemented; targeted Windows acceptance passes. Distribution acceptance
for 2026112004 is recorded separately in the [verification report](verification/20261120-projection.md).
The accepted interaction requirements remain in
[the modeling follow-up](MODELING_FOLLOWUP_20261119.md).
The 2026-11-24 extension adds Project relief; its native preparation and finite
acceptance are recorded in [the relief contract](design/PROJECTION_RELIEF.md).

## User-visible behavior

Projection has two presets in the shared Feature command: Project sketch and
Project relief (`Promítnout skicu` and `Promítnout reliéf` in Czech). They also
appear directly below Twist, before Wrap sketch and Wrap relief.
It owns an ordinary editable planar input Sketch and
uses selected existing faces as its target. Its ordinary numbered Tree name
follows the current feature naming policy.

| Output | Result | Material behavior |
| --- | --- | --- |
| Sketch | Spatial curves lying on the selected faces, derived from the planar input | No material change |
| Split | Partition the selected faces along the projected boundaries | Same volume, material and occupied space |
| Surface | Independent bounded surface patches selected from the projected regions | Surface output; no added or removed solid material |
| Relief | Filled Sketch projected onto the nearest selected faces, swept along the projection direction or offset normally | Positive Correction adds material; negative Correction removes it |

The input remains a planar Sketch in all four modes. A curved target can
produce spatial output curves, which must not be put into the input Sketch's
two-dimensional solver or edited as a second independent Sketch. Editing the
source dimensions drives the feature and its dependents.

For Sketch output, a selected partial plane, cylinder, cone or sphere continues
on its exact analytic support beyond trimming boundaries (approved 2026-11-27).
Holes or portions covered by another feature do not interrupt that mathematical
projection. Keep the selected source identity and the ordinary ray direction,
nearest-hit and ambiguity rules. Freeform targets retain their actual domain;
no sampled extrapolation is introduced. Split, retained Surface regions and
Relief continue to partition the actual target faces and material.

The default projection direction is the oriented input Sketch normal. Reverse
changes its direction. An explicit direction reference uses the established
reference resolver for an axis or directed straight edge, including its retained
orientation. Multiple selected faces form one Up-to target: choose the first
nonnegative hit along each projection ray. Their table order must not change which
surface is nearest.

Show missing portions and ambiguous hits in the pending preview. Do not silently
project them onto another unselected face. Connected target faces can share
output at their common boundary; a periodic seam is not a physical gap. A
coincident zero-distance start retains the native contact curve and is covered
by an exact contact regression; no arbitrary displacement of the Sketch is used.
The direction must be transverse to the source plane. A direction within that
plane collapses its two-dimensional projection domain and is rejected explicitly.

Closed contours may bound regions. An open contour is valid for a region output
only when it actually separates a region together with existing target
boundaries. Surface output uses explicit region selection when several patches
are possible; never choose a patch by traversal order or nearest centroid.
Incomplete boundaries must not produce an apparently successful partial patch.

There is no Thin output, separate Add/Subtract selector or wrapping mode.
Project sketch offers Sketch, Split and Surface; Project relief exposes a signed
Correction instead. Surface Thicken/Shell remain available for downstream work.
Relief derives all material regions from the filled Sketch and does not expose
retained-region selection. Its stored depth direction defaults to Along
projection direction for a new command; Normal to target surface is the other
choice. The normal sign follows the solid's material side in both modes.
Rotational wrapping and FEM loads
remain separate commands. FEM may later reference a stable projected region;
that does not give Projection ownership of an analysis case.

## View and Properties

The ordinary View shows the calculated output on its target. Single click only
selects and synchronizes the Tree. Double-click adds the original Sketch and
its existing dimensions in the original Sketch plane. Dimension double-click
uses the current editor. Selection and inspection reuse prepared data and make
no geometry calculation or Undo transaction.

Creation and later Properties editing use one shared Feature dialog path with
the current in-application presentation, ordinary placement and owned-Sketch
editor. Show input and derived output together, with the established distinct
preview presentations. Use `zima/ui/reference_cell.hpp` for targets/direction:
active input, replacement/clear where meaningful, independent inspection and
middle-button end-entry behavior. Clearing a target must not delete its source.

OK validates, calculates and commits once. Cancel discards the complete draft.
Unchanged OK neither calculates nor adds history. Middle-button double-click
uses the shared confirmation mechanism. Resizing preserves anchored controls.
All labels, errors and tooltips require cs/en/de/fr/ru in the same change.

## Calculation and identity

At the feature's input history boundary, resolve the real bounded native target
faces and exact input curves. Preserve units, Body/occurrence transforms,
material-side choices and read-only external ownership. Reject dependency
cycles. A missing source retains the last valid frame and an unresolved
definition; it must not authorize a new material calculation.

The calculation should have one common projected-curve stage for all outputs:

1. Resolve the direction in the input geometry's frame and calculate bounded
   curve/target intersections along that direction.
2. Classify positive-ray intervals, target-boundary crossings, periodic seams,
   missing spans and ambiguous branches. Retain exact native curves and their
   surface parameter curves; display polylines are not modeling input.
3. Publish on-surface curves for Sketch output. For Split and Surface, feed the
   same validated curves to native face partitioning and region classification.
4. Keep all partitioned material for Split; extract the explicitly retained
   patches for Surface. Validate result topology, bounds and native ancestry.

Derived identities need the input curve identity, target face identity and a
durable branch/domain identity. Face partitions also retain their actual native
boundary parents and region intent. Numerical coordinates, face enumeration,
temporary kernel object addresses and sampled-polyline indices are unsuitable
persisted identifiers. Changed topology that loses a region must break the
dependency instead of silently reassigning it.

## Existing implementation paths to consume

| Area | Existing path and intended reuse |
| --- | --- |
| Feature lifecycle | Shared Feature Properties, owned Sketch editing, `feature_definition_calculation_policy`, rollback and `commit_part_document` |
| Reference input | `zima/ui/reference_cell.hpp`, shared source/direction resolvers and active-occurrence ownership |
| Native reference curves | `surface_intersection_geometry.inc` and `face_curve_reference.hpp` provide established ancestry and read-only reference conventions |
| Face partition/region identity | `surface_trim_geometry.inc` provides native splitter history and retained-region validation |
| Document validation | `surface_trim.inc` and related surface operations demonstrate input-boundary checks and feature identity validation |
| Transaction/no-op | `surface_trim_operations.cpp` demonstrates draft-to-request validation, unchanged definition detection and one final transaction |
| Scene lifecycle | `workspace/surface_trim.cpp` supplies a reference-entry/rollback lifecycle to review, not copy wholesale |

Current Surface Trim accepts one surface-result face, while Projection must
accept bounded solid faces and a face set. Reuse compatible kernel helpers;
do not broaden Surface Trim's public contract or emulate Projection through
hidden temporary Surface Trim history entries. The protected shared placement
solver remains unchanged.

## Preview and performance

Publish the rollback/input scene once on entry. Keep transient source and output
overlays separate from calculated document geometry. A lightweight directional
preview may use the already calculated target viewer packet; exact projection
and partitioning belong to explicit OK/Regenerate. Preview samples never become
persisted reference geometry or proof that a region is valid.

Reuse prepared projection overlays while the source, target geometry generation,
direction and occurrence frame remain unchanged. Invalidate all relevant data
when any of those inputs changes. Ordinary hover, selection, inspection, pan,
zoom and tab switching must not invoke OCCT. Record scene publication and kernel
operation counts as well as elapsed time on representative targets.

## Persisted scope and approval gate

A Projection definition needs its output mode, ordinary input Sketch ownership,
target face references, direction mode/reference and reversal, plus explicit
region intent for Surface output. Calculated native output and its reference
bindings remain inside the existing native document. There are no required
sidecar files or private revision caches.

Adding this required definition is a native-contract extension. The authorized
schema is recorded below. The release gate includes an inventory of start
templates and factory native assets, recoverable backups, and isolated copies
of the protected Toyota project. Private originals must not be overwritten and
legacy migration branches must not be added. Update and verify affected factory
assets with the changed native implementation before release.

## Acceptance matrix and implementation order

First establish the agreed native scope and independent geometry fixtures.
Implement the common projection calculation and Sketch output, then Split and
Surface on that same result. Complete the shared GUI/inspection lifecycle and
the persisted asset/template gates before publishing the feature.

| Contract | Required representative variants |
| --- | --- |
| Geometry | Plane, cylinder, cone, sphere and doubly curved native face; translated/rotated Sketch and Body; lines, arcs, ellipses and splines |
| Direction | Sketch normal, reversal, explicit axis/edge, multiple hits, tangency, zero-distance start and no hit |
| Target domain | Trimmed face, holes, connected faces, disconnected selection, periodic seam, boundary overlap and ambiguous hits |
| Outputs | Open/closed contours, multiple regions, nested loops; valid boundary-assisted open partition and rejected non-partition |
| Split invariants | Independent before/after volume, area, centroid and material-space checks, preserved unaffected topology |
| Dependencies | Curve references, edge-treatment supports, Surface Thicken/Shell and downstream region references after source edits |
| Transactions | Repeated edits, no-op counters, failed OK without document mutation, Cancel, Undo/Redo, native reopen and cold regeneration |
| GUI | Actual View/Tree clicks, source dimension editing, target replacement/clear, independent inspection, resize, both themes and five languages |
| Ownership | Active editable Body, rollback boundary, nested active Part occurrence and explicit read-only external sources where supported |

Source dimension editing must also satisfy the mandatory Sketcher matrix:
applicable dimension kinds and driving/reference/lock states, repeated edits and
drags, independent equations, selection orders, native/external supports and
dependent trimming. A successful initial solve is insufficient. Report each
unverified variant and platform explicitly.

## Approved implementation scope — 2026-11-20

The user explicitly authorized the native-file changes required by Projection
on 2026-11-20. This approval covers the definition and derived reference data
below, template/factory verification and investigations on isolated copies of
the protected Toyota project. It does not authorize changing shared placement
solving or overwriting private project originals.

Projection extends the existing `FeatureParameters.type` discriminator with
`projection`. The ordinary `sketch_id`, Sketch ownership and container placement
remain authoritative. A required `projection` object belongs only to that type:

| Field | Definition |
| --- | --- |
| `output` | `curves`, `split` or `surface`; independent of solid/Thin output settings |
| `targets` | Selected face references using existing owner/key/instance-path identity and the existing explicit external-target snapshot where applicable |
| `direction` | `sketch_normal` or `reference` |
| `direction_reference` | Required for reference direction; original axis/straight-edge identity, with its retained directed orientation |
| `reverse` | Boolean reversal of the resolved direction |
| `regions` | Explicit retained region intents for Surface output, bound to target and source-curve ancestry; inactive choices survive switching to another output |

An input-field display label is presentation, never identity. A region pick may
carry its point in the target's local frame for initial classification, but the
calculated boundary-parent key is authoritative on regeneration. A lost key is
an unresolved dependency, not permission to select a nearby patch. Output edge,
vertex and partition identities use the existing native reference archive;
no external cache is required to reopen the result.

The extension must not reinterpret existing Feature kinds or silently turn
Projection into an extrusion. Existing native documents without Projection do
not require a new field. Their established format discriminators and stored
definitions stay unchanged unless implementation evidence demonstrates a
separate necessary change. This conditional definition is not a legacy-format
migration path. An older executable must reject an unknown Projection type.

Before release, inventory committed native assets, verify unchanged asset
definitions and regenerate/save/reopen every affected asset with the changed
implementation. Regenerate the current start templates and exercise new
documents through the GUI, including active editable context. Keep private
inventories, recoverable backups and test copies outside published artifacts.
The previously recorded unsupported Toyota formats remain a verification gap;
the extension does not add a legacy reader or establish their compatibility.
