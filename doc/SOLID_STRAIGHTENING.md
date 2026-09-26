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
geometry tests. The treatment of other cuts and body modifications has not been
generalized from the confirmed hole and fillet behavior.

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
