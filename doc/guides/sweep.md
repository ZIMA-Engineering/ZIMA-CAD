# Sweep

## Before you start

Activate an editable Part Body. Choose **2D sweep**, **3D sweep** or
**Helix Sweep** according to the path you need; they are separate tools.

## Steps: 2D Sweep

1. Place the container and select its **XY / XZ / YZ** path plane.
2. Open **Path sketch…**. Draw one connected open path or a supported closed chain. An open path can start away from the Origin; its authored direction determines the first station. Finish the Sketch.
3. Open the first station's **Sketch** and define a nonempty profile. Its plane
   is perpendicular to the path tangent.
4. Add later profiles where needed. An empty later station inherits the preceding profile; the closing station of a closed path uses the first profile. A different supplied profile creates a transition.
5. Select **Body**, **Thin feature** or **Face**, review the material operation and
   confirm **OK**. **Cancel** discards pending edits.

## What to check

A thin-wall result needs a thickness and side; a surface result is uncapped and additive only. In 3D sweep,
use **Point order** to match profile boundaries. Helix Sweep requires its base
circle/start point, radial guide, pitch and cross-section.

**Custom precision** changes body approximation tolerance in millimetres, not
just display quality. Preserve the accuracy required by your model.

See [2D Sweep](../SWEEP_2D.md), [3D Sweep](../3D_CURVE_AND_SWEEP.md) and
[Helical Sweep](../HELICAL_SWEEP.md).
