# Wrap on cylindrical, conical and spherical faces

Status: implemented with finite Windows verification. The user approved the
native definition on 2026-11-24, including affected factory assets and start
templates. Follow-up acceptance is recorded in
[Wrap fixes](verification/20261124-wrap-fixes.md).
The existing From target surface relief remains a separate, working operation.

## User-visible scope

- Use **Wrap** as the English command name and **Nabalit** in Czech, as selected
  by the user. There is no inverse Unwrap command: the original planar Sketch
  remains editable. This follows the established Wrap terminology used by
  SOLIDWORKS and Onshape. SOLIDWORKS uses
  [Nabalení in its Czech help](https://help.solidworks.com/2023/czech/SolidWorks/sldworks/HIDD_DVE_SURF_WRAPPING_SKETCH.htm);
  [Onshape documents Wrap](https://cad.onshape.com/help/Content/Sketch/wrap.htm).
- Add Wrapping as a type of the existing Feature, with its existing owned Sketch,
  Properties window, placement contract and history lifecycle.
  Expose **Wrap sketch / Nabalit skicu** and **Wrap relief / Nabalit reliéf**
  as separate commands below Project sketch / Project relief, and as two
  choices in the downward-opening Feature type list. The first creates
  **Sketch**, **Split** or **Surface** results and embedded artwork. Split
  partitions the target face without changing material volume. Relief uses a
  signed normal **Correction / Korekce**: positive adds outward and negative
  removes inward, respecting the target solid’s material side.
- Support a cylindrical, conical or spherical face. The placed Feature Origin
  defines the anchor; do not introduce a separate independently picked anchor.
  Signed Sketch coordinates wrap in both directions from that anchor;
  a centered inscription can extend equally to either side. Axial distances and
  circumferential arc lengths are preserved on the reference cylinder.
- Preserve the authored side and direction, including inner and outer faces and
  reversed cylinder axes. Kernel UV seams must not change the selected anchor.
  Support crossing a kernel UV seam. Validate overlaps and singularities in the
  actual mapped geometry, rather than using the kernel seam as an input limit.
- Wrap closed vector contours, including text holes and separate islands, as
  raised or recessed geometry using the existing Add/Subtract ownership rules.
  Open contours produce surface curves rather than a fabricated solid.
- Map an embedded Sketch reference image using the same surface map as contours.
  Pixels do not become exact engineering contours or relief automatically.
  Keep its aspect, opacity, orientation and original embedded bytes.
- The user removed inverse unwrapping and explicitly added cones and spheres
  on 2026-11-24. Sheet-metal unfolding is outside this command.

## Analytic maps

Cylinders use circumferential arc length and axial length. Cones use an isometric
development into a circular sector. With cone half-angle `a`, anchor slant length
`s0` and source coordinates `(u,v)`, the developed radius is
`s = hypot(u,s0+v)` and the cone angle is `atan2(u,s0+v)/sin(a)`. The apex is a
singularity; the developed chart cut must not be confused with OCCT's UV seam.
See the cone development in
[Whitman College's calculus text](https://www.whitman.edu/mathematics/calculus_online/section09.10.html).

Spheres use the exponential (geodesic) map from the tangent frame at the anchor.
For radius `R`, normal `n`, tangent basis `(ex,ey)` and `rho = hypot(u,v)`, the
mapped unit direction is `n*cos(rho/R) + (u*ex+v*ey)*sin(rho/R)/rho`. The zero
limit is evaluated analytically. Distances from the anchor are preserved;
transverse lengths scale by `sin(rho/R)/(rho/R)`. A sphere cannot preserve all
planar lengths and areas. The chart ends at the antipode, where its Jacobian
loses rank. This is a deliberate application design, not a claim about other
CAD products. The equation is given in section 7.1 of Sommer, Fletcher and
Pennec's [Introduction to differential and Riemannian geometry](https://www-sop.inria.fr/asclepios/cours/Peyresq_2019/chapter1.pdf).

Contours and image pixels share these maps. Preserve analytic geometry where
the mapping permits it; other mapped curves require tolerance-controlled native
curves and independent error checks, never a display polyline substituted for
the engineering curve. Source tangent orientation, both signed directions and
normal material offsets must remain consistent in preview and calculation.

## Native definition and ownership

Extend the Feature type mapping with `wrapping`. Its definition stores the
stable target face reference, direction choices, and normal height/depth for
material results. The existing Feature Origin supplies the anchor and tangent
orientation. Reuse the existing
owned Sketch identifier and shared placed-container state. Store explicit source
identities for generated surface curves, walls, rim edges and caps, based on
Sketch curves/points and Feature ownership, never OCCT enumeration order.

All required state and embedded image data stay inside the existing `.prtz` or
`.asmz` native file. There are no required sidecars. FEM videos are unrelated
exports and never become part of these files. Existing non-wrapping Feature
definitions keep their current representation and execution path; do not add
legacy adapters. The shared placement solver and reference-entry semantics are
consumed without modification.

## Calculation and interaction

Publish the rollback input once when opening Properties. The reference table
uses the shared reference cell, separate inspection eye and compact controls.
Read target analytic parameters from the calculated reference packet for a
lightweight cyan wire/image preview. Do not calculate bodies during picking,
parameter preview, ordinary selection, tab changes or unchanged OK.

Explicit OK and Regenerate perform native geometry construction and validation.
Cancel restores the unchanged model and view. Lost targets retain the last valid
frame, remain repairable and do not authorize material calculation. Source changes
invalidate the mapped result. Family suppression and Undo/Redo retain all source
and side identities.

## Conversion and verification gate

Before implementation, inventory the tracked start templates, FORM definitions
and other native factory assets against the current Feature encoding. Regenerate
Part, Skeleton and Assembly start templates with the changed native code and run
the actual new-document GUI activation/command checks. Recalculate, save and
reopen every factory asset affected by the new required contract; compare authored
history, role Bodies, references and geometry. Package the exact committed assets.
Unchanged assets need a native-load verification, not a version-number rewrite.

Test user models and the protected Toyota project only on verified copies.
Preserve original documents and custom libraries. Cover cylinder/cone seams, cone
apices, sphere poles and antipodes, independently measured sphere distortion, both
directions, centered wrapping, inner/outer faces, holes, images, source edits,
missing/replaced references, creation/editing/Cancel, unchanged OK, Family,
Undo/Redo, save/reopen and explicit regeneration. Check equations, topology
identity and operation counts independently of screenshots. Verify all five
UI languages. Windows execution and Linux execution are reported separately.

## Development checkpoint (2026-11-24)

Naming research is complete. The shared `CylindricalWrap` analytic map and
`zima_cpp_cylindrical_wrap_tests` pass on Windows: signed circumferential and
axial lengths, both direction choices, inward/outward radial offsets, rotated
frames, seam-independent anchors and continuous inverse mapping are covered.
The completed command now uses this map and the corresponding cone/sphere maps.

The pre-change inventory at `ab2ed163340a37f6a89118564fb1ac3904be6b64` contains
1,074 tracked native Part/Assembly/library assets, including six start templates.
Existing Feature definitions use Sketch, Modeling and Axis; none uses Wrapping.
The path/hash inventory is retained privately in
`build/fem-20261124/wrapping-input-audit.json`. This is an inventory check only;
the changed-runtime native load and template GUI gates have subsequently passed.

The implementation now includes the native Feature discriminator and payload,
shared cylinder/cone/sphere maps, native curve/surface/relief construction,
embedded curved images and the shared Feature Properties dialog. There is no
Unwrap command. The original Sketch remains the editable planar definition.

Development verification passes. The analytic map tests cover signed directions,
metrics, inverse coordinates, singularities and overlapping image domains.
The native regression matrix includes all three supports and output types,
both relief sides, image-only output, open curves, holes and downstream cuts.
The GUI checks exercise real target picking, reference clearing/inspection,
preview operation counts, native saving, unchanged OK, Cancel and Undo/Redo.
The finite native/GUI matrix, factory/template results and explicit coverage
limits are recorded in [Wrap verification](verification/20261124-wrap.md).
Packaged-runtime checks remain required before publication.
