# Sketch references, text and owned Surface Sketch follow-up

## Scope and implementation

- Reference-to-profile conversion retains exact native B-spline coefficients.
  Interior coefficients of source-driven curves are no longer shown as ordinary
  contour points in the View or Tree, or offered for horizontal/vertical point
  inference. Endpoints, editable spline controls and points independently used
  by authored geometry, constraints or dimensions remain available.
- Owned Loft Sketch context retains exact reference data for resolution while
  omitting other Sketches' auxiliary external-reference presentation.
- Ordinary Sketch text now requests Y-up glyph contours. The title-block/frame
  horizontal convention remains separate from this vertical convention.
  Existing stored glyph contours are not silently rewritten on load; accepting
  Text Properties rebuilds them using the corrected Sketch convention.
- Planar Surface Properties uses the existing Sketch button beside the work
  plane controls. Owned General Surface Sketches reuse the parent Origin entry
  already used by Loft. The Origin picker can reveal a preceding owned Sketch
  through its Tree row or source identity.
- Tree diagnostics recognize a Sketch's own container Origin. Reference
  resolution after a failed history operation retains exact identities from
  earlier valid sources in the recovery input. References to a failed source
  itself remain unresolved. The placement solver, persisted reference format,
  geometry side choices and last-valid-frame contract are unchanged.
- General Surface editing separates the active Sketch from preceding owned
  Sketches and 3D curves. Preceding curves remain read-only reference sources;
  future siblings are excluded. Native Sketch points are white. The owned
  Sketch's placement preview uses its selected XY/XZ/YZ plane.
- Draft curve-plane intersection resolution uses the current parent draft,
  preserves both circle intersection branches and composes Body/parent frames.
- With the user's explicit approval, newly selected placement planes enable
  FRONT orientation and the next independent plane supplies TOP orientation.
  Explicitly disabled orientation and stored reference choices are preserved.
- Twist centroid paths follow the actual twist law, with exact selectable
  start/end references for each disconnected profile region.
- Attached extrusions after a solid Twist use the existing end-frame transport
  during Straighten/Restore. The frame includes axial translation and section
  rotation, including off-centre children and their downstream extrusion.
- Twisted Sheet Properties aligns the cyan axis with the placed strip's local
  Z direction. Shared View inspection supplies length, angle and an L/R selector.
  Draft edits remain transient; ordinary inspection uses the existing commit path.
- Flat profile axes and their endpoint references follow the material frame
  through Unbend/Bend Back. Source reference packets retain the authored frame;
  separate state-owned references represent the current geometry.

## Evidence and regression matrix

Windows native build, Qt 6.11 / OCCT 8.0.1. Logs and isolated test documents are
under `build/loft31-followup-20261120/`. Original user documents were read only.

