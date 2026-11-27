# MECH keyway follow-up verification, 2026-11-27

This record covers the follow-up to `20261127-mech-keyways.md`, rather than
reclassifying the earlier delivered build as verification of these changes.
Completion checks below distinguish native mathematics, actual GUI interaction
and remaining verification limits against the final local executable.

## Changed behavior

- The default keyway Origin is on the exact cylindrical/conical support.
  Axial and angular planes can be entered in either order, with automatic entry
  advancement. The user explicitly approved the shared cylinder-to-cone angular
  solver extension. No serialized fields or document versions were changed.
- Parallel shaft cuts use a local XY Sketch tangent to the exact support and
  remove inward. Hub Sketches are transverse to the bore axis. Cylindrical
  hubs and axis-parallel conical floors use an ordinary axial Extrusion;
  generator-parallel conical floors use two transverse Sketches and a Loft.
- Hub keyways always have two visible axial Up To reference rows and open
  ends. Length is derived, not editable; inward end corrections are rejected.
  Shaft properties hide the hub target rows.
- Multiple MECH owners retain the ordering and identities of their existing
  generated operations during regeneration. Floor dimensions and roughness
  bind against the actual native input boundary, including compact saved caches.
- Hover and ordinary click use the same authored wire. Double-click retains
  the established parameter inspection path. Keyway properties have a specific
  title and grow vertically when surface-texture controls become visible.
- The first placement plane of a new 2D Sweep drives the local XY Sketch
  orientation, using the existing profile-plane authoring policy. Explicitly
  selected alternative or external Sketch planes retain their meaning.
- Exact Up To accepts contact at the Sketch rather than jumping to the far wall
  of a cylinder. Conical clipping uses the bounded positive-radius analytical
  continuation, avoiding ambiguous half-space classification of both nappes.
- Parallel keys and decomposed shaft slots use native overall end-to-end length
  dimensions. Native extrema are constrained to the authored arc and Sketch
  axis. Parallel-key width is a linear side-to-side dimension.
- Eleven parallel-key assets carry a native face appearance on the lower seat.
  The verified source assets and thumbnails replaced only unchanged generated
  factory files; the previous files and hash receipt are retained under
  `.local-backups/key-library-before-seating-20261127`.
- Key illustrations use the user's `0000-index/thumbnails` paths and sit beside
  the source drawings. The passage-diameter diagram shows a shaft section,
  inserted key and concentric envelope through the outer key corners. Shaft
  hatching is continuous; localized columns distinguish shaft d and passage D.

## Additional 2D Sweep agreement

During final testing the user reported that one or two offset Sketch segments
offered no stations. The planar path reused the helical guide's (0,0) start
assumption. A planar path must instead accept a connected chain anywhere in its
Sketch and initially start at the first authored endpoint. Existing authored
profile endpoints retain their orientation priority, including factory sheet
definitions originating at zero.

The user requested a Reverse direction button and explicitly authorized the
native persistence change on 2026-11-27. The scoped addition is the optional
`Sweep2DParameters::path_reversed` preference, default false; it is serialized
inside the existing Sweep payload without changing document extensions or
introducing sidecars or a second geometry implementation. Reversal exchanges
incoming/outgoing station roles while keeping actual profile point identities
and physical frames. An inherited end profile is evaluated transiently at a new
first station without inserting a duplicate authored Sketch.

Calculation normalizes the traversal preference to the original authored order.
This preserves both the asymmetric section's material side and the complete
cap, rim, rail and endpoint identity contract. Reversing station entry produces
the same exact kernel request and fingerprint. The existing shared kernel
transport and shared container placement are unchanged. The native regressions
check request identity, endpoint positions, exact BRep bounds, volume and area.

Inherited profiles follow canonical authored path order, including a varying
profile across a segment junction; reversal does not insert duplicate Sketches.
An endpoint-only profile extends into otherwise undefined leading stations so
that authoring at the reversed start remains reversible. Properties and the
wire preview use the same source-selection helper as calculation.

The initial open-path start selection also affected closed contours and the
existing KAPSA radius-cut regression detected a changed material side. Automatic
first-endpoint selection is therefore restricted to open paths. Closed contours
retain their established seam and traversal unless Reverse is explicitly used.
The KAPSA regression additionally reverses its asymmetric cutting section four
times and checks the independent cut volume and surface area after each action.

