# Sketcher mechanisms and drawing interaction review

## Scope and invariants

This review follows Windows 2026110402. All product changes use the shared
C++ implementation, OCCT 8.0.1 and Qt 6.11.0. Native document schemas and the
protected container-placement solver are unchanged. Protected project data is
inspected read-only or edited in isolated copies; it is not release content.

The Sketcher changes reuse the bounded simultaneous equation fitter only after
ordinary solving fails. A cursor outside a mechanism's one-dimensional motion
path is projected through the real constraints. Unrelated free geometry does
not hide that mobility. Fully constrained geometry cannot move, and a failed
edit cannot publish a partial result. The ordinary solver validates the fitted
candidate. This is not a new general-purpose solver for unsupported splines,
ellipses or offsets.

## Native Sketcher matrix

Each study independently checks equations after edits and drags, rather than
accepting initial convergence. The tests also compare persisted identities,
read-only external supports and native save/reopen and Undo/Redo where applicable.

| Study | Variants | Successive numeric edits | Moving drags | Additional checks |
| --- | ---: | ---: | ---: | --- |
| Slider-crank, four-bar, midpoint branch, Watt and Scott Russell linkages | 324 | 4,176 | 2,400 | 1,800 off-path drags; both branches, rotated frames, endpoint orders, native/external supports and reference/unlocked/locked drivers |
| Tangent slider | 432 | 864 | 864 | Scale, oblique supports, both selection orders, isolated minimum-distance position and locked rejection |
| Dimensioned U profiles | 1,440 | 2,880 | 2,880 | 1,440 locked rejections; orientations, order and coupled parallel/perpendicular branches |
| Swivel profile | 180 | 360 | 180 | Ten dimension drivers, Mirror and native tangent contacts |
| Trimmed circular fragments | 96 | 192 | 192 | Two/three survivors, mirrored curves, native/external cutters, tangency order and radius/diameter states |
| Angular sectors | 768 | 1,024 | 1,536 | Four rotations/sectors, both selection/endpoint orders, native/external datum, three constraint states and explicit positive/negative values |

All listed native matrices and the complete existing Sketcher contract executable
passed on Windows Release. Trimming retains shared contact points and independent
C/T constraints. Surviving native arcs share the authored center and a minimal
EqualRadius star; an authored radius/diameter retains its identity and state.
Removing T does not remove the existing shared junction.

Mechanism sources used for independent constructions:

