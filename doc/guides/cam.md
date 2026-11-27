# CAM

## Before you start

Prepare calculated stock and target Bodies in a Part, plus fixture Bodies
where required. Deactivate the current Body to reach **Create CAM**.
For pocket or contour milling, prepare a preceding native profile Sketch.

## Steps

1. Choose **Create CAM**. Select stock, target and fixtures, then place the
   setup's work frame.
2. Open **Machine** and **CAM tools**. Review travel limits, tool geometry,
   cutting conditions and the work-origin offset. When needed, define contact
   and jaw-top Sketches and their oriented sides in **Clamping Sketches**.
3. In **Machining operations**, choose **Add facing**, **Add pocket**, **Add contour**
   or **Add manual operation**. For a pocket or contour, click the **Machining profile** reference field and select a preceding Sketch in the Tree or View.
   Set the tool, top and bottom levels, depth increments, allowances, clearance
   and tool-path direction. Set **Step over** for a pocket; for a contour,
   choose **Inside** or **Outside**.
   Review operation order. The editor's **OK** returns changes to the setup
   draft; confirm the setup's **OK** to commit them.
4. Choose **Calculate CAM**. Inspect the **Generic NC program**, associated
   tool motion and calculated remaining stock.
5. Review collision and clamping reports. Use **Export generic NC text…** only
   with its stated generic-program meaning.

## What to check

Yellow paths represent cutting feed; dashed red paths represent rapid motion.
Explicit calculation checks machine limits and simulates swept material; stepping
through the result does not recalculate material.

Pocket and contour milling is three-axis, 2.5D machining of closed native Sketch
regions parallel to the setup XY plane, including islands and disconnected loops.
It requires a cylindrical flat cutting portion. Entry uses a cutting plunge and
transfers retract to clearance. **Path tolerance** bounds the approximation of
noncircular curves; lines and circular arcs retain analytic paths.

Clamping Sketches define oriented exclusion/contact regions and jaw-top checks.
Actual fixture Bodies remain additional obstacles. A cutter or shank intersection
with a fixture or exclusion volume is a collision. Removal below the jaw top and
loss of contact generate distinct warnings; remaining geometric contact does not
prove sufficient holding force. Colliding results cannot become stock for a later setup.

Setup **Cancel** discards pending edits. Reopening a current result does not
repeat simulation. Manual operations support linear and XY circular motion.
Controller postprocessors, turning, 4/5-axis motion, adaptive/ramped entry,
automatic workholding-library placement and arbitrary-solid tool conversion
remain unavailable. Generic NC export is not controller-ready machine code.

See [CAM workflow](../USER_MANUAL.md#first-stage-cam-workflow) and
[current profile milling scope](../CAM_BODY_DESIGN.md#profile-milling-stage-2026-11-26).
