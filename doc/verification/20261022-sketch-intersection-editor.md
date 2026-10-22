# Associative Sketch face/plane intersections

Status: Windows editor/native implementation and focused verification; portable
publication is a separate committed-source acceptance gate. Linux execution is
not verified here. The persisted definition is within the scope explicitly
approved in `SKETCH_INTERSECTION_REFERENCE_PLAN.md`.

## Commands and ownership

**Intersection reference** selects an actual face and calculates its bounded
intersection with the active Sketch plane. A unique branch is accepted directly;
several branches are offered as individual native wires. Select the required
branches separately. A short middle click returns from the prepared branches to
face entry; another ends entry. **Intersection → contour** uses the same choice
and creates associated native Sketch geometry. Isolated contacts become points;
an open curve's endpoints remain children of that selected curve.

Ordinary Reference still offers individual original edges/curves, vertices, axes
and datum planes. It does not acquire whole-face outline selection. Commands
share the existing Sketch draft/transaction and cancellation paths. No additional
dialog or independent placement solver is introduced.

Part queries use the actual preceding calculated boundary, including separate
placed Bodies. Assembly queries consume exact occurrence/native input snapshots,
including the input before the owning or following cutter. Nested and calculated
copy geometry stays in its actual frame. A Part edited in Assembly context cannot
silently acquire Assembly-owned cuts or a dependency cycle. General Surface
definitions declare their parent-local frame; the interactive display Sketch
declares its already transformed Body frame. The existing transforms are consumed
once, without changing shared placement equations.

## Native definition and source updates

An explicit `intersection_` kind requires an `intersection` object containing the
actual offered face owner/key and selected native branch key. The existing source
owner/key retain original ancestry, and occurrence/context fields retain their
meaning. The definition, exact curve cache and native sources are inside the
document; no required sidecar is added. Ordinary reference kinds retain their
projection meaning. A reader without this kind rejects it instead of silently
projecting an outline. This is a current additive variant, not a migration path.

Explicit regeneration groups endpoints/branches by their source face and plane,
calculates once per group and matches the persisted branch identity exactly.
Missing, split or ambiguous identities become broken while retaining their own
last usable geometry. Ordinary scene preparation and projection refresh skip the
intersection query. Cache-only context refresh must not break unrelated ordinary
projections by passing empty source geometry to them.

An axial plane cut can leave two cylinder generatrices with identical native
edge ancestry. Their section endpoints previously collided and were correctly
rejected as ambiguous. For Sketch association only, an analytic cylinder/plane
pair now supplies its two predefined signed root domains. The line must be
parallel to the cylinder axis, the adjacent native plane must contain that
direction, and its two root equations must hold within the requested tolerance.
Names contain parent references and positive/negative domain roles, never sampled
coordinates, enumeration or sorted positions. Tangent/uncertified cases retain
the ambiguity rejection. Source bindings and existing Surface Intersection/Trim
feature names are unchanged.

Certified circles/arcs remain native circles/arcs, including their center and
radius. Genuine nonconic geometry uses exact rational B-splines. OCCT's section
approximation can change its pole count after a source edit; the associated
Sketch correctly refuses to reinterpret existing native pole identities. For a
certified full-domain perpendicular slice of a spline extrusion, the Sketch query
therefore retains the exact native isoparametric source curve. Certification uses
the native parameter domain, monotone section p-curve and roundoff bounds, rather
than display samples or a looser modeling tolerance. Section still discovers the
bounded domain, vertices and ancestry. Existing Surface Intersection/Trim feature
representations are unchanged. Partial/non-isoparametric cases retain the general
section result; an incompatible changed basis remains broken/frozen, not rebound.

Identical exact rational bases are also continuous coincident supports, not
discrete crossings between display chords. Scene preparation records the pair
outside chord loops; Trim uses the same certification. This does not change
general noncoincident spline intersection accuracy or hide real transverse cuts.

## Finite verification matrix

The executable passes **341 cases**: 34 basic Part/persistence/copy cases, four
Assembly occurrence cases, four disconnected-branch cases, two qualified copies,
24 circular edits, five contextual Part cases, six standalone Assembly cases,
two General Surface frames, 24 arc edits, eight dependent solid cases,
20 trimmed-cylinder cases, 192 line
dimension/edit/drag cases and 16 spline edit/cold-open cases.

- Part lines/circles with placed/unplaced and shared/separate Bodies; successive
  plane movements, disappeared branches and recovery, duplicate rejection,
  unrelated branch identity, native save/reopen and Undo/Redo. Independent Save As
  rebases the document namespace without changing the selected branch or source.
- Direct/nested Assembly occurrences; qualified calculated copies; contextual
  Parts with source radius edits, preserved ordinary projections and rejection
  of Assembly-owned modified sources. Standalone Assembly Sketch regeneration,
  cold `.asmz`, regeneration Undo/Redo and real following-cut input boundaries.
