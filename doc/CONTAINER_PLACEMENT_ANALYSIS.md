# Container placement: combination and consistency audit

## Status and conclusion

Analysis completed on 2026-09-16 against commit
`33e9e5f5099a9ffbd48f0323fb39ec74c9b7720f`.
The initial analysis changed no product code. The user subsequently approved
the ordered FRONT/TOP contract and the 0.01-degree angular limit. The current
implementation includes that repair; the baseline findings below describe the
pre-change behavior, not the new contract. Configuration and user models remain
outside this change.

The existing solver can construct a fully located, right-handed container frame.
The main problem is inconsistent assignment and preservation of reference roles,
especially in the shared Sketch Properties path used by Bend, Flat and Holes.
This is not a reason to replace the placement system.

**The reported extra rotation has a reproducible explanation.** In Bend,
edge -> incident planar face -> endpoint retains 1 R, whereas endpoint -> face
-> edge reaches 0 R. The first positional plane suppresses other orientation
contributions. This happens already in the dialog's pending values and is also
enforced by the commit operation; it is not just a save/reopen problem.

The same result was reproduced with native Sketch geometry, original Box
topology, and a translated/rotated original Box. Native save/reopen, Undo/Redo
and reference re-resolution preserved the resulting placements in these cases.

## Scope: inputs -> means -> outputs

- **Inputs:** original persisted points, finite straight edges/Sketch segments,
  explicit infinite axes, planar faces/planes, exact curves, selected reference
  roles and order, offsets, manual angles, and the last valid frame.
- **Means:** ZIMA's reference packets, common assignment helpers, analytic
  position equations, exact-curve constraints, FRONT/TOP frame construction,
  property dialogs and native transactions. OCCT was used only by explicit
  disposable body calculations in the verification program.
- **Required outputs:** a valid frame satisfying the declared roles, truthful
  remaining freedoms and editability, stable persistence, and an understandable
  rejection of contradictory or undefined input.
- **Independent check:** a rigid frame has three translational and three
  rotational freedoms. Position-equation rank and independent direction count
  must agree with the frame that is actually computed.

Spherical and other nonplanar face placement are outside this audit. Existing
Assembly mates are a separate contract. No user's saved Bend was modified or
assumed to have exactly the same references as the disposable reproductions.

## 1. Baseline implementation

### Position

All positional references constrain the **same container origin**:

| Reference | Positional meaning | Remaining T when used alone |
|---|---|---:|
| Point | Origin equals the point | 0 |
| Plane/planar face | Origin lies on the support plane plus its signed offset | 2 |
| Explicit axis | Origin lies on the infinite axis | 1 |
| Exact edge/Sketch curve | Origin lies within the trimmed curve domain | 1 at a regular interior point |

A face contributes its infinite support plane, not a requirement to stay inside
its face boundary. An exact edge is finite; it must not silently become an
infinite axis. Multiple positional points normally conflict unless a specific
ordered-point frame rule applies.

The positional solver uses the previous coordinates for unconstrained values.
Missing or unsolvable references retain the last resolved position and rotation
and mark placement invalid. Retaining a visible frame does **not** mean that the
references still resolve.

### Orientation

- FRONT is local +Y; TOP establishes local +Z after projection perpendicular
  to FRONT. Local X follows the right-hand rule.
- A plane provides its normal; an axis provides its direction; a curve provides
  its tangent at the anchored origin.
- Once a primary direction exists, an orientation-only straight edge may provide
  the secondary direction even when it is remote from the origin.
- A point with role `direction` supplies the vector from the solved origin to
  that point. A positional point by itself supplies no orientation.
- One nonzero direction leaves 1 R. Two independent directions assigned to
  distinct frame roles determine orientation. Two copies of the same direction
  do not provide another independent rotation constraint.

TOP projection is important: two oblique reference normals define a frame, but
that does not mean both orthogonal local datum planes become parallel to two
nonperpendicular source planes.

Absolute/free angles and correction angles are separate persisted parameters.
An editable correction is an intentional change relative to the reference frame;
it must not be confused with a remaining rotational freedom.

The properties panel labels its three numeric columns **Position**, **Rotation**,
and **Rotation correction**. Assembly component properties place **Position** and
**Rotation** side by side. These localized headings and layout changes do not
alter reference solving, coordinate values, or persisted placement semantics.

### Automatic role assignment

The generic input path examines the existing references before each new pick.
When position is already fully determined and rotation remains free, a subsequent
row becomes **orientation-only** (`direction`). Otherwise a geometric reference
can contribute position and an automatic FRONT/TOP role together.

