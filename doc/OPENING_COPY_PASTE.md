# Opening settings copy and paste

## Agreed scope

The user approved Ctrl+C and Ctrl+V for the current Opening command only on
2026-10-19. No toolbar button, menu action or context-menu item is requested.
Ordinary selection in the View or Tree supplies the copied feature. Text
editors retain their normal clipboard shortcuts.

The initial clipboard is a settings snapshot owned by the current CAD window,
usable across its open Part documents. A private system-clipboard token detects
when another copy operation (including ordinary text Copy) has replaced it.
Transfer to another application process/window or across restarts is not
implemented. The token contains no document geometry and is not a native file
format. Copying an unsupported feature invalidates the local settings snapshot
so a subsequent paste cannot unexpectedly reuse the preceding Opening.

Copy captures the current calculated definition's scalar settings, including
plain/threaded type, dimensions, handedness, end modes, entrance chamfer, drill
point and property locks. Paste starts the ordinary creation dialog with these
settings and a fresh identity. It requires a new placement; source placement
references and end-face references are not copied. An Up-to end mode therefore
requires a new target selection. Relations from the source document are not
cloned: the snapshot contains its current evaluated values.

Every paste is independent of its source and previous pastes. Creating the new
feature uses the existing Opening creation transaction and the unchanged shared
placement controls. Cancel inserts nothing. OK creates one normal undoable
Opening. No native document schema change is needed.

## Lifecycle and verification status

Copy reads the selected definition without geometry calculation, document
mutation or an Undo step. Paste prepares a draft through the ordinary creation
path. Placement interaction uses the existing wire preview. Explicit OK owns
calculation and the single history commit.

The Windows GUI regression passed with the built application on 2026-10-19:
View/Tree selection, real Ctrl+C/Ctrl+V delivery, ordinary numeric placement,
new whole-Origin references and offset, independent IDs, repeated pastes,
Cancel, Undo/Redo, save/reopen, unsupported selections and text-editor shortcut
preservation. The final native material volume matches three independently
calculated holes, including the entrance chamfer and drill point. Evidence is
in `build/opening-copy-origin-fixed/`; the extended inspection run is in
`build/opening-copy-origin-inspection/`. The numeric-placement baseline is in
`build/opening-copy-gui-v7/`.

## Whole-Origin calculation correction

This verification exposed a command-local error independent of copying. An
Opening treated any positional plane reference as a single-face FRONT support,
even when the existing shared resolver had supplied a complete Origin frame.
It therefore cut along local -Y while the saved frame correctly indicated +Z.
At a box boundary this removed half the expected volume.

The Opening now consumes the existing whole-Origin capability predicate when
choosing its work plane. Kernel requests, wire preview and Up-to thread-length
measurement use the same command-local choice. Inspection dimensions also consume
the existing complete-Origin predicate, so axial witnesses and diameter-plane
normals follow the calculated bore. Single-face FRONT attachment
retains its existing behavior. No shared placement solver, orientation/offset
rules, reference storage, native schema or topology identity is changed.

`zima_cpp_opening_command_tests` fails before the fix with exactly half the
expected cut and passes after it. The added matrix covers 32 cases: plain,
metric, Whitworth and pipe; both directions; plain blind, chamfer/tip blind,
Through all and Up to an Origin plane. It checks independent volume, resolved
frame, preview axis, retained references, unchanged OK, Undo/Redo, cached native
reopen and cold calculation for the chamfer/tip variants. Up-to thread length is
checked against an independent plane intersection. Existing single-plane,
catalogue, lock and invalid-edit cases also pass.

The extended matrix checks axial dimension witnesses and diameter-plane normals
in all 32 cases. The GUI test additionally double-clicks the pasted whole-Origin
feature and verifies both length dimensions, both diameter planes, no Properties
dialog and unchanged revision. This completes the dependent inspection correction
in build 2026101902; the unissued 1901 source tag remains immutable.

No new visible text is introduced by the geometry correction. The clipboard
status message is translated in cs/en/de/fr/ru. Cross-process clipboard transfer,
the full GUI matrix of every end target, and Linux execution remain unverified.

Existing documents retain their saved calculated geometry when opened. Use
explicit Regenerate to update a previously calculated whole-Origin Opening to
the corrected work plane; the correction does not rewrite documents on load.