- Two disconnected open branches with four branch-owned endpoints; moving the
  plane and losing only one branch. Two General Surface definition/display-frame
  cases, including a placed Body and a rotated/translated parent.
- Dependent native Extrusion Bodies consume the changed circular section rather
  than a stale profile cache. Three successive source radii produce independent
  exact volumes `2*pi*r*r`, with placed/unplaced source Bodies and cold native
  reopening followed by explicit calculation.
- Actual half-cylinder fragments retain their original source ancestry and
  create arcs rather than the untrimmed original circle. Three successive radius
  and cut-plane edits retain signed endpoint domains in both plane orientations,
  placed/unplaced Bodies and cold native readback. Independent material, radius
  and Sketch-plane equations are checked; sources remain unchanged by refresh.
- Circle and arc radius/diameter dimensions, source dimension locks, successive
  values and source drags; opposite plane orientations for arcs. Reference
  measurements update, bound centers/endpoints reject dragging, and sources
  remain read-only.
- Line length, X, Y and angle measurements in both Sketch
  orientations, placed/unplaced Bodies and locked/unlocked source dimensions.
  Point-pair measurements use both selection orders; direct segment angles have
  no two-point selection order.
  Each performs three value edits and drags, independently checks source and
  plane equations, rejects bound endpoint movement and prevents a reference
  measurement from driving the source. Unlocked driving dimensions correctly
  update their values while dragging; locked ones retain their equations.
- Genuine cubic spline deformation through three successive paired-pole drags,
  placed/unplaced Bodies and both plane orientations. Independent source/plane
  equations, four stable native pole IDs, rejected bound drags, real transverse
  Trim splitting, absence of coincident-chord cuts, cold native reopening and
  Undo/Redo are checked.

The separate native foundation retains its 108-case bounded-domain, both-normal,
source-displacement and cold-copy matrix. The 392-case coincident-circle matrix,
Reference command and dimension command suites pass. The dimension suite includes
the existing 64 native/external angular edit matrices, 96 symmetric/chamfer
matrices and 240 projected cylinder/cone boundary cases; these are regression
evidence, not additional intersection-editor cases.

Actual GUI verification passes eight Part/Assembly × circle/disconnected-open ×
reference/contour scenarios, plus a genuine four-pole spline contour in the dark
theme and an actual trimmed-cylinder arc in the light theme. It checks face/branch mouse choices, short-middle-click return, native
circle/spline types, source immutability, Undo/Redo and native save/reopen. Nine
related suites pass in 174.39 s: all five translation catalogs, circular profiles,
ordinary face references, straight/circular external Trim, conic centers,
dimension entry, coincidence and the existing Surface Intersection command.
All five languages in both themes pass the circular contour mouse workflow.
New text uses shared catalog keys in cs/en/de/fr/ru; internal diagnostic logs
are not substituted for localized user status messages.

## Explicit work and asset acceptance

The final measured GUI executable SHA-256 is
`148ff81f5dbc096dea80febc50158d2364cb85e3b03ecc9e2a903947e6c40590`.
The eight-case matrix takes 29.152 s: 12 explicit face choices cause exactly 12
queries (1.852–3.842 ms), and all 32 command/source/branch/middle transitions
publish exactly one required scene. The spline case takes 4.789 s with one
12.361 ms query. The trimmed-cylinder case takes 5.065 s with one
3.018 ms query. Both publish one scene for each of their two transitions.
Branch selection, Undo/Redo and save cause no additional query. These are synthetic Windows
observations, not universal speedup or complexity guarantees. Reproduce with
`tools/verify-sketch-intersection-ui.py`; `--spline` selects the genuine spline
case; `--trimmed` selects the cut-cylinder arc. Reports and images stay under `build/sketch-intersection-editor/`.

The committed-source inventory at `6ea08e046` contains 1,210 tracked native assets
(1,185 factory assets) and no existing intersection definitions. This additive
variant therefore requires no existing factory model conversion. All six start
templates were saved/reopened with the changed native code and remain
byte-identical. Actual mm and inch new-document GUI checks pass (43.43 s and
46.35 s), including first editable context and ordinary command availability.
No personal project or library is bulk rewritten. `Projects/0101.prtz` remains
SHA-256 `39a0461d5107651e42cb6ad9b0217b7508c78c00f2499364990496e054944359`.

## Limits

These finite checks do not certify every dimension/constraint combination or
every tangent, trimmed, seam, multiply closed or topology-changing source.
Arbitrary equal-parent closed branches without a certified support-domain role
remain ambiguous. Pole-basis changes outside the certified full spline slice
remain unresolved rather than renumbered. Full GUI manipulation of embedded
General Surface/contextual Part Sketches, isolated contact creation and all
inference/constraint variants on genuine nonconic intersections remain unverified.
The fixture matrix does not replace representative large user-model timing,
Linux execution or committed portable-package acceptance. Ordered multi-face
Up To/correction, Projection Feature and view-relative H/V are separate work.
