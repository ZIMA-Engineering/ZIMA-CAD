# 2D Sweep stations and profile context

Status: implemented and verified for the Windows cases recorded below; not a
published release.
Baseline: `fe231125a`. Shared C++ implementation; no native format change.

## Follow-up: original endpoint table restored

The user subsequently requested the original table presentation. The current
table again contains the start and end of every route segment: four rows for a
line followed by an arc and eight rows for the closed four-span route. Incoming
and outgoing definitions remain separate rows, including inherited profiles.
The geometric point identity and native profile ownership are unchanged.
The read-only previous-profile Sketcher context described below is retained.

The updated Windows GUI matrix passed in 19.38 s on 2026-11-20, including the
representative copied model, both explicit joint definitions, removal/inheritance,
Cancel, save/reopen, Undo/Redo, successive dimension edits and center drags.
Evidence: `build/rip25-20261120/review-gui-2/stations/output.log`.
The grouping/side-selector descriptions and results below record the superseded
implementation from `f77bfa594`; they do not describe the current endpoint table.

## Behavior and preserved ownership

The Properties table groups route entries by the persisted Sketch point identity.
A line and an incident arc therefore have three geometric station rows. A joint
offers its incoming and outgoing profile definitions in one selector. Selection
of that side is transient presentation state: it does not calculate, alter the
document, or create an Undo transaction. The native route and both profile slots
remain unchanged. Removing a profile still removes its explicit definition only;
the structural station remains available for inheritance or replacement.

While editing a profile, the existing Sweep preview supplies the other profiles
in their actual frames, including inherited profiles. The active geometric
station is excluded. Repeated inherited entries at a shared point are omitted;
an explicit definition on the other side remains independently representable.
Point labels use `station · correspondence point` numbers. Circles without native
correspondence points have their wire but no invented numbered points.

The command prepares this context once on Sketch entry, applies Body and active
occurrence transforms, and publishes a read-only scene-coordinate overlay.
Drawing and drag overlays cannot replace it. It is absent from the picker and
the model GPU packet. Finish and dialog close clear it. Returning to Properties
and entering another Sketch rebuilds it from the current pending definition.

## Verification matrix

The following are required checks; this table does not report passing results
until the corresponding execution is recorded below.

| Area | Checks |
| --- | --- |
| Station ownership | Line/arc: three rows; closed four-span route: four rows; incoming/outgoing choice, inheritance and explicit overrides |
| Native persistence | Both joint profiles through OK, save/reopen, Undo/Redo; Cancel leaves the source unchanged |
| Profile placement | Independent circle equations and planes at the first/last stations; no active-station duplicate |
| Editing | Successive path distance and profile radius edits; repeated free center drags; radius and center equations after editing |
| Reference identity | Native K circle correspondence points; unchanged sources; no context points in the editable mesh |
| Preview lifecycle | No scene publication on station-side selection; drawing, dimension editing and dragging retain guides; Finish clears them |
| Appearance | Live Light/Dark/Light, both station table and Sketcher context; five-language catalogs |
| Existing modeling | Native 2D Sweep contract, embedded FORM profile editing and normal Sweep GUI lifecycle |

Linux execution, nested Assembly occurrence context, arbitrary spline profiles,
all driving/reference and fixed/free dimension combinations, and the complete
Sketcher regression matrix remain unverified for this change. No exhaustive
Sketcher or cross-platform claim is made from the finite cases above.

## Execution record

- Catalog validation passed (16.70 s).
- Initial native Sweep CTest did not run because its executable had not yet been
  built. After building it, the native 2D Sweep contract passed (12.96 s).
- Native Sweep ownership/command tests passed (13.12 s), and profile point
  reference tests passed (0.41 s).
- The first expanded GUI run timed out during the second center drag: the
  verification sent RMB while the previous point was still selected, opening
  its context menu. The fixture now clears the completed selection before
  offering the next point. Successive distance/radius edits and both center
  drags now pass; their independent equations and retained context pass.
- Five-language station selector GUI checks passed. The table has three rows
  and the joint exposes both existing native endpoint definitions.
- The existing FORM GUI matrix passed (19.64 s): repeated radius/angle mouse
  edits, center drag, source references, OK/Cancel, Undo/Redo and native reopen.
  Its close-fixture helper now handles an unsaved-change confirmation explicitly;
  arbitrary errors/context menus remain failures. The first attempt timed out
  at that unhandled confirmation, before any such handling was installed.
- The unresolved owned-Sketch recovery GUI contract passed (2.95 s).
- The expanded station GUI matrix passed (19.43 s), including three line/arc
  stations, four closed-route stations, independent incoming/outgoing profiles,
  removal with inherited fallback, Cancel, exact native save/reopen, Undo/Redo,
  and the representative `26.prtz` copy. The original file was not modified.
  The persistence assertion compares the native Sweep definition and every
  profile: non-persisted path-plane orientation bookkeeping is not a contract.
- Light, Dark, and representative-model screenshots were inspected. Azure
  profile wires and station/point labels remain visible in Sketcher. The small
  synthetic empty-profile view retains the ordinary camera scale; this change
  does not impose a different Sketcher fit or view orientation.
