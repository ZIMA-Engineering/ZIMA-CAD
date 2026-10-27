# Current project status

Reviewed on 2026-10-27 against the current shared C++ implementation, focused
native/GUI contracts and the live GitHub release list. Dated proposals and
historical release records remain evidence for their original scope; they are
not the current feature backlog.

## Current release boundary

Signed Windows [2026102701](releases/2026102701.md#windows-publication-acceptance)
is the latest published stable build. It was published at
`2026-10-27T12:12:44Z` from immutable source
`21466680c6c065ee9b2defea6735338fd7229ded` with Qt 6.11.0 and OCCT 8.0.1.
Candidate and signed native smoke each pass with all 1,185 factory assets;
signature/trust, 17,401 immutable archive entries, authenticated and anonymous
public downloads, and trusted 2026102603 updater discovery pass.
Native Linux execution remains a separate host requirement.

The release includes compact exact viewer-cache packets, responsive saved-tab
close, restored oriented-Rectangle axis entry and safe rounded-Mirror deletion.
See the [release record](releases/2026102701.md) and
[publication evidence](verification/20261027-windows-2701-publication.json).

Windows and Linux use the same C++ modeling implementation and pin OCCT 8.0.1.
This release changes neither the native format version nor the protected
container-placement, geometry-side, reference-identity or precision contracts.

The current source identifies Windows candidate `2026102702`. It adds exact
unchanged-cache reuse, direct pooled-string loading, immutable calculated packets
for metadata history, responsive New Feature and Sketch interaction, command-only
axis endpoints and View navigation during calculation. The authored native
format and precision remain unchanged. See the
[candidate record](releases/2026102702.md)
and [measured performance/gate](performance/20261027-native-cache-close-and-redesign-gate.md).

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

Current Sketcher includes the signed Intersection reference/contour, associative
curve/Sketch-plane points, view-relative H/V, rounded Mirror and junction work
described by the linked verification records. Six old standalone primitives
remain removed; current Part modeling uses the unified Feature and profile
workflows. Historical acceptance details remain in their immutable release
records rather than being repeated here.

## Current correction batch

The published 2601 Skeleton work is deliberately narrow. A pure last-Feature
thickness edit can reuse an exact 25-operation prefix and unchanged local
reference state. Placement, profile-frame, external-document, multi-Body,
Boolean, Section and downstream-reference cases keep the complete resolution
path. The final GUI test passed two edits, draft isolation, Undo/Redo and native
save/reopen. The warm confirmation is 4.627 seconds, so the desired one-second
confirmation target is not claimed. See the
[measured limits](performance/20261026-skeleton-history-edit.md).

Published `2026102602` adds direct Flat-to-Flat attachment on every
outer perimeter edge, numbered Flat/Bend/Pattern defaults, sheet and cylindrical
face selection independent of axis endpoint-marker visibility, and parent-driven
Mirror/Pattern construction axes and endpoints. Focused native/UI contracts and
read-only copies of the reported Skeleton and sheet Part pass on Windows. The
release changes no native format, container-placement contract, calculation
precision or shared OCCT 8.0.1 pin. Native Linux acceptance is not claimed. See
the [release record](releases/2026102602.md).

Published `2026102603` retains the exact 25-of-26 Skeleton history-prefix reuse
and adds compact final-result cache reconstruction plus in-place cache expansion.
On the protected 51,025,383-byte copy, accepted samples observed
18.206--18.464 s old-cache load, 15.975--16.237 s save and 13.204--13.579 s
rewritten-cache reopen. Complete authored definitions and every calculated
history packet match. No precision was reduced and the authored native format
version did not change. The candidate also contains the Sketcher, unit-entry,
naming, sheet-conversion and export changes listed in its
[release record](releases/2026102603.md). Windows publication acceptance passed;
native Linux verification remains pending on the Linux host.

The 2502 source tag was not published as a GitHub release. Its changes are
superseded by published 2601; the historical
[2502 record](releases/2026102502.md) remains the finite junction/selection
evidence. Published 2501 and every older signed release remain immutable.

## Still unfinished or planned

| Work | Current boundary / next step |
| --- | --- |
| Current correction/release batch | Windows 2026102701 is published and accepted. Candidate 2026102702 contains exact native reuse, responsive interaction and command-only axis endpoints; signed Windows publication and Linux verification remain separate. |
| Skeleton interaction | New Feature opening measures 0.280--0.353 s, Default Origin 0.272--0.280 s and the representative Sketch move about 6 ms. Unchanged-cache save measures 0.587--0.633 s and rewritten-v2 open 5.991 s. An unchanged late Feature OK still measures about 47.128 s; the hard calculation target is not claimed. A broader redesign is adopted only after a tenfold representative improvement with exact equivalence. |
| Remaining fasteners | Cast wing-nut studies and toothed/fan washers remain unfinished. Preserve source-dimension discrepancies and private user edits. Two-part locking nuts are excluded. See [library queue](FASTENER_REMAINING_WORK.md). |
| Sketch intersection references | Native source/branch association and editor commands are implemented in the documented finite scope. Arbitrary curved arrangements and the native Linux matrix remain unverified. See [editor verification](verification/20261022-sketch-intersection-editor.md). |
| Sketch Projection Feature | Requested as another Feature type consuming a Sketch; associative 3D result, direction, clipping and ambiguous intersections still need design. |
| File Manager directory prototypes | Discuss ordinary/linked metadata types and configurable prototype `0000-index` folders before implementation. |
| Direct mesh/Surface editing | Design ideas: independent meshes, point groups, primitives, subdivision, deformation, optional explicit point-to-model bindings and conversion to suitable shells/solids. No implemented Blender-style mesh editor is claimed. |
| FEM | Requested analysis as an additional Body type within a Part; supported materials, boundary conditions, meshing, persistence and independently checked results remain to be designed/implemented. |
| Voice and pointer context | Requested speech input and lightweight semantic mouse context for AI; design and integration pending. |
| Further modeling extensions | Dedicated spring/pipe workflows, additional pattern variants and individual unsupported surface/solid cases remain scoped follow-ups, not missing foundations. |
| Continuing verification | Real-project geometry/reference recovery, larger Assemblies, solver edge cases, production Drawing workflows and a later comprehensive Undo/Redo audit. Undo/Redo itself is implemented. |
| Linux | Each new change/release requires native build and GUI/package verification on the Linux host. Windows results do not establish that acceptance. |

Newly reported product defects retain priority over speculative extensions.
Native Sketch offsets already exist; STEP-derived offsets remain outside the
requested scope.

## Maintaining this index

User-selected interoperability bookmarks are retained in
[External resources](EXTERNAL_RESOURCES.md). Active design proposals must state
whether their command already exists, is implemented but unreleased, or is only
planned. Verification records preserve finite evidence and known limits; release
records preserve immutable package/publication acceptance. Do not copy a dated
"pending" statement into this index after a later release has completed it.

Update this page, the root README and the corresponding active roadmap entry
when a capability changes status. Link implementation/verification evidence and
state release/platform limits. Preserve historical release records as history;
do not rewrite their original acceptance results into current promises. Search
engines and generated summaries can retain older snapshots and need not update
immediately after a Git push.