Therefore the same point can mean either:

1. put the origin at this point; or
2. aim a direction from the fixed origin toward this point.

That distinction is necessary, but it needs one consistent, visible contract.
Changing click order can currently change the roles, not merely the order in
which equivalent equations are solved.

Relevant implementation:

- `cpp/modules/document_core/src/part_document.cpp`: position solver at 3021,
  frame construction at 2523, placement resolution at 4015, positional feedback
  at 4192 and rotational feedback at 4588.
- `cpp/modules/workspace/src/feature_reference_input.cpp`: automatic input roles.
- `cpp/modules/document_core/include/zima/document/placement_reference_assignment.hpp`:
  independent positional and FRONT/TOP rows and automatic plane copies.
- `cpp/app/workspace/reference_selection.cpp`: candidate filtering and role
  assignment before confirmation.
- `cpp/modules/document_core/include/zima/document/sketch_placement.hpp`:
  first-plane normalization.

## 2. Geometric reference matrix

This table specifies what the geometry can determine **when the stated roles
are actually retained**. It is not a claim that today's automatic GUI implements
every row consistently. Counts assume compatible positional references and
zero correction angles. P means point, L means straight edge/axis, F means plane.

| Combination and declared meaning | T | R | Necessary condition / limitation |
|---|---:|---:|---|
| No references | 3 | 3 | User-entered frame |
| P as origin | 0 | 3 | Point alone has no direction |
| L as position and primary direction | 1 | 1 | Nonzero line; finite bounds still apply |
| F as position and primary normal | 2 | 1 | Valid plane |
| F + F | 1 | 0 | Nonparallel normals; origin can move along intersection |
| L contained in F | 1 | 0 | Line and normal supply independent directions |
| L crossing F obliquely | 0 | 0 | Intersection is inside a finite edge, if used |
| L perpendicular to F: drill tip | 0 | 1 | Normal and line repeat one direction; preserve roll |
| P on L + L direction | 0 | 1 | Point is on the curve for a primary exact-curve tangent |
| P + F normal | 0 | 1 | Point fixes origin; a later orientation-only plane does not additionally impose incidence |
| Two points: origin + direction | 0 | 1 | Distinct points |
| Two intersecting L references | 0 | 0 | Nonparallel, compatible finite domains and roles |
| Three independent F references | 0 | 0 | Normal rank 3 fixes position; retain two direction roles |
| Two F + L | 0 or 1 | 0 | L must fix the remaining position; coincidence with the planes' intersection leaves 1 T |
| L + incident F + endpoint | 0 | 0 | Endpoint fixes station; retain line and face directions |
| L + perpendicular F + off-line direction point | 0 | 0 | Point is orientation-only after the intersection fixes the origin |
| Same, with third point on L | 0 | 1 | Point cannot determine roll around L |
| P + two independent directions (L/F/P) | 0 | 0 | Both vectors must be defined at the selected origin and nonparallel |
| Three ordered points | 0 | 0 | Distinct, noncollinear; define and preserve the point-order convention |

All ten unordered triples of P/L/F are represented in the diagnostic sequences:
PPP, PPL, PPF, PLL, PLF, PFF, LLL, LLF, LFF and FFF. Different geometry and
reference roles can make any nominal combination redundant or contradictory.
For example, a third line after two intersecting lines can be redundant;
three mutually skew positional lines cannot locate one common origin.

### Cases that must not be labelled fully defined automatically

- Parallel/coincident planes and repeated directions.
- A point outside the finite segment but on its supporting infinite line.
- Disjoint or skew positional curves and points away from their required curve.
- Zero-length edges, coincident direction points and collinear three-point frames.
- Nearly parallel directions, where tiny perturbations can produce large roll.
- Curve cusps, corners and self-intersections without a unique tangent.
- Multiple isolated intersections: 0 local DOF does not mean one globally unique
  solution. The current branch or an explicit selected point must choose it.
- A vanished source: use last-valid-frame retention and a missing-reference
  diagnostic, not a new apparent geometric solution.

## 3. Confirmed baseline findings

### A. First-plane normalization discards valid secondary directions

`normalize_sketch_front_references()` selects the first positional plane as
FRONT, clears orientation on other positional references, and removes
orientation-only references from other sources. It is used by Sketch Properties
and Sketch, Holes, Bend and Flat commits.

As a result, an independent edge or off-axis point can exist in the reference
list without contributing the direction the user intended. Merely selecting
three references is not enough to overcome that normalization.

