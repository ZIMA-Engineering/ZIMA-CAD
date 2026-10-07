# FORM Boolean topology verification

Date: 2026-10-07. Native Windows build, shared C++ implementation, OCCT 8.0.0.

## Cause and change

The original FORM history contains an extrusion, a fillet, a sweep and the
subtracting profile feature named `Vytažení 002`. Boolean face propagation
assigned several source parents to a merged face, making its reference ambiguous.
Disconnected descendants also inherited the same face reference. Missing face
context then caused six edge identities to be reused for twelve distinct edges.

Boolean Add and Cut now complete face references before completing edge references.
Coplanar merged faces retain the oldest contributing original reference, ordered
by document history. Exact disconnected body fragments receive operational
identities derived from their persisted parents and boundary ancestry; these do
not replace the original placement reference. Colliding symmetric fragments are
refined using their named adjacent faces. Unchanged single descendants retain
their references. The oldest-reference contract is recorded in AGENTS.md and
STABLE_TOPOLOGY_NAMING.md.
Unresolved identical ancestry remains ambiguous; enumeration, coordinates and
random identifiers never break ties. Ordinary Add permits connected coplanar
faces to unify across different source references. Opposite material sides are
kept separate, accounting for both face orientation and the plane frame's
handedness. Exact angular-end exceptions and other unification callers retain
their existing behavior. The file schema and pinned kernel version are unchanged.
The ordinary Add unifier uses a local 1e-9 radian angular tolerance for numerical
Sweep cap frame roundoff, with the existing linear tolerance retained. The
isolated FORM probe still retained its four front joins at 1e-10 radians and
removed them at 1e-9 radians with a valid B-Rep. Cross-source nonplanar boundaries
remain protected. This is a local same-domain decision, not a reduction of mesh,
curve sampling, measurement or body integration accuracy.

Reference entry offers and highlights the actual displayed face fragment through
the common picker. It never enables untrimmed original or intermediate faces.
For validation and reference storage, Boolean fragment ancestry is decoded from
persisted length-delimited parent identities until the original owner is reached.
The candidate's displayed geometry and occurrence path remain unchanged. Malformed
or ambiguous parent sets are rejected. Placement solving, orientation, offsets and
its storage contract are unchanged.

Confirmation inspection also resolves persisted Boolean ancestry when finding
the displayed face. Previously this path matched only the stored original key
and fell back to the untrimmed original face after a correct Display hover.
The current click retains its exact operational fragment for the dialog session;
scene refresh resolves that fragment by identity rather than a triangle index.
Reopened inspection prefers the current visible descendants of the original
reference. Occurrence paths remain part of every match, and closing inspection
retires the transient confirmation choices. This is a presentation fix; the
native document schema and reference solving are unchanged.

Displayed boundary polylines now use the supporting face triangulation's edge
polygon and parameters. Previously independently sampled curve chords could
fall behind their own mesh, breaking visible transition lines in both 3D and
interactive Drawing views. Mesh accuracy, exact splines, measurements and depth
occlusion are retained. Wire geometry without a supporting polygon keeps the
existing curve sampler.

Drawing scale descriptions and the Properties placeholder use `M1:1`, `M1:2`,
etc. The existing translated Scale label is retained in all five languages;
`M` is a shared engineering symbol.

## Verification

- The native FORM regression checks every history boundary, valid unique face and
  edge references, OCCT BRep validity and independently integrated area/volume.
- The four-operation fixture volume is 18497.255433974693 mm³ and area is
  8563.080366200782 mm².
- The Sweep boundary retains the original prism's front-face identity and has
  no internal coplanar front join between same-side faces in the same solid.
- Every ordinary body edge display sample matches a supporting mesh vertex.
  Construction/overlay centerlines are intentionally outside this assertion.
- All twelve formerly ambiguous edges accept fillets at both 0.05 and 0.1 mm.
  Each variant covers Undo/Redo, native save/reopen and regeneration.
- Both disconnected front faces support Surface From Solid, including native
  save/reopen, exact reference persistence and Undo/Redo.
- A successive change of the cutter length preserves the relevant edge identities.
- Automated Qt GUI tests use the common MeshView picker and actual hover/LMB
  events for two cutter edges and both front faces. They exercise Cancel, OK,
  reopening Fillet properties, persisted output and Undo/Redo.
