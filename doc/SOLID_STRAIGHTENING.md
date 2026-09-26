# Solid straightening

Status: specification and command icons prepared; modeling commands are not yet
implemented. This document does not describe a released capability.

## Confirmed scope

The Part Modeling commands are **Straighten** and **Restore shape** (Czech UI:
**Narovnat** and **Obnovit tvar**). They retain the authored feature history so
formed and straight states can be used in Family Tables and drawings.

Inputs are additive solid revolutions and sweeps, including helical sweeps,
with a constant cross-section. Sheet metal, surfaces and variable cross-sections
are outside the requested scope. Subtractive features are not independently
straightenable sources; supported holes in a source solid must nevertheless be
preserved as described below. Unsupported geometry must not be silently replaced
by a constant initial section.

The base straightened solid is an extrusion of the original cross-section,
with supported subsequent modifications retained. Its length is the length of
the trajectory of the cross-section's area centroid,
multiplied by a positive dimensionless coefficient. The reference is the section
centroid trajectory, not the centroid of the complete solid or necessarily the
authored guide curve. Cross-section dimensions remain unchanged.

- `1.0` preserves the centroid-trajectory length.
- `0.9` shortens it by ten percent.
- `1.1` lengthens it by ten percent.

This coefficient is a length multiplier, not a sheet-metal neutral-axis K-factor.
Restore shape restores the authored curved trajectory while retaining supported
modifications added in the straight state. Repeated state changes must not
accumulate coefficient multiplications or replace the current history with an
old geometry snapshot.

Representative inputs are L sections, rectangular hollow sections and wire.
Straightening preserves the complete section, including inner and outer corner
radii, wall thickness and enclosed voids. The coefficient changes only the
developed length, not the cross-section dimensions.

## Attached downstream features

The user explicitly requires downstream features attached to the final curved
end face to follow that face when the source is straightened. Their positions
and orientations must follow the corresponding target end frame. Restore shape
must carry them back through the same dependency chain. This applies to features
already present before a state change and to supported additions made in the
straightened state; neither case may leave detached geometry at its old world
coordinates.

Consume the existing placement and explicit dependency-regeneration contracts.
Retain the exact source identity, selected geometry side, orientation references
and authored offsets. Do not copy world-space positions into persistent placement
or silently redirect a reference to a nearby face. An unavailable or ambiguous
correspondence must reject the change without partially committing it.

Verification must include a source bend/sweep, a feature attached to its end face,
and another feature attached to that child. Check both state directions, changed
length coefficient, nonzero offsets, opposite sides at zero offset, save/reopen,
Undo/Redo and Family Table regeneration. The existing shared placement solver is
protected; this requirement does not authorize changing its general contract.

## Verification targets

For a circular centroid trajectory of radius `R` and angular travel `theta` in
radians, the unscaled length is `abs(R * theta)`. A constant-radius helical
centroid trajectory with radius `R`, pitch `p` per turn and `n` turns has length
`abs(n) * sqrt((2 * pi * R)^2 + p^2)`. These provide independent checks of the
eventual kernel implementation. They apply to the centroid trajectory itself;
an offset section centroid must not be mistaken for the guide-curve origin.

The implementation must verify exclusions, coefficient editing, restoration,
history suppression, Undo/Redo, native save/reopen, Family Table evaluation and
drawing source geometry. It must preserve source identities and use the existing
in-application properties and reference-entry contracts. Geometry calculation
belongs to explicit confirmation or regeneration.

## Preservation of subsequent modifications

The user confirmed on 2026-09-26 that fillets must survive straightening and
holes in straight portions must be preserved. Producing clean stock while
discarding those features does not satisfy the command's requirements.

A hole intersecting a curved portion is explicitly unsupported in the first
version. The user approved rejecting that operation with an explanation. The
operation must leave the document and its calculated geometry unchanged; it must
not remove the hole, move it speculatively, or partially commit other sources.
Classifying only a hole's center is insufficient: its complete removed-material
extent must lie in a supported straight portion. A hole crossing a straight/curved
boundary is therefore unsupported as well.

Fillets already present in the source profile remain part of that profile.
Subsequent Fillet history operations require their corresponding edges to be
resolved on the straightened geometry and their authored radii and contour
direction to be retained. A nonlinear deformation of a finished fillet surface
alone does not prove that the specified radius survived. Where an edge vanishes
or a requested radius becomes impossible, reject the state change rather than
silently suppress the fillet or reduce its radius.

This preservation also applies in the opposite direction. The user explicitly
confirmed the sequence `Source -> Straighten -> Fillet -> Restore shape`: the
new Fillet must remain on the restored curved source. Reapply the authored
treatment to the corresponding segment/edges in the target state, preserving
its parameters and ancestry. Restoring only the geometry cached before
Straighten would incorrectly discard that later edit.

The current implementation exposes the required starting information through
`HistoryContainer::edge_treatment` and `kernel::FilletRequest`: persisted edge
references, radius values, the constant/linear mode, reversal and contour-start
vertices. Sweeps and extrusions use different semantic edge roles. A later
implementation must explicitly map their ancestry; copying an old edge key or
matching edges by OCCT traversal order is not a valid correspondence.

