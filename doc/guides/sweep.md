# Sweep

## Before you start

Activate an editable Part Body. Choose **2D Sweep**, **3D Sweep** or
**Helical Sweep** according to the path you need; they are separate tools.

## Steps: 2D Sweep

1. Place the container and select its **XY / XZ / YZ** path plane.
2. Open **Path Sketch**. Draw an open path starting at its Origin, or a supported
   closed connected chain. Finish the Sketch.
3. Open the first station's **Sketch** and define a nonempty profile. Its plane
   is perpendicular to the path tangent.
4. Add later profiles only where needed. An empty later station inherits the
   preceding profile; another supplied profile creates a transition.
5. Select **Solid**, **Thin** or **Surface**, review the material operation and
   confirm **OK**. **Cancel** discards pending edits.

## What to check

Thin needs a thickness and side; Surface is uncapped and Add-only. In 3D Sweep,
use **Point Order** to match profile boundaries. Helical Sweep requires its base
circle/start point, radial guide, pitch and cross-section.

**Custom precision** changes body approximation tolerance in millimetres, not
just display quality. Preserve the accuracy required by your model.

See [2D Sweep](../SWEEP_2D.md), [3D Sweep](../3D_CURVE_AND_SWEEP.md) and
[Helical Sweep](../HELICAL_SWEEP.md).