Inventory found one affected factory definition:
`config/lib/01-SHEETMETAL/01-FORM/VentilationWindow.prtz`.
Factory recalculation/readback, start-template refresh and GUI creation, and
sheet regressions are required before delivery. No production project is
converted or replaced. The additional tests must cover first-point order,
both directions, asymmetric profiles, zero-origin sheet behavior, native
readback, and Undo/Redo; their final results are recorded below when available.

## Narrowing Bend corner and independent RIP plane

The POKUS4 snapshot exposed a direction error in the corner interpolation.
Widening ends use `sin(theta)/sin(A)`. A narrowing end is the opposite traversal
of that corner and must use `1 - sin(A-theta)/sin(A)`; at 90 degrees this is
`1-cos(theta)`. Previously both signs used the widening law, so the terminal
spans matched while the intermediate seam left the neighbouring cylinder.
The correction is shared by the authored preview and the existing loft-section
generator. The initial sign-based correction was subsequently extended for the
three-level KURVA case described below; shared placement remains unchanged.
The existing loft approximation tolerance is unchanged; analytical preview and
section equations are checked separately from the calculated BRep.

The native Bend suite now checks 96 plane-intersection cases: widening,
narrowing and mixed ends; both endpoint orders; global and rotated placement;
45, 90 and 135 degrees; zero and nonzero gaps; repeated extension changes;
native readback. Command tests also repeat positive and negative end edits,
Undo/Redo and native saving. The complete Bend command suite passed (46.42 s).

`POKUS4-input.prtz` is a read-only diagnostic snapshot with SHA-256
`63D2AF8D1875321170604D693761CD15CF1BACF8752179415BFFF1A1866E248B`.
Its enabled corners form seven meeting pairs; enabling both ends on the isolated
test variant yields eight. Every inner preview seam satisfies the neighbouring
analytical cylinder within 1.09e-12 mm. The variant was calculated, saved as
`POKUS4-corners-verified.prtz`, reopened and cold-regenerated without errors.
The user's original corner choices and source file were not overwritten.

A new owned RIP Sketch no longer inherits the Sheet-from-Body source face.
Its first placement reference is empty and the ordinary shared picker is armed.
Editing an existing RIP retains its saved support. This is a command-local
initialization change; it does not modify shared placement solving. The GUI
regression covers new/cancel/recreate, explicit support, reopening, drawing,
conversion confirmation and persistence. No user-visible text was added by
either the Bend or RIP correction.

## Evidence and boundaries

Logs and isolated inputs are under `build/mech-followup-20261127`.

### Final Windows checks

| Check | Result | Evidence |
| --- | --- | --- |
| Native 2D Sweep, Sweep commands and helical contracts | 3/3 passed, 89.07 s total | `final-sweep.log` |
| Main-window 2D Sweep, MECH properties, all-language catalogs, FORM Sweep Sketch, FORM, Sheet Profile and Sheet State UI | 7/7 passed, 176.70 s total | `final-gui.log` |
| Sheet and MECH native group | 14 passed initially; the two failures were corrected and rerun successfully | `final-sheet.log`, `final-corrections.log`, `final-factory-corner.log` |
| Independent sheet branches | 11 passed initially, including the actual GUI matrix; the remaining Transition test passed after its Windows test-stack correction | `final-branch.log`, `final-corrections.log` |
| Refreshed VentilationWindow factory source | Placement, unchanged OK, position edit, Undo/Redo, Unbend, save/reopen passed | `factory-form-final.log` |

The Profile and Transition test executables exceeded the default 1 MiB Windows
stack. Isolated copies with an 8 MiB stack passed; their MSVC target options now
use the same 8 MiB budget as the existing application. Registered CTest reruns
passed in 10.68 s and 58.32 s respectively. No geometry assertion or calculation
precision was reduced.

The older finished-corner FORM fixture attached two independent Flats to a root
Bend, producing three independent sheet branches under the current contract.
Its single-solid expectation was therefore invalid before inserting the FORM.
The fixture now authors a connected Flat -> Bend -> Flat chain and centers the
FORM on a midpoint-anchored Bend. It retains strict single-solid, BRep-validity
and transition-radius assertions. The registered finished-corner test passed
in 2.53 s. Production branch behavior and the library corner definition were
not changed for this fixture correction.

The focused native angular placement matrix and full placement command tests
passed before the final keyway changes: cylinders/cones, both plane orders,
signed offsets, source radius/slope edits, save/reopen and Undo/Redo.

