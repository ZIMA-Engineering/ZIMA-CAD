# Ordinary Sketch text anchors — 2026-10-19

## Scope and lifecycle

Ordinary Part and Assembly Sketch text previously omitted the native insertion
point already used by title blocks. New text now creates that point through the
shared C++ Sketch implementation. The existing solver updates the anchor and
translates every glyph contour. The ordinary viewer already offers this point
for picking, orange hover and cyan selection; no separate viewer drag path is
introduced. Ordinary text points are auxiliary; existing template point roles
remain unchanged.

Text Properties preserves the point identity. An unchanged definition returns
without creating geometry. An actual edit of existing current-format text that
has no optional anchor creates its point; opening or unchanged confirmation does
not modify it. Conflicting position edits are atomic. Content, height, angle,
alignment and Unicode/multiline changes retain an existing constrained point.
All errors from this dialog now use the shared translator. The new constrained
position message is present in the Qt section of all five language catalogs.

Point merging rewires text incidence. Curve removal and trimming retain points
still owned by text. Explicit point deletion removes attached text; deleting only
text preserves the separate point and its independent dimensions. Symbol Explode
maps a source text point in ordinary coordinates as well as template coordinates.

The existing optional `SketchText.anchor_point_id` is reused. No native schema,
format version, required persisted contract, start template or factory asset is
changed. Custom user libraries remain untouched. The shared container-placement
contract and dependency pins are unchanged.

The GUI matrix also exposed roundoff-only drag commits when an aligned distance
and ordinate fully constrained a point. The locked-distance projection fallback
now checks the existing solver rank and rejects a globally fully constrained
baseline before projection. Ordinary successful drags do not perform this extra
rank calculation. No equation tolerance or geometric precision was changed.

## Regression matrix

| Area | Cases and independent assertions |
| --- | --- |
| Point-pair dimensions | 324 cases: plain point, symbol and text; XY/XZ/YZ; both sides and participant orders; X/Y/aligned distance; reference, unlocked driver and locked driver; three successive edits/drags; fixed support, incidence and independent distance equations; native reopen |
| Point-to-line | 72 cases: symbol and text; native/read-only external supports; horizontal, 37-degree and vertical; both contact sides; all three dimension states; three successive edits/slides; independent normal distance, unchanged external sources and native reopen |
| Angular/symmetric | 144 cases: plain point, symbol and text; three-point, point-line and four-point angles, symmetric coordinate; both orders/sides; all three states; three successive edits/drags; independent cosine or symmetric-distance equations |
| Fully constrained aligned distance | 12 additional configurations: point/symbol/text, both sides/orders; three successive value edits and three rejected drag targets after each edit; exact unchanged serialized data |
| Identity and ownership | External-point binding and atomic rejection; both merge orders; actual retained-interval trimming and connected-curve deletion; explicit anchor deletion; retained independent dimensions after annotation deletion |
| Native history/persistence | Part and Assembly annotation mutations, Undo/Redo and native save/reopen; stable anchor identity and contour incidence |
| Real font and modeling | OSIFONT alignment/rotation/flip, Unicode and multiline text; every glyph vertex translated after three moves; dependent holed-glyph extrusion retains volume and translates its 3D bounds; fully constrained rejection; unchanged Properties; explicit edit of unanchored current-format text |
| GUI entry and manipulation | Part and Assembly Text OK/Cancel, Undo/Redo, center/middle alignment checked against actual glyph bounds; three real text-point mouse drags per document; ordinary picker identity and every contour vertex; existing symbol creation/drag/decomposition and parent profile Cancel |
| GUI dimensions | Nine symbol and twelve text configurations: X/Y/aligned distance; reference, unlocked driver, locked driver, plus fully constrained text; three successive inline edits/mouse attempts; permitted movement, independent equations, stable glyph incidence, rejected fully constrained gestures and drag Undo/Redo |
| GUI localization | Actual constrained Text Properties rejection in cs/en/de/fr/ru; exact catalog message, unchanged fixed Sketch; existing symbol menus and Properties translation checks |

The counted native matrix contains 540 cases plus the explicitly listed
additional checks. Symbol Explode retains its 929 factory/synthetic combinations.

## Results and limits

The final native build passed. Eight CTest suites passed in 74.39 seconds:
translations, Sketcher contracts, symbol Explode, symbol anchors, title-block
Family, Sketch text commands, real text geometry and Drawing contracts. The
540-case anchor suite including the additional fixed configurations took 1.02
seconds; the text geometry suite including repeated dependent extrusion took
9.37 seconds. These are local observations, not platform-independent timing
guarantees.

The template and New Document GUI contracts passed in 32.95 and 41.74 seconds.
The final five-language symbol/Text Properties GUI contract passed in 18.17
seconds after moving the new source-text key into the Qt catalog section.
The final Sketch symbol controls GUI matrix passed in 111.77 seconds, including
center/middle text placement in Part/Assembly and all 21 linear dimension
configurations. Screenshots are retained locally under `Projects/test` as
`part-text-anchor.png` and `assembly-text-anchor.png`; assertions also check the
native saved geometry, rather than relying only on those images.

This is finite coverage: GUI dimension tests use XY, one participant order and
positive coordinates; the native matrix supplies both sides/orders and all three
planes for linear point-pair dimensions. Angular, symmetric and line-support
matrices use XY. Angular external supports, every redundant constraint
combination, negative/zero coordinate text edits and every dependent modeling
command are not separately repeated here. The existing Sketcher contract suite
remains an additional regression gate. Factory title-block Family and template
GUI checks cover existing template behavior; this is not a new factory conversion.

All execution evidence is Windows-native with Qt 6.11.0 and OCCT 8.0.0. Linux
compilation, GUI interaction and runtime acceptance remain unverified. The public
2026101906 release is immutable; this follow-up updates the local development
build and source, without replacing that published package.
