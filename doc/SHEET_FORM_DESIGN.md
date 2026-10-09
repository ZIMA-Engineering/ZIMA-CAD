# Sheet Form design

User agreement: 2026-10-07. This document records the intended behavior;
implementation and verification status must be recorded separately.

## Native library definition

### Corner variant agreement (2026-10-08)

#### Closed-solid implementation checkpoint (2026-10-09)

The current library candidate is
[CornerGusset90.prtz](../config/lib/01-SHEETMETAL/01-FORM/CornerGusset90.prtz).
It contains a native sloped-wall solid in `FORM`, empty `FORM_CUT` and `FORM_FLAT`
Bodies, and an independent `FORM_SYMBOL` Sketch copied from the exact XY closing
outline. Its editable profile has a 20 mm width, R8 cap and 10 mm centre offset.
The user's original FORM-EDGE document is retained separately. The earlier
surface/two-cut prototypes below remain experimental evidence, not this asset's
construction recipe.

The reader derives two unique closing faces on the source XY and XZ planes from
persisted visible-face identities and their matching analytic reference packet.
Reopened display triangle tags intentionally do not carry analytic surfaces;
the reader consumes the matching persisted original reference instead of
traversing OCCT. The complete solid archive is copied into the inserted native
definition. Recognition, preview, Properties and picking do not calculate a
body. There is no new native schema field or required sidecar.

Placement selects two perpendicular outer flat sheet faces adjacent to the same
90-degree Bend. Both offsets are zero. A third point, transverse straight edge
or plane establishes the signed station along the Bend. In-plane rotation is
disabled. The command-local resolver verifies the actual persisted Bend cylinder
and selected oriented sides; it consumes the shared placement implementation
without changing its contract. Source-body order is not role identity. An
author-created cross-Body dependency still follows the ordinary Body ordering
rules; the supplied manufacturing symbol has no such dependency.

Explicit confirmation reuses the complete calculated source solid, removes its
two closing faces with native Shell, trims the wall against the real outer Bend
radius, subtracts the complete closed cutter and joins the formed material.
Only the actual inner and outer transition routes are rounded: inner `t`, outer
`2t`. These internal Fillets use exact rational circular parameterization and
`1e-7` mm spatial approximation tolerance. Ordinary Fillet retains its existing
parameters. Intermediate results do not publish meshes, properties or Undo
transactions. Bounded preparation reuse depends on the immutable source and
actual thickness; changed inputs invalidate reuse.

The `(t, Ri) = (1, 2)` mm implementation passed native connectivity, strict BRep
curve-on-surface checks, independent GK volume and centroid/tensor calculations,
embedded-copy save/reopen and regeneration. The fixture uses two genuine Flats
attached to the Bend, 120 mm axial width and 50 mm extension. A short earlier
20 mm fixture did not contain the larger source footprint; its larger-source
failures must not be interpreted as supported-thickness limits. A separate
unattached-flat fixture also could not verify ordinary Unbend attachment.

Native Pattern and Mirror consume connected cutter and formed-material operands
derived from the completed rounded change when a downstream copy or sheet-state
operation actually needs them. Retaining only tiny Boolean differences produced
invalid p-curves in copied blends and was rejected. A private geometry copy can
reproject an inconsistent p-curve and recalculate SameParameter; it preserves the
3D curve, owning surface and native identity and rejects tolerance growth beyond
the document's existing budget. Cached source/stock is not repaired in place.
Original references belong to the completed rounded feature/copy, not its
pre-rounding Shell. Ordinary planar Form operands are unchanged.

GUI verification passed creation from the library, exact hover/click identity on
both outer planes, independent inspection, rollback, Cancel, unchanged OK,
longitudinal placement, definition replacement, Undo/Redo and native reopening.
Complete GUI creation measured **1.583 s** in the final Windows Release run
(the preceding run measured 1.636 s), including explicit
calculation, commit and scene publication. Grid scanning in the GUI test is test
setup and is not part of the insertion timing. The earlier small-source native
confirmation measured 1.179 and 1.423 s. These are fixture observations, not a
universal performance promise.

The fixed-radius construction fails for the tested `Ri = t` combinations at
`t = 0.5, 1, 3` mm. The inner Fillet builder reports a faulty contour, rather than
an otherwise completed shape failing display validation. No smaller radius is
silently accepted. The feature reports a localized transition-fit error; its
definition remains available under the ordinary failed-feature contract.
Two adaptive-radius experiments were rejected: a variation confined to the Bend
failed both orientations; a taper extending onto the flats passed only one of
two equivalent orientations and took 4.878 s. Their production code was removed.
The final asset also passed all 24 actual native Body orders, independent copy
role lookup, stale-source calculation rejection and oriented source-frame
equations. The supplied symbol's discarded projection dependencies were removed
during asset authoring and actual references were resolved again before saving.
The production dependency resolver was not changed for this asset correction.