The complete older reference-assignment test has an unrelated initial-widget
failure: its fixture expects an exclusive orientation driver despite creating
no initial driver and despite the current multiple-driver contract. Its
production UI behavior was not changed to satisfy that fixture. The focused
`--cylindrical-angle` suite passed. This is not a claim that the full suite passed.

The user document is tested only on an isolated copy. SHA-256 of both the closed
original `ZE-CAD/DRAZKA.prtz` and `DRAZKA-closed-input.prtz` before testing:
`195BB2FD8025000D9F36C3985F43AB48E100677070F52E07BDBDF43BAC414CA5`.
The original and protected Toyota project are not modified.

Windows tests do not establish Linux runtime verification. Packaging, release,
commit and push follow the user's own keyway trial and confirmation. Website
work is the subsequent task, not part of this modeling change.

## Three-level conversion and fractional corner coefficients

The user approved independent numeric coefficients in [0,1] on 2026-11-27,
including the persistence change, affected factory assets and start templates.
RIP width is explicitly deferred until the user tests this package.

`BendParameters::corner` now stores two doubles. Each end interpolates between
the existing linear extension (0) and complete corner correction (1). The
complete corrected displacement is `(extension-gap/2)*law`; the coefficient
interpolates the entire displacement, including the gap. Exact endpoint values
0 and 1 retain their corresponding laws. Section counts and approximation
tolerances are unchanged. Properties, inline CC dimensions and CLI commands
share the numeric values, with finite range validation before mutation.

The KURVA example exposed two additional dependencies. Corrections of upstream
Bends move the attachment frames of later Bends, so remaining corner corrections
must run in bounded dependency waves using the existing calculated cache. Also,
extension sign does not determine the correct seam law for every return Bend.
The first and third rings share their start tangent plane; the middle ring
shares its end tangent plane. Conversion determines that choice from the exact
paired material frames and persists an optional per-end direction override.
Unattached manual Bends retain the signed-extension convention. Regeneration
and native readback therefore retain the chosen geometric branch.

The current `corner_closure.ends` payload contains numbers; optional `reversed`
entries contain booleans or null. Document extensions and version constants
are unchanged. No Boolean compatibility loader, required sidecar or alternate
geometry implementation was introduced. Diagnostic conversions of older test
files were restricted to private copies, discarded their derived cached bodies,
and used the changed native engine to calculate, save and reopen the result.

All 12 KURVA meeting pairs passed an independent adjacent-cylinder equation,
including the four third-level pairs. Maximum residual was 6.99e-13 mm, checked
before and after native save/readback and cold regeneration. This is an analytic
seam check, not a claim of that precision for the existing loft approximation.
Evidence: `coefficient-inputs/verify12-final.log`.

Native conversion rejects a terminal cylinder returning into an already visited
plane when no exact corner continuation reconstructs it. It requests an explicit
RIP instead of silently dropping the returning Bend. Existing reconstructed
crossing corners and finished STEP reconstruction retain their paths. The
no-RIP KURVA copy is rejected with the expected error; the complete native Sheet
Exchange suite also passes. Evidence: `closed-loop-input/rejection.log` and
`coefficient-exchange-final.log`.

Opening the owned RIP Sketch now finishes the parent reference picker through
the existing command lifecycle helper before entering Sketcher. Clicking Sketch
points cannot append container-placement references. The GUI regression arms
the next placement row deliberately and verifies the entire reference bundle,
position and orientation after drawing and reopening.

### Native coefficient verification

- 1,920 independent plane/interpolation cases: 45/90/120/135 degrees, rotations,
  endpoint orders, signed extensions, independent end coefficients, gaps and
  automatic/explicit seam directions (`coefficient-bend-tests.log`).
- 36 command edits, rejected out-of-range inputs without document changes,
  Undo/Redo, unchanged confirmation and native persistence; full Bend suite
  passed in the same log.
- Attached Flat endpoint identities and one-solid results pass fractional end
  edits, persistence and Undo/Redo (`coefficient-flat-final.log`). The fixture
  explicitly resets the coefficient before its separate uncorrected-width
  volume assertion. The full Sheet Profile suite passes fractional changes
  (`coefficient-profile.log`).
- Six 90/120-degree fractional cases pass Unbend, Bend Back, Undo/Redo, native
  reopening and regeneration, with folded volume preserved within 1e-5 mm3
  (`coefficient-unbend.log`, `coefficient-unbend/acceptance.json`).
