# CAM

## Before you start

Prepare calculated stock and target Bodies in a Part, plus fixture Bodies
where required. Deactivate the current Body to reach **Create CAM**.

## Steps

1. Choose **Create CAM**. Select stock, target and fixtures, then place the
   setup's work frame.
2. Open **Machine** and **CAM tools**. Review travel limits, tool geometry,
   cutting conditions and the work-origin offset.
3. In **Machining operations**, add **Facing** or a supported **Manual operation**.
   Review the order and confirm the setup.
4. Choose **Calculate CAM**. Inspect the **Generic NC program**, associated
   tool motion and calculated remaining stock.
5. Review collision and clamping reports. Export **generic NC text** only with
   its stated generic-program meaning.

## What to check

Yellow paths represent cutting feed; dashed red paths represent rapid motion.
Clamping Sketches define oriented exclusion/contact regions and jaw-top checks;
actual fixture Bodies remain additional obstacles. Colliding results cannot
become stock for a later setup.

Setup **Cancel** discards pending edits. Reopening a current result does not
repeat simulation. The initial scope excludes controller postprocessors, pocket
and contour operations, turning and 4/5-axis motion. Generic NC export is not
controller-ready machine code.

See [CAM workflow](../USER_MANUAL.md#first-stage-cam-workflow) and
[CAM scope](../CAM_BODY_DESIGN.md).
