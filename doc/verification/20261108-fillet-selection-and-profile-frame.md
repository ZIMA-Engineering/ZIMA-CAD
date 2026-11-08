# Fillet selection and automatic profile-frame verification

## Scope

This correction covers three demonstrated defects without changing the native
document format or the placement solver equations:

- a selected Fillet or Chamfer could lose its complete authored azure wire when
  later Boolean topology no longer carried every original route contribution;
- automatic tangent selection could cross a visible material break when the two
  tangent edges had equal opening angles but different supporting face pairs;
- automatic tangent selection needed to follow a forward compatible current
  Fillet/Chamfer edge while still rejecting a tangent curve returning along the
  incoming side;
- separately selected tangent line and arc members could be treated as two
  route ends instead of one explicitly authored continuous path;
- additive and subtractive routes needed different automatic behavior when a
  standalone current edge reaches an earlier Fillet arc;
- a curved Body terminal split into several trimmed faces could not be used as
  one analytical clipping surface, so `12.prtz` Fillet 01 could overrun or stop
  at the wrong finite face boundary;
- a route ending where two different curved faces jointly continue its support
  faces could be mistaken for one split cylinder and extended to a remote Body
  cap instead of stopping at the local edge;
- the Body wire publisher could hide a real smooth junction merely because its
  two adjacent faces were cylindrical, even when their radii or axes differed;
- asymmetric `A x B` and `A + angle` Chamfer needed to preserve its authored
  A/B side assignment over one connected multi-segment route while rejecting
  disconnected or branched selections;
- deleting an earlier Fillet could retain a visible surviving Body whose
  optimized boundary packet contained no selectable edge owners;
- a cold edit could calculate the right geometry but omit the live calculation
  proof from a byte-unchanged reused prefix packet;
- a fresh or replacement first planar support for an automatic profile could
  retain the generic row-zero FRONT role, mapping the profile to local XZ instead
  of the established TOP/local-XY default.

The user explicitly approved the narrow shared-placement change on 2026-11-08.
Existing persisted FRONT/XZ frames are not migrated merely by loading or editing
the document. An explicit replacement pick is a new authoring action and uses
TOP/XY. Manual work planes and complete-Origin entry retain their established
paths.

## Exact user-model result

The read-only reproductions used `10.prtz`, `11.prtz` and `12.prtz` under
`C:/Users/vladi/Prace/ZIMA-Engineering/03-CAD/01-ACTIVE_PROJECTS/ZE-CAD`.
None of the source files was overwritten.

- Every Fillet and Chamfer in the current `10.prtz` returns a nonempty analytical
  authored cage. A single route has two longitudinal rails and two terminal
  sections; Fillet 07 returns three such cages for its three persisted routes.
- The demonstrated tangent chain follows a current Fillet/Chamfer edge when its
  exact direction and material-side pair continue. A tangent returning curve,
  a changed side pair and an ambiguous branch remain terminal.
- The history boundary immediately before Fillet 07 contains 125 vertices,
  136 triangles and 90 edges. Opening its Properties keeps that rollback Body
  visible; Cancel preserves the original revision.
- Actual rounded-surface clicks select the matching Tree feature without face
  fill, double-click shows its radius, and selection publishes the same
  analytical cage as creation/Properties without an OCCT calculation.
- The exact demonstrated two-member selection was reconstructed from the
  current `11.prtz` boundary. Its separately persisted straight and curved
  tangent members are joined into one calculation route and one cutter. A
  shared tangent node is no longer trimmed as two independent ends.
- The demonstrated three-member `11.prtz` route preserves its source edge,
  intermediate earlier-Fillet arc and Boolean split descendant as one exact
  persisted path. The Properties count, azure View wire and calculation all
  consume the same three members; no post-selection filter removes the arc.
- A standalone additive edge terminates at the beginning of an unselected
  earlier arc. A subtractive edge alone may continue to the next planar cap only
  when the material-side probe confirms that its removal wedge passes the arc;
  the opposite orientation remains stopped at the arc.
- `11.prtz` Fillets 13 and 15 retain their verified results. Fillet 09 recognizes
  its cylinder/Fillet compound terminal and stops at that local Body edge rather
  than extending to a remote planar cap. Fillet 19 stops at the preceding arc;
  when its new profile radius exceeds that arc radius, only the construction
  tool is overgrown and the final result is clipped by the exact curved surface.
- `12.prtz` Fillet 01 recognizes both trimmed halves of its coaxial cylinder as
  one curved terminal. Its local tool crosses the terminal profile and is then
  clipped by the complete analytical cylinder; no material remains outside the
  cylinder and the analytical azure cage retains its terminal curve.
- Regenerating `12.prtz` retains true one-face parameter seams and same-cylinder
  splits, while two smooth edges between geometrically different cylinders are
  again published in the Body wire. The authored blue preview and ordinary Body
  display therefore no longer disagree because of the broad cylinder-type test.

## Regression evidence

The Windows Release build completed and the following focused checks passed:

- tangent-route traversal, including a same-angle wedge whose material sides
  rotate 90 degrees, forward Fillet/Chamfer continuations, returning tangent
  rejection and nested treatment ancestry;
- direct planar Fillet calculation at both small and large radii with an
  automatically detected angled planar end, checked against the same explicit
  end face;
- standalone additive termination at an earlier curved face, plus equivalent
  volume and area for one joined tangent route and the same line/arc persisted
  as two explicit selection items; the latter produces exactly one cutter;
- a split coaxial-cylinder terminal whose two current faces separately meet
  the two route supports, checked for validity and zero added material outside
  the complete cylinder;
- a larger additive profile clipped by a preceding smaller-radius cylinder,
  while the established smaller-profile exact-stop case remains unchanged;
- read-only regeneration of the current `11.prtz` and `12.prtz` diagnostic
  copies, including the compound Fillet 09 end and the differentiated Body seam
  classification;
- `A x B` and `A + angle` on connected two-segment bent routes, including
  reversed member order, support-role transport and atomic rejection of a
  disconnected asymmetric selection;
- complete retention and preview of a mixed source/treatment-arc/Boolean-split
  route, including the exact current-Body treatment-arc identity;
- deletion of a Fillet used by a later Fillet, retaining the later definition
  as broken while the current Body still exposes valid selectable edges;
- complete history-deletion coverage, including cold continuation, save/reopen,
  Undo/Redo and exact fresh-geometry/reference comparison;
- 48 automatic first-face cases across XY/XZ/YZ supports, both sides, quarter
  turns, offsets, calculated volume and native reopen;
- 12 thin rectangle variants with 36 two-edge Fillets, including exact
  four-stroke analytical cages, 12 thin Revolutions and 72 rounded thin-profile
  cases;
- edge-treatment commands, radii, section distances, volumes, locks, rollback,
  reference ownership and native files;
- treatment fragments, calculation methods, ordinary and symmetric treatment
  lifecycle, direct segment selection and the complete work-plane GUI contract;
- Czech, English, German, French and Russian catalog/source coverage.

Focused Windows Release results were `zima_cpp_contract_tests`,
`zima_cpp_tangent_edge_route_tests`, `zima_cpp_occt_planar_edge_fillet_tests`
and `zima_cpp_occt_planar_edge_chamfer_tests`. The translation, edge-treatment,
symmetric-treatment and treatment-selection GUI contracts also passed. The
diagnostic model copies regenerated without calculation errors; the original
user files remained untouched.

No user-visible string changed. Shared C++ remains common to Windows and Linux;
native Linux compilation and execution are a separate host gate.