Fixture: E is the segment `(-10,0,0)..(10,0,0)`, F is XY, N is YZ, P is
`(10,0,0)`, Q is `(0,5,0)` and O is the origin.

| Actual shared Sketch Properties -> Bend commit | Current result |
|---|---|
| E -> F -> P | 0 T, **1 R** |
| F -> E -> P | 0 T, **1 R** |
| P -> F -> E | 0 T, **0 R** |
| P -> E -> F | 0 T, **0 R**; different primary direction |
| E -> N -> Q | 0 T, **1 R**, off-axis direction discarded |
| Three perpendicular planes entered individually | 0 T, **1 R** |
| Perpendicular plane + axis, either order | 0 T, **1 R**, appropriate for a drill tip |
| O -> E -> remote transverse straight edge | 0 T, **0 R** |
| Three noncollinear points | 0 T, **0 R** |

The first three rows were also verified on original solid geometry in two
frames. This corrects the earlier conceptual assumption that every existing
three-plane or edge/face/point workflow already produces 0 R in the product.
Bulk Origin selection was not treated as interchangeable with selecting the
three individual plane rows.

### B. Generic edge-first assignment can create conflicting automatic roles

For an ordinary Box, the real `placement.reference.set` command accepts F -> E
-> P but rejects E -> F at the second reference. The source geometry is identical.

The edge occupies FRONT in the positional row. The next plane receives TOP,
but automatic mirroring also puts that plane into the first empty independent
orientation slot, FRONT. Frame construction then sees the same plane as FRONT
and TOP instead of the intended independent edge and plane.

This differs from the Sketch/Bend dialog behavior and should be corrected in
the shared role-assignment layer, not with a Bend-specific geometry workaround.

### C. Frame resolution and DOF diagnostics disagree near degeneracy

The frame constructor normalizes the projected secondary direction using a
roughly `1e-12` zero threshold. Rotational DOF feedback tests normalized direction
independence using `1e-6`. Near-parallel directions can therefore determine a
computed frame while feedback still reports 1 R.

In a direct test, angular separations from `1e-10` through `1e-6` radians were
accepted while reporting 1 R. At `1e-5`, feedback reported 0 R.

More visibly, E + positional N + a direction point on E was rejected from the
exact zero seed, but accepted from seed `(2,3,4)` with approximately
`(RX,RY,RZ)=(180,36.87,90)` and from `(2,4,3)` with `(180,53.13,90)`.
Both still reported 1 R. Tiny position residuals were amplified into a roll
direction that the geometry does not determine.

This requires a shared numerical decision about independence and degeneracy,
including position residuals when deriving a direction from points.

### D. Initial placement onto a finite straight edge can depend on the seed

The isolated exact segment `(20,20,0)..(20,40,0)` failed initial attachment from
`(0,0,0)` and `(30,50,0)`, but succeeded from `(0,25,0)` and `(20,30,0)`.
The geometry has valid nearest endpoints in both rejected cases.

This reproduces in the shared curve solver and in the generic assignment
probe. It is distinct from intentionally rejecting an explicit coordinate edit
outside the finite range. Initial attachment and constrained numeric editing
need separate acceptance tests under the existing finite-curve meaning.

### E. The special three-positional-point frame lacks matching generic feedback

`resolve_placement()` recognizes three ordered positional points as a frame,
even when the rows have no orientation flags. A direct valid noncollinear case
resolved successfully, while the independent public orientation query returned
3 R. The automatic point -> direction-point -> direction-point route reports
0 R correctly. These are two existing interpretations that need one consistent
diagnostic contract; the invalid-state DOF count must also not be read as a
certificate of validity.

### F. Bend reference assignment has no complete public command route

The real `placement.reference.set` command on a Bend returns `wrong_feature`
(`This container is not a supported primitive.`). Dispatch falls through to the
primitive operation. Bend GUI placement works through Sketch Properties and
`commit_bend()` instead. Flat/Holes use similar GUI ownership, but their command
dispatch was not independently executed in this audit.

GUI/CLI equality for these reference operations therefore cannot currently be
claimed. Numeric placement edits and reference assignment are different APIs.

## 4. Approved and implemented contract

1. The first directional reference owns **FRONT (local +Y)**. An origin point
   contributes position only. A plane contributes its normal; an attached curve
   contributes its tangent. An orientation-only point contributes the direction
   from the already located origin to that point.
