# Remaining fastener authoring - 2026-10-20

Windows 2026102002 is published and immutable. The following work is authorized
for a subsequent version. This is a completion checklist, not a claim that these
families already contain accepted native models.

| Family | Current state | Remaining work |
| --- | --- | --- |
| Historical wing nuts | Six pressed M3-M10 Parts installed; four private cast studies remain | The accepted pressed path moves the axisymmetric hub seam out of the wing plane. The exact failing case, 210 successive edits, M3/M10 mouse edits, six native Drawings and five-language pages passed; see [acceptance](PRESSED_WING_NUT_LIBRARY.md). Earlier private candidates remain rejected. Cast perimeter/root transitions, M16 source-radius disagreement and M24 indicative mass remain outstanding. |
| Non-metallic insert lock nuts | Thirteen ISO 10511:2012 Parts, M3-M36, installed | User-selected single steel Part with a blue symbolic insert Body; both use steel density. All thirteen physical checks and 143 successive edit/persistence checks passed. M3/M36 native GUI scenarios and six native Drawings passed. Five-language tables retain the separate archived CSN dimensions. See [acceptance scope](INSERT_LOCK_NUT_LIBRARY.md); committed-source/release acceptance remains pending with the other families. |
| Formed-nose tab washers | Thirteen formed M4-M27 native Flat/Bend/Flat Parts installed beside the preserved flat family | Original CSN 02 1753 (1962) preview reviewed; M4/M22 outside diameters differ from the archived shared table. All 273 successive edits, original identities, actual M4/M27 GUI edits, unfolding, thirteen seating Assemblies and six framed dimensioned Drawings passed. See [acceptance](FORMED_NOSE_WASHER_LIBRARY.md); committed-source/release acceptance remains pending. |
| Toothed and serrated washers | Archived dimensions/illustration; no factory models | Review the three distinct historical constructions and available ISO correspondence; author actual teeth, slits and formed geometry without substituting an ordinary plain washer; verify dependent edits and drawings. |
| Spherical wheel washers | Compressed native bores 18.5, 20.5 and 22.5 mm installed | Two additions passed 30 successive edits, actual GUI, independent volume/mass and six dimensioned Drawings; the original 20.5 assets are unchanged. The 12.5 nominal meridian has negative outside wall thickness, while 14.5 leaves only 0.072 mm before selected corner rounding; those two interpretations remain unresolved. See [finite acceptance](verification/20261020-shaped-fasteners.md). |
| Two-part locking nuts | Table only | Explicitly excluded by the user on 2026-10-20; do not implement in this follow-up. |
| Native Linux execution | Shared source and factory data; Windows verification exists | Native Linux build, GUI/regression execution and package acceptance require a Linux host; a Windows layout simulation is not native Linux verification. |

Every installed model must pass explicit Regenerate, independent physical mass,
saved Relations, repeated dimension changes, native save/reopen and Undo/Redo.
Representative actual GUI editing, native axes and associative dimensioned
reference drawings are required. New names, descriptions and tables cover cs,
en, de, fr and ru. Keep custom user libraries and unrelated working changes.

The already installed cap, coupling, flange, spherical/conical wheel, knurled
and weld-nut families are not outstanding model-authoring work. Their finite
verification gaps remain documented in their own records. Known unresolved
source facts must not be disguised by a different density, radius, standard
designation, geometry tolerance or a copied indicative catalog mass.

The lock-nut representation decision was changed explicitly by the user on
2026-10-20: use one steel Part and indicate the insert by color, without an
Assembly or per-Body material extension. Its reported mass is a documented
homogeneous-steel approximation, including the symbolic insert. It is not a
claim of the physical mass of an actual steel/PA6 product.

After accepted installation, update the documentation and translations, commit
and push the reviewed files, then create and verify a new signed Windows release
through the repository distribution pipeline. Do not replace 2026102002 assets
or move its source tag.

## Requested follow-up after nut completion

### Active drawing correction, requested on 2026-10-20

The user requested independently selectable horizontal and vertical strokes of
an end-on hole-axis cross for manual angular, linear and chain dimensions.
Previously the renderer drew the cross while the measurement model retained only
the projected centre of the physical 3D axis. A physical axis viewed end-on must
not silently acquire an arbitrary line direction. The two displayed strokes need
distinct associative identities, retaining the current centre attachment.
The user explicitly approved this extension of persisted Drawing reference
semantics on 2026-10-20. Part/Assembly formats and protected container
placement are outside its scope. Verify both strokes and selection orders,
repeated occurrences, edge/axis combinations, exact hover highlighting, command
Cancel, Undo/Redo, regeneration, visibility, camera and scale changes, and native
save/reopen. References that cease to exist must become unresolved rather than
silently rebinding to another direction. This work has program priority; keep
the fastener completion list active. The implementation and finite Windows
native/GUI checks are complete; see [verification](DRAWING_AXIS_CROSS_DIMENSIONS.md).
Native Linux and release-package acceptance remain pending.