- Primitive placement on the original front reference covers mouse picking,
  Cancel, OK, native save/reopen, regeneration and Undo/Redo in the GUI.
- New primitive placement on each of the two disconnected final front fragments
  checks actual Display geometry from the common mouse picker and translation to
  the same oldest original front reference; Cancel restores the original body.
- Viewer tests cover nested persisted Boolean parent chains, occurrence identity,
  rejection of malformed/multiple parents, and absence of candidates in a removed
  area of the original face. The six visible-reference regression tests passed.
  The final strengthened FORM GUI test passed in 41.99 s, with viewer and
  translation checks also passing; see
  `build/form-diagnostic/visible-fragment-final-tests.log`.
- Confirmation inspection adds framebuffer equality against explicit inspection
  of the exact clicked Display fragment, for both disconnected front faces.
  Nested ancestry, multiple visible descendants, remembered exact fragments and
  stale occurrence choices are tested independently. Viewer, FORM GUI,
  surface-placement GUI and translation tests passed (59.57 s total), recorded
  in `build/form-diagnostic/inspection-tests.log`.
- Existing Part Boolean fragment, edge treatment command/query/GUI, shell,
  body-reference and surface-thickening checks pass. The dedicated FORM native
  and GUI checks pass. Drawing GUI checks include real depth rendering, hidden
  edges and cache invalidation. DXF and label tests verify the `M` scale prefix;
  translation coverage passes, with no new localized prose.

## Verification limits

The broader extrusion-limit test fails with `Expected 20.000000, got 20.635083`
and the surface-profile test fails with `Hidden sheet retained geometry/picking
points`. Both failures were reproduced using the exact pre-change kernel source
from Git HEAD (84c121a1), not attributed to this fix. The original complete Boolean
fragment test also crashes in its Assembly case with 0xC0000005 on that baseline;
the existing Part cases remain separately runnable and pass.

These unrelated defects are not changed here. Linux execution remains unverified.
Filleting every displayed boundary is not promised: tangent boundaries have no
ridge and parameter seams are not ordinary offered fillet edges.

The regression fixture contains the native four-operation FORM definitions
without body caches. The first investigation backup is
`Projects/FORM-before-topology-fix-20261007.prtz`.

The user's current FORM additionally contains two Fillets, for six history
operations. The one-off document repair uses each saved operation's input cache
to locate a selected edge that needs rebinding, and accepts only a unique match
of owner, exact spline degree, poles, weights, knots and measured length. It
refuses absent or ambiguous matches and does not remap asymmetric Chamfer sides.
This is a user-authorized file repair, not a product migration or runtime fallback.
Every boundary's area and volume, native reload and cold regeneration are checked.
Ten selected edge references in the two newer Fillets were rebound by unique
exact-curve matches. A native-file audit confirms that only these route references
and derived body caches changed in the Part. Drawing regeneration changed
projected edges, triangles and model annotations; view identities, scale,
positions, orientation and display settings were retained.
The current backup pair is `Projects/FORM-before-coplanar-fix-20261007.prtz` and
`Projects/FORM-before-coplanar-fix-20261007.drwz`. Document and feature identities,
user definitions and Drawing view settings are retained. Drawing cached view
geometry is refreshed from the repaired Part.

Final verification records under `build/form-diagnostic`:

- `final-form-test.log`: four-operation native FORM matrix passed.
- `final-coplanar-gui-tests.log`: five Boolean/edge/shell/placement/FORM GUI tests passed.
- `dependent-final-tests.log`: seven viewer/reference/Drawing tests passed.
- `localization-final-tests.log`: all-language translation contract passed.
- `final-repair-test.log`: current six-operation file repair and cold regeneration passed.
- `final-drawing-render-tests.log`: final refreshed Drawing diagnostic and FORM GUI passed.

Actual GUI framebuffer captures were inspected for the 3D model and both restored
Drawing views. The first Drawing view intentionally hides tangent edges according
to its retained settings; the second uses thin tangent edges. Visible transitions
are continuous and true hidden geometry continues to be depth occluded.
