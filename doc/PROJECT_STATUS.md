# Current project status

The 2026-10-24 Loft follow-up completes parent-local owned Sketch placement,
missing-reference snapshots, owned parameter identifiers and numeric helper
labels. See [the follow-up verification](verification/20261024-loft-parent-origin.md)
and [Windows 2402](releases/2026102402.md) for finite acceptance and publication.

## OCCT 8.0.1 Windows release — 2026-10-24

The shared kernel pin is updated to 8.0.1. Windows Debug/Release SDK and complete
application/test builds pass. The 317-case run has 275 passes and 42 failures;
all 42 reproduce on the current implementation with OCCT 8.0.0. They remain
open. Analytical integration, selected GUI and representative timing checks pass;
see [the finite upgrade record](verification/20261024-occt-801-upgrade.md).
Windows 2026102401 portable/signing/publication acceptance passes; native
Linux verification requires its host. No document format or factory asset is
changed for the kernel upgrade.

After that Windows release, the requested standalone **Loft** command uses
planar 2D Sketch inputs only: profiles in the upper list, guide Sketches below.
The native implementation and GUI now use owned 2D Sketches with shared Sketch
Properties, Solid/Thin/Surface results, persistence and Undo/Redo. It remains
distinct from the existing Sweep profile transitions and is not included in the
immutable 2401 package. See [Loft](LOFT.md) and its
[finite verification](verification/20261024-loft.md).

Reviewed on 2026-10-24 against the current C++ implementation, focused feature
guides, verification records and public GitHub release. This is the current
capability index. Dated design proposals in the roadmap and development notes
must not be read as a list of features still missing today.

