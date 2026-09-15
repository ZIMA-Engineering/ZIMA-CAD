# Dimensions, manufacturing tolerances, and limited motion

## Purpose

One dimension model supports parametric control, limited Sketcher motion, and Assembly
component motion. Manufacturing tolerances and motion limits are separate properties
and must not affect each other. The same principle applies to linear, angular,
radius, and diameter dimensions.

## Data model

A dimension contains:

- `nominal_value`: editable nominal value also determining current geometry position;
- `range_enabled`: whether motion limits are enabled;
- `lower_limit`: absolute lower bound in the dimension's coordinate system;
- `upper_limit`: absolute upper bound in the same system;
- separate manufacturing-tolerance metadata.

Limits are not deviations from nominal. All three numerical values share the same
absolute zero and units.

```text
Nominal value:             100 mm
Lower limit:               90 mm
Upper limit:              110 mm
Nominal value after move:  105 mm
```

Both bounds may be positive or both negative, for example `20 .. 30 mm` or `-30 .. -20 mm`.

## Rules

With range enabled:

```text
lower_limit <= nominal_value <= upper_limit
```

Invalid/reversed bounds must not be saved. Editing nominal value directly changes
geometry position without shifting absolute limits.

View displays one dimension, not a second range dimension that could show a confusing
extra zero beside a piston mate, for example.

## GUI and editing

Dimension Properties contains nominal value, motion-limit enablement, and lower/upper
bounds. All three numerical fields have arrows, allow whole-value replacement, and
use document decimal precision from Settings. No separate Current Value field is
shown. `OK` validates, calculates, saves, and closes; `Cancel` discards changes.

Initially enabling a range fills bounds between zero and nominal: `0 .. 20` for
`20`, or `-20 .. 0` for `-20`.

Inline dimension editing and current-displacement changes in Component Properties
cannot exceed persisted limits. Component Properties controls stop at bounds;
Dimension Properties rejects invalid confirmation.

## Sketcher solver

A dimension without a range adds the ordinary `measured_value = nominal_value`
equation. An unlocked ranged dimension leaves that degree of freedom movable but
constrains current value to absolute limits during input and dragging. On release,
geometry stays at the last valid position.

## Assemblies

An Assembly mate's `nominal_value` is the editable displacement/angle used by the
solver. The mate exclusively owns motion data. Component Properties **Current
Displacement / Angle** and Dimension Properties **Nominal Value** edit the same
value. The component table has no separate allowed-range column; limits are edited
in Dimension Properties. `dimension_styles` stores only appearance and manufacturing tolerance.

## Manufacturing tolerance

Manufacturing tolerance is separate drawing/manufacturing data. It does not enter
the solver, constrain dragging, change current position, or automatically convert
to/from motion limits.

Tolerance units follow dimension kind: linear/radius/diameter use document length
units; angular tolerance uses degrees and shows `°` in View.

## Persistence rules

Dialogs, viewer, Sketcher, and Assembly share one data model. Loading restores current
position deterministically. Old relative-range modes are unsupported; format rules
prohibit adding migration branches.
