# Feature relief from a target surface — 2026-11-23

The approved scope is recorded in
[the design and native-storage agreement](../FEATURE_SURFACE_RELIEF_DESIGN.md).
This implementation adds **From target surface** beside a solid Extrusion's
Up To Correction. It retains only the layer between the original target and
that same target translated by the signed path correction. It is not a normal
offset or cylindrical wrapping. Add/Subtract remains authoritative; Assembly
Features remain subtract-only.

## Implementation boundaries

The shared native target resolver prepares one target chain. One profile prism
is drafted, then clipped against the two limits. Positive and negative corrections
retain their authored original/corrected cap roles. Symmetric sides produce two
separate layers, without material bridging back to the Sketch. Both limits use
the same source identities and retained-side choices.

The optional enabled flag lives in the existing Feature side `up_to` record;
the disabled encoding and native format version are unchanged. The calculation
fingerprint includes the enabled mode. No sidecars, migration path or shared
container-placement change is introduced.

The existing exact profile grouping path now also handles disconnected linear
loops, including rectangles combined with circular holes and separate islands.
The ordinary circle/linear composition must not append a circle a second time
after that shared grouping has already consumed it.

The Assembly exact-preview calculation now restores the native-face status of
ordered targets supplied by the Assembly resolver before collecting sources.
The generic Part carrier represents unresolved external planar faces as datum
fallbacks. Treating those as real datums skipped the native source and changed
the meaning of persisted retained-side choices. The actual native face domain
and orientation are now used for both the private preview and calculated tool;
no authored side choice or shared placement solver is changed.

## Windows verification

Native C++ Release, MSVC 19.51, Qt 6.11.0, OCCT 8.0.1. Evidence is retained locally
under `build/surface-relief-20261123` and
`build/fillet-radius-replay-20261123`; generated private models are not factory
assets or required application data.

- Kernel matrix: 36 relief cases, including both directions and correction signs,
  planar and near cylindrical supports, convex/concave ordered limits and draft.
  Independent area/section integrals verify volume; boundary checks reject
  material extending back to the Sketch or to the cylinder's far side.
  All 76 exact preview comparisons and 872 existing ordered-limit checks pass.
- Native definitions: 39 serialization/validation checks and 68 analytical
  single-face wire checks pass. Ordinary disabled definitions preserve their
  existing encoding; signed zero and reference choices remain covered.
- Native Part: 24 successive signed correction edits cover reversed/symmetric
  directions, holes and islands, zero-thickness rejection, unchanged OK,
  Undo/Redo and cold native reopening. Exact preview is one private native
  operation; changed OK performs one Body calculation. Unchanged OK and
  Undo/Redo perform none. Family members retain the mode and signed correction.
- Reference loss: deleting the actual target retains the failed definition,
  numeric placement and owned Sketch frame. Native saving/reopening and
  Undo/Redo retain the unresolved state and restore the source correctly.
- Assembly relief: subtraction affects the selected occurrence only, preserves
  the source Part, follows its occurrence placement, survives native reopening
  and cold regeneration, and restores the previous result on Undo. A fresh
  private preview of the reopened Assembly stays between the independently
  calculated original and corrected plane-chain heights.
- All 117 native ordered Feature checks pass, covering independent target sets,
  corrections, reversal, failed-definition repair, cold regeneration, and the
  mixed Assembly Up To/Through All cutter and its persisted preview wire.
  The invalid same-side fixture separately checks the parallel-plane planner
  guard and the native rejection of the tilted chain. Reversing the two source
  lists carries their authored retained-side choices with them.
- GUI: the real shared Feature dialog passes toggle/edit, signed correction,
  bounded cyan preview, one rollback scene, Cancel, changed/unchanged OK,
  native saving and Undo/Redo. The full ordered-target GUI check also passes
  real View picking, row inspection and failed-definition repair.
- Ordinary profile command, external circular profile (192 cases plus 16 exact
  trim/source cases), closed spline extrusion and Family suites pass.
- All five translation catalogs and runtime translations pass, including
  checkbox visibility and its label/tooltip after language changes.

The final four synthetic cylindrical relief calculations took 25.0, 20.6, 29.5
and 32.6 ms on this Windows host. These timings exclude target construction and
subsequent physical analysis. They are observations for the small fixture, not
a promised speed for user models or a comparison with ordinary Up To.

## Native assets and remaining scope

The inventory contains 1,210 tracked native assets and no existing Feature-side
`up_to` records affected by this optional flag. All six Part/Skeleton/Assembly
start templates were regenerated and reopened with the changed native code on
isolated copies; their SHA-256 values are identical to the tracked templates.
Actual New Document GUI creation, active editing context, commands and localized
options pass for millimetres and inches in all five languages.

All 172 protected project copies retain verified source/backup hashes. The
earlier protected-project audit records unsupported legacy files that also fail
in published Windows 2026112302; this work does not claim those files have been
converted or repaired. No original private project or custom library was replaced.

Linux execution remains unverified. The C++ implementation is shared and the
dependency pins remain aligned. Cylindrical wrapping, its native schema and
seam/anchor behavior remain a separate future scope. This local development
change is not a new published portable release.
