# Solid straightening

Status: specification and command icons prepared; modeling commands are not yet
implemented. This document does not describe a released capability.

## Confirmed scope

The Part Modeling commands are **Straighten** and **Restore shape** (Czech UI:
**Narovnat** and **Obnovit tvar**). They retain the authored feature history so
formed and straight states can be used in Family Tables and drawings.

Inputs are additive solid revolutions and sweeps, including helical sweeps,
with a constant cross-section. Sheet metal, surfaces, subtractive features and
variable cross-sections are outside the requested scope. Unsupported geometry
must not be silently replaced by a constant initial section.

The output of straightening is an extrusion of the original cross-section.
Its length is the length of the trajectory of the cross-section's area centroid,
multiplied by a positive dimensionless coefficient. The reference is the section
centroid trajectory, not the centroid of the complete solid or necessarily the
authored guide curve. Cross-section dimensions remain unchanged.

- `1.0` preserves the centroid-trajectory length.
- `0.9` shortens it by ten percent.
- `1.1` lengthens it by ten percent.

This coefficient is a length multiplier, not a sheet-metal neutral-axis K-factor.
Restore shape returns to the original authored geometry; repeated state changes
must not accumulate coefficient multiplications.

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

The treatment of later cuts, holes, fillets and other body modifications is
awaiting clarification: a clean stock extrusion and a deformation preserving
those modifications are different geometric operations.

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