The final native roundtrip/copy suite passed in 119.78 s and the complete GUI
sequence in 128.86 s. Four supported `(t, Ri)` pairs passed both admissible
orientations and rejected the other 14 choices per pair: `(0.5, 2), (1, 2),
(2, 4), (3, 6)` mm. Each of the three `Ri = t` rejection cases rejects all 16
choices. Final individual insertion observations were 1.170–1.713 s. These
suite timings include independent equations and persistence/copy checks; they
are not insertion timings. This checkpoint does not claim that tight bends are
solved or that arbitrary definitions always fit. Linux execution and packaged
release acceptance remain separate verification gates.

#### Revised solid-based direction (2026-10-08, evening)

The user subsequently authorized finding the simplest repeatable construction,
and proposed using a closed FORM solid as the complete Boolean cutter instead
of two cutting Sketches. For this 90-degree variant, compare attachment to the
two adjacent inner and outer planar sheet faces on equivalent physical geometry.
Choose the final attachment side from measured performance and stability;
do not assume that an outer attachment is faster. Derive the cut from the authored solid, create its
thin wall through the native Shell/offset mechanisms, and evaluate inner and
outer transition fillets at the actual destination Bend. The two perpendicular
closing faces of the source solid establish its attachment planes. An empty
FORM_CUT role is intended for this solid-based variant; manually authored cut
Sketches should not be required. Preserve the selected side references.

This supersedes the earlier inner-plane/two-cut prototype as the preferred
direction. The earlier prototype measurements below are evidence about that
prototype only, not verification of the new complete-solid cut or automatic
fillets. The solid-based route must be compared for valid geometry, native
ancestry, repeated insertion, placement, destination thickness/radius changes,
performance, persistence and GUI behavior before it is released. Ordinary
single-cut planar Form behavior remains unchanged.

#### Closed-cutter comparison evidence (2026-10-08)

The subsequent user correction requires direct Shell construction for the corner
variant. The surface-thickening prototype adds unwanted rim/transition geometry
and its lower isolated offset time is not sufficient grounds for choosing it.
Finish the Shell, trim its material against the destination Bend's outer radius,
then blend the two real transition routes. The agreed automatic blend sizes are
**outer radius = 2 x destination sheet thickness** and
**inner radius = destination sheet thickness**. Both values must follow thickness
changes; smaller diagnostic radii below are experiments, not the product rule.
The existing Ventilation Window remains unchanged unless a solid-based definition
proves simpler while preserving its explicit opening and all existing behavior.

The experimental native test `--compare-body-corner` uses the same simplified,
sloped-wall solid from FORM-EDGE for both attachment sides. The source solid and
the destination stock are prepared before timing. Each measured insertion
includes support validation, a rigid placement, the complete-body Boolean cut,
surface thickening when the preparation is cold, the union, viewer geometry,
mass properties and native BRep serialization. It does not measure library
loading, dialog confirmation, automatic source-cap recognition or transition
Fillets. This is Windows Release with the pinned OCCT 8.0.0 implementation.
The working changes are based on commit
`b066b1a554e87f7e5468fe6fd1c4ceaf58b9b790`. The generated comparison definition is
`build/form-diagnostic/CornerGusset90SimpleInnerLip.prtz`, SHA-256
`063d1eb130bdbf50cc0eebafc4953384680f66a9e662c56c0de1f20435b62ece`.
Its cutter consumes the complete first solid; the prototype's two inset cutting
Sketches do not participate in this branch's Boolean subtraction.

The nine `(thickness, inner Bend radius)` pairs are `(0.5, 0.5)`, `(0.5, 2)`,
`(1, 1)`, `(1, 3)`, `(2, 1)`, `(2, 4)`, `(3, 1)`, `(3, 3)` and `(3, 6)` mm.
All 36 supported insertions passed exact BRep validity, one connected solid,
independent GK volume integration and unchanged Sheet Cut manufacturing records.
The 108 incompatible side/intersection/direction combinations were rejected.
For each equivalent inner/outer attachment pair, surface area and volume agree,
and Boolean differences in both directions have zero volume. The selected side
identities remain distinct despite the equivalent physical results.

Insertion observations range from 0.118 to 0.239 s. Each pair's first insertion
on each side prepares its offset; its second insertion reuses the bounded source
preparation. The measurements show no convincing attachment-side speed advantage.
Outer-face attachment is preferred because it directly fixes the visible outer
shape and avoids deriving a thickness-dependent frame translation. This choice
is based on simpler placement semantics, not a claimed geometric speedup.
Local evidence is in `build/form-diagnostic/corner-body-cut-range-*.log`.

The existing native Shell operation was also tested on the same closed source,
removing its two actual perpendicular closing faces. Positive thicknesses 0.5,
1, 2 and 3 mm passed BRep, connectivity and independent volume checks; the measured
Shell step took 0.0247 to 0.0286 s. The prepared open-surface thickening step took
approximately 0.0065 s at 1 mm. These are different wall construction paths;
their isolated stage timings do not prove equivalent complete end-to-end results.