| Area | Verification | Result |
| --- | --- | --- |
| Exact converted spline | Rational projection, coefficient/pole identity, refresh, detach, source loss, serialization, offset/trim and extrusion; hidden poles do not attract H/V inference, detached editable poles do | Passed `zima_cpp_exact_spline_contract_tests` |
| Text native commands | Ordinary Y-up bounds, semantic text editing and persistence | Passed `zima_cpp_sketch_text_command_tests` |
| Glyph geometry | Existing native glyph/font geometry suite, including coordinate conventions | Passed `zima_cpp_sketch_text_geometry_tests` |
| Text and symbol GUI | Part and Assembly creation, OK/Cancel, center/middle alignment, repeated actual mouse drags, Undo/Redo, save/reopen | Passed `gui-text-followup`, 75.58 s |
| Annotation dimensions | GUI DX, DY and distance; driving, reference and lock states; underconstrained and fully constrained text; successive edits and drags with independent anchor/contour equations | Passed in the text GUI matrix |
| Planar Surface layout | Shared plane row, adjacent Sketch action, offset and Cancel | Passed `gui-planar-followup`, 2.10 s |
| General Surface owned Sketch | Parent Default Origin, translated parent, preceding Sketch Origin revealed by a real Tree click, mixed Sketch/Curve context, Cancel, save/reopen, unchanged OK and ordinary inspection | Passed `gui-general-followup`, 6.57 s |
| Reference diagnostics | Own Origin accepted; unrelated failure, truly missing plane, broken external reference, broken owner placement and repair | Passed construction Tree tests |
| Failed-history reference resolution | Three successive source sizes, exact surviving source identity and frame, save/reopen, Undo/Redo; existing failed-source rejection and last-frame tests | Passed full `zima_cpp_model_calculation_tests` |
| User `31.prtz` red Sketch | All three stored references exist; resolving against current geometry succeeds. Full reference regeneration with the correction restores valid placement; serialization/reconstruction preserves it | Passed `construction_reference_tree_tests --resolve` on `user31-1235.prtz` |
| User converted outline and Tree GUI | Actual Regenerate clears the independent Sketch's missing-reference state while the invalid General Surface remains marked; 40 internal poles disappear from View and Tree, all four exact curves remain; enter/finish Sketch and parent Cancel | Passed `gui-outline-final`, 5.79 s, and installed executable `gui-installed-final`, 5.62 s; screenshots inspected |
| Owned Loft intersection GUI | XZ and YZ side Sketches; actual source and intersection clicks, stable positions/identities, middle-button completion, persistence in pending parent and parent Cancel | Passed `gui-intersection-xz-final`, 4.03 s, and `gui-intersection-yz-final`, 4.36 s |
| Properties UI contracts | Full Windows UI suite, including ordinary/template horizontal-flip defaults and unchanged text OK | Passed `ui-full-windows.log` |
| Existing Loft native matrix | Straight/curved and reversed guide cases, independent guide-distance samples, profile correspondence, Undo/Redo and persistence | Passed full `zima_cpp_loft_document_tests`, `loft-restored.log` |
| Three-station guided Loft | One, two or four exact quadratic guides through user-profile anchors, both directions; independent guide distances | All six passed `guided-matrix-final.log`; maximum measured deviation approximately 1e-13 |
| General Surface follow-up | Mixed preceding Sketch/Curve context, active Sketch identity, placement plane and Cancel | Passed `gui-general-followup`, 9.351 s |
| Curve-plane reference frames | Eight Body/parent frame combinations; native projection/intersection geometry | Passed `curve-plane-projection-final.log` |
| Draft circle intersections | XZ/YZ, both exact branches, real mouse reference clicks, repeated draft radius update, pending-parent persistence and Cancel | Passed XZ 4.346 s, YZ 4.206 s |
| Placement defaults | All six plane orders, first FRONT/second independent TOP, explicit off and stored references | Passed `ui-contract-followup.log` |
| Twist centroid law | 81 independent samples, smooth/linear and reverse variants, disconnected profiles, endpoints, persistence and Straighten/Restore | Passed `profile-law-controlled.log`, 9.69 s |
| Twist centroid GUI | Four actual endpoint reference clicks, stable identity/position, Cancel and two native reopen cycles | Passed `gui-twist-followup`, 7.168 s |
| Solid Twist attachments | 30/90/120 degrees in both directions; two off-centre rotated extrusions, fixed start attachment, four Straighten/Restore transitions, unchanged OK, Undo/Redo, cached reopen and fresh-kernel regeneration | All six passed `twist-attachment-ready.log` |
| Existing solid-state transfers | Existing revolution, 2D/3D spline and helical continuation matrix, invalid-input rejection, no-op, Undo/Redo and cold native reopen | Passed `solid-state-regression-final.log` |
| Twisted Sheet View annotations | 64 free/attached, reversed boundary, material-side, direction and angle combinations; actual preview centres and independent angular equations | Passed `twist-dimensions-final.log` |
| Twisted Sheet GUI | Repeated real double-click edits of length/angle/L/R; ordinary inspection commits and Undo/Redo; placed preview axis; Part OK, unchanged OK and Cancel; active Part in Assembly Cancel | Passed `gui-sheet-followup`, 8.653 s |
| Attached Flat axis transport | Both twist directions, four Unbend/Bend Back transitions, axis/endpoints compared with exact B-Rep hole cylinders, unchanged authored axes, Undo/Redo, cached native reopen and cold regeneration | Passed `tab-axis-ready.log`, 7.78 s |
| Sheet axis GUI | Formed/unfolded in both directions, two opens per document, actual endpoint mouse references, exact identity/position and Cancel | All four passed `gui-sheet-axis-*`, 6.20–7.28 s each; screenshots inspected |
| Existing Twisted Sheet matrix | Formed and unfolded cuts, chained Flat/Bend attachment through depth three, sides, profile-side fixture, persistence | Passed `twisted-sheet-regression-final.log` |
| Installed GUI follow-up | Normal launcher executable: Twisted Sheet annotations and placement, General Surface owned Sketch context, Twist centroid endpoint references | All passed `gui-installed-twist.log`: 9.364 / 9.008 / 6.124 s |
| Thin three-profile Loft | User-copy rectangle/circle/slot transition; thickness 0.1 / 1 / 3 mm on both sides and symmetrically | All nine remain failing strict solid validity/interference checks in `thin-latest.log`; all three zero-thickness control cases pass |
| Localization | Existing five-language translation/catalog coverage | Passed `zima_cpp_translations_contract`, 18.22 s; no new user-visible strings |
| Follow-up localization | Five-language catalogs and affected shared keys for twist direction/right/left | Passed `translations-twist-followup.log`; L/R are intentional symbolic values |
| Template integration | Native relation templates and View feature/dimension cursor insertion | Passed `gui-templates-followup`, 3.67 s |

