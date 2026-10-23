# Reference and Up To corrections reported in 2000.prtz

Status: earlier finite Windows checks are recorded below. User review rejected
both the flat and nominal untrimmed extended-limit editing wires. A command-local
exact native preview is now in development and is not yet accepted by this record.
Ordinary plane/contour picking restoration and approved curve/plane point bindings
are also pending final verification. Portable publication remains blocked on the
completed checks and user review.

## Causes and scope

- A nonzero single-face Extrusion correction entered the multi-face profile-only
  preview. It now reuses the analytical extrusion wire and translates a private
  resolved support along the directed sweep. Symmetric reflection, both logical
  sides, Thin boundaries and source definitions retain their existing meaning.
  Joined supports and corrected Revolutions require an exact transient calculation
  of the pending operation. The nominal untrimmed draft was rejected by the user
  and is being removed. The replacement must share native limiting algorithms
  with OK, retain private immutable inputs and reject stale asynchronous results.
  It must not replay downstream history or modify document/Undo state.

- A pending Sketch has no persisted owning history row. Its intersection-result
  transform previously used a construction-entry helper that also appended Body
  Origins. An unrelated Origin point could consequently be stored as an isolated
  intersection, then become broken after the owner was committed. Results now
  consume the existing Sketch reference-frame transform without adding candidates.
  Shared placement solving and its coordinate contracts are unchanged.
- Two distinct intersections on one circular/elliptical boundary could have the
  same endpoint ancestry. For Sketch association only, certified conic/plane
  root domains distinguish these endpoints when their ordinary ancestry collides.
  Existing unambiguous names remain unchanged. The stored branch keys supply
  transient query hints so a remaining root retains its semantic name when the
  other root leaves a trimmed source. No positions, sorted roots or OCCT indices
  define identities. Ordinary Surface Intersection and Trim features retain their
  previous naming path.
- Ordered support adjacency previously required a transverse intersection.
  Exact plane/cylinder and parallel-cylinder tangent generators now derive an
  authored branch across the tangent seam. A transverse curtain retains the
  source's unambiguous domain; the tangent radial hemisphere avoids wrapping
  around the opposite side of a periodic cylinder. These derived domains do not
  reinterpret stored positive/negative choices against adjacent material normals.
  A pre-existing explicit transverse choice cannot silently become a tangent
  choice. Other tangent pairs require a certified authored G1 seam and retain
  their actual finite domains. Connected descendants of one exact support can be
  unified across its parameter seam; distinct source identities are not merged.
- Analytic supports have defined continuations; general free surfaces retain
  their actual trimmed domains, including holes. Source faces may be physically
  separated when their exact supported continuations define the required chain.
  This is not a claim that arbitrary independent/disconnected limiting regions
  or every free-surface tangency has been implemented.
- Interactive Part Feature OK may keep a structurally valid definition whose
  calculation fails. Its existing calculated boundary records the error and real
  input state. Native ownership/reference validation still rejects invalid data.
  The same shared transaction is available to `feature.create`/`feature.set` with
  `keep_failed: true`; scripted calls default to atomic rejection. This option is
  Part-only. Other feature kinds and Assembly-cut failure policy are unchanged.

The native schema is unchanged. These changes reuse the approved Feature target
and retained-patch fields, existing Sketch branch keys, calculated errors and
ordinary Undo/Redo. No user-library or project conversion is performed.

The Czech command is now `Průsečnice → reference`, paired with
`Průsečnice → obrys`. Names and the affected Part-only command error are translated
in cs/en/de/fr/ru. Ordinary Reference continues to select individual curves,
vertices, axes and datum planes; whole-face selection belongs to the separate
face/Sketch-plane intersection command.

## Actual model evidence

Read-only snapshots are under `build/2000-correction-wire-20261023/`. The saved
model containing Feature 005 has SHA-256
`95e9c7d8b4de1a3ae6818ef22cbb563d6d67e82cb36b486f82fddba772bba662`.
Its broken intersection has branch key `origin:point`; the corresponding actual
face/plane query returns no geometry. Its two ordinary edge references resolve
successfully. Neither the original file nor those valid definitions were changed.

With the corrected root domains, two relevant cylindrical faces each offer two
native curves/four branch endpoints, and the two relevant planar caps each offer
one curve/two endpoints. The formerly ambiguous queries pass on the saved model.
Evidence: `native-005-faces-final.log` in that investigation directory.

Four explicit tests of Feature 004 use its saved first planar target, a native
cylindrical target and the screenshot's 15-degree placement correction. Both
original logical-side variants of each curved source calculate successfully and
save isolated copies; Undo restores the exact initial definition. Measured model
volumes are 612929 and 613031 mm3 at the log's printed precision. These are measured
real-model results, not independent analytical blend-volume proofs.
Evidence: `native-tangent-004-final.log` and `tangent-004-1.prtz` through
`tangent-004-4.prtz` in the investigation directory.

## Verification checkpoint and remaining gates

- The original 31 Feature-definition checks and 60 single-face wire cases pass
  (both logical sides within each case).
- The expanded 75 Workspace checks pass, including explicit retention of an
  invalid geometric definition, native error persistence, Undo/Redo and repair;
  strict missing-source rejection still preserves the document.
