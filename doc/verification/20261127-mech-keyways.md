# MECH keyways: development verification

Windows uses MSVC Release, Qt 6.11.0 and OCCT 8.0.1. The development launcher
remains `zima-cad.bat`. This records measured checks and their limits, not a
standards certification or portable release. Linux execution is unverified.
The CLI reports development version 2026112602, base commit
`cef676514574fba417ff78e1c2918ee37af35b9f`, with `source_modified: true`.
This work has not been published as an immutable release.

## Native geometry and editing

The current native suite passes fourteen calculated variants: cylindrical hub;
first-open and last-open shaft slots; Woodruff with and without a bottom fillet;
conical shaft; conical hub with axis-parallel and generator-parallel floors;
two retaining holes; translated/rotated cylindrical hub, conical hub and Woodruff
shaft; cylindrical and conical Woodruff hubs. Transformed Bodies use translation
(31, -17, 53) and rotation (31, 22, -19) degrees.

Independent cylinder/capsule, circular-segment and cylindrical/conical hub
integrals verify removed material. Nonfilleted comparisons use 2e-5 mm3.
Woodruff floor fillets use the ordinary OCCT route: 2,883 exact UV samples
satisfy the independent torus equation, maximum residual 4.2466e-15 mm.
Pattern volume error is 0.00865482 mm3, below the explicit face-area times
linear-tolerance bound 0.0310967 mm3 at document tolerance 0.001 mm. Copied
fillet UV points have not all been independently sampled.

The suite covers repeated support-diameter changes and table boundaries,
Woodruff alternatives, stable child/annotation identities, missing-reference
frame retention and repair, compact-cache reload, regeneration, save/reopen,
Undo/Redo and exact unchanged OK. Enabling two real face-attached roughness
Symbols after compact-cache reload performs zero body calculations. Managed
Symbol removal is rejected; owner deletion removes them and Undo restores them.

Whole-feature circular Pattern is checked for filleted Woodruff, retaining-hole
and transformed conical-hub cases, including cold replay and decomposition of
its source. Full decomposition retains material and releases ordinary Sketch,
cut, Fillet and Hole operations in one transaction. Repeated free-profile edits
and drags check radius, symmetry and residuals; the depth annotation follows
subsequent actual floor movement.

Drawing annotation collection supplies reference-only width with its fit,
the persistent depth dimension and both Symbols in all fourteen variants,
without body calculations or document changes. The complete native scenario
ran in 16.905 seconds with the final signed-tolerance implementation
(`build/mech-keyways-20261127/native-final.log`), concurrently with compilation.
This is not a per-command benchmark or a claimed speedup.

## Main window, dialogs and localization

The rebuilt main-window scenario passed in 4.127 seconds including startup:

- Actual mouse support selection, ordinary placement, transient preview,
  create/Cancel and create/OK, persistence and grouped Tree ownership.
- Unchanged OK and Tree single/double inspection without calculation or revision
  change; editable length, Undo and Redo.
- ISO 21920 default and both roughness standards; normal Symbol property editor,
  user overrides retained across standard changes and metadata-only OK.
- Actual 3D Symbol double-click and Cancel with unchanged saved annotations.
- Whole Body Origin entered through Tree after clearing initialized references;
  reference correction, independent inspection, resize, save/reopen and Cancel.
  Short middle click does not commit; middle double-click over View confirms.

Evidence: `workspace-rebuilt.log`. Whole-Origin entry fills empty shared slots;
initial harness failures assumed replacement of populated slots and then three
populated rows. The harness was corrected; protected placement code was unchanged.

`ui-contract-rebuilt.log` passes SubWindow/no Apply, unchanged/changed OK, Cancel,
shaft/hub restrictions, two independent limits, View parameter routing,
read-only catalogue width, valid Woodruff alternatives and diameter changes,
five languages and live themes. `symbols-regression.log` passes ordinary Symbol
insertion/edit/Cancel, middle confirmation, Tree/View selection and five languages.
The production localization checker found and then received the missing managed
MECH-child guard translation in all five catalogs; the final result is recorded
in `translations-final.log`.

## Sketch and manufacturing annotation coverage

`sketch-outline:section:` explicitly selects an analytic cylinder/cone section
normal to its axis. Existing actual face-outline/intersection contracts retain
their meaning. R(z) = R0 + z tan(alpha), not display tessellation, defines the
section. Nonpositive radius, oblique section and inconsistent support are rejected.
Native reference identity and dependency refresh use the ordinary shared path.

Woodruff shaft depth is direct t1 at the cutter centre. Other shaft/hub dimensions
use the opposite analytic surface and the actual floor. Tilted conical-hub floors
use their exact native face/Sketch-plane intersection. Ordinary reference
constraints survive full decomposition.

The native regression matrix previously passed 541 Sketch-intersection and 1,162
coincident-reference-circle manipulation cases, plus reference refresh/commands.
These include repeated edits/drags, constraint states, orientations, source
immutability and persistence; they are not exhaustive mouse coverage.

The wider external-face and plane/generator GUI harnesses initially inspected
saved data before confirming the owned Sketch's parent Properties. They now
finish and confirm both editing levels before native persistence checks, while
recording endpoint-picking geometry in the active Sketch. A pending draft must
not be implicitly committed by Save. Both corrected harnesses pass:

- Four solid/thin and start/end-cap cases: real edge mouse selection, source
  endpoints, existing-reference preservation, Undo/Redo and native readback
  (`external-face-rebuilt.log`, 25.348 seconds).