The text matrix exercises point-anchor dimensions. Radius/diameter, curve-contact
side and curve trimming are not operations on text anchors. Exact spline
conversion retains read-only external controls; source updates and detach are
the applicable edit operations. These checks do not establish exhaustive
coverage of every Sketch curve/dimension interaction.

The full UI suite cannot use the offscreen platform for its framebuffer-based
point-color fixture (it returns a zero-size framebuffer). The suite was rerun
with the Windows platform and passed. An initial converted-outline GUI harness
retained a parent-dialog pointer across Sketch editing, when that dialog is
recreated. The harness now reacquires the live parent before Cancel; the complete
rerun passed.

The new sheet-axis fixture originally omitted its Body; the GUI activation
helper consequently accessed a nonexistent Body. The fixture now uses ordinary
Body ownership. A second GUI harness defect retained a Tree-row pointer after
Properties Cancel rebuilt the Tree; the harness now reacquires the row by its
stable identity. Both complete GUI matrices pass after these harness repairs.
Cached display triangle tags intentionally omit analytic surface payloads, so
axis verification reads the actual saved B-Rep cylinders rather than relying on
transient analytic tags. The existing profile-side fixture already contains a
calculated mass of `0.076`; its stale assertion expecting an empty mass was
updated without modifying that native asset.

These checks cover the requested end-attached plain extrusions. They do not
establish every mixed or curved downstream feature combination. The new View
controls use the existing shared lock and dimension interaction paths; the
64-case annotation matrix does not constitute a new exhaustive Sketch solver
dimension/drag matrix.

## Local runtime deployment

The verified shared C++ GUI was installed at
`build/cpp-windows-release/zima-cad-cpp.exe`. The existing root `zima-cad.bat`
continues to launch this path. The CLI was also relinked and reports build
`2026112002`, Qt 6.11.0 and OCCT 8.0.1, with modified local source explicitly
reported. This is a local development build, not a portable release.

- Installed GUI SHA-256:
  `5C994257969E8888AB875B69CE96351A9626776B5192BCA41217B79AD114CE1F`.
- Previous executable retained at
  `build/loft31-followup-20261120/local-before-followup/zima-cad-cpp.exe`.
- The installed executable passed startup and the user-copy regeneration /
  converted-outline GUI scenario. The running user session was not terminated
  and original user documents were not overwritten.
- The later Twist/sheet-axis build retains the previously running executable
  as `build/cpp-windows-release/zima-cad-before-twist-20261120.exe`; the user
  session was not stopped. Dependency deployment emitted locked-DLL copy
  warnings. All 80 deployed release Qt libraries were subsequently hashed
  against the selected Qt 6.11 SDK and matched exactly. The CLI reports the
  same native build identity and kernel/Qt versions.
- Restart is required to load the new executable. Regenerate repairs a stored
  invalid placement state when its references are valid. Existing text contours
  require accepting Text Properties to rebuild their vertical orientation.

## Sheet dialog and transition preview follow-up

