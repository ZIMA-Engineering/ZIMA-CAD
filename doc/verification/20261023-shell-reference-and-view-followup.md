# Multi-solid Shell, Sketch reference dimensions and view follow-up

Windows verification uses the shared native C++ implementation, OCCT 8.0.0 and
Qt 6.11.0. Native formats and the shared container-placement contract are unchanged.
The live Toyota project is protected; investigations use disposable snapshots.

## Shell

Positive-thickness Shell evaluates each selected opening's owning solid through
the existing hollowing algorithm, then publishes one atomic result. Other solids
retain their exact source shape and reference identities. An empty opening list
hollows all solids. Zero-thickness surface extraction retains its single-solid
contract. Unchanged existing topology is not renumbered to disguise reference
duplicates inherited from authored fillet fragments.

The native command matrix covers separated boxes, multiple and opposite openings,
selection order, successive Shell features, closed hollowing, editing, unchanged
OK, rejected operations, Undo/Redo and cold save/reopen. Twelve additional supplied
skeleton cases cover one through four openings at 1, 3 and 5 mm thickness, with
native round trips and cold regeneration. Evidence:
`build/multisolid-shell-skeleton.log`. The input snapshot SHA-256 is
`dd574dda9e65d49243412b11c3333316337aac59dcf5a59052bdd0959e78e16c`.

The supplied four-solid GUI scenario passes creation OK/Cancel, the shared
reference table, native result/save/reopen, Undo/Redo, edit Cancel and unchanged
OK (`build/followup-2302-development-gui.log`). This scenario populates the
reference table programmatically. Actual mouse picking of all four supplied
openings is not established by this run; the experimental cursor harness was
not accepted as evidence for that variant.

## Sketch dimensions

Universal dimension entry accepts a native point or straight segment against a
certified planar Face Reference's straight support, in either selection order.
Straight Intersection branches use the same shared input route. Curved supports
are not silently replaced by their endpoint chords. External sources remain
read-only. During an unlocked line-distance drag, the existing temporary
parallelism equation also applies to ordinary DistanceLine, so editing does not
lose the parallel-support contract.

`build/reference-dimension-complete-native.log` records the full Sketcher suite
and the finite matrix: 384 point/line support cases, 54 angular cases and 48
external-point cases. It includes applicable rejection states, repeated value
edits and drags, independent equations and native round trips. The dimensions
cover Edge, Axis, planar Face and persisted straight Intersection supports,
three orientations and both contact sides. Angular and point-pair subsets do
not enumerate every fixed/lock combination of the larger distance matrix.

`build/external-face-dimension-gui.log` records four actual GUI cases on the
supplied snapshot: point/segment and both selection orders, repeated inline
edits, mouse dragging, GUI Undo/Redo and native save/reopen. The source Part and
external reference identities remain unchanged. GUI straight-Intersection and
every native lock/orientation combination are not claimed as covered by these
four Face Reference scenarios.

## Views and terminology

XZ Sketch entry puts X right and Z up without changing the persisted Sketch
frame. YZ remains unchanged. `build/xz-sketch-entry-gui.log` covers six default/
placed plane cases and four real solid/Thin cap cases. TOP uses X right/Y up.
Vertical dimension labels read bottom to top, with a tolerance for numerical
projection noise. Transparent surfaces no longer disable edge depth occlusion;
the display-mode contract is checked at opaque, translucent and zero alpha.

The Sketch command is named **Intersection** (Czech **Průnik**) in all five
catalogs. No other new user-facing text is introduced by these fixes.

## Pattern construction presentation

Individual feature copies formerly transformed material topology but omitted
generated construction axes and helper points. They now reuse the source's
calculated construction packet and the existing Mirror/Pattern transformations.
Source Origin/centroid generation options remain authoritative. Copy ancestry
is decoded only for presentation classification, display overhang and endpoint
association; exact copied reference identities remain intact for picking.
No geometry is calculated during hover or visibility changes.

The native copy contracts cover linear/circular patterns, source Origin/centroid
options off/on, nested feature copies, exact transformed circular axes, native
packet round trips, cold calculation and incremental invalidation after changing
source options. Endpoint-to-axis identity and fitted display length are checked.
The complete derived-copy command suite passes geometry, source/placement modes,
locks, atomic errors, save/reopen and Undo/Redo. Its saved-source check now uses
the actual current native Feature geometry, rather than an obsolete fixture
assumption that every feature owns an Extrusion Sketch.

The appearance contract compares framebuffer equality between copied and ordinary
axis/curved-centerline presentation at the same settled camera, for Pattern and
Mirror, including centroid markers and Axes off/on restoration. Evidence:
`build/pattern-construction-final-native.log` and
`build/pattern-axis-presentation.log`. The existing profile-centerline and
dimension-layout suites also pass. Full arbitrary Sweep/Assembly GUI combinations
are not claimed by this focused presentation matrix.

Native Linux execution remains unverified. These are finite matrices, not a
claim of exhaustive coverage of every Sketch or topology combination.