- [CMU planar mechanisms](https://www.cs.cmu.edu/~rapidproto/mechanisms/chpt5.html)
- [Illinois linkage examples](https://dynref.engr.illinois.edu/aml.html)
- [MIT kinematics lecture](https://ocw.mit.edu/courses/2-00aj-exploring-sea-space-earth-fundamentals-of-engineering-design-spring-2009/e8c25391a19f4a1ef3c427017307f509_MIT2_00AJs09_lec07.pdf)
- [MIT ESP exercises](https://acdl.mit.edu/esp/archive/Training2021/ESP/lectures/exercises.pdf)

U and Swivel fixtures use the exercise proportions with an explicitly chosen
test scale, not a claim that the source supplies millimetre dimensions. The
intentional Swivel kink is not replaced by an invented tangent constraint.
The exercise book's separate Oval example was not reconstructed; the existing
slot matrix is separate evidence.

## Drawing geometry and interaction

| Verification | Result |
| --- | --- |
| Exact cylindrical generators | 432 axis/view/scale/facing/occurrence/rim/selection-order variants; successive diameter and length changes, label drags and embedded reopen passed |
| Existing circular surface contours | 128 radius/drag variants passed, including native embedded cone/torus reopen |
| Actual native rounded shaft | Three successive Fillet radii, both side views, 24 R/diameter checks, label drags, stable contour identities and native reopen passed |
| Repeated dimension command | 15 actual mouse creations, five kinds and two views; normal OK opens a fresh same-kind command, short MMB stays, empty double MMB ends, one Undo per dimension and native reopen passed |
| Projected views | Eight diagonal mouse displacements create four cardinal projections, inherit the parent and undo atomically; Escape, subsequent Properties and native round trips passed |
| Native regressions | Fourteen focused suites for dimensions, picking, annotations, labels, title blocks, balloons, view commands, source refresh, threads and drawing contracts passed |

New projected-view creation rejects diagonal directions through both GUI and
native commands. Existing stored views retain their existing projector and
persistence behavior. Ordinary creation stays frame-only through Properties;
explicit OK prepares the real view geometry. Existing sections, details,
breaks, precise export and reference geometry use their established paths.

The contour loader consumes analytic supports already stored in exact boundary
adjacencies. It does not add a native field or reconstruct topology on open.
Apparent cylinder generators use zero cone slope even if an irrelevant
semi-angle field is nonzero. Axial views do not invent zero-length generators.

## Representative Assembly and BOM

The supplied top-level Assembly has 99 displayed leaf paths. All 99 resolve
appearance through their true immediate owners, including closed nested source
Assemblies. Native tests cover repeated nested instances, Body/face groups,
occurrence overrides, open unsaved Part authority and unchanged revision/data
generation. Appearance lookup reads already prepared source data, with no file
load, opened tab or OCCT calculation.

The saved supplied Drawing contains one first-level BOM row for its one immediate
subassembly, quantity one. The native repeat region grows upwards at 10 mm.
The actual title-block render shows the row above its header. The imported
subassembly has no authored parameter/alias entries, so name, stock, material
and mass cells have no source values. This is not evidence of a repaired BOM
row-insertion defect; the pre-confirmation draft was unavailable. The first-level
contract remains Parts and subassemblies, without flattening nested Parts.

## Performance evidence

The supplied saved Drawing is approximately 303 MB, with a measured view of
60,011 projected edges, 372,202 triangles and 14,545 measurement curves.
On the same Windows workstation:

| Operation | Before | After |
| --- | ---: | ---: |
| Picker preparation | 52,657.5 ms | 311.052 ms |
| Changed view OK, run 1 | 42.8679 s | 2.49261 s |
| Changed view OK, run 2 | 72.6073 s | 2.71666 s |
| Changed view OK, run 3 | 70.5609 s | 2.86404 s |

For the same 150 picking queries, all 1,011 ordered candidates retained SHA-256
`ee813d6dc894189eb01eb52d1d5bdaf07a35c0ea18718524412f46df8478db03`.
This comparison predates the intentional addition of cylindrical contour
references; it proves the traversal optimization, not equality after a new
geometry capability is added.

The GUI activity scenario recorded 2,661 event-loop ticks, 2,627 with the shared
overlay visible, across three rotation/confirmation/Undo/Redo cycles. Cold
source preparation remained approximately 19--22 s; the 17--27x changed-OK
result is not a claim about complete cold opening or model regeneration.

The changes index exact face triangles and projected source ancestry instead
of rescanning the whole Assembly for each curve. Interactive projection skips
unused precise-occlusion preparation. Repeated Part annotation seeds are reused
only within one source-preparation operation, before occurrence transforms.
Three camera/output fingerprints matched before and after those optimizations.
The source files retained their hashes after the read-only scenario.

## Other reviewed behavior and finite limits

Bookmark menus now retain persistent bookmark identity across the nested menu
event loop; icon updates may rebuild the Tree. The previous stale item access
reproduced a Windows heap crash. Sixteen real menu/Edit/Properties cycles passed
after the repair using an isolated browser profile.

Document close starts the shared centered activity after the Save/Discard/Cancel
decision and retires it after remaining-scene publication. Immediate activity
must paint before synchronous work starts; the added paint regression failed
before that guarantee was implemented. Deferred activity retains its ordinary
delay. Background Drawing preparation owns private source/current-state snapshots
and blocks competing document commands while preserving timers and painting.

The Arc R/diameter GUI matrix passes 40 language/major/minor/re-click variants
and 120 numeric edits, including preview Cancel, Undo/Redo and native reopen.
Final local GUI acceptance additionally passed 32 actual angular-sector
previews with both endpoint/selection orders, positive automatic values,
Cancel, placement and Undo/Redo. Mirror/Trim source and reflected dimension
edits and shared-point/C/T mouse workflows passed. On isolated copies, the
actual 02.prtz R150 dimension and two dimensions of the supplied imported sheet
profile each passed three successive inline edits, Feature OK, dependent
reference checks, Undo/Redo and native reopen. The real Assembly framebuffer
passed closed/repeated nested Body colors, occurrence overrides and an open
unsaved source color. Dirty Cancel/Discard and clean close painted and retired
the centered activity and preserved the remaining scene and saved source.
All five localization catalogs and source coverage, the complete measurement
GUI, six axis-cross scenarios and actual shaft R/diameter mouse drags passed.

The expanded history-recovery test exposed an obsolete assertion requiring
all material to disappear after the first stock fails. The existing recovery
contract retains later independent valid stocks. The updated oracle requires
exactly the second stock's independent 504 mm3 volume, propagated first-stock
errors and reference geometry identical to a calculation without old cache.
No product recovery behavior changed.

The later requested general ISO 21920 roughness Empty variant contains no
Sketches, strokes, brackets or text. The native authoring tool adds it to the
factory symbol and to each of the five factory title-block embedded definitions
individually, preserving their own authored geometry. The native symbol,
Drawing-symbol and five-language title-block Family suites pass. Ten GUI cases
cover both factory and independently embedded definitions in all five languages,
actual Properties entry, localized selection, hidden text fields, unchanged OK,
Cancel, OK, Undo/Redo and native Drawing reopen. An inserted definition gains the
optional row only when the user commits Empty; no custom library or existing
document file is rewritten by opening Properties. Unchanged title-block-symbol
OK now returns before the refresh/Undo transaction.

Package acceptance results must be recorded before publication. Native Linux
build, runtime, GUI and timing are not established by
Windows tests. These finite matrices are not a proof for every possible Sketch
or topology event, nor a new generic apparent-curve implementation for arbitrary
oblique toroidal or freeform surfaces.