The earlier diagnostic transition Fillets were not yet a reliable construction rule. Selecting
all joins includes coplanar and sheet-boundary edges, which cannot be Filleted.
After excluding those and separating the two sheet sides, inner transitions pass
at radii 0.1, 0.25 and 0.5 mm, but the outer transition fails validation. An earlier
overbroad 1 mm experiment terminated the diagnostic process in OCCT. That case
must not enter the interactive command. The unrounded closed-cutter results are
valid; complete automatic smoothing, solid-only definition reading, empty
FORM_CUT insertion, GUI placement and persistence remain unverified for this
experimental route. No release is claimed from these benchmarks.

#### Prescribed blends and property calculation experiment

The subsequent direct-Shell experiment removes the two source closing faces,
trims the wall against the actual outer Bend cylinder, and reuses the ordinary
native Fillet implementation for inner `t` and outer `2t`. At `(t, Ri) = (1, 2)`
mm, all four supported placements passed strict BRep validity, one-solid
connectivity, independent serial GK volume integration and Boolean equivalence
between the two physical attachment sides. Twelve incompatible placements were
rejected. Both prescribed blends rendered in the native viewer. This remains an
experimental kernel request; the solid-only library reader and complete GUI
insertion/persistence contract are not yet verified.

Internal blends do not publish intermediate meshes, mass properties or history
transactions. The complete insertion then measured 2.934–3.391 s in Windows
Release, including the final viewer packet and properties. Its geometry stages
consume approximately 0.25 s; rational-face integration dominates the remainder.

The corner-only property experiment runs OCCT's independent face-volume
integrations concurrently with the original common reference point, `1e-12`
tolerance and span handling. It reduces contributions in the original face
order; it does not change geometry, integration equations, precision or the
ordinary feature path. A standalone comparison with one, two and four workers
matched the serial result within `2.1e-12` mm³. The complete `(1, 2)` insertion
then measured 2.037–2.151 s. Running the unchanged full-tensor integration
concurrently with the face-volume work reduced the four insertion observations
to 1.667, 1.674, 1.807 and 1.724 s. Independent serial calculations verified
volume and all centroid coordinates within `1e-6`. All nine inertia matrix
entries use an absolute `1e-6` floor plus eight machine-epsilon units relative
to the largest absolute tensor entry. Off-diagonal products can cancel large
terms, so their own small output value is not a valid rounding scale. This
accounts for floating-point rounding of the independent
full-BRep integration versus native Body tensor aggregation. Strict BRep
validity, connectivity and physical-side equivalence also passed.
Source-file loading, stock preparation, independent
verification and GUI confirmation are outside these insertion observations.
Evidence: `build/form-diagnostic/corner-shell-single-result.log`,
`corner-volume-parallel.log`, `corner-shell-fast-1-2.log` and
`corner-tensor-overlap-1-2.log`.

A second serial run measured 1.698, 1.710, 1.737 and 1.689 s with the same
four supported and twelve rejected placements; the same independent checks
passed. Evidence: `build/form-diagnostic/corner-speed-final-1-2.log`.

Related Windows native regressions passed for ordinary Form geometry, the
six-scenario Pattern/Mirror spatial/flat/restoration matrix, copies after Unbend,
ordinary Fillet document transactions, Shell and Surface Thicken. The new
independent tensor checks initially rejected Body aggregates at large moments
because their absolute comparison was below floating-point rounding. The
comparison now uses the complete tensor scale described above; product
integration and document results were not changed to satisfy the test.
Evidence: `corner-speed-regressions.log`, `corner-speed-copy-regressions.log`
and the final six-scenario pass in `corner-speed-copy-final.log`.
The optimized corner guide resolver also passed the independent original-guide
and tangent-visibility comparison on all four `(1, 2)` placements in
`corner-speed-guide-verification.log`. That diagnostic ran concurrently with
regression checks and is not performance evidence. The latest native GUI
diagnostic rendered both sides successfully, and the translation GUI verified
all five languages (`corner-speed-preview.*.log` and
`corner-speed-translations.*.log`). This rendering-only diagnostic does not
verify solid-only library insertion or its persistence contract.

The extended prescribed-radius matrix exposed seven failing pairs among the
nine thickness/Bend-radius pairs listed above. Only `(0.5, 2)` and `(1, 3)` passed
that matrix; `(1, 2)` also passed separately. The failing pairs are not accepted
with reduced radii. Extending the transition selection across the actual curved
Bend skin yields ten native edges per side but does not resolve these failures.
Reversing the order of the two blends also fails at `(0.5, 0.5)`, `(1, 1)` and
`(2, 4)`. A joint calculation of both radii fails at the same three pairs and
provides no material speed improvement at `(1, 2)`; that experiment was removed.
The remaining geometry issue requires a different transition construction,
rather than reduced radii. These results are not release gates.
Changing the GK projection plane was also rejected: its first volume differed
from the retained serial result by approximately `2.5e-5` mm³, exceeding the
independent comparison tolerance.

Localization review for this experiment: no product UI strings were introduced;
existing validation keys are reused. Windows was measured; Linux verification
remains outstanding.