Signed Windows [2026102401](releases/2026102401.md#windows-publication-acceptance)
was published at `2026-10-24T07:47:19Z` from immutable source
`2bbbb08b02f09c7880753a5f90ff5ba9e289e057`, using OCCT 8.0.1 and Qt 6.11.0.
All 20 candidate and 16 fresh signed GUI scenarios pass. Both native smoke runs
load all 1,185 factory native documents. Exact source/resources,
17,305 immutable installed checksums, all 28 packaged OCCT DLL
hashes, production trust, complete authenticated and anonymous downloads, and
trusted 2303 updater discovery pass. No update installation is claimed.
The broader upgrade matrix retains 42 failures reproduced with OCCT 8.0.0;
see [the finite upgrade evidence](verification/20261024-occt-801-upgrade.md).
Native Linux execution remains unverified on this Windows host.

Signed Windows [2026102303](releases/2026102303.md#windows-publication-acceptance)
was published at `2026-10-23T22:45:07Z` from immutable source `96c89fd2e73ad04b64327ac6b0c648aec6b99443`.
It includes linked corner-radius editing and zero collapse, planar Reference
and straight Intersection dimensions, multi-solid Shell, consistent mirrored
Thin profiles, copied construction axes/centerlines, camera/text orientation
and transparency-independent hidden-edge display. All 20 candidate and
16 fresh signed GUI scenarios pass. Both native smoke runs load 1,185
factory documents; 7,353 exact resource comparisons and
17,301 immutable checksums pass. Production trust, all authenticated
and anonymous downloads and trusted 2301 update discovery pass. No update
installation is claimed. Native formats, private user data and the development
BAT are preserved; native Linux execution remains separate host work. Linked
finite coverage limits apply.

## Available implementation

"Implemented" means the documented command and native workflow exist. It does
not imply universal geometric convergence, every proposed extension, or a new
Linux verification. The linked guides give supported cases and finite checks.

| Area | Implemented scope | Details |
| --- | --- | --- |
| Family Table | Part/Assembly variant editor, stable dimension/presence bindings, shared parent file and linked members, shared edits, Assembly insertion/Replace, Drawing variant selection, persistence and Undo/Redo | [Family Table](FAMILY_TABLE.md) |
| Sketcher | Points, segments, circles/arcs, ellipses, splines, profiles, constraints and dimensions, dragging, Trim, native offsets, exact external references, native text and symbols | [Sketcher](SKETCHER.md), [text](verification/20261019-sketch-text-anchor.md), [symbols](SYMBOLS_USER_GUIDE.md) |
| Part Features | One Feature interface for Point, Axis, Plane, Sketch and Extrusion/Revolution; independent sides, Solid/Thin/Surface results, draft and existing extent targets | [Feature](UNIFIED_FEATURE_TYPES.md) |
| Bodies and history | Multiple Bodies, activation, history editing, Booleans, linked Body derivation, stable source references and transactional edits | [Bodies](MULTIBODY_AND_BOOLEANS.md), [derivation](BODY_DERIVATION.md) |
| Sweeps and paths | Ordered 3D Curves, 2D/3D Sweeps, multiple profile stations/Loft, Thin modes and Helical Sweep | [3D](3D_CURVE_AND_SWEEP.md), [2D](SWEEP_2D.md), [helical](HELICAL_SWEEP.md) |
| Edge and hole tools | Fillet, equal-distance/two-distance/distance-angle Chamfer, Shell, drilled channels, Hole/Opening, shaft threads and Drill Point | [Edge treatments](EDGE_TREATMENT_COMMANDS.md), [Holes](HOLES.md), [openings](THREADED_OPENING.md) |
| Copies and transformations | Part/Assembly Mirror and linear/circular Pattern, independent Save As with associated Drawings, mirrored-Part workflows | [Mirror/Pattern](MIRROR_AND_PATTERN.md), [copies](DOCUMENT_COPY.md) |
| Surfaces | Profile surface results, General Surface, Fill, Sewing, Intersection, Trim and Surfaces from solid | [Surface tools](NETWORK_SURFACE.md) |
| Sheet Metal | Flat, Bend, revolved/twisted sheet and transitions, cuts, FORM definitions, independent Unbend/Bend Back and intervening edits for supported material regions | [Sheet Metal](SHEET_METAL.md), [state operations](SHEET_STATE_DEVELOPMENT.md) |
| Solid state changes | Straightening and Restore shape for supported original geometry | [Solid straightening](SOLID_STRAIGHTENING.md) |
| Assemblies | Part/subassembly insertion, exact nested activation, mates and placement, source editing, Assembly cuts, occurrence identities and explicit dependency regeneration | [Assembly references](ASSEMBLY_REFERENCES.md), [activation](COMPONENT_ACTIVATION_COMMANDS.md) |
| Drawings | Sheets and views, projected/section/detail workflows in their documented scope, associative dimensions/chains, Show/Erase, frames/title blocks, BOM regions/balloons, symbols, PDF/DXF/image output | [Drawings](DRAWINGS.md), [manual](USER_MANUAL.md) |
| Engineering data | Parameters, Relations, materials, units, physical properties and measurements, native engineering tables and fastener libraries | [Relations](RELATIONS.md), [properties](PHYSICAL_PROPERTIES.md), [library status](FASTENER_REMAINING_WORK.md) |
| Exchange and automation | STEP/IGES/DXF import/export, supported STL output, shared GUI/CLI commands and context-aware AI console | [Coverage](CAD_COMMAND_COVERAGE.md), [AI console](AI_CONSOLE.md) |
| User interface | Czech, English, German, French and Russian; light/dark themes, internal Properties and native file management | [Manual](USER_MANUAL.md), [localization](LOCALIZATION.md), [File Manager](FILE_MANAGER.md) |
| Distribution | Native Windows portable releases, version-owned runtime/resources/source and authenticated updater; Linux has separate native-host acceptance | [Distribution](NATIVE_DISTRIBUTION.md), [updates](UPDATES.md), [Linux](LINUX_RELEASE_HANDOFF.md) |

Family Table is therefore an existing feature, not a future implementation task.

Windows 2026102301 also provides **Intersection → reference** and
**Intersection → contour** in Sketcher. They select bounded native face/Sketch-plane
branches and persist their exact source/branch definitions. Certified circles and
arcs remain native conics. See the [finite editor matrix and limits](verification/20261022-sketch-intersection-editor.md).
The approved curve/Sketch-plane extension adds isolated associative points from
native Sketch and authored 3D curves, with conservative branch validation and
authoritative unsaved Assembly sources. Rounded Sketch Mirror and intrinsic
radius annotation deletion are included in signed Windows 2026102301; see the
[point/mirror record](verification/20261023-curve-plane-points-and-rounded-mirror.md).
Windows portable acceptance is recorded above. Sketch Projection and
view-relative H/V remain separate queued work.
The [2000.prtz follow-up](verification/20261023-2000-reference-and-up-to-corrections.md)
records correction-wire, tangent-chain and intersection-reference fixes
included in signed Windows 2026102301 after native and GUI verification.
Its linked editing and single-parent-file workflow were already included in
[Windows 2026091508](releases/2026091508.md); later work extends and verifies it.
Six old standalone primitives are removed; current modeling uses the Feature
and profile workflows rather than the former primitive commands.

## Latest released correction batch

Signed Windows [2026102201](releases/2026102201.md#windows-publication-acceptance)
was published at `2026-10-22T13:49:48Z` from immutable source
`a49735ec42d3442a5d1bb0c29a0eb94c7c568e04`. It includes original preview-wire selection, aligned profile/Origin
entry, stable side reversal and numeric Direction/Relations, negative arithmetic
length/angle input, Revolution grip limits, Sketch entry/inspection corrections,
axis-based Opening direction, partial-bottom Drill Points and the accepted Sketch
command grouping/icons. Candidate and signed native smoke load all 1,185 factory
assets. Fourteen candidate and seven fresh signed GUI scenarios,
7,353 exact factory resource comparisons,
17,224 signed immutable checksums, production trust,
all authenticated draft downloads and all anonymous public downloads pass.
Trusted 2004 offers the update without installation. The native factory/template
refresh is included; private user files and the development BAT are preserved.
Linked diagnostic-path, finite geometry/GUI and native Linux limits remain explicit.

The preceding [2026102004](releases/2026102004.md) release remains immutable;
its accepted library/mass and other historical verification are preserved in
that release record. Newer verification does not erase earlier rejected probes.

Post-release development restricts [ordinary Sketch Reference entry](verification/20261022-individual-sketch-reference-entry.md)
to individual geometry and repairs [Fillet on connected segments of one original curve](verification/20261022-connected-fillet-curves.md),
verified on isolated copies of `2000.prtz`. These corrections are in the current
local build and source, not retroactively in the immutable 2201 archive.
The subsequent [symmetric Feature endpoint correction](verification/20261022-symmetric-feature-endpoints.md)
completes shared operational junctions through existing Boolean ancestry and
repairs the variable Fillet radius law across a complete multi-edge rail.
Focused native, real mouse/GUI, FORM GUI and localization checks pass. An older
standalone FORM test has separate archive-reader/fixture assumptions under audit;
the verification record preserves that failure and release-baseline comparison.

The approved [native source-binding extension](verification/20261022-sketch-intersection-source-bindings.md)
now supplies cold open-solid intersection ancestry and preserves bindings through
rigid placement and derived copies. Its 108-case matrix, factory refresh and all
1,185 read-only native loads pass. All 137 Drawings pass reference/regeneration
comparison against the released runtime without rewriting their factory files.
The subsequent editor commands, native branch association and manipulation checks are implemented; see the [finite editor matrix](verification/20261022-sketch-intersection-editor.md). Windows portable publication is accepted in 2026102301.
The subsequent [coincident reference-circle picking correction](verification/20261022-coincident-reference-circle-picking.md)
removes artificial display-chord crossings while retaining native circles/arcs.
Its 392-case native matrix, reported `0101.prtz` pairs, mouse checks and all five
languages in both themes pass. A separate [preselected circular C input fix](verification/20261022-preselected-circle-coincidence.md)
passes the existing saved-equation GUI regression. Both are local post-release
changes; the immutable 2201 archive is unchanged.

## Still unfinished or planned

| Work | Current boundary / next step |
| --- | --- |
| Current correction/release batch | Completed in Windows 2026102301 with English documentation, all five UI languages, commit/push and verified signed publication. Further new defects retain priority. |
| Remaining fasteners | Cast wing-nut studies and toothed/fan washers remain unfinished. Preserve source-dimension discrepancies and private user edits. Two-part locking nuts are excluded. See [library queue](FASTENER_REMAINING_WORK.md). |
| Sketch intersection references | Native source/branch association and both editor commands are implemented and verified in the documented finite matrix, including repeated edits, native persistence and GUI entry. Windows portable publication is accepted in 2026102301; arbitrary curved arrangements and the native Linux matrix remain unverified. See [editor verification](verification/20261022-sketch-intersection-editor.md). |
| View-relative H/V | Requested display/input mapping for 90-degree turns and view flips; retain the internal geometric constraints and persisted meanings. |
| Multiple Up To faces and correction | Approved native extension and shared Feature tables are implemented. Convex/concave limits, directed retained patches, signed length/angle corrections, native Part/Assembly cuts, Relations/locks and GUI entry pass focused regressions. Factory refresh and signed Windows portable acceptance are complete in 2026102301. See [scope and checkpoint](FEATURE_ORDERED_UP_TO_PLAN.md). |
| Sketch Projection Feature | Requested as another Feature type consuming a Sketch; associative 3D result, direction, clipping and ambiguous intersections still need design. |
| File Manager directory prototypes | Discuss ordinary/linked metadata types and configurable prototype `0000-index` folders before implementation. |
| Direct mesh/Surface editing | Design ideas: independent meshes, point groups, primitives, subdivision, deformation, optional explicit point-to-model bindings and conversion to suitable shells/solids. No implemented Blender-style mesh editor is claimed. |
| FEM | Requested analysis as an additional Body type within a Part; supported materials, boundary conditions, meshing, persistence and independently checked results remain to be designed/implemented. |
| Voice and pointer context | Requested speech input and lightweight semantic mouse context for AI; design and integration pending. |
| Further modeling extensions | Dedicated spring/pipe workflows, additional pattern variants and individual unsupported surface/solid cases remain scoped follow-ups, not missing foundations. |
| Continuing verification | Real-project geometry/reference recovery, larger Assemblies, solver edge cases, production Drawing workflows and a later comprehensive Undo/Redo audit. Undo/Redo itself is already implemented. |
| Linux | Each new change/release requires native build and GUI/package verification on the Linux host. Windows results do not establish that acceptance. |

This correction/release batch is complete. Resume the agreed remaining library
and modeling follow-ups; newly reported program defects retain priority. FEM, voice and direct mesh brainstorming
do not interrupt the current work. Native Sketch offsets already exist;
STEP-derived offsets are outside the requested scope.

## Maintaining this index

User-selected interoperability bookmarks are retained in
[External resources](EXTERNAL_RESOURCES.md). The approved next Sketch reference
extension is scoped in [Sketch-plane intersection references](SKETCH_INTERSECTION_REFERENCE_PLAN.md).
The [external trim and cap-reference corrections](verification/20261021-external-reference-trim.md)
pass their Windows native and Solid/Thin fixture mouse matrices. After changing
face picking to current visible fragments, the actual `1000.prtz` final-feature
start cap passes; its end cap has no matching offered Face candidate and remains
unresolved. The former original-geometry cap test is not acceptance of this
changed visible-face behavior. The new intersection calculation has a
[51-case native foundation matrix](verification/20261021-sketch-face-plane-foundation.md);
the new Sketch command and persisted branch definition remain in progress.
The current [command-preview selection correction](verification/20261022-command-preview-selection.md)
uses the actual analytical creation/Properties wire for Modeling Features,
Extrusions and Revolutions, shared by hover, click and inspection. Exact current
Face boundaries for placement are retained. Direct GUI segment comparisons pass
for the synthetic and both L30X30X3 snapshots, along with Sheet/Solid state
inspection. The user accepted the L30X30X3 experiment. The broader
[factory origin/preview audit](verification/20261022-factory-origin-preview-audit.md)
passes all 1,185 native factory loads, existing frames/native Sketch data in
1,034 Parts, 21 finite preview GUI cases and fifteen dependent contracts.
The separately approved [Feature side reversal](verification/20261022-feature-side-reversal.md)
retains the original panels, locks, references and logical Start/End while
reversing physical direction; its focused native and actual GUI matrices pass.
Factory integration and Windows 2026102201 publication are complete. Linux
execution remains unverified. The preceding
[GPU-wire investigation](verification/20261021-feature-selection-gpu-wire.md)
retains historical `1000.prtz` feature 003 measurements. The
[endpoint equality and rounded-corner annotation correction](verification/20261021-sketch-equal-endpoint-corner-markers.md)
records the native dimension/drag matrix and actual mouse interaction cases.

Update this page, the root README and the corresponding active roadmap entry
when a capability changes status. Link implementation/verification evidence and
state release/platform limits. Preserve historical release records as history;
do not rewrite their original acceptance results into current promises. Search
engines and generated summaries can retain older snapshots and need not update
immediately after a Git push.
