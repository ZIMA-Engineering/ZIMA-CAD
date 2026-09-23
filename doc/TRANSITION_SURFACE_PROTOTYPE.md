# Editable transition surface prototype

Status, 2026-09-23: the surface study now also supplies a native **Sheet
transition** Part feature. The standalone study remains a separate,
zero-thickness geometry experiment; its SVG/DXF output is not a production blank.

## Native Sheet transition command

In the Sheet Metal workspace choose **Sheet transition** in an active editable
Body. The command creates one ordinary placed history container. Its main Origin
owns the first Sketch; a second nested Origin owns the other Sketch. The second
Origin has XYZ translation and rotation relative to the main Origin. Moving the
main container moves both profiles together. The shared placement controls offer
numeric correction, position and FRONT/TOP references, whole-Origin selection
and inspection without changing the common placement solver.

Two **SKETCH** buttons open the existing Sketcher and return to the same pending
properties window. The initial example contains a semicircle R80 and a rounded
half-rectangle 200 wide by 80 deep (full rectangle depth 160), corner R20,
separated by 150 mm. Both corner strips start with four facets. Thickness and
K-factor use the Part sheet defaults; the initial inside radius equals thickness.
Sketch dimensions are seeded, and double-clicking the container exposes its
Sketch dimensions and relative Origin distances in the View. These values remain
editable through the common dimension editor. The tree nests both owned Sketches
under their respective Origins; their actions reopen the same editor.

The first Sketch accepts a semicircle or two connected quarter-circle arcs. The
second accepts an open rounded half-rectangle: three straight sides and two equal
tangent quarter-circle corners. Native Sketch corner-fillet records are supported;
construction geometry is ignored. Sketch geometry defines exterior dimensions.

The shared creation/editing window specifies thickness, positive inside bend
radius, K-factor and independent right/left corner facet counts. Both Sketches,
their frames and the wire preview remain transient until OK. OK calculates and
commits one history container; Cancel restores unchanged input/history.
Middle-button double-click over the View also confirms. Preview changes use ZIMA
geometry only and never calculate an OCCT body.

The corner profiles describe tangent exterior geometry. A finite number of
planar facets approximates the curved boundaries; it does not preserve an exact
circular rim. Increasing the facet count reduces this deviation. The geometric
study below documents the boundary-deviation calculation and representative
values.

Planar panels are offset inward from the exterior envelope. Each nonzero
dihedral crease is replaced with a finite-radius bend, with outer radius
`inside radius + thickness`. Panel setback is `outer radius * tan(angle/2)`;
development uses `angle * (inside radius + K * thickness)`. Authored section
frames are preserved when constructing the bend loft. Each panel and bend has
its own material chart beneath the single owning feature, so existing Unbend
and Bend Back commands can select the whole transition. Calculated bend axes
follow the inner-skin convention and are exposed through Drawing annotation
sources. The existing sheet DXF command extracts the developed contour.

The native file stores both owned Sketches, the second Origin identity and
relative frame, placement references and all transition parameters.
Generated semantic keys include authored source-curve ancestry before kernel
calculation. No required sidecars are introduced. The feature consumes the existing
container-placement contract without changing its solver.

Current limits are deliberate: only the stated circular half-profiles are
accepted, and only orientations for which both corner strips and connecting
panels pass developability, planarity and intersection checks. Arbitrary
two-axis tilt or twist is **not** guaranteed. Incompatible input is rejected,
not replaced with a stretched loft. Reversed bends and radii that consume an
entire panel are rejected. Press-brake tool access, forming sequence and
optional cutting notches remain outside this command's verification.

Native transaction tests cover creation, unchanged OK, invalid-edit atomicity,
save/reopen, cold regeneration, Undo/Redo, selection of the owning feature for
Unbend/Bend Back, DXF contour extraction and Drawing bend-axis state. The GUI
contract covers the toolbar icon, both SKETCH buttons, numeric and whole-Origin
placement, relative distance editing, the internal properties window,
MMB confirmation, Undo/Redo and edit Cancel. Five-language catalog and actual
dialog translation checks cover cs/en/de/fr/ru. Kernel tests inspect one valid
connected solid in folded, directly developed and native state-transformed
forms. Geometry checks also cover a material cut through Unbend/Bend Back and
positive and negative compatible tilts with a non-default K-factor. Regression
coverage includes existing 2D/3D sweeps, sheet-state operations and chained
twisted sheet.

