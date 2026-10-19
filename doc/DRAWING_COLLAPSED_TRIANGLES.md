# Collapsed pole triangles in drawing projection

A native cap-nut crown exposed a short false line near its pole in exported
shaded drawings. The BRep, blind end wall, independent volume and ordinary
3D shading were valid. The cached mesh contained a triangle with two distinct
indices at exactly the same geometric pole position. Its three-dimensional
area was exactly zero.

`project_drawing_edges` omitted that triangle from depth occlusion but still
classified its zero projected determinant as back-facing while collecting
silhouette adjacency. Its valid front-facing neighbour then appeared to meet
a back face. This generated an approximately 2 mm internal contour despite
there being no material boundary there.

The projection now skips exactly collapsed 3D triangles before both occlusion
and silhouette classification. It does not use a new size threshold, discard
genuine edge-on triangles, change mesh precision, modify native topology or
replace measurement/reference geometry. The correction is shared by Windows
and Linux; Linux execution remains unverified on this host.

The drawing regression compares all four real boundaries before and after
adding a duplicate-pole triangle at three scales and two coordinate offsets.
Existing partial occlusion, grazing facets, cylindrical silhouettes and all
standard view orientations remain covered by the same drawing suite. A native
cap-nut gate additionally checks that its oblique projection has no internal
contour near the crown pole, alongside actual solid, sphere, blind bore,
volume/mass and constrained-dragging checks. The drawing suite and all 21
staged cap-nut variants pass these checks on Windows. All 21 native raster
thumbnails were regenerated; the M6 crown was visually inspected and the
previous internal stroke is absent. Image, PDF and DXF export and drawing-view
command regressions also pass (four tests). The initial sandbox run prevented
atomic native file replacement in two tests; rerunning those same tests with
ordinary host filesystem access passed without code changes.

There are no new or changed user-visible strings in this correction.
