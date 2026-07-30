# Sketcher Terminology

## Geometry type

The geometry type describes the geometric shape:

- `segment`
- `arc`
- `circle`
- `spline`

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
- **Pomocná geometrie**

Changing the role must not change the underlying geometry type. For example,
an arc remains an `arc` when switched between `profile` and `construction`.
Auxiliary geometry keeps its original finite shape, is displayed as a yellow
dashed line and is excluded from profiles consumed by 3D features.

## Construction line

**Konstrukční čára** is a separate infinite line geometry. It is currently
stored as the legacy geometry type `construction`. Converting a `segment` to
a construction line deliberately changes the geometry type and converting it
back creates an ordinary profile segment.