The native GUI diagnostic rendered the exact closed-cutter result from both
sides. The original Bend strip no longer bridges the formed recess; the sloped
source walls are retained. The diagnostic publishes the calculated viewer packet
into a stock-document View, rather than saving a fabricated Form history/cache
match. Its JSON packet is disposable test output, not a document dependency.
Captures are `Projects/test/corner-form-inside.png` (exterior) and
`Projects/test/corner-form-outside.png` (interior); those camera suffixes predate
the current geometry and are not material-side identifiers.

The ordinary planar Form geometry regression passed after this experiment,
including spatial/flat states and native save/reopen. The Surface Thicken
regression passed exact planes, curved and periodic surfaces, spatial B-splines,
connected sewn skins, all offset sides, native ancestry, persistence, no-op and
Undo/Redo. The translation contract passed `cs`, `en`, `de`, `fr` and `ru`.
Linux verification and complete solid-only Form GUI insertion remain pending.

Localization review: this comparison adds diagnostic output and English design
documentation only; it introduces no new product UI strings. Existing corner
validation strings still require the normal five-catalog validation before release.

One standalone Sketch in `FORM_CUT` selects ordinary planar Form behavior.
Two perpendicular cutting Sketches select the corner variant, initially limited
to a 90-degree Bend. This classification comes from the native role's Sketches,
not the library filename, UUID, Body order or a special FORM-EDGE case.
The native XY cutting plane determines the primary attachment frame regardless
of the two Sketches' history order. Their common longitudinal direction and
plane intersection must agree. Unsupported definitions are rejected explicitly.

Both cut profiles reuse ordinary solid Extrusion tools and the existing Boolean
cut. From each selected inner plane, the corner cut runs outward through its
plate; it must not unnecessarily cut away the Bend remnant between the planes.
They do not become Sheet Cut manufacturing records. The definition remains
an independent native copy; no format field or required sidecar is added.
An empty `FORM_FLAT` still means no precut. `FORM_SYMBOL` remains a Sketch.

The Bend is a command-local exception to the planar trimmed-face insertion
boundary. The user selects the two adjacent inner planar sheet faces; these
references establish the container placement and orientation. Its virtual sharp
corner must be established from those actual adjacent sheet sides, the persisted
native Bend material frame and its real axial span. Preserve both selected side
identities. This exception does not authorize changing general container
placement.

The library author need not know the destination Bend radius. Read the actual
radius and sheet thickness from the destination Bend and adapt only the local
joining geometry of the inserted independent definition. The cuts should retain
the relevant curved Bend remnant, which joins the formed material. Following the
user's refined approach, first offset the authored surface by the sheet thickness
and place that formed volume using the two inner planar references. Then identify
and fill the local exterior gaps between that volume and the remaining Bend.
Do not require additional smoothing of the inner transition. Exterior additions
must define closed material joined to the sheet, not isolated display patches;
a successful solid union alone does not prove an acceptable outer transition.
Rounded patches at both ends of the cut may be spherical where the boundary
geometry supports that construction; a spherical patch is not a mandatory
surface type for every radius and authored boundary.

This is an explicitly symbolic representation rather than a manufacturing
simulation. The user accepts approximate local transition shape and prioritizes
fast insertion and a visually coherent result over submillimetre agreement.
The primary design and verification range is 0.5 to 3 mm sheet thickness. This
range does not certify tool capability, material strain or resistance to cracking,
and is not itself an instruction to reject other thicknesses in the UI.
This does not permit holes, invalid solids, stale references or changes to
ordinary modeling accuracy. Avoid recalculating the entire library definition
merely to adapt its local transitions. Placement interaction, local adaptation,
the residual Bend radius, connected geometry, performance, save/reopen and later
dependent operations require verification before release. This paragraph records
the intended design, not a completed or verified implementation.

The prepared example is named **Corner gusset 90 degrees**. Its cutting profiles
retain the exact filleted boundary curves. Coincident endpoints must share native
graph identity, not merely appear closed at the display resolution.

### Industry references reviewed (2026-10-08)