Additional acceptance cases are required before this capability is complete:

| Case | Required result |
| --- | --- |
| Rounded source cross-section | Preserve the complete section, including its arcs |
| Fillet applied after the source solid | Retain the authored fillet definition on corresponding edges |
| Hole wholly inside a straight portion | Preserve the hole with that portion in the straightened state |
| Hole intersecting a curved portion or transition | Explain the unsupported case and commit no changes |
| Fillet invalid after straightening or coefficient change | Report the failed modification and commit no changes |
| Restore shape after an accepted state change | Recover the authored shape with its holes and fillets |
| New Fillet after Straighten, followed by Restore shape | Retain the new fillet on the corresponding restored segment |
| Save/reopen, Undo/Redo and Family Table state changes | Preserve the same modifications and reference identities |

These are implementation and verification requirements, not results of completed
command tests. The treatment of other cuts and body modifications has not been
generalized from the confirmed hole and fillet behavior.

## Kernel feasibility check (2026-09-26)

A disposable native C++ probe exercised the existing kernel operations without
adding state commands or changing document data. It manually constructed both
the formed and straight definitions, so it does not verify automatic conversion,
state persistence, GUI behavior or source-reference remapping.

- An L section with 20 mm legs and 4 mm thickness has area 144 mm2. Its centroid
  was calculated independently from two rectangles. For a quarter-turn about the
  test axis, straight extrusion volumes at coefficients 0.9, 1.0 and 1.1 agreed
  with `area * centroid-trajectory length * coefficient` within 0.00001 mm3.
- The same persisted longitudinal-edge reference and a 1 mm Fillet request
  evaluated successfully on both the extruded and revolved versions. This proves
  reuse for that specific original-edge case, not a general mapping for every
  generated, split or treatment-created edge.
- A two-segment L Sweep with a straight leg followed by a quarter-circle and its
  manually straightened counterpart retained the hole in the straight leg.
  Each subtraction removed `4 * pi` mm3 within 0.0001 mm3. The same subsequent
  Fillet request also evaluated on both definitions.
- A 20 x 20 mm hollow section with a 16 x 16 mm void retained the independently
  expected 144 mm2 section area. A circular wire of radius 4 mm retained its
  expected section area. Their straight extrusion volumes agreed with the
  independent length/area calculations within 0.00001 mm3.

The probe did not test automatic exclusion of curved-region holes, arbitrary
Sweep transport, Helical Sweep, multiple bodies, or newly created Fillet topology
referenced through a state container. Those remain required implementation work.

The user identified a general spherical surface as a subsequent Part feature.
It is outside this straightening implementation and outside the 2026092604
Drawing maintenance release.

## Pending approval: reference evaluation across a state boundary

The existing `calculate_part_reference_state()` in
`cpp/modules/workspace/src/model_calculation.cpp` supplies calculated original
reference geometry to `PartDocument::resolve_constructions()`. Existing sheet
state operations publish their own derived topology while earlier original
reference packets remain immutable. Reusing that mechanism alone would leave a
feature attached to the original curved end face at its original frame.

The proposed extension is a history-boundary-specific reference view for solid
state operations. It would resolve the same persisted source/semantic identity
to the corresponding straight or restored end frame before the existing placement
solver evaluates downstream containers. Descendants would then follow through
the existing dependency passes. Authored source geometry and reference keys,
side choices, offsets, input controls and the placement solver equations must
remain unchanged. The reference view must be derived from native history and
calculated reference data, including after a cold reopen; opening Properties
must not invoke OCCT.

This changes the geometry supplied to the protected shared placement contract.
Explicit user approval was requested on 2026-09-26 and is still pending at this
checkpoint. No shared placement or reference-resolution implementation has been
edited for this proposal. Approval must precede those edits. The downstream
chain, side, zero-offset, cold-reopen and Undo/Redo checks listed above remain
acceptance gates, together with unchanged placement behavior in documents that
contain no solid state operation.

## Command icons

`resources/icons/straighten.svg` depicts a curved profile becoming a straight
profile. `resources/icons/restore-shape.svg` depicts the reverse transformation.
They are registered as `:/zima/icons/straighten.svg` and
`:/zima/icons/restore-shape.svg` in the application's Qt resources for subsequent
command and history-tree integration.

Both use the existing 24-unit SVG grid, rounded 1.75-unit strokes and azure
`#00D1FF` destination geometry. Source geometry uses `currentColor`, resolved by
the existing shared icon renderer from the Qt palette. No new palette handling
or fixed light/dark background is introduced.

The icons have been rendered with Qt SVG at toolbar size and enlarged on light
and dark backgrounds. The Windows application builds with the new resources.
Localization review: the assets add no action labels, tooltips or other runtime
UI strings; their English SVG titles are asset metadata. The existing five-language
translation coverage and catalog contract passes. Command text still requires
five-language localization when the commands are implemented.