Final Windows verification: native command transactions (including parameter
edits and source-Sketch movement) passed in 11.08 s for the complete test;
the native GUI creation/edit/state workflow passed in 13.18 s. These are test
suite runtimes, not timings for a single user operation. New Part/Assembly GUI
creation from the refreshed factory templates and all five translation checks
also passed. The template writer produced no content changes. The local
`zima-cad.bat` launcher uses the verified C++ executable; no new portable release
has been published for this feature.

## Standalone surface study

### Agreed dimension and bend-side contract

Sketches specify the **external dimensions of the finished transition**. There
is no internal-dimension input mode. Native finite thickness extends inward
while preserving the authored exterior. Bend allowance must be calculated on
the neutral layer, not by treating the exterior surface as that layer.

Bend axes belong to the **inner skin**, following the existing
`sheet_material::bend_lines` contract in
`cpp/modules/kernel_api/include/zima/kernel/sheet_material.hpp` and
[Sheet state development](SHEET_STATE_DEVELOPMENT.md#bend-lines-in-developed-material).
That implementation respects thickness sign and inherited orientation and
places the axis halfway through the developed curved span. Production
integration must reuse that rule, including persisted side identity. The
zero-thickness study has no distinct inner/outer skin yet: its lines are ideal
crease locations, not verified finite-thickness inner-skin positions. No shared
side or placement behavior was changed for this experiment.

### Bend-axis presentation and study export

`transition_pattern.*` builds one derived pattern for screen and export.
Boundary edges occur once and remain continuous outlines. Shared edges produce
one finite bend axis only when their signed dihedral angle has magnitude above
1e-8 radians; this is a presentation tolerance, not normalization of the source
angle or material side. Coplanar panel boundaries are omitted. Regeneration
rebuilds the axes from the committed model and its common unfolded edges.

The study's 2D panel uses thin yellow dash-dot axes and thicker white outlines.
It supports zoom and drag navigation. The export button writes geometric-study
DXF or SVG. DXF uses millimetres, finite LINE entities, separate OUTLINE and
BEND_AXES layers, CONTINUOUS/CENTER linetypes and 0.50/0.18 mm lineweights.
SVG uses the same geometry and widths. Bend axes are not notches or cutting
geometry. These exports lack bend allowance and are not manufacturing blanks.
The native Part feature instead uses the existing Drawing bend-axis annotations.
The standalone study exporter does not replace native Drawing/PDF export.

## Inputs, means, outputs

Inputs are two quarter-circle or principal-axis quarter-ellipse Sketch curves,
two coordinate frames, and a requested number of planar faces. The first frame
places the entire model; the second is relative to the first. Editing the second
frame preserves the authored coordinates and identities of both Sketches.

The intended manufacturing means is a press brake. The present geometric means
is a strip of planar quadrilaterals joined along straight fold lines. The output
is an editable surface model and its length-preserving geometric unfolding.
The native command adds finite thickness, bend radius and bend allowance as
described above. Tool access and forming sequence remain unverified.

The native implementation is in `cpp/research/transition_surface.*` and
`transition_model.*`. It uses real `sketcher::Sketch` objects and curve IDs, with
resolved mathematical frames. It does not alter the shared container placement
solver. Native integration consumes the existing placed Sketch frames.

## Construction and limitations

Write the two arcs as A(u) and B(v), each with parameters from zero to pi/2.
A regular ruled developable surface requires

    (B(v) - A(u)) dot (A'(u) cross B'(v)) = 0.

For each u the implementation solves the resulting trigonometric equation in v.
It requires one increasing correspondence covering both specified endpoints,
with regular, consistently oriented tangent planes. Branch checks use 513
samples; they are numerical checks, not an interval proof for all parameters.
Ambiguous, singular and incompatible cases are rejected. Rejection does not
prove that no other segmented construction could connect those profiles.

Joining arbitrary samples of the two arcs would generally produce warped
quadrilaterals. Instead, the implementation samples common tangent planes.
Adjacent planes intersect in straight folds; their intersections with the two
profile planes define polygonal boundaries. Each face is therefore planar.
Endpoint planes are tangent at the requested endpoints. N planes produce N
faces and N-1 internal folds; endpoint faces span half parameter intervals.

Each quadrilateral unfolds by rigid geometry. Verification checks all six
pairwise vertex distances per face, area preservation, convexity and planarity,
flat-pattern overlap and intersections between nonadjacent spatial faces.
The ideal conic boundaries are approximated, not preserved exactly. A sampled
two-sided Hausdorff distance bracket reports boundary deviation with a sampling
error bound. The default 0.1 mm sampling step is not a 0.1 mm model tolerance.
Excessive work budgets fail explicitly instead of silently skipping this check.

An independent scale check illustrates why facet count matters: a circumscribed
R100 quarter-circle with four tangent planes deviates by
100*(sec(pi/12)-1) = approximately 3.528 mm. The numerical bracket contains that
analytic value. A tested ellipse pair (20 x 30 to 100 x 60 semiaxes, 150 mm
separation) has upper deviation bounds of 8.473 mm with four faces and 2.221 mm
with eight. These are coarse shapes, not precision approximations.

## Editable study application

Build target `zima-transition-study` with tests enabled. From the repository
working directory, run `build/cpp-windows-release/zima-transition-study.exe`.
The ordinary `zima-cad.bat` entry point and published product remain unchanged.

The study displays the spatial surface and its geometric unfolding together.
Its internal Properties window edits the first circle radius, second ellipse
semiaxes, facet count, and both frames' translations and Euler rotations
(Rz * Ry * Rx). OK calculates and commits; Cancel discards pending inputs.
Middle-button double-click over the View confirms through the shared mechanism.
An invalid calculation leaves the editor open and the committed model intact.

The GUI offers the original corner study and a complete half-transition study.
It is not a complete Sketch drawing or reference-selection workflow. The corner
study uses two native Sketches; the half-transition currently uses explicit
dimensions to derive its profiles. Model state is editable in
memory; there is no native Part feature serialization or study save command.
The model tests exercise the existing Sketch serialization separately. Optional
OBJ files written by the model test are derived inspection artifacts only, not
required document dependencies or editable native documents.

The application uses the existing native viewer and shared internal properties
window, without OCCT calculation. The mesh has no invented persistent topology
references. UI strings are supplied in cs, en, de, fr and ru.

## Verification

Run CTest with `-R "transition|translation" --output-on-failure` in the native
build directory. All five selected tests passed on Windows on 2026-09-23:

- `zima_cpp_transition_surface_tests`: analytic circle check, ellipse refinement,
  rigid transform invariance, compatible tilted ellipses, independent radius,
  offset and tilt variations, invalid and incompatible inputs.
- `zima_cpp_transition_model_tests`: actual Sketch edits and IDs, frame ownership,
  Sketch serialization, facet edits and invalid-result mesh suppression.
- `zima_cpp_transition_half_tests`: complete half-transition continuity in 3D
  and in the common unfolding, area and metric preservation, asymmetric face
  counts, radius/offset variations, compatible tilts and invalid-input rejection.
- `zima_cpp_transition_study_ui_contract`: all five languages, internal window,
  Cancel, middle-button OK over View, reopening and invalid-input rejection.
- `zima_cpp_translations_contract`: existing catalog and localization contracts.

The complete surface regression executable took approximately 0.10 seconds in
CTest, including process startup. This is a suite measurement, not a per-feature
latency guarantee. The GUI contract took approximately 3 seconds for all five
languages. Screenshots are produced under `build/transition-model` when that
artifact directory exists.

## Native container verification (2026-09-23)

The Windows development executable is built through the existing CMake target
and remains reachable with `zima-cad.bat`. Targeted native checks cover:

- Owned-profile creation, parameter edits, both Sketcher round trips and Cancel.
- Main-container rigid translation/rotation with invariant volume, relative
  second-Origin translation and a compatible 15-degree tilt.
- Numeric/reference placement, whole active-Body Origin entry through the Tree,
  saved references, reopened dimensions, middle-button OK and Undo/Redo.
- Native save/reopen and cold regeneration; finite-radius solid construction,
  Unbend/Bend Back, intervening cuts, DXF contours and Drawing bend axes.
- Existing 2D/3D Sweep and sheet-state command regressions.
- Catalog/source coverage and rendered dialog strings in all five languages.

Factory Part, Skeleton and Assembly templates were rewritten with the current
serializer. The template GUI contract creates new documents and verifies their
initial active editing context and enabled commands. Empty templates retain the
same serialized contents because transition data belongs to authored features.

## Research references

- Pottmann et al., [Freeform surfaces from single curved panels](https://www.geometrie.tuwien.ac.at/geom/ig/publications/oldpub/2008/panels08/paper_docs/panels.pdf): developable surface and planar-panel principles.
- Autodesk, [Lofted flange](https://help.autodesk.com/cloudhelp/2020/ENU/Inventor-Help/files/GUID-B6271A15-FD68-4672-9461-C86F8D55DA87.htm): press-brake facets, radii and convergence controls.

The remaining geometric research gate is to extend the valid geometric family.
The native command above already supplies owned Sketches, thickness and real bends. Arbitrary independent
plane orientation is accepted as input but is not guaranteed to yield a valid
patch with this restricted common-tangent construction.

The intended half-transition input is one semicircular/elliptical arc and an
open half-rectangle profile with rounded corners, in separate Sketch frames.
The user should not have to add sampling points or split a semicircle manually.
Logical subarcs and segmentation stations should be derived from the authored
curves and editable facet counts. Two quarter arcs are a natural symmetric
decomposition, not a guaranteed correspondence for tilted or asymmetric input.
## Complete half-transition experiment

`transition_half.*` now assembles both corner strips, a central triangular panel
and two end triangular panels. The semicircle is in the first frame. The second
frame carries the upper half of a rounded rectangle: x spans -width/2 to width/2,
and y spans zero to depth/2. Depth therefore denotes the full rectangle depth.
The corner radius must be strictly less than both half-dimensions; zero-length
straight portions are outside this initial implementation.

N right-corner faces and M left-corner faces give N+M+3 surface panels. Four
interfaces with the triangular panels are coplanar in the aligned tangent
construction. Internal panel boundaries therefore do not all denote nonzero
bends. No extra diagonal folds are introduced inside quadrilateral faces.

One sequential rigid unfolding traverses the entire strip; the corner flat
patterns are not independently positioned approximations. Tests compare both
ends of every shared edge in 3D and 2D, every face's pairwise distances, and
total area. Convex-polygon overlap tests reject positive-area overlap in the
unfolding. Spatial tests reject strict edge/face crossings and coplanar area
overlap. Isolated non-coplanar tangential contacts are not certified, so this
remains a surface experiment rather than a manufacturing collision guarantee.

For the complete boundary, the maximum corner deviation upper bound remains
valid because the straight pieces are exact. A nonzero lower bound from an
individual patch does not necessarily survive union with other patches, so the
complete model intentionally reports a zero lower bound.

The GUI exposes radius, rectangle width/full depth, corner radius, independent
left/right face counts, and the two coordinate frames. Changes remain pending
until OK, and invalid inputs preserve the committed result. Profiles are
generated automatically; users do not author auxiliary segmentation points.
This standalone study remains separate from the native Part feature described above.

Compatible tilted examples include R80, rectangle 200 x 160, corner R20,
separation 150, with second-frame Y rotations -0.5, 0.2 and 0.5 radians. Here
the upper rectangle tangent has y=R; this special geometric relationship allows
the prescribed quarter endpoints to correspond. It is not evidence that every
tilt or offset is supported. An incompatible tilted and offset input is also
tested and rejected. The five-language GUI test exercises a 30-degree tilt at
240 mm separation, checks invalid corner-radius rejection and reopens editing.

The complete half-transition regression suite took approximately 0.17 seconds
including process startup on the development Windows host. No OCCT calculation
or shared placement-contract change was required.
