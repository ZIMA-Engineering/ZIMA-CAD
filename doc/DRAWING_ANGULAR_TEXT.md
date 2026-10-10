# Drawing angular-dimension text clearance

The Drawing label follows its dimension arc, including when dragged beyond the
measured sector. Its readable tangent determines which side is above the line.
The baseline is offset along that tangent's upper normal. An unconditional
radial offset is incorrect after reversing a lower-half tangent for readability:
it can place the glyphs across the dimension line.

The convention was checked against ISO 129-1:2018, clauses 4.1.1, 5.7.2 and
5.7.3, and Figures 24 and 25. Dimension values are readable from the bottom or
right, above and parallel to the dimension line; an angular line can continue
outside the measured angle. The consulted standard is available from
[Universidad Carlos III de Madrid](https://www.uc3m.es/pruebasacceso/media/pruebasacceso/doc/archivo/doc_dtaapd-norma-une/dibujo-tecnico-ap.arpldis-norma.pdf).

The shared Drawing presentation considers the actual font bounds, including
tolerances and a basic-dimension frame, and the already generated arc and its
continuation. It moves the baseline only as far as necessary to clear those
curved strokes. Painting, masking and picking consume the same presentation.
The measured value, references, authored label bearing, arc grips and arrow
choices are retained. The ordinary 3D/Sketcher shelf presentation is unchanged.
No body calculation is involved and no new UI text is introduced.

## Windows verification

`zima_cpp_dimension_layout_contract_tests` exercises 384 combinations: two zooms,
two font sizes, three signed sweeps, eight orientations, inside/outside text,
and plain/basic presentation with a long tolerance label. It independently
checks readable orientation, all text-mask corners above the tangent and no
intersection with the actual stroked arc. Existing layout, occurrence, grip
and persistence contracts also pass. The proof is
`build/drawing-angular-text-clearance.png`.

The actual Drawing GUI measurement harness verifies angle creation, preview and
Cancel, middle-button confirmation, all three grips, four successive text
placements above/below the vertex and inside/outside the angle, unchanged
references and measured value, native save/reopen, missing-reference repair,
PDF and DXF export. Evidence is `build/import-followup/drawing-angular-final-ui.log`
and `build/drawing-angle-position-{0..3}.png`. The placement screenshots were
produced by actual mouse interaction on the dark Drawing canvas. The analytical
proof uses black strokes on white output paper. Native Linux GUI acceptance is
pending in the agreed post-reboot session.