- Six warmed command-edit samples per case measured complete command latency,
  including geometry regeneration. Medians at coefficients 1/.5/.25 were
  79.0/78.6/77.2 ms for 90 degrees and 71.1/91.5/77.6 ms for 120 degrees.
  These small fixtures establish the measured cost, not a universal speedup or
  a guarantee for all models (`coefficient-performance.json`).

### Factory and protected project audit

The inventory covers 1,219 native files and found 14 factory Bend assets plus
two protected Toyota current/recovery documents containing Bends. None had an
authored corner-closure payload. Verified recoverable copies and SHA-256 receipts
are under `.local-backups/bend-coefficients-20261127`. All 14 factory nose-washer
assets were recalculated, saved and reopened with unchanged authored definitions
and zero calculation errors. The six start templates were refreshed and reopened
with the changed native serializer; their bytes were already identical. Only
hash-matching factory originals were replaced (`coefficient-assets/installed.json`).

The protected current Toyota Part uses native format 50; the current runtime
already requires 57. Its isolated native reopening remains unverified because
of that pre-existing incompatibility. No compatibility path was added and no
Toyota original or recovery archive was overwritten. Historical recovery files
are retained as backup evidence, not represented as successfully converted models.

### Perforated key library

Twenty-six native keys with one or two retaining holes were authored from the
historical CSN 02 2575/02 2570 table data. Through holes and top counterbores are
ordinary editable subtractive Features. Relations keep their longitudinal
centres and through depth attached to the current key dimensions. The bottom
seat has a persisted face appearance. Each model passed explicit regeneration,
independent volume/mass checks, repeated length/width edits, constraint checks,
Undo/Redo and native readback (`retaining-verified/verification.json`).

The installed thumbnails show the counterbores from above; their native source
hashes remain unchanged. All 15 affected reference pages loaded every image in
the browser. Following the user's final layout correction, all four parallel-key
illustrations stay in one row and shrink to fit; a narrow 480-pixel check also
rejects stacked images. Captions have spacing between neighbouring figures.
New captions and coefficient/error text are localized in all five languages.

## Final upper-Flat correction

Checking only coincident inner Bend seams missed four intersections between
the upper Flats of the three-level KURVA fixture. Each intersection occupied
71.807738 mm3. A concave corner closes on the outer material skin; closing its
inner skin leaves a thickness-by-thickness overlap in the attached Flats.
The continuation correction now intersects both analytic skin endpoint motions
and selects the material span that avoids this overlap. Direction detection
recognizes the corresponding inner or outer common tangent. It retains the
existing dependency waves, authored identities and numerical tolerances.

The rebuilt copy contains 12 Bends and 13 Flats. Exact Boolean volume checks of
all 78 Flat pairs find zero overlaps above 1e-6 mm3. All 12 matching Bend seam
pairs still pass the independent adjacent-cylinder equation, with maximum
residual 6.99e-13 mm, before/after save and cold regeneration. The complete
native Sheet Exchange suite passes after this correction. Evidence:
`flat-overlaps-before.log`, `flat-overlaps-after.log`,
`upper-flats-corners.log`, `upper-flats-exchange.log`.

## Document-unit creation defaults and Point frames

New solid Fillet/Chamfer commands and Sketch curve Fillet use one creation-only
default policy: 1 mm in metric documents and 0.040 in (1.016 mm) in inch
documents. Both Chamfer distances receive this default. Explicit command
arguments override it. Editing existing features and changing display units
never resize authored values. Four native mm/in x Fillet/Chamfer cases pass
unit changes, Undo/Redo, save/reopen and explicit regeneration
(`treatment-defaults.log`).

The first surface reference of a new Point uses the established automatic
profile TOP policy: local Z is the normal of local XY. This includes owned
Points in 3D Curve and 3D Sweep. The command override runs only with an empty
reference bundle and outside whole-Origin entry, so stored choices and
replacement references retain their existing meaning. The shared placement
solver is unchanged. The native 3D Curve/Sweep suite passes exact arc normals,
sides, spatial tangency, variable profiles and persistence
(`curve3d-native-final.log`).

These final changes introduce no user-visible strings or new persisted fields.

