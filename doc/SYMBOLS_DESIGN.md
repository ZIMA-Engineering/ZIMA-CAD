# Shared symbol design proposal

Status: initial native library definition and C++ reader/writer implemented.
Interactive symbol authoring, insertion and title-block binding are not yet
implemented. The remaining sections describe the intended interaction design.

The native library extension is `.symz` (symbol). Version 1 stores UTF-8 JSON
with format identifier `zima.symbol`, millimetre units and an embedded native
Sketch. The file is self-contained. See the first library asset under
`config/symbols/drawing-conventions/ZE-PROJECTION-METHOD.symz`.

The agreed creation entry is **Symbol (.symz)** before **Part** in the shared
New Document dialog. Confirming the name should open an empty symbol Sketcher.
Open must edit an existing `.symz` using the same Sketcher contract. Start with
fixed geometry and text; parameterized content can follow after the basic
authoring and insertion round-trip is verified.

## Inputs, means and outputs

Inputs are user-authored symbol geometry, labels, parameter values, attachment
references and the destination Part, Assembly or Drawing. Existing means are
the native Sketcher, persisted ZIMA reference identities and annotation painters.
The output should be one reusable definition with occurrence-specific placement
and values in any of the three document workspaces.

Define geometry once in a local 2D plane, using the native Sketcher for authoring
from the local origin `[0, 0]`, the default insertion point.
The definition should contain curves, text, parameters and named insertion/leader
attachment points. A Drawing instance places that plane on paper; a Part or
Assembly instance places it in a chosen spatial plane. This is annotation data,
not solid geometry, and must not require OCCT calculation to display or select.

The user requires one shared nominal size in millimetres for Drawing, Part and
Assembly placement, with no separate enlargement merely because the symbol is
in 3D. Camera zoom and output scaling must preserve that intent. Spatial plane orientation and optional
camera-facing presentation need an explicit interaction contract rather than
being inferred from the current camera. References in Assemblies must retain
exact occurrence paths and original-object ownership.

Embed each inserted definition and its instance parameters in its owning native
document. Library files are creation sources, never required sidecars for
reopening an existing model or Drawing. Updating a library definition must not
silently change existing inserted symbols. Define an explicit update operation
later if needed.

## First candidate: title-block projection symbol

The first requested candidate is the projection-method symbol in the title block.
Use one definition with first-angle and third-angle variants, selected from the
owning Drawing's projection setting. The title-block occurrence needs no leader.
The first library asset contains both variants in one Sketch, with named
visibility groups for their outlines and finite axes. Each pair of circles
shares a single centre point on the horizontal axis. The insertion point is
`[0, 0]`; the outer circle diameter is 6 mm. This is the chosen initial library
size, not a claim that the standard mandates that size. The `variant_source`
declaration records `drawing.projection_method`; consuming this declaration in
the title block remains future work. Existing title blocks are unchanged.

The arrangement was checked against figures 4 and 7 of
[ISO 5456-2:1996](https://www.iso.org/standard/11502.html): with a cone widening
to the right, first-angle places the circles on the right and third-angle on
the left. Both variants contain two concentric circles and two centre lines.

`zima_symbol_library_tool` reads the actual asset, validates group references,
checks native save/reopen, rejects invalid references and unsupported versions,
checks both arrangements and creates a preview. Use `--generate` only when
explicitly rebuilding the factory definition. The library API is independent
of Part/Assembly placement and makes no OCCT calls.

Separate the reusable definition (Sketcher geometry, text fields and parameters)
from each inserted occurrence (values, placement, attachment and optional leader
with or without an arrow). Named variants should control geometry-group visibility
within one definition rather than duplicate entire sketches. Insertion is intended
for Sketcher, Part, Assembly and Drawing. Occurrence edits must not modify other
occurrences or the library source.

Verification must exercise both projection settings, save/reopen and title-block
output, including a check that the displayed variant matches the owning Drawing.

Open decisions include zoom/output behavior, variable
text expressions, attachment selection, leader handling and whether model-owned
symbols are offered through Drawing Show/Erase. Projection-method symbols can
eventually consume the Drawing projection setting instead of fixed geometry.
These items are not authorized as an implementation by this design note alone.