- [Wilson Tool: One Hit Gusset Press Brake Application](https://wilsontool.com/en-us/resources/one-hit-gusset-press-brake-application)
  demonstrates a formed gusset produced with a dedicated punch and die. Use
  "formed gusset" or "sheet metal gusset" to distinguish this feature from a
  separate welded reinforcement plate.
- [SOLIDWORKS: Adding Sheet Metal Gussets](https://help.solidworks.com/2023/english/solidworks/sldworks/t_adding_sheet_metal_gussets.htm)
  accepts either a Bend face or two adjacent planar faces, with alignment and
  positioning references. It supports rounded or flat profiles and independent
  inner/outer corner fillets. This supports our placement choice; it does not
  specify our surface construction algorithm.
- [Protolabs: Standard Forming and Manufacturing Guidelines](https://www.protolabs.com/media/roognect/sheet-metal-tolerances-one-pager-2.pdf),
  page 5, ties the Bend radius of formed gussets to the available tooling. Its
  listed tooling radii are supplier-specific, not universal limits for ZIMA-CAD.

These sources do not establish a universal spherical transition or guarantee
intersection of an arbitrary authored offset with every Bend remnant. Native
geometry tests must verify that intersection. The user authorizes adjusting the
example FORM geometry to obtain a practical visual result; preserve the ordinary
native authoring and independent library-copy mechanisms.
Keep the sloped side walls in the example's surface-extrusion Sketch where they
work. The user accepts editing that authored profile to use straight side walls
if the slopes prevent a practical result. This is an example-definition fallback,
not permission to silently rewrite every inserted library profile.

### Initial corner calculation evidence (Windows, 2026-10-08)

A working copy retains the first source Extrusion and its Surface conversion,
rotates only the FORM Body into the inner-corner quadrant, and keeps the two CUT
attachment planes in native XY/XZ. Its independently authored cut contours have
a 3% inset to establish a joining lip. Projected external sources remain read-only
construction context; the inset contour is deliberately independent of the import
mapping. This is a prototype asset, not yet the released library definition.

Nine native 90-degree Bend cases cover thicknesses 0.5, 1, 2 and 3 mm and inner
radii 0.5, 1, 2, 3, 4 and 6 mm in the recorded combinations. Each case exercises
16 side/orientation combinations: the two consistent inner-face placements pass
and the other 14 are rejected. The 18 accepted calculations pass independent
exact B-Rep validity, one-solid connectivity and volume integration checks, and
do not introduce Sheet Cut manufacturing records. Measured incremental insertion
calculations take 0.332 to 0.658 seconds; source loading, stock preparation and the
complete GUI confirmation are outside this measurement.

Thickening now retains the complete parent set for a longitudinal edge when a
zero-extent sweep vertex has coincident start/end identities. Only identical
source and offset vertices qualify; numerical proximity is insufficient. Existing
single-parent keys remain unchanged. Standard Surface Thicken and ordinary planar
FORM geometry regressions also pass on Windows. Exterior joining surfaces,
appearance, actual corner GUI interaction, native document regeneration and
Undo/Redo, definition replacement, unfolding and Linux execution still require
verification. The initial matrix must not be presented as complete corner support.

A library definition is an ordinary native Part with ordinary editable Bodies.
The four role names are resolved when inserting or replacing the definition:

Role lookup is independent of Body creation order and Tree order. `FORM_CUT`
may be authored after `FORM`, for example when its cutting Sketch references
the already authored outer surface. Required names must remain distinct when
loading a library Part. Insertion stores the matching native Body IDs; later
renaming or reordering of the copied Bodies does not reassign their roles.
Ordinary reference dependencies still follow the Part history rules.

The factory Ventilation Window definition is authored with its cutting and
symbol Sketches in XY. Its outer shell and the separately oriented cutting
profile were rotated together, preserving source IDs, external supports,
constraints and the two R15 cut corners. The insertion frame derives from
the actual cutting Sketch; Body order and a global source-plane assumption
must not define that frame.

| Body | Definition |
| --- | --- |
| `FORM_CUT` | Cutting Sketch on the outer sheet surface; may support `FORM`. |
| `FORM` | Spatial forming geometry whose retained surface defines the outside. |
| `FORM_FLAT` | Optional cutting Sketch in the flat pattern. An empty Body means no precut. |
| `FORM_SYMBOL` | Sketch used as the Drawing symbol. |

The current `Projects/FORM.prtz` contains all four Bodies. `FORM_FLAT` is
intentionally empty. Roles must be stored explicitly in the inserted operation,
so later Body renaming does not change their meaning. Ordinary Part ownership,
history, visibility, activation and Sketch reference rules remain authoritative.

Use the agreed library location `config/lib/01-SHEETMETAL/01-FORM/` and native
file browser. Insert an independent editable copy of the definition and its
internal references into the destination `.prtz`; opening, regeneration and
export must not require the original library file or a geometry sidecar.

Global Settings now exposes the independently configurable `Paths/Forms` path,
with `lib/01-SHEETMETAL/01-FORM` relative to the factory configuration as its
default. The five-language shared Settings dialog, path persistence, Cancel and
preservation of the separate symbol path are verified on Windows. The Sheet Metal
FORM action and Tree use the `sheet-form` icon. Insertion and replacement use the
shared native file browser with a single `.prtz` filter.

The independently saved [Ventilation Window](../config/lib/01-SHEETMETAL/01-FORM/VentilationWindow.prtz)
now contains the user's four native Body roles and intentionally empty flat
role. Its document namespace and internal self-references were copied through
the native writer. No Drawing accompanies the library asset. The definition
reader validates unique roles, native ownership, active standalone Sketches
(including a general Feature in Sketch mode), a calculated outer surface and
self-contained references. Invalid or missing roles are rejected. A persisted
existing surface identity anchors the connected shell; traversal positions do
not define new topology identities.

Windows native tests passed for role validation, independent copy references,
cold regeneration with stable shell ancestry and all three 1 mm thickening
directions of the actual Ventilation Window (35.03 s). Independent checks compare
both resulting skins with source surface points offset along their normals on
every patch. The one-sided modes also retain all original skin vertices.
Native geometry and persistence now have an initial Windows regression for
insertion into a planar sheet, one connected valid solid, an empty flat-cut
variant, restoration of the authored shape, independent embedded definition
namespaces and native save/reopen. Stored Body IDs retain the roles after renaming.
The shared internal Properties dialog supports insertion and later editing,
independent definition replacement, native face entry and rotation about the
selected sheet normal. The authored definition remains editable as an ordinary
library Part; the inserted independent copy can be replaced in FORM Properties.
The initial native regression passed in 31.94 s; this includes multiple
calculations and independent B-Rep and volume checks, not one user action.

## Placement and thickness

The first implementation targets one planar Flat. Forming across a Flat/Bend
corner remains deferred. Align the definition planes with the selected outer
sheet side, with the fixed forming direction pointing into the sheet. The
defined outer surface stays in place; the sheet thickness grows inward.

Form Properties now presents a feature-specific placement form: the sheet face,
two positioning references with signed offsets, and one in-plane angle. It
uses the shared reference-entry items, entry indicators and independent inspection
eyes. The generic Origin, FRONT/TOP, flip and numeric XYZ controls are not shown.
Creation and later editing use the same internal Properties dialog.

Face entry consumes the common confirmed face-hit point and fixes the sheet
side with zero offset. New positioning rows remain empty until explicitly
selected by the user; replacing the support retains already entered references.
Either positioning row accepts a straight
segment, point, plane or planar face. A plane contributes its ordinary signed
distance; a segment contributes the signed distance to its projection in the
sheet plane, with the distance normal defined by sheet normal cross segment
direction. A point contributes the zero-angle sheet-frame X coordinate in row
2 or Z coordinate in row 3, plus that row's offset. The same point can therefore
drive both independent coordinates. Parallel/dependent constraints, curved
segments and missing sources are rejected without replacing the calculated frame.
Each position edit preserves the support side and normal. The angle rotates the
definition about that normal without changing its insertion point.

The preview axis and the ordinary result axis follow the local-Y support normal.
The result axis has no endpoint grip points. Point/text visibility controls sit
at the left edge below the definition and angle fields; the replacement action
shares the definition row. Existing translation keys cover all five languages.

Local Windows build 2026100801 passed the Form GUI, native geometry and
five-language translation suites (106.77 s total). Actual GUI confirmation at
-370 degrees measured 1.401 s, including result publication. The GUI matrix
checks manual coordinates, support selection/replacement, incomplete-entry OK
rejection, successive angles, offsets, result-axis direction, absence of axis
endpoint grips, resizing, Cancel, unchanged OK, replacement, Undo/Redo and native
save/reopen. Separate native probes at -370 and -10 degrees completed with valid
connected solids. The user's unusually long calculation was not reproduced on
this synthetic sheet; its original unsaved input is unavailable. These results
do not establish timings for arbitrary sheets or definitions. Linux verification
and portable-release acceptance for this build remain separate.

The placement equations belong only to Form. Persisted references retain their
original source identities, offsets and side flags in the existing native schema;
the shared general placement solver is unchanged. The ordinary native FRONT
solution supplies the exact side-aware orientation, with its free absolute
local-Y angle. Preview and confirmation consume the same Body-local resolver;
hover, click and cycling retain the viewer's common candidate list. No OCCT
calculation occurs during reference entry, offset editing or rotation.
During explicit reference regeneration, FORM consumes the current support's
native thickness metadata and updates its inward offset. Three successive sheet
thickness changes, actual skin metadata and thickness Undo/Redo passed.

The command is localized as **Form** in English, **Tváření** in Czech,
**Umformen** in German, **Formage** in French and **Формовка** in Russian.
The literal native role names `FORM_CUT`, `FORM`, `FORM_FLAT` and `FORM_SYMBOL`
and user-authored names remain unchanged.

The Ventilation Window's first `FORM_CUT` Sketch now rounds its two corners at
the 20 mm end with R15, matching the two quarter-circle guide corners of the
outer Form surface. This is distinct from its separate R3 spatial edge treatment.
The original 100 by 20 mm dimensions, four design points and segment identities
remain intact. The same native edit was applied to the user's `Projects/FORM.prtz`;
its previous contents were preserved as `FORM-before-cut-radius-20261007.prtz`.

The feature-specific placement extension passed eight focused Windows suites.
Native equations cover both sheet sides, signed offsets from points, straight
segments, planes and planar faces, rejected dependent lines and persisted signed
zero. The actual GUI checks successive reference/offset/angle edits, removing and
restoring both positioning rows, exact native preview/confirmation agreement,
unchanged OK, rollback, replacement, Cancel, resizing, Undo/Redo and save/reopen.
All five translated placement controls and tooltips were checked. The current
native schema and general placement implementation are unchanged. Linux execution
and arbitrary curved-sheet insertion remain outside this verification scope.

## Interactive performance requirement

The user clarified on 2026-10-07 that FORM is a symbolic manufacturing
representation. Its appearance matters; it is not a forming-process simulation
or a prediction of the exact machine-produced surface. The target is about 1 s
for an ordinary confirmed insertion, with 2 s as the maximum acceptable time.
Selection, rotation and draft display must remain immediate and must not invoke
the solid kernel. Unchanged OK must create neither calculation nor Undo entry.

The agreed input is the complete calculated, connected outer shell, not a
replay of the modeling history that created it. Its normal offset closes the
open rims into an idealized constant-thickness volume. Actual forming can thin
the sheet; FORM deliberately does not simulate that process. The main purpose
is recognizable manufacturing geometry in the model and Drawing. Dimensionable
edges and tool identification are secondary and must not make insertion slow.
User-created definitions remain ordinary native Sketch/Body models.

FORM uses the shared offset builder with a command-specific symbolic policy:
normal offsets, connected solid structure, positive volume and native ancestry
remain checked, while expensive all-pairs interference certification is not
required for this manufacturing indication. Ordinary Surface Thicken retains
its strict interference check and all existing precision requirements.
Sheet Cut does not project its normal-wall calculation onto symbolic FORM
skins. The actual FORM_CUT and optional FORM_FLAT Sketches remain independent.

Calculated surface snapshots retain opaque BRep object addresses alongside
already authored face, edge and vertex references. Addresses only locate those
identities inside that exact archive; they are never topology IDs. A matching
native history fingerprint permits explicit FORM calculation to read the
existing shell directly. Changes invalidate reuse. Both the authored history
and its disposable calculated snapshot stay inside the native Part file.

The first exact implementation does not meet this target: the actual Ventilation
Window measured 8.72 s for insertion, 8.03 s for flattening and 3.49 s for shape
restoration on Windows Release; definition preparation was 0.007 s. These are
initial observations on one model, not an accepted performance result.
The current surface-snapshot and symbolic-validation path measured 0.97 s for
cold insertion and 1.24 s after reopening the native embedded definition.
Flattening measured 0.64 s and restoration measured 1.02 s. These Windows Release observations concern the
actual Ventilation Window and include preparation of geometry, exact volume
properties, viewer data and native ancestry; they are not a universal timing
guarantee. The complete GUI insertion, from OK through calculation, commit and
scene publication, measured 1.95 s on this fixture. Repeated immutable embedded
definition parsing previously made it 4.62 s. A bounded, weakly owned parsed-source
cache now reuses that same native definition without retaining mutable Part
instances. Changing the payload or role metadata invalidates/rejects reuse.
The first packaged candidate exposed an additional unnecessary calculation:
the native Body-local reference resolver expressed a displayed +180 degree Euler
angle as -180 degrees only after preparing the first result. Cold GUI confirmation
reached 2.112 s, so that candidate was not published. FORM now consumes that same
resolver and its cached native input boundary before explicit kernel evaluation.
This command-local preparation preserves reference sides and the shared placement
contract; it does not normalize angles or signed zero globally. Initial local
measurements fell to 1.159 and 1.176 s with one kernel/result preparation instead
of two. Correctly rotated GUI insertion subsequently measured 1.183 s. Packaged
GUI acceptance is recorded with the final release. The signed Windows package
measured 1.586 s, passed the two-second gate, and prepared exactly one body/result.

The FORM rotation control uses the shared absolute local-Y angle that remains
free with a FRONT face reference. The local-Y correction is ignored by that
ordinary reference solution and must not be used as the FORM angle. The preview
also resolves the pending definition through its actual native Body-local input
frame, matching confirmation at Euler singularities. Native checks compare
0, 30 and -70 degree rotations on both sheet sides against independent Rodrigues
equations. GUI checks exercise 30, -40 and 15 degrees, fixed position and normal,
and every preview curve sample against the confirmed native frame. These changes
consume existing placement APIs without changing their shared implementation.
The former 4.91 s restoration
included repeated projection work for sheet-state-wrapped FORM references;
the same FORM-specific UV guide path now recognizes their persisted ancestry.

FORM edge guides consume existing UV p-curves and owning-wire orientation,
instead of repeatedly projecting every sample and two probes onto an entire
spline. Missing or degenerate UV data falls back to the established resolver.
Mesh deviation, display points, exact source curves and identity are retained.
Independent BRep/volume checks passed. A diagnostic comparison with the old
projection resolver passed for side directions within 0.001 in unit-vector
distance; offset splines can have slightly different nearest-point parameters.
The stricter tangent-edge visibility comparison also passed, using the same
classification threshold as the drawing renderer. The diagnostic comparison
runs the old resolver additionally and is excluded from performance timings.
The native geometry regression also passed after a Sheet Cut, verified that
symbolic FORM skins were excluded from its regions, and rejected stale surface
snapshot reuse after changing the authored shell parameters. The definition
regression passed separately. Evidence is recorded in
`build/form-diagnostic/form-latest-tests.log` and
`build/form-diagnostic/form-latest-timing-tests.log`; these are disposable local
verification logs, not required document data. Focused Windows GUI checks passed
for creation, native hover/click identity, planar rotation, independent inspection,
rollback, unchanged OK, definition replacement, Cancel, Undo/Redo and save/reopen.
The extended GUI matrix also passed actual reference-field clicking and a new
insertion point, plus ordinary Thicken Surface on all patches of the authored
Window shell (76.72 s for the complete matrix). The latter keeps the selected
shell's complete inspection wire and leaves no unconsumed surface patch.

Any simplification
must be explicit, preserve the independent authored definition and its stable
references, and leave ordinary modeling accuracy unchanged. Native Sketch/Body
authoring of user-created FORM definitions remains required.

## Replacement and dependent results

FORM Properties allows replacing the copied definition with another library
Part. Both definitions use the common definition planes. Keep the inserted
feature ID, attachment, selected sheet side, insertion position and rotation.
Replace the copied geometry and role definitions atomically. Validate later
references against the replacement; never present absent geometry as resolved.
An unavailable source retains its provenance and explicit broken-reference
state under the ordinary dependency contract. Replacement Undo/Redo restores
the corresponding resolved/broken states; this native regression passed.
Cancel preserves the previous complete definition.

`FORM_CUT` defines the spatial cut. `FORM_FLAT` defines only the optional flat
cut; an empty definition performs no precut. The spatial forming result is
suppressed in the flat pattern. `FORM_SYMBOL` supplies the Drawing symbol and
must remain distinct from cutting geometry. Add a FORM command and its semantic
icon to sheet tools and the ordinary feature Tree.

The Part viewer also displays the evaluated symbol curves on unfolded material.
These are state-owned display overlays, not placement-reference owners or cuts.
Returning to the spatial state removes the overlays. Drawing Show/Erase continues
to own Drawing symbol visibility independently, without duplicate Part overlays.
Changing the embedded symbol Sketch invalidates its native history fingerprint.

Feature Pattern and Mirror copy the forming operands, material frame and native
ancestry together. Unbend/Bend Back replay each instance's spatial and flat tools;
nested copies resolve the original FORM definition without replacing source IDs.
Reflection carries the symbol's handedness through the reflected material frame.
Properties uses the shared reference controls in a compact four-column table.
The populated required support remains replaceable and inspectable without a
clear/entry indicator. Preview and confirmed axes use a 10 mm nominal length and
do not introduce endpoint grips.

Drawing Show/Erase offers the evaluated native `FORM_SYMBOL` Sketch as one
manufacturing symbol, retaining a stable identity derived from the inserted FORM
feature. The adapter preserves authored curves and text in a standalone annotation
copy; external supports and dimensions remain source editing context. Symbols use
the ordinary nominal paper-size and readable-orientation rules. Native material-
state frame mathematics carries their attachment through Unbend/Bend Back without
OCCT. Body and exact nested occurrence transforms are applied at the existing
Drawing boundary. The source definition and its references remain unchanged.
Native checks passed for Show/Erase, rotated frames, visibility retention, Drawing
annotation persistence, suppression, two Assembly occurrences and Part save/reopen.
They also passed a real nonempty `FORM_FLAT` replacement: its 4 x 4 mm precut removes
16 mm3 from a 1 mm sheet only in the flat state; Undo restores the spatial skin.

Verification is focused, not universal. Insertion across a bend, unsupported
offset shells, recursive symbols inside FORM_SYMBOL, and direct editing of the
embedded definition are not established GUI workflows. Linux build and GUI
acceptance must be performed on the Linux host. Windows 2026100703 is signed,
published and independently verified; see [the release record](releases/2026100703.md).
Windows 2026100704 independently verifies the focused placement extension,
translated saved reference labels, two R15 cut corners and all eleven packaged
GUI cases. Complete packaged confirmation measured 1.365 s; production trust,
public bytes and update discovery passed. See [the current release record](releases/2026100704.md).

## Prerequisites agreed in the same discussion

- A valid subtractive feature that misses the body remains a valid feature
  with no material change. Its Sketch and generated tool must still be valid.
- A feature whose explicit calculation fails retains its definition, Sketch,
  parameters, reference identities and error in the native document. Mark it
  red in the Tree and display the preceding valid calculated geometry. Do not
  lose the definition by refusing to create its history container.
- Add Default immediately left of Origin in container placement. It performs
  the same whole-Origin selection as clicking the appropriate Tree Origin:
  the owning Body for a direct feature, the immediate owning container for a
  nested feature. Reuse existing entry behavior and localization.
- Retained-face conversion must hide retired automatic solid axes in passive
  Bodies as well as active ones. Editing FORM must keep the visible standalone
  Sketch in preceding FORM_CUT available as read-only reference context.
- Thicken Surface operates on the selected connected sewn shell, including
  all its patches, while preserving independent sheets and solids. Its
  Properties window starts at its natural content height.