- The final 541 Sketch intersection cases pass. They include four pending-profile
  Origin cases, 192 conic endpoint cases and four successive trimmed-root
  disappearance/return edits. Circles/ellipses cover radius/diameter or major/minor
  dimensions, driving/reference and locked/unlocked edits, free/fixed sources,
  repeated source dragging, both point orders, independent conic/plane equations,
  atomic impossible edits, native reopening and Undo/Redo.
- The final 872 ordered kernel checks pass, including 96 tangent cases spanning
  source orientations, row order, separated authored domains, periodic seams,
  opposite hemispheres and signed corrections. Independent integral volumes,
  immutable source bytes and absence of invented tangent normal-side choices
  are checked.
- All eight final native suites pass in 112.56 s, also covering the 108 bounded
  face/plane cases, read-only external Trim, Feature side reversal and draft.
  Evidence: `build/2000-final-native-regression.log`.
- The five-language catalog/placeholder/source coverage contract passes in
  30.29 s. Evidence: `build/2000-localization-regression.log`.
- Seven final main-window GUI suites pass: actual saved Feature 004 correction
  wire; ordered row mouse entry, independent eyes, Cancel and unchanged OK;
  failed-definition OK/save/reopen/repair and Undo/Redo; ordinary and axial-conic
  face/plane branches in Part and Assembly, with separate reference/contour output;
  exact spline and trimmed arc output; actual Feature 005 individual rim picking;
  and Feature side reversal. The edge-entry seed removes one already inserted
  cap reference from an isolated copy, so the test exercises new insertion rather
  than asking duplicate insertion to add another row. The remaining reference
  and all feature parameters are preserved. Neither review nor original file is
  modified by the GUI fixtures.
- Ten additional startup GUI runs pass for cs/en/de/fr/ru in both themes,
  including translated action labels, native contour type, Undo/Redo and reopen.
  Evidence: `build/2000-review-gui/results.json` and
  `build/sketch-intersection-editor/locales/locale-results.json`; the
  [machine-readable checkpoint](20261023-2000-reference-and-up-to-corrections.json)
  records the tested implementation blobs and binary hashes.
- The first synthetic wire assertion used a bounded general-surface fragment
  that did not cover the full profile even before correction. That assertion
  was replaced by the actual saved analytic plane case; general-surface domain
  validation was not weakened. A first real-model insertion assertion also
  encountered an already stored cap reference; the isolated insertion seed
  explicitly removes that existing row before exercising addition.
- Native Linux execution and portable release acceptance remain separate gates.

No OCCT work is added to hover or ordinary drawing. New support work runs only
on explicit calculation; query hints come from the existing native definition.

The read-only factory impact audit examines all 1,185 committed native assets.
It finds no intersection-bound Sketches and only three active Up To sides:
one Point in BridgeLance and two Planes in CornerGusset90, all without correction.
No factory asset uses the changed conic-root or tangent-chain contracts.
The previously converted factory files remain unchanged; the audit does not
replace packaged-runtime acceptance. Evidence: `build/2000-factory-impact-audit.json`.

The independent review copy is `Projects/2000-kontrola-20261023.prtz`, SHA-256
`dd31d70b5a09ebe7ce85be3255ce3edce7a155dd8a9ec860c7b0791dec98f27d`.
Only the invalid `origin:point` reference was removed, followed by explicit native
regeneration and Save As. The two valid ordinary references are retained. The
original model hash remains unchanged. The repair is not a general automatic
rebind or a bulk conversion of user data.

## Rejected intermediate wire: historical evidence only

The following checkpoint describes the nominal untrimmed implementation rejected
by the user. Its 310-case suite and wire claims are superseded, and cannot serve
as acceptance evidence for the exact private preview now under verification.

The user saved the successfully calculated two-target Feature into the review
copy. Its later SHA-256 is
`4f414c6aba647dd666306445a15108d57c300d2c8f023aed2cf906801e840465`;
these user-authored changes are preserved. The original `2000.prtz` remains
byte-identical. The initial single-face GUI assertion correctly stopped on this
new multi-target definition because it expected no draft notice; the real-model
probe now checks the appropriate notice for the actual selected definition.

- All 310 untrimmed-wire checks pass: two/three targets, corrected single-target
  Revolutions, both physical directions, symmetric/independent sides, Solid,
  Surface and every Thin wall mode, signed corrections, overlapping nominal turns,
  native reopening and source immutability. Thirty cases independently verify
  that a multiple-target side does not discard the other side's exact single-face
  limit or correction. Radial and axial bounds are checked independently.
- The original 31 definition checks and 60 single-face corrected-wire cases
  still pass. Side-reversal and draft regressions pass after relinking with the
  changed shared library. Five suites, including catalog/source coverage and
  actual five-language/two-theme live dialog text, pass in 44.07 s.
- The actual newly saved multi-target Feature passes complete-wire checks during
  successive correction edits. The main-window test also adds the second and
  third targets by mouse, checks the full connector wire after each addition,
  changes correction repeatedly, and verifies that the displayed provisional
  extent remains its nominal length. Cancel, independent inspection, exact OK,
  failed-definition repair, unchanged OK and Undo/Redo continue to pass.

Evidence: `build/2000-multiple-target-wire-native.log`,
`build/2000-multiple-target-wire-regression.log` and
`build/2000-review-gui/ordered/`. No exact modeling algorithm, persisted field,
source reference, placement solver or render style changes in this follow-up.
The draft uses the existing analytical wire paths; no OCCT call is added to entry.