The final GUI group passed owned Point editing (28.84 s), pending Curve Points
(2.77 s), surface-placement selection/offset branches (98.42 s), creation defaults
(5.04 s), Sheet-from-Body/RIP (19.50 s), same-Body sheet attachments (17.49 s)
and five-language catalog validation (18.46 s). The Sketch curve Fillet GUI
matrix passed all 38 cases in 141.96 s: native/virtual curves, Part/Assembly,
both selection orders, mm/in defaults, manual R/diameter, repeated value edits
and drags, deletion, Cancel, Undo/Redo and native readback. This creation-default
change does not modify Sketcher equations. Evidence: `defaults-points-gui.log`.
The curved-surface test initially could not create its output in an absent
fixture directory; the harness now creates that directory explicitly.

The repaired curved-surface GUI harness passes in 30.48 s. Its eight cases
combine circular/elliptical cylinders, Curve/Sweep and plain/transformed
Body/parent frames. Real mouse picks are checked against independent surface
equations; repeated signed offsets retain exact local XY normals. Planar caps
retain Origin projection and the same XY convention. Every Sweep station opens
its actual section editor and receives an asymmetric section, with stable
point/incoming identity, a right-handed frame and a normal matching the exact
station tangent. Cancel performs no Body calculation. Saved and redone Points
retain their positions, rotations and TOP reference roles
(`curved-point-final.log`).

The final corner GUI check passes in 6.07 s, including independent fractional
end edits, five-language labels, Cancel, native reopening and Undo/Redo.
`corner-ui/corner-result.png` was inspected after the final correction; the
upper walls no longer intersect. This is backed by the exact Flat volume
checks above, rather than inferred from the screenshot alone.

The user trial copy is `ZE-CAD/KURVA-OPRAVENO-20261127.prtz`, SHA-256
`F8DC1EC2B06FF1410557E471EFF79498BAF156005FAEE34E42B7D02CA6747C43`.
Its source and destination hashes match (`trial-copy.json`). The original
`KURVA.prtz` remains at SHA-256
`5BB9B13965DC628190CE803A62D17A11E2888AF77FC52CB968496FC368D412A9`.

## Subsequent user corrections

The user deferred a separate RIP-width parameter in favour of Bend and Flat
corrections. Converted attached Flats now receive two native signed DistanceX
dimensions, initially zero, against their exact Bend endpoint references.
Ordinary native parallel constraints retain the authored adjacent straight
edge directions. Hidden construction guides record those directions; they are
not display tessellation. No document field or format version was added for
these corrections.

The focused Flat matrix passes both source orders, rectangular/slanted and
triangular outlines, successive signed dimension edits, driving/reference
dimensions, locked/unlocked point dragging, serialization and regeneration.
The integration matrix passed material calculation, cold regeneration,
Undo/Redo and Unbend with nonzero corrections. The complete native Flat suite
also passes, including existing merged-face and reversed-reference cases.
On a converted copy of the user's complex KURVA fixture, both correction
dimensions accept successive 0.5 and -0.25 inputs through the native command
and retain their identities through Undo. Evidence: `flat-final-native.log`,
`flat-reference-retention.log`, `sheet-unbend-corrections.log` and
`user-flat-native.log`.

The ordinary Sheet from Body GUI contract passes, including cleanup of the
transient RIP plane. This ordinary thin-box fixture has no attached Flat and
does not establish GUI coverage of the new correction dimensions. The separate
complex-fixture test passes actual View mouse double-clicks on both labels,
successive positive/negative inputs, native save, Undo/Redo and unchanged
single-click inspection. The selected dimension identifier and stored native
value are checked after each edit. The final screenshot was inspected and shows
the edited 0.25 mm magnitude and the other zero correction; no transient RIP
plane remains (`flat-corrections-ui.stdout.log`,
`flat-corrections-ui/flat-correction-after-edit.png`). Correction
labels retain the ordinary DistanceX convention: the displayed length is a
magnitude and a negative input reverses the current direction. The regression
checks the resulting native signed value, rather than requiring a negative
displayed length.

File Manager checkbox tests pass real multi-selection press/release, repeated
and mixed-state toggling, double-click, a single row and Space. The handler
consumes the release after its batch press transaction. MECH zero-angle rays
now derive from the exact radial contact direction and passed zero, signed
small angles, quadrant angles and a full turn without material calculation.

On 2026-11-27 the user explicitly approved a generator-parallel conical hub
keyway represented by two transverse native Sketches and an ordinary Loft.
Cylindrical hubs use ordinary axial Extrusion. This expands the permitted
ordinary cut child of the MECH compound, without adding a new serialization
field or a format version. Both owned Loft Sketches retain exact support and
end-plane references after Explode. Changing the source diameter, length and
cone angle resolves those references again and keeps the sections transverse.
The command selects the existing TOP/FRONT roles and numeric correction through
the established placement solver; the shared placement contract was not changed
for this follow-up.

