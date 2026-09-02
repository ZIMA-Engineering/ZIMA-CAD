# Sketcher Terminology

## Geometry type

The geometry type describes the geometric shape:

- `segment`
- `arc`
- `circle`
- `spline`

A `circle` references one centre point and owns one scalar `radius`.
The circumference click used while drawing is an input gesture, not a
persistent sketch point. Points on a circle are separate entities connected
by an explicit constraint.

## Point topology and relation markers

A native Sketch point is the only owner of positional X/Y state. Geometry
references stable point IDs. When two native points are made coincident, their
topology is merged into one surviving point ID; this is not a stored
constraint and it has no persistent `C` marker.

Use the relation markers consistently:

- `C` — a native point lies at an arbitrary position on another geometry;
- `K` — a native point is anchored to an exact generated curve keypoint,
  currently the characteristic quarter points of supported curves;
- `T` — two geometries are tangent at the identified contact.

Therefore an ordinary line-to-circle or line-to-Arc contact is `C + T`.
`K + T` is valid only when that same contact is exactly the selected generated
keypoint. The fact that a support is curved does not by itself imply `K`.

In the Czech UI, **Shodnost** means the interaction command. Its result is
either the point-topology merge above or the explicit point-on-geometry `C`
relation, depending on the selected candidate types.

## Geometry role

The geometry role describes how the geometry is used:

- `profile` — regular profile geometry used by features such as Extrude and
  Revolve
- `construction` — auxiliary finite geometry used only for constraints,
  dimensions and sketch construction

Do not use `solid` as a sketch geometry role. In ZIMA-CAD, **solid** is
reserved for a three-dimensional body.

In the Czech user interface, use:

- **Profilová geometrie**
- **Konstrukční geometrie**

Changing the role must not change the underlying geometry type. For example,
an arc remains an `arc` when switched between `profile` and `construction`.
Auxiliary geometry keeps its original finite shape, is displayed as a yellow
dashed line and is excluded from profiles consumed by 3D features.

## Construction line

**Konstrukční čára** is a specific line entity defined by two controlling
points. It is currently stored as the legacy geometry type `construction`.
It participates in constraints and dimensions but is excluded from profile
wires. Revolve uses the first construction line as an axis and mathematically
extends its direction for the rotational operation. Converting a `segment` to
a construction line deliberately changes the geometry type; converting it
back creates an ordinary profile segment.

Do not call an arbitrary arc, circle or spline a construction line. Switching
those entities out of the profile changes only their role and is described in
the Czech UI and documentation as **konstrukční geometrie**.
