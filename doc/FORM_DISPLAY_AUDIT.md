# FORM face ancestry and display audit (2026-10-07)

## Scope and evidence

The latest inspected user documents were `Projects/FORM.prtz` and
`Projects/FORM.drwz`, saved at 08:33 on 2026-10-07. The Part has ten calculated
body boundaries and 54 distinct final faces. A read-only snapshot was used
because the user continued editing the documents during the investigation.
Neither user document was rewritten by these display fixes.

Every decoded Boolean face parent exists in the persisted boundaries or
original reference packets. Every final Fillet/Chamfer face parent edge also
exists in those packets. All final source owners resolve to existing history
containers. In particular, later cuts retain the explicit parent relation to
the earlier Fillet faces; they do not transfer those faces to the cutter.

## Container picking and inspection

Ordinary face-based container picking previously attributed a Boolean fragment
to the operation that produced the fragment. A trimmed Fillet could therefore
offer the cutter and highlight its circular profile. Container picking now
follows the stored Boolean face ancestry to its source owner. Explicit display
ownership remains authoritative where it is provided. Face-taking commands
retain the actual operational fragment identity.

Visible treatment boundaries also consume the persisted ancestry of their
adjacent faces. Exactly one adjacent face belonging to a treatment identifies
its visible outer boundary; two faces of that treatment identify an internal
patch join. This augments the explicitly recorded treatment ownership without
querying OCCT or reconstructing missing topology. The same boundary-owner
function serves picking exclusions and the viewer's inspection index.

Native viewer regressions cover nested Boolean parents, original face storage,
Fillet container ownership, occurrence isolation, and the distinction between
an outer treatment boundary and an internal patch join. The current user Part
was additionally opened through the GUI; common mouse picking confirmed the
earlier Fillet, synchronized the Tree and found its actual visible boundary.

## Smooth shading

Vertex normals are now weighted by each triangle's corner angle instead of
the number of incident triangles. Subdividing one part of an uneven fan no
longer biases its normal or creates a false lighting ridge. A native regression
compares the normal before and after unequal fan subdivision independently of
the renderer. Existing planar-face, sharp-boundary, reversed-triangle and
separate-occurrence checks remain in place.

This is a render-only change. Vertex positions, edge samples, face identities,
calculation precision and persisted geometry are unchanged. It does not hide
real edges, remove distinct surface patches, or refine the silhouette mesh.
Cached smooth and edge renders were inspected before and after the change;
no new solid calculation is needed to obtain the new shading.

## Drawing

The inspected sheet specifies thick lines of 0.5 mm and thin lines of 0.25 mm.
Pohled 1 hides tangent edges; Pohled 2 displays them thin. Their cached packets
contain respectively 336 and 196 silhouette segments; none is incorrectly
marked tangent. Both views were rendered using the interactive depth renderer
with dark-background white thick outlines, gray thin transitions, and also
with the light-background palette. The outside outlines were continuous in
the inspected renders. The normal thin-line screen mode intentionally uses
one pixel for both widths; the lineweight preview displays the sheet widths.
The existing tangent settings were preserved.

The drawing controls regression previously assumed Czech default view names
even when the application language was English. Its expected numbered names
now use the active translation. No product UI text was introduced or changed;
the five-language translation contract passed.

## Verification limits

Verification was performed on Windows with the existing shared native build.
Linux was not run. The initial audit had no accessible screenshot; the
follow-up below uses the screenshot subsequently supplied by the user. Very close zooms,
other lighting angles and the exact screenshot may reveal remaining mesh or
patch-boundary effects; the shading change is not a claim that every possible
faceting artifact is eliminated. Printing and vector export were not visually
retested in this audit; the native drawing contract was run.

Diagnostic audit and render logs are under `build/form-diagnostic/`:
`current-ancestry-audit.log`, `current-form-confirmed-render.log`,
`current-display-unit-tests.log`, and `current-display-confirmed-tests.log`.
The final five-test run passed: translations, native viewer, FORM mouse
selection/editing/persistence, drawing view controls, and native drawing.

## Follow-up: supplied screenshot and Extrusion 002

The user subsequently supplied `Snímek obrazovky 2026-10-07 090222.png` from
their Screenshots directory. It shows distinct triangular shading blotches
on a narrow pocket Fillet. The Part saved at 09:01 was inspected separately
from the earlier snapshot.

Angle weighting alone did not fix this case. The triangle-local 0.75 cosine
cutoff selected a different normal fan for different triangles meeting at the
same vertex of one persisted face. An independent audit found 125 such vertices
in the 3,610-triangle final packet. Referenced face fans now use a consistent
hemisphere and all same-face incident angles, independent of the triangle
being drawn. The native GUI diagnostic checks all 2,519 referenced face/position
groups and found no triangle-dependent normals. Zoomed renders of both the
pocket Fillet and circular cut Fillet were visually inspected. Distinct face
and occurrence boundaries remain isolated; unreferenced geometry retains its
existing crease policy. Silhouette polygon resolution is unchanged.

Extrusion 002 also highlighted long prism edges in addition to its circular
authored tool. The original-reference packet deliberately retains Boolean
result edges for later operational selection, but container-wire highlighting
incorrectly included them solely because the Boolean operation owned their
fragment identities. Ordinary authored container wires now exclude these
Boolean result roles, including wrapped solid-state source roles. The original
Extrusion tool wire remains visible. The packet itself and operational edge
selection are unchanged. This common viewer rule applies to other Boolean
containers, not only FORM or this feature ID.

Additional evidence is in `shading-audit.log`, `radius-fan-current-form.log`,
`pocket-radius-detail.log`, and `cutter-highlight-fixed.log` under the diagnostic
directory. Product changes remain confined to shared native viewer code;
test-only render targeting and camera focus help reproduce the user cases.
The final follow-up run passed all four checks: translations, native viewer,
surface placement GUI, and the complete FORM operational GUI regression
(`cutter-radius-final-tests.log`, 60.94 seconds). The current user Part remained
6,460,356 bytes with its 09:01 save timestamp; no native document repair was
needed for these two viewer defects.