The inventory examined 1,098 native factory/config assets and found no MECH
keyway compounds, so this expanded child contract requires no factory model
rewrite (`factory-keyway-inventory.json`). The six start templates had already
been refreshed by the approved coefficient work. The final GUI template and
new-document-options contracts pass, as do the ordinary Flat-end correction
and Sheet Exchange GUI contracts: four tests, 135.18 seconds in total
(`final-template-sheet-gui.log`). The protected Toyota project remains
inspection-only on backup copies, with the format-50 limitation recorded above.

The shaft frame matrix passed twelve native cases: XY/XZ/YZ, both axial
directions, and plain/transformed Bodies, including equivalent removed volume,
decomposition and cold reopening. The complete keyway matrix additionally covers
cylindrical/conical bores, both floor policies, open ends, Woodruff variants,
retaining holes, exact manufacturing dimensions and Drawing annotations,
roughness, Pattern, annotation-only edits, unchanged OK, repeated source edits
and native save/reopen. No material calculation occurs for authored wire or
dimension inspection. Decomposed native profiles are dimensioned and dragged
repeatedly, including locked/reference cases; this is not initial-solve-only
coverage.

The final expanded native run passes all eighteen variants, including a
generator-parallel conical hub in a translated and rotated Body. It checks
both exact section frames after repeated source diameter, length and cone-angle
changes, then cold reopening after Explode. Section equations are compared in
the same world frame as the current support; treating Body-local sections as
world-space coordinates is not a valid orientation check
(`keyway-transformed-cone-final.log`).

Independent cross-section integration checks unfilleted bore removal within
2e-5 mm3. For bottom fillets, the surface equation is also checked on trimmed
samples. Cylindrical and conical hub fillet faces match the expected cylinders
within approximately 1.04e-14 mm. The conical Loft fillet's volume differs from
the independent integral by 0.02722 mm3; its acceptance bound includes the
affected surface area times the existing 0.001 mm model tolerance, as in the
established Woodruff fillet contract. No product tolerance was relaxed to pass
this check (`keyway-final-native.log`).

The real-mouse MECH workspace test passes in 6.29 seconds: creation, hover and
selection, double-click dimensions, reference and whole-Origin placement,
unchanged OK, Cancel, reopening Properties, length editing, native persistence,
Undo/Redo, both surface-texture standards and middle-button confirmation.
This GUI fixture is a shaft; the hub variants have native mathematical and
persistence coverage, not a claim of the same full GUI matrix for every hub
variant (`mech-workspace-final/stdout.log`). The separate parameter-panel and
five-language localization contracts pass in 2.94 and 17.55 seconds
(`mech-dialog-localization.log`). No new user-visible strings were introduced
by the final frame, checkbox, RIP cleanup or Flat-correction changes.

The copied DRAZKA user example opens successfully. Before any regeneration its
saved calculation-error map already reports an unresolved original edge on
Fillet `01a21d59251476e8afb8aad49c9275e5` (named `Skica`). This prevents a clean
whole-history regeneration of that unchanged example; it is not counted as a
passing cold-reopen geometry check. The original file was not overwritten.
Evidence: `drazka-audit/history.json`, `user-drazka-final.log`.

### Conversion performance observation

On an isolated copy of the complex KURVA-RIP fixture, the native diagnostic
completed cold regeneration, rebuilding the existing conversion and saving in
36.95 seconds, with no concurrent compiler or other native test. Profiling
attributes 27.55 seconds to calculation phases and 3.72 seconds to reference
resolution; the remainder includes loading, other dependency phases and saving.
This is a scoped diagnostic observation, not GUI click-to-ready timing or an
established speedup. No precision, supported geometry or reference checks were
removed. The earlier 52.73-second run overlapped compilation and is not a valid
before/after baseline. Reducing repeated calculation remains an unresolved
performance opportunity (`performance-profile/conversion-idle.json`,
`performance-profile/conversion-idle.log`).

Evidence for this follow-up is under `build/file-checkbox-20261127`. These
results were obtained on Windows with the shared native implementation; Linux
runtime verification remains outstanding. The normal repository-root
`zima-cad.bat` continues to launch the current local C++ build. This is a local
development delivery, not a committed portable release.