2. The first independent subsequent direction supplies **TOP (local +Z after
   perpendicular projection)**. Later references must not overwrite FRONT or an
   already established TOP. Automatic plane copies cannot assign one source to
   two axes. References which do not determine orientation remain available for
   positional solving and persisted inspection.
3. Normalized cross products use **sin(0.01 degrees)** for independence. This
   handles parallel and antiparallel directions symmetrically. A redundant second
   direction leaves the roll free; a later independent direction may close it.
   Missing sources, zero directions and ambiguous curve tangents remain errors.
4. Frame construction and rotation DOF feedback consume the same resolved
   directions. Free absolute angles, constrained-axis corrections, signed offsets,
   reference flips, exact occurrences and last-valid-frame fallback are retained.
5. Automatic owned Sketch planes use local XZ, normal to FRONT. The offset is
   along that normal. A manual XY/XZ/YZ choice remains a manual choice. Owned
   Sketch history resolution consumes the container references directly instead
   of replacing them with a separate first-plane rule.
6. Sketch, Bend, Flat, Holes and owned Extrusion/Revolution profiles preserve
   their independent secondary references. Properties replacement excludes the
   removed source's automatic twin from its baseline. GUI candidate evaluation
   starts from the actual pending coordinates and solves the proposed reference
   set before evaluating its remaining freedoms.
7. Initial finite-curve attachment starts at a point on the trimmed curve. This
   admits a distant segment even when the old origin lies beyond an endpoint.
   Explicit coordinate edits remain subject to trimmed-domain validation.
8. `placement.reference.set/remove` supports Bend, Flat and Holes through their
   existing atomic owned-Sketch transactions. No document schema or required
   sidecar was introduced. Construction-specific geometric definitions (for
   example a plane through three points) remain distinct from feature placement.

Expected examples:

| Ordered references | Position | Rotation | FRONT |
| --- | --- | --- | --- |
| Edge, incident plane, endpoint | 0 T | 0 R | Edge tangent |
| Plane, incident edge, endpoint | 0 T | 0 R | Plane normal |
| Endpoint, its curve, independent direction | 0 T | 0 R | Curve tangent |
| Axis, perpendicular plane | 0 T | 1 R | Axis direction |
| Three independent positional planes | 0 T | 0 R | First plane normal |

Different reference orders may intentionally produce different frames; they
must not produce contradictory reference ownership or false freedom counts.

## 5. Baseline verification

### Executed against the existing Windows build

- **15 existing suites passed:** curve placement; placement commands, assignment
  and removal; construction commands, reference commands and occurrences;
  Bend, Flat, Holes, work planes, Sketch properties, primitive/profile reference
  commands and UI contracts.
- **304 assignment sequences:** all P/L/F triple categories, selected degeneracies
  and all permutations of the designated triples for Box, Sketch, Bend and Flat
  feature records. This probe invokes the existing preparation adapter and, for
  Bend/Flat, their normalization rule. It does not pretend that the missing Bend
  CLI dispatch exists or that every sequence was entered through mouse picking.
- **19 direct solver cases**, with independently specified roles.
- **17 numerical boundary cases:** finite-edge seed sensitivity, near-parallel
  direction thresholds and the collinear roll example.
- **17 real Sketch Properties dialog / Bend commit cases:** 11 Sketch/datum cases
  and 6 cases using actual original solid topology, including a rotated source.
  All 17 commits, save/reopen equality, Undo/Redo equality and document reference
  re-resolution checks passed. The observed extra freedoms are recorded above;
  passing persistence does not make their role assignment correct.
- **4 actual command sequences:** Box E/F/P rejection, Box F/E/P success, Sketch
  E/F/P success with 1 R, and missing Bend reference command dispatch.
- For all 304 preparation sequences, the final retained valid state remained
  stable through 100 repeated resolutions. This includes cases whose proposed
  next reference was rejected; it is not a claim that all sequences succeeded.

Disposable diagnostics and JSON evidence are under `build/placement-analysis/`:
`audit.cpp`, `gui_audit.cpp`, `numeric_audit.cpp`, `build_probe.py`,
`audit-results.json`, `gui-results.json` and `numeric-results.json`.
They link the current repository libraries without replacing the development
application. The GUI probe calls the real dialog API with the reference roles
used by the picker; it is not a full mouse/hover end-to-end test.

### Implementation acceptance gates identified by the audit

- Encode an independent expected result for each declared role combination;
  test true freedoms, constraints actually satisfied and the final basis vectors.
- Test all allowed orderings, distinguish role changes from equation order, and
  check independence from the initial frame when the result is fully defined.