The user also asked about balloon attachment at arbitrary curve locations and
inside a component face. The common measurement picker already has continuous
curve contacts for symbols, but balloon entry does not enable that option.
Face attachment needs exact source-face and occurrence ownership, visible-depth
picking, associative contact persistence and BOM row resolution. The user then
explicitly limited the follow-up to arbitrary curve contacts, after completing
the ongoing work. Implement that smaller extension using the existing curve
contact path. Do not implement face attachment in this task. The curve extension
and native/GUI checks are complete; see
[curve-contact verification](DRAWING_BALLOON_CURVE_CONTACTS.md).

### Corrected private geometry diagnostic

Save As intentionally exports a copy and keeps the source document active.
The initial private hub trace mistakenly continued in that source session.
Its suppression snapshots therefore did not isolate operations, and its modified
private M3 source candidates are not eligible for installation. The preserved
pre-edit exports in `build/pressed-hub-trace-v1` retain the original full histories.
No factory library was edited by that trace.

The corrected `tmp/trace-pressed-hub.py` explicitly checks the source remains
active after Save As, closes it, reopens the exported copy, and verifies the
active absolute path before each mutation. It checks SHA-256 before/after and
after every stage. `build/pressed-hub-trace-guarded-v1/results.json` confirms
both source exports were unchanged. The corrected trace reproduces the geometry
problem: the hub alone has volume 58.556379 mm3, but its Add leaves the failing
wing-only volume unchanged at 112.233575 mm3. The baseline union is 155.257610
mm3. The final through-hole volumes are respectively 109.542644 and 143.859885
mm3. Both failed and baseline final outputs contain one valid solid, so ordinary
BRep validity alone is insufficient to establish an equivalent union.

Changing Boolean fuzz, non-destructive mode, inverted-solid checking, argument
order or regularity flags did not resolve the isolated failing union. A private
analytic-endpoint experiment changed the result into an open shell and was
reverted; it is not a product fix. The independent inspector now reports actual
solid count and material-property state as well as volume. Product geometry and
Save As behavior remain unchanged. Further native authoring must use fresh
candidates and repeat acceptance checks; these private probes are not release
evidence.

On 2026-10-20 the user proposed extending the modeling Feature's **Up To** end
condition to multiple selected faces, including disconnected faces, with a
signed positive/negative correction from the resulting contact. Complete the
current nuts first. This is a deferred feature request, not implemented behavior
and not a reason to alter the ongoing fastener assets.

Before implementation, trace the current Feature endpoint/target collection,
kernel limiting operation, GUI reference-entry controls, persisted end condition
and regeneration paths. Resolve the treatment of gaps, missing intersections,
overlapping or differently oriented supports and which local part of the profile
terminates on which selected face. Reuse the shared reference-entry controls and
preserve the permanent geometry-side identity, including zero correction. Scope
ordinary Extrusion/Revolution and any other applicable Feature modes explicitly.
Read the performance and geometry-side contracts, and obtain the separately
required approval before any protected shared container-placement change or
native persisted-contract extension. Merely consuming existing target lists
does not authorize changing those contracts.

The user clarified and agreed the intended surface ordering on 2026-10-20:
targets form a consecutive chain. The first surface intersects the second,
the second intersects the third, and so on; adjacent intersections determine
the joined limiting shape. This is not a per-ray priority list with fallback
to the next target. Retain selection order, with explicit Up/Down reordering
through the existing shared list controls. Validate adjacency and identify
missing intersections. The retained side of each patch and whether underlying
supports may extend beyond selected face bounds still require explicit design.
After the libraries, finish English documentation and all five translations,
commit and push, then verify and publish the Windows release before beginning
this Feature work. Include subtractive Features limited by shaped supports.

The user also requested considering **Sketch projection onto a face** through
the same Feature interface. Candidate inputs are a source planar Sketch, one or
more target faces and a projection direction (Sketch normal or a selected axis).
For curved supports, the result must be actual associative 3D curves, not a
planar Sketch pretending to lie on a curved surface. Review existing Surface
Feature and 3D-curve paths before adding a mode. Specify clipping to actual face
boundaries, gaps, multiple intersections, misses and tangential singularities;
retain source identities and explicitly reject ambiguous unsupported cases.
Regeneration must follow the source Sketch and target geometry. Splitting a
face or using the curves for a later cut are downstream operations to scope
separately. The interface proposal is recorded; no projection feature has been
implemented or verified yet.

The user reaffirmed retaining the existing Feature principle: source Sketch,
Extrusion or Revolution path, Up To targets, signed correction and chosen result.
Use the existing Feature interaction model when evaluating this extension.
The discussed visibility default is to show the derived result, retain the source
Sketch hidden in the Tree, and expose both in the editing context with ordinary
visibility controls. At zero correction the projected result lies on its target;
nonzero correction offsets the endpoint along the operation path. These are
design intentions to verify, not an implemented promise.
