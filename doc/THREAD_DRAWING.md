# Thread representation in Drawings

Ordinary surface and hole contours retain their normal thick line weight.
Technological thread boundaries and silhouettes are an explicit exception: gray
in the interactive Drawing and the sheet's thin black line weight in print/PDF.
Thread classification is independent of tangent-edge visibility and is saved
with the projected edges in the native Drawing document.

An axial view uses one open 280-degree circle at the nearest end of the thread.
The gap occupies the upper-right quadrant of the view; both endpoints extend
five degrees past its center lines. Internal threads use the nominal-diameter
boundary; external threads use the root-diameter boundary. Oblique views retain
the projected thread surface contours. Shaded views retain their surface fill.

Projection recognizes camera-normal circular boundaries from saved viewer data,
without OCCT calls or new model topology. Source owner and occurrence path keep
repeated components separate. Technological thread surfaces do not occlude real
geometry; the real solid still controls hidden-line visibility, including for
the symbolic circle. The same sheet renderer supplies View, PDF and image/DXF
exports. Regenerate existing Drawing views to update their stored projection.

The native thread Drawing test covers the axial and oblique views, blind-hole
occlusion, repeated occurrences, an entrance chamfer, save/reopen, PDF generation
and distinct printed widths for normal and thread contours.