- Cover aligned/oblique frames, translated sources, finite endpoints, reversals,
  signed offsets, flip/quarter-turn controls and free versus correction angles.
- Check replacement/removal in every occupied row, Undo/Redo, save/reopen,
  source edits, missing sources and last-valid-frame retention.
- Exercise the actual common hover/click candidate path, including GUI filtering
  of redundant references, and all shared feature/dialog families.
- Extend scale and near-degeneracy tests across the supported model range.
- Preserve exact source ownership, history order and occurrence identity; include
  repeated nested occurrences in the final regression campaign.
- Do not call body calculation from picking, previews of placement references,
  or status/DOF queries.

These checks establish a bounded, explicit acceptance contract. The finite audit
does not prove every floating-point geometry or every interactive sequence is
already reliable. It identifies specific reproducible gaps and the work needed
to make supported placement combinations predictable.

## 6. Regression coverage added with the repair

- Ordered edge/plane/point frames, automatic and manual work planes and offset
  vectors during actual owned-Sketch history resolution.
- Parallel and antiparallel angular cases on both sides of 0.01 degrees, retained
  free roll, useful third directions and repeated-resolution stability.
- Initial attachment to a remote trimmed segment from four starting locations.
- Real Sketch Properties assignment/reopen, including the shared Bend/Flat/Holes
  editor contract and exact removal of paired orientation from replacement input.
- Public Bend, Flat and Holes placement commands, reference removal, Undo/Redo,
  regeneration and native save/reopen.

### Verification results

- The focused run passed 15 suites. All 41 targeted command, geometry and GUI
  suites passed across the final runs; the seven final application integration
  checks also passed on the normal development EXE. A former Assembly profile assertion which
  expected an obsolete automatic FRONT to survive replacement was updated to
  verify successful replacement, exact occurrence ownership and Undo/Redo.
- The 76 ordered input combinations were rerun for Box, Sketch, Bend and Flat:
  304 sequences in total. All four feature families agree on acceptance stage,
  final position, orientation and remaining freedoms. 248 complete sequences
  were accepted; 56 proposals were rejected while retaining a valid prefix.
  All 304 retained states remain stable through 100 resolutions each.
- Seventeen real Sketch Properties/Bend commit scenarios passed, including
  original Box topology and a translated/rotated Box. Save/reopen, Undo/Redo and
  reference re-resolution preserve the frame in every scenario. Edge/plane/point
  sequences give 0 T / 0 R; axis/perpendicular-plane combinations retain 1 R.
- Seventeen additional numerical boundary cases and four public command
  sequences verify remote trimmed segments, near-parallel directions and
  collinear point directions from different initial coordinates.
- The Windows GUI and CLI were rebuilt at their ordinary development paths.
  The unchanged repository-root `zima-cad.bat` starts the updated GUI.

This is targeted verification, not a claim that the complete repository suite
passes. The pre-existing umbrella `zima_cpp_contract_tests` still stops on an
unsupported old native fixture; it also contains an obsolete format-version
assertion. That fixture maintenance is outside this placement repair. The
deterministic geometric and interactive tests above cover the repaired paths;
they do not prove every possible degenerate geometry or user interaction.

### Sketcher return regression (2026-09-16)

The first GUI audit verified dialog assignment, commit and reopen, but missed
the workspace preview callback used by Sketch, Holes, Flat and Bend during
Sketcher entry/return. That callback still implemented the obsolete first-plane
override: it removed TOP, could replace a preceding curve's FRONT and wrote the
resulting incomplete rotation state back into the dialog. Consequently a valid
placement could show a rotated work plane and an editable RY after Sketcher.
Extrusion/Revolution use a different preview path and did not have this override.

The callback now consumes the dialog's complete reference set without inventing
or removing direction references. Preview and document resolution therefore use
the same inputs. This changes neither the solver contract nor the native format.

`zima_cpp_sketch_return_frame_ui_contract` reproduces the original failure
("Sketcher return released a constrained rotation (RY)") on the old callback.
It drives actual workspace dialogs and Sketcher actions, independently compares
the working axes with the document frame, and checks rotation-field availability
after returning. Cases include new/existing Sketch, Holes, Flat and Bend;
three plane orders; edge/plane/point and point/curve/plane input; manual planes;
offsets; Back/quarter-turn controls; a legitimately free roll; repeated returns;
Bend's auxiliary sketches; and OK/save/reopen.