Twisted Sheet and Revolved Sheet no longer show Add/Subtract buttons. Their
existing confirmation paths already force Add; this change removes misleading
controls without changing modeling behavior. Flat, Bend and Sheet Transition
already omit this selector. Ordinary solid operations retain it.

Sheet Transition preview no longer appends three loose Sketch Origin strokes at
each profile. Profile/material wires still come from the same generator used by
Properties and ordinary feature inspection; actual Origin overlays and native
reference axes retain their existing paths. No persisted geometry changed.

Windows verification is recorded under `build/sheet-preview-20261120`:

- Full native transition suite passed in 63.03 s, including both rectangular
  side counts, tilted profiles, reference attachment, Unbend/Bend Back,
  save/reopen, cold regeneration and Undo/Redo. The material-preview checks now
  also reject loose Origin strokes.
- Transition GUI passed in 23.95 s on the private candidate and 20.77 s on the
  normal installed executable. Creation, edits,
  Cancel, unchanged OK, ordinary selection/inspection and state changes pass;
  the rectangular Properties screenshot was visually inspected.
- Twisted Sheet GUI passed in 9.02 s on the candidate and 8.82 s on the normal
  installed executable, including repeated View parameter edits and reopening.
- Five-language catalog and translated-dialog checks passed in 11.39 s,
  including absence of both sheet operation buttons and the existing Add
  result. No new visible strings were introduced. An initial test incorrectly
  expected a manually seeded Subtract definition to survive the existing
  additive-only dialog; the assertion was corrected to the actual contract.
- The normal GUI and CLI targets built successfully. Locked Qt DLL deployment
  warnings were checked: all 80 deployed Qt libraries match the selected SDK.
  The existing development launcher is unchanged. Linux was not exercised.

## Connected Flat attachment correction

The user correctly identified that the original axis-test screenshot showed a
tab beside the Twisted Sheet. Its placement references were real, but the
fixture manually drew a 0..40 profile while the resolved attachment interval
was -40..0. The earlier axis assertions verified transport only and did not
prove material connectivity. They must not be cited as evidence of a joined tab.

The fixture now resolves the actual Flat attachment and uses the same
`initialize_attached_flat_profile` helper as the GUI. Its hole is positioned
relative to the resolved endpoints. The test requires exactly one valid
manifold solid before and after every Unbend/Bend Back, Undo/Redo, cached native
reopen and cold regeneration. Both twist directions, both edge endpoints and
both material skins pass: eight cases, each with four state transitions.
The existing independent hole-cylinder/axis and endpoint checks remain active.
Evidence: `build/sheet-preview-20261120/attachment-matrix.log`.

All four actual GUI endpoint-reference scenarios were rerun on the normal local
binary with connected fixtures: right unfolded 6.15 s, right formed 5.92 s,
left unfolded 5.80 s and left formed 6.83 s. Native reopening and Cancel pass.
The formed screenshot was visually checked and shows full-width continuation.
Evidence: `build/sheet-preview-20261120/gui-connected-axis-*`.
This correction changes test construction and assertions, not product geometry
or user-visible text; the previous five-language checks remain applicable.

## Outstanding release gates

- The original immediate disappearance of the selected Loft intersection was
  not reproduced in the isolated mouse test. The foreign-reference clutter was
  reproduced and corrected. Do not describe the unobserved disappearance as
  proven fixed.
- The user confirmed that the off-contour points came from Reference-to-profile
  conversion. That defect is corrected and verified above. Manual point
  placement was an investigative hypothesis, not a separately confirmed user
  defect; it must not be presented as another established outstanding bug.
- The previously failing three-station guide matrix now passes. The exact
  common-basis Gordon retry and bounded patches retain strict result checks;
  the working two-station stretch path is preserved. Arbitrary guides away
  from numbered profile anchors remain unverified. The thin rectangle/circle/
  slot result is a confirmed remaining failure: all nine positive-thickness
  cases fail the current native offset path. Alternate offset probes do not
  establish a consistently valid solid with complete native ancestry; no
  validity or ancestry gate has been relaxed to accept them.
- The proposed faster dimension-entry workflow remains a proposal and has not
  changed the existing dimension confirmation interaction.
- No Windows portable release has been published for these changes. Linux
  build and GUI checks must run on the Linux host.
