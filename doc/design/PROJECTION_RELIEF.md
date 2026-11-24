# Projection relief

The agreed command order below Twist is Project sketch, Project relief, Wrap
sketch, Wrap relief. Extrusion retains its existing material operations.

Project sketch retains Sketch, Split and Surface outputs. Project relief uses
the same placed Feature, source Sketch, target reference controls and editing
lifecycle. Its signed Correction adds material outward for positive values and
removes material inward for negative values, following the target solid's side.
The shared placement solver is unchanged.

## Approved persistence scope

The native Projection definition has a Relief output, with a positive height
stored only for that output. Reuse the existing Feature material operation for
the correction sign, as Wrap relief does. Part definitions embedded in Assembly
documents carry the same contract; extensions remain `.prtz` and `.asmz`.
Existing Sketch, Split and Surface definitions do not acquire required fields.
No migration path or required external cache is introduced.

## Implementation and acceptance

Reuse the native directional projection and retained-region partition, followed
by the established surface thickening and Boolean paths. Preserve source,
region and geometry-side identities. Reject incomplete, ambiguous or invalid
regions without committing a partial solid. External read-only targets cannot
be changed by relief. Draft changes publish lightweight preview only; unchanged
OK, hover and inspection do not calculate geometry or create Undo transactions.

Verify positive and negative correction on planar and curved targets, reversed
surface parameterization and bore sides, holes, successive edits, source changes,
save/reopen, regeneration and Undo/Redo. Exercise actual GUI command creation,
reference entry, preview, unchanged OK, Cancel and both command names in all five
languages. State remaining unsupported or unverified target variants explicitly.

Regenerate all six start Part/Skeleton/Assembly templates with the changed native
runtime and create new documents through the GUI to verify active contexts and
commands. Recheck the tracked native library inventory: the current audit has no
active Projection definitions. Recalculate any affected factory asset if that
inventory changes; verify every factory asset can load with the release runtime.
Use copies of user models and preserve originals. Windows and Linux share the
implementation and committed assets; Linux verification requires its own host.

## Native preparation

Projection tool edges with existing target parameter curves are checked before
partitioning. Pure 3D tools retain the splitter's native parameter-curve
construction, also used by Wrap Split. Region boundaries are joined across parameter seams while physical
and authored tool boundaries remain protected. Complementary closed-surface
regions retain distinct identities through their oriented authored boundaries.
Region seeds use nearest surface parameters, avoiding the spherical antipodal
stationary point returned by a bounded 3D face classification.

Relief prepares a deep copy of each selected region before joining its boundary
curves. This isolation is necessary because native unification can increase
tolerances on reused seam vertices even in safe mode. Parameter curves are
refined against the unchanged native 3D curves, and edge/vertex tolerances are
certified from actual correspondence and incident endpoint distances. Only the
disposable copy may have those tolerances reduced. The usual native thickening,
solid-validity, self-interference and ancestry checks still accept or reject the
result; no check is bypassed and no display mesh supplies modeling geometry.

Status: functional and persistence scope explicitly approved by the user on
2026-11-24 under AGENTS.md File-format compatibility. GUI creation and editing
pass for all five outputs, including both relief signs. Six native cases cover
cone and equatorial/polar sphere relief in both directions, with independent
volume checks and seam-side regeneration. Eight document cases cover planar
profiles with and without holes, outer cylinders and inner bores in both
directions, repeated height edits, no-op, Undo/Redo, save/reopen and cold
regeneration. Target edits also verify cache invalidation. This is finite
coverage, not a guarantee for every trimmed support or arbitrary contour.
All six templates have been regenerated and reopened (byte-identical
because no template uses the new output), and actual GUI document creation
passes. All 1,190 tracked native factory assets pass the current-reader audit.