All 48 scenarios passed on the rebuilt development GUI, including two main
Sketcher round trips per scenario and both auxiliary Sketches on each Bend
round trip. The unmodified GUI failed the new test on the first scenario,
confirming that it detects the reported RY regression.
Twelve related suites also passed on the final build: Extrusion/Revolution frame
round trips, working planes, Holes UI, owned-profile references, curve placement,
placement commands, Part/Assembly profile commands, Sketch properties commands,
and Flat/Bend/Holes commands. The usual repository-root launcher still opens
this updated development executable.

### Repeated Body Origin click (2026-09-16)

The whole-Origin shortcut also needs a click on an already selected Tree row.
Such a click emits `itemClicked` without `itemSelectionChanged`; the placement
command previously ignored it and left all reference rows empty. The click
handler now routes that case through the existing whole-Origin validation and
assignment path while placement input is active. Origin visibility selection
retains its separate behavior. No placement-solver or native-format change is
involved.

The GUI regression reproduced empty rows before this repair. Afterwards,
24 new/existing Sketch, Holes, Flat and Bend scenarios passed: empty input,
partially filled input and an already selected Body Origin, followed by Sketcher
return and native save/reopen. The user's particular edited older document was
not independently reproduced; this verifies current documents and the confirmed
repeated-click defect.

### Bend attachment and the third positional reference (2026-09-16)

The user approved the ordered **straight Edge -> containing planar Face ->
Point** placement meaning while discussing Bend attachment to a narrow sheet
end face. The edge and face fix orientation and leave translation along the
edge. The final point supplies only its coordinate along that edge, so an
opposite corner of the face locates the origin by perpendicular projection.
The shared position solver and DOF feedback use this same equation, and the
third reference row identifies it as a station along the edge.

This is an ordered reference contract, not a least-squares relaxation of
conflicting anchors. Point-first remains coincident. A curved first edge, a
nonplanar second face, or a plane that does not contain the edge does not acquire
station semantics. A third plane continues to use its ordinary intersection and
offset equation. Existing ordered reference records persist the entire meaning;
the native format and templates require no new fields.

Bend independently maps its internal automatic start profile into the attachment
face, with the segment along the outer generatrix and thickness into the face.
This feature-local mapping consumes the common container frame; it does not
reassign FRONT/TOP. An explicit Base plane remains available. New GUI profiles
are laid out toward the other end of the picked edge, retaining their width and
point identities. Start endpoints have C-to-axis constraints and origin
dimensions; end endpoints retain C-to-axis constraints and transported endpoint
difference dimensions. See [Sheet Metal](SHEET_METAL.md#bend--unbend).

Regression coverage is in `zima_cpp_bend_command_tests` (calculated Bend End
faces at four angles, two world frames and both edge directions, both off-edge
corners, Bend/Unbend, missing references, dimension edits and native persistence)
and `zima_cpp_bend_attachment_ui_contract` (new/edit Properties, points and stop
planes, free translation, manual planes, orientation controls, both auxiliary
Sketchers and save/reopen).

### Final verification of the combined repair (2026-09-16)

The rebuilt Windows development GUI and CLI passed 25 focused CTest contracts:

- Eighteen core, command and dialog contracts covering curve placement, reference
  assignment/removal, working planes, construction references and occurrences,
  Sketch, Extrusion/Revolution, Flat, Bend, Holes and both Sweep paths
  (57.04 seconds).
- The Assembly profile command contract, rebuilt against the same final sources
  (1.63 seconds).
- Six actual Windows GUI contracts: Extrusion/Revolution frames, 48 Sketcher
  return scenarios, 24 Body Origin click scenarios, 16 Bend attachment scenarios,
  working planes and owned-profile external references (307.17 seconds).

The Bend command contract also verifies 16 calculated Bend-to-Bend attachments
at 35, 90, 145 and 180 degrees, in ordinary and rotated world frames, with both
edge directions. Opposite off-edge corners, prepared dimension edits,
Bend/Unbend/Bend, missing references and native save/reopen are included.
Independent analytical volume checks caught a rotated half-turn calculation
stall: exact circular revolutions now retain analytic section edges and cylinder
surfaces instead of needlessly converting them to NURBS. The complete Bend
contract passed in 3.81 seconds after that correction.

Local logs: `build/placement-analysis/final-unit-regressions.log`,
`build/placement-analysis/final-assembly-test.log`,
`build/placement-analysis/final-gui-regressions.log` and
`build/placement-analysis/bend-analytic-final-test.log`.
These are focused regression results; the earlier full-suite limitation remains.