- Eight solid/thin plane, finite-generator, segment and partial-cylinder cases:
  real selection, exact geometry, immutable source and Undo/Redo. Planar support
  rendering also covers six zoom/pan states and foreground line/circle visibility
  (`plane-generator-rebuilt.log`, 31.410 seconds).

The final runner checks each scenario's output marker as well as its exit code.
Its first combined run retained empty environment flags and repeated MECH for
the last two cases; those results were discarded and the reference cases rerun
with actual environment-variable removal. `final-gui-results.json` records the
corrected results, not those false scenario selections.

View and Drawing share one MECH parameter-dimension generator. The native
annotation Sketch remains the sole Drawing source of manufacturing depth.
The isolated Drawing test creates a cross section, shows 10P9 and the 30 mm
floor-to-opposite dimension and checks values, identities and visibility after
save/reopen (`drawing-check-final/verification.json`). The final exported section
PNG was visually inspected: 10P9 and 30 +0 /-0.2 have the intended witnesses,
readable labels and no duplicated tolerance signs. The source Part hash is
unchanged.

Visual rendering exposed a pre-existing extra-sign defect in inline explicitly
signed deviations, for example `/--0.2`. The shared text formatter, Sketch
presentation and stacked-text adapter now preserve explicit signs. Unsigned
magnitudes retain the established upper-plus/lower-minus convention. This is
display-only: no native format, tolerance value or geometric equation changes.
The rebuilt text-layout matrix passes both-positive, both-negative, unsigned,
zero, inch, inline/stacked and literal-override cases (`layout-final.log`,
7.886 seconds). The same executable verifies existing angular/diameter display,
1,344 projection/sector/side/arrow/font/tolerance variants, three real text drags,
Light/Dark/Light, native presentation readback and unchanged measured geometry.

The main GUI, CLI and three targeted test executables built successfully.
A subsequent identical build reports `ninja: no work to do`
(`build-up-to-date.log`). The unchanged root development BAT resolves to that
current Windows executable. The tracked diff passes the whitespace check.

## Native keys and reference tables

The initial factory library contains eleven DIN 6885-1 form-A parallel-key
sections with one editable preferred length each, and 23 distinct DIN 6888
form-A Woodruff sizes. Both parallel-key ends remain rounded. Woodruff models
have nominal sharp outlines. These are not all lengths or drilled-key families.
Names are globally unique: `PK-DIN6885-1-A-bxhxL.prtz` and
`WK-DIN6888-A-bxhxD.prtz`.

Every model was authored through the native CLI, explicitly regenerated, saved,
reopened and checked against exact volume/mass equations. Four successive edits
per model (136 total) verify geometry, dimension-dependent stock Relations and
isolated-copy readback. Volume error is below 1e-5 mm3, mass error below 1e-10 kg;
published rounded mass is checked separately. Native PNG previews leave source
hashes unchanged. C45 density 7,800 kg/m3 is a stated typical material assumption.

Tools: `generate-key-library.py`, `verify-key-library.py`, `render-key-library.py`
and `install-key-library.py`. Accepted build evidence is `key-library-final`,
`key-edit-verification` and `key-thumbnails`; earlier experimental stages are
not deliverables. Standards and numerical provenance are in
`doc/MECH_KEYWAYS_STANDARDS_REVIEW.md`.

Historical user diagrams remain unchanged, with five-language source captions
and added native model previews. Generator consistency passes for 72 directories
and 270 localized pages. The key chapters' 20 pages pass 1100- and 600-pixel
layouts, image loading/aspect, light/dark contrast, selection and print checks.
Two native previews and the Czech parallel-key page were visually inspected.
Known historical numerical discrepancies remain explicitly unverified.

## Native format, factory assets and protected project

Only the new `mech_keyway` payload/discriminator and managed-child bindings are
added. Existing non-MECH Parts retain their contract; internal Part JSON version
80 and native INI envelope version 57 are unchanged. No migration branches or
required sidecars were introduced.

Current native-helper checks pass all twelve isolated inputs: four Part/Skeleton
templates, two Assembly templates, four factory FORM definitions and both
surface-texture Symbols. Parts are regenerated, saved and reopened; FORM geometry,
reference contracts and repeatability are checked. Source hashes remain unchanged.
No old factory asset requires MECH conversion. The 34 new key models have their
own full authoring/readback evidence. Automated New Document creation from mm and
inch templates passes Part/Skeleton/Assembly/Drawing context and command checks
in all five languages. Inventory: 1,070 .prtz, 2 .asmz, 137 .drwz, 10 .symz,
1 .frmz and 1 .tblz under config; not all inventory files were regenerated.

The verified Toyota backup contains 172 files and 2,603,459,762 bytes. Ten native
inputs, including recovery Parts, were attempted only on isolated copies.
They use pre-existing unsupported formats (Part envelope 50, Assembly 37,
Drawing 21) and are rejected before MECH evaluation. Their modeling/reference
operations therefore remain unverified. No conversion was attempted. Hashes
confirm original production documents and backup inputs are unchanged; see
`asset-audit/results.json`.

## Remaining verification boundaries

- Full mouse coverage of both Up To references, every orientation/contact-side
  and dimension/drag/trim combination, Pattern and decomposition is incomplete.
  Native and dialog coverage is selective, not exhaustive GUI evidence.
- Drawing editing of both roughness standards and every view/section orientation
  is not an exhaustive GUI matrix.
- Conical hub floor choices are mathematically tested, not independently
  certified manufacturing defaults from a complete standard.
- Toyota conversion, Linux execution, packaging and signing are not verified.
  Future Assembly matching-key insertion and Family tables remain recorded ideas.
